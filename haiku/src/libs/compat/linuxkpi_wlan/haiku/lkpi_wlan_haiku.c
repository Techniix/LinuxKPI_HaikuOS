/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Haiku attach ordering for LinuxKPI 802.11.
 *
 * freebsd_network's device_attach() calls freebsd_wlan's start_wlan() as soon
 * as the driver's attach method returns; start_wlan() looks the net80211
 * device up by the device's name and creates the station vap. Linux drivers
 * (mt7921 among them) return from probe at once and register with mac80211
 * later from a work queue (firmware load first), so we wait in attach until
 * LinuxKPI has created the ieee80211com for this device.
 *
 * Haiku holds Giant across the whole attach (driver.c), and LinuxKPI's
 * ieee80211 attach takes bus_topo_lock(), which is Giant here, so Giant is
 * released while waiting.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net80211/ieee80211_var.h>

#include <KernelExport.h>

#define	LKPI_WLAN_ATTACH_TIMEOUT_MS	20000

extern int (*lkpi_pci_attach_wait_hook)(device_t dev);	/* lkpi_pci.c */

static int
lkpi_wlan_attach_wait(device_t dev)
{
	const char *name = device_get_nameunit(dev);
	bool giant = mtx_owned(&Giant);
	int ms;

	if (giant)
		mtx_unlock(&Giant);
	for (ms = 0; ms < LKPI_WLAN_ATTACH_TIMEOUT_MS; ms += 10) {
		if (ieee80211_find_com(name) != NULL)
			break;
		snooze(10000);
	}
	if (giant)
		mtx_lock(&Giant);

	if (ms >= LKPI_WLAN_ATTACH_TIMEOUT_MS) {
		device_printf(dev, "LinuxKPI 802.11: no wireless device "
		    "registered within %d s (firmware missing?)\n",
		    LKPI_WLAN_ATTACH_TIMEOUT_MS / 1000);
		return (ETIMEDOUT);
	}
	device_printf(dev, "LinuxKPI 802.11: %s registered after %d ms\n",
	    name, ms);
	return (0);
}

static void
lkpi_wlan_haiku_init(void *arg __unused)
{
	lkpi_pci_attach_wait_hook = lkpi_wlan_attach_wait;
}
SYSINIT(lkpi_wlan_haiku, SI_SUB_DRIVERS, SI_ORDER_FIRST, lkpi_wlan_haiku_init,
    NULL);
