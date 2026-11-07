# Link-check description of the mt7921e add-on (sourced by tools/link-check.sh).
# Each merge object: name, source dir, flags, sources -- as in the Jamfiles
# under haiku/src/add-ons/kernel/drivers/network/wlan/mt7921e/linux/.
MT=$DRV/imported/mt76
WLAN=1
driver_objects() {
	S=$(jam_sources "$DRV/linux/core/Jamfile" mt7921e_mt76.o | sed "s|^|$MT/|")
	DRIVER_FLAGS="-I$MT -DCONFIG_ARCH_DMA_ADDR_T_64BIT -include $DRV/linux/core/kbuild_modname.h" \
		build driver mt7921e_mt76.o $S
	S=$(jam_sources "$DRV/linux/mt7921/Jamfile" mt7921e_mt7921.o | sed "s|^|$MT/mt7921/|")
	DRIVER_FLAGS="-I$MT -I$MT/mt7921 -DCONFIG_ARCH_DMA_ADDR_T_64BIT -include $DRV/linux/mt7921/kbuild_modname.h" \
		build driver mt7921e_mt7921.o $S
	DRIVER_OBJS="mt7921e_mt76.o mt7921e_mt7921.o"
}
