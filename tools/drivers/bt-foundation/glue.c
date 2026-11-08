/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Link-check vehicle for the LinuxKPI Bluetooth foundation (see
 * tools/drivers/bt-foundation.sh): the glue a non-network LinuxKPI add-on
 * uses. Not built by jam.
 */
#include <sys/bus.h>
#include <sys/haiku-module.h>

#include "lkpi_driver.h"

LKPI_HAIKU_DRIVER_GLUE(lkpi_bt_foundation)
HAIKU_DRIVER_REQUIREMENTS(FBSD_TASKQUEUES);
/* USB: no newbus interrupts, no MII */
NO_HAIKU_FBSD_MII_DRIVER();
NO_HAIKU_CHECK_DISABLE_INTERRUPTS();
NO_HAIKU_REENABLE_INTERRUPTS();

/* Linux "mediatek/<file>" -> <file> in
 * /boot/system/[non-packaged/]data/firmware/<driver>/ */
HAIKU_FIRMWARE_VERSION(0);
HAIKU_FIRMWARE_NAME_MAP({
	{"mediatek/BT_RAM_CODE_MT7922_1_1_hdr.bin",
		"BT_RAM_CODE_MT7922_1_1_hdr.bin"},
});
