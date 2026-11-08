# Link-check description of the LinuxKPI Bluetooth foundation (sourced by
# tools/link-check.sh): linuxkpi_usb + linuxkpi_bt + the imported btmtk, with
# a test glue (tools/drivers/bt-foundation/glue.c). Not an add-on; it proves
# that everything the Bluetooth layers and btmtk need resolves against the
# Haiku kernel.  Usage: tools/link-check.sh tools/drivers/bt-foundation
R=$REPO/haiku/src/libs/compat
BTDRV=$REPO/haiku/src/add-ons/kernel/drivers/bluetooth/linuxkpi/imported
GLUE_PROFILE=shim
driver_objects() {
	build usb linuxkpi_usb.o "$R/linuxkpi_usb/lkpi_usb.c"
	build hk linuxkpi_usb_haiku.o "$R/linuxkpi_usb/haiku/lkpi_usb_haiku.c"
	build bt linuxkpi_bt.o "$R/linuxkpi_bt/lkpi_hci.c"
	build hk linuxkpi_bt_haiku.o "$R/linuxkpi_bt/haiku/lkpi_bt_haiku.cpp"
	EXTRA_FLAGS="-DCONFIG_BT_MTK=1 -DCONFIG_BT_HCIBTUSB_MTK=1" \
		build bt btmtk.o "$BTDRV/btmtk.c"
	DRIVER_OBJS="btmtk.o linuxkpi_bt.o linuxkpi_bt_haiku.o linuxkpi_usb.o linuxkpi_usb_haiku.o"
}
