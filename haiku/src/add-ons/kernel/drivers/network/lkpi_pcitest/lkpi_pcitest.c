/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Stage (a)/(b) test driver, written purely against the Linux kernel API.
 * It must build unmodified on Linux too; on Haiku it runs on LinuxKPI.
 *
 * (a) attach to a MediaTek MT7921 (or any listed ID), map BAR0, allocate one
 *     MSI vector, install a handler, log everything, tear it all down.
 * (b) request_firmware() the MT7921 blobs and log size + checksum.
 *
 * Self-test in probe(): Haiku's freebsd_network keeps a driver loaded only
 * if it attached a network interface, and does not detach devices when it
 * gives up. So this driver runs the whole setup/teardown sequence inside
 * probe() and then declines the device with -ENODEV on purpose; Haiku then
 * deletes the device and unloads the driver cleanly. Everything is logged to
 * syslog with the prefix "stage a:" / "stage b:".
 *
 * Only MSI is requested: with legacy INTx the freebsd_network glue cannot
 * know whether a shared interrupt was ours (see docs/ARCHITECTURE.md).
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/crc32.h>

#define DRV_NAME "lkpi_pcitest"

static bool load_firmware = true;
module_param(load_firmware, bool, 0444);
MODULE_PARM_DESC(load_firmware, "stage (b): request the MT7921 firmware");

static const char *const fw_names[] = {
	"mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin",
	"mediatek/WIFI_RAM_CODE_MT7961_1.bin",
};

struct pcitest {
	struct pci_dev	*pdev;
	void __iomem	*bar0;
	int		irq;
	atomic_t	irqs;
};

static irqreturn_t
pcitest_irq(int irq, void *arg)
{
	struct pcitest *t = arg;

	atomic_inc(&t->irqs);
	/* We never enabled any interrupt source on the device, so any hit is
	 * spurious or shared: tell the core it was not ours. */
	return IRQ_NONE;
}

static void
pcitest_firmware(struct pcitest *t)
{
	const struct firmware *fw;
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(fw_names); i++) {
		err = request_firmware(&fw, fw_names[i], &t->pdev->dev);
		if (err != 0) {
			dev_err(&t->pdev->dev, "stage b: %s: error %d\n",
			    fw_names[i], err);
			continue;
		}
		dev_info(&t->pdev->dev, "stage b: %s: %zu bytes, crc32 %08x\n",
		    fw_names[i], fw->size, crc32_le(~0, fw->data, fw->size) ^ ~0);
		release_firmware(fw);
	}
}

static void pcitest_teardown(struct pcitest *t);

static int
pcitest_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct pcitest *t;
	int err;

	dev_info(&pdev->dev, "stage a: probe %04x:%04x (subsys %04x:%04x) rev %02x\n",
	    pdev->vendor, pdev->device, pdev->subsystem_vendor,
	    pdev->subsystem_device, pdev->revision);

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	t->pdev = pdev;
	atomic_set(&t->irqs, 0);

	err = pci_enable_device(pdev);
	if (err != 0)
		goto fail_free;
	pci_set_master(pdev);

	err = pci_request_regions(pdev, DRV_NAME);
	if (err != 0)
		goto fail_disable;

	t->bar0 = pci_iomap(pdev, 0, 0);
	if (t->bar0 == NULL) {
		err = -ENOMEM;
		goto fail_regions;
	}
	dev_info(&pdev->dev, "stage a: BAR0 start %#llx len %#llx mapped at %p\n",
	    (unsigned long long)pci_resource_start(pdev, 0),
	    (unsigned long long)pci_resource_len(pdev, 0), t->bar0);

	err = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (err < 0)
		goto fail_unmap;
	t->irq = pci_irq_vector(pdev, 0);
	err = request_irq(t->irq, pcitest_irq, IRQF_SHARED, DRV_NAME, t);
	if (err != 0)
		goto fail_vectors;
	dev_info(&pdev->dev, "stage a: irq %d installed (msi=%d)\n", t->irq,
	    pdev->msi_enabled);

	pci_set_drvdata(pdev, t);

	if (load_firmware)
		pcitest_firmware(t);

	dev_info(&pdev->dev, "stage a: attach OK, tearing down again\n");
	pcitest_teardown(t);
	dev_info(&pdev->dev, "stage a: self-test complete; declining the "
	    "device on purpose (-ENODEV)\n");
	return -ENODEV;

fail_vectors:
	pci_free_irq_vectors(pdev);
fail_unmap:
	pci_iounmap(pdev, t->bar0);
fail_regions:
	pci_release_regions(pdev);
fail_disable:
	pci_disable_device(pdev);
fail_free:
	kfree(t);
	dev_err(&pdev->dev, "stage a: probe failed: %d\n", err);
	return err;
}

static void
pcitest_teardown(struct pcitest *t)
{
	struct pci_dev *pdev = t->pdev;

	free_irq(t->irq, t);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, t->bar0);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	dev_info(&pdev->dev, "stage a: detach OK (%d interrupts seen)\n",
	    atomic_read(&t->irqs));
	kfree(t);
}

static void
pcitest_remove(struct pci_dev *pdev)
{
	struct pcitest *t = pci_get_drvdata(pdev);

	/* Not reached in self-test mode (probe declines); kept so the driver
	 * is also a normal, complete Linux PCI driver. */
	if (t != NULL)
		pcitest_teardown(t);
}

static const struct pci_device_id pcitest_ids[] = {
	{ PCI_DEVICE(0x14c3, 0x7961) },	/* MT7921 */
	{ PCI_DEVICE(0x14c3, 0x0608) },	/* MT7921K / RZ608 */
	{ PCI_DEVICE(0x14c3, 0x0616) },	/* MT7922 variant / RZ616 */
	{ PCI_DEVICE(0x14c3, 0x7922) },	/* MT7922 */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, pcitest_ids);

static struct pci_driver pcitest_driver = {
	.name		= DRV_NAME,
	.id_table	= pcitest_ids,
	.probe		= pcitest_probe,
	.remove		= pcitest_remove,
};
module_pci_driver(pcitest_driver);

MODULE_DESCRIPTION("LinuxKPI_HaikuOS stage a/b PCI test driver");
MODULE_LICENSE("Dual MIT/GPL");
