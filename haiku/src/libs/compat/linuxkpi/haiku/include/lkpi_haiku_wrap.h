/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Out-of-line freebsd_network functions are compiled with Haiku's (negative)
 * errno values. LinuxKPI code does "return (-error)" on their results and
 * Linux callers test "< 0", so every such call made from a LinuxKPI unit is
 * routed through lkpi_errno_from_haiku(). A function-like macro does not
 * re-expand its own name, so the inner call reaches the real function.
 * The prototypes are pulled in first so declarations are not macro-expanded.
 */
#ifndef _LKPI_HAIKU_WRAP_H_
#define _LKPI_HAIKU_WRAP_H_

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/taskqueue.h>
#include <sys/kthread.h>
#include <dev/pci/pcivar.h>
#include <lkpi_errno.h>

#define bus_setup_intr(...)	lkpi_errno_from_haiku(bus_setup_intr(__VA_ARGS__))
#define bus_teardown_intr(...)	lkpi_errno_from_haiku(bus_teardown_intr(__VA_ARGS__))
#define bus_dma_tag_create(...)	lkpi_errno_from_haiku(bus_dma_tag_create(__VA_ARGS__))
#define bus_dma_tag_destroy(...) lkpi_errno_from_haiku(bus_dma_tag_destroy(__VA_ARGS__))
#define bus_dmamap_create(...)	lkpi_errno_from_haiku(bus_dmamap_create(__VA_ARGS__))
#define bus_dmamap_destroy(...)	lkpi_errno_from_haiku(bus_dmamap_destroy(__VA_ARGS__))
#define bus_dmamap_load(...)	lkpi_errno_from_haiku(bus_dmamap_load(__VA_ARGS__))
#define bus_dmamem_alloc(...)	lkpi_errno_from_haiku(bus_dmamem_alloc(__VA_ARGS__))
#define taskqueue_start_threads(...) lkpi_errno_from_haiku(taskqueue_start_threads(__VA_ARGS__))
#define kthread_add(...)	lkpi_errno_from_haiku(kthread_add(__VA_ARGS__))
#define device_attach(...)	lkpi_errno_from_haiku(device_attach(__VA_ARGS__))
#define device_detach(...)	lkpi_errno_from_haiku(device_detach(__VA_ARGS__))

/* freebsd_network pci.cpp returns Haiku ENXIO/EINVAL/EOPNOTSUPP (negative),
 * e.g. into LinuxKPI's inline pci_enable_device(). The translation is
 * idempotent for values that are already >= 0. */
#define pci_enable_io(...)	lkpi_errno_from_haiku(pci_enable_io(__VA_ARGS__))
#define pci_enable_busmaster(...) lkpi_errno_from_haiku(pci_enable_busmaster(__VA_ARGS__))
#define pci_find_cap(...)	lkpi_errno_from_haiku(pci_find_cap(__VA_ARGS__))
#define pci_find_extcap(...)	lkpi_errno_from_haiku(pci_find_extcap(__VA_ARGS__))
#define pci_set_powerstate(...)	lkpi_errno_from_haiku(pci_set_powerstate(__VA_ARGS__))
#define pci_alloc_msi(...)	lkpi_errno_from_haiku(pci_alloc_msi(__VA_ARGS__))
#define pci_alloc_msix(...)	lkpi_errno_from_haiku(pci_alloc_msix(__VA_ARGS__))
#define pci_release_msi(...)	lkpi_errno_from_haiku(pci_release_msi(__VA_ARGS__))

#endif
