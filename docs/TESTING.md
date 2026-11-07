# Testing on real hardware

Target: x86-64 machine with a MediaTek MT7921 (PCI vendor `14c3`; the test
driver matches devices `7961`, `0608`, `0616`, `7920`, `7922`).

The test driver is a kernel add-on. A bad kernel add-on can panic the
machine at boot, so **read the recovery section before installing.**

## 0. Before you start

- Have **wired ethernet or a USB tether** available. Haiku has no native
  MT7921 driver, so nothing changes for Wi-Fi either way, but you'll want
  a network to pull logs off the machine.
- Know how to reach the boot menu: hold **Shift** (or press Space) right
  after the firmware/BIOS screen.
- Record your exact card:

  ```sh
  listdev | grep -B2 -A2 -i 14c3
  ```

  Note the `device` ID. If it isn't one of the five above, stop and report it.

## 1. Build (on a Haiku build host)

```sh
git clone https://github.com/Techniix/LinuxKPI_HaikuOS
cd LinuxKPI_HaikuOS
tools/install.sh /path/to/haiku          # applies patches/haiku/*.patch, copies haiku/
cd /path/to/haiku/generated.x86_64
jam -q -j4 lkpi_pcitest
```

The binary lands in
`objects/haiku/x86_64/release/add-ons/kernel/drivers/network/lkpi_pcitest/lkpi_pcitest`
(the exact path depends on your build configuration; `find . -name lkpi_pcitest -type f`).

Note: patch 0004 changes `freebsd_network`'s `atomic_set_*`, which every
FreeBSD-derived network driver uses. The test only needs our own
`lkpi_pcitest` binary, which links the patched code statically, so you do
**not** have to replace any system drivers.

## 2. Install, non-packaged (on the test machine)

```sh
D=/boot/system/non-packaged/add-ons/kernel/drivers
mkdir -p $D/bin $D/dev/net
cp lkpi_pcitest $D/bin/
ln -sf ../../bin/lkpi_pcitest $D/dev/net/lkpi_pcitest
```

Firmware for stage (b) — copy the two files from `linux-firmware`
(`mediatek/`):

```sh
F=/boot/system/non-packaged/data/firmware/lkpi_pcitest
mkdir -p $F
cp WIFI_MT7961_patch_mcu_1_2_hdr.bin WIFI_RAM_CODE_MT7961_1.bin $F/
```

Without firmware, stage (a) still runs; stage (b) logs an error.

## 3. Run

```sh
rescan lkpi_pcitest
sleep 2
grep -E "stage|linuxkpi|lkpi" /var/log/syslog | tail -40
```

### Expected output

```
... stage a: probe 14c3:7961 (subsys ....:....) rev ..
... stage a: BAR0 start 0x... len 0x... mapped at 0x...
... stage a: irq N installed (msi=1)
... stage b: mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin: NNNN bytes, crc32 ........
... stage b: mediatek/WIFI_RAM_CODE_MT7961_1.bin: NNNN bytes, crc32 ........
... stage a: attach OK, tearing down again
... stage a: self-test complete; declining the device on purpose (-ENODEV)
```

Declining the device is intentional: no network interface appears in this
stage. Compare the crc32 values with `crc32` from Linux or with
`python3 -c "import zlib,sys;print('%08x'%zlib.crc32(open(sys.argv[1],'rb').read()))" FILE`.

Please send: the `listdev` lines, the full syslog excerpt, and the Haiku
revision (`uname -a`).

## 4. If it goes wrong

### Kernel debugger (KDL)

If the screen shows the kernel debugger:

```
bt
```

Photograph or write down the full backtrace, then type `reboot`. Optional
extra detail: `threads`, `thread`, `syslog` (shows the last log lines).

### Recover a machine that won't boot

1. Reboot and open the boot menu (hold **Shift**).
2. **Select safe mode options → Disable user add-ons**. This skips everything under
   `/boot/system/non-packaged/add-ons`.
3. Continue booting, then remove the driver:

   ```sh
   rm /boot/system/non-packaged/add-ons/kernel/drivers/dev/net/lkpi_pcitest
   rm /boot/system/non-packaged/add-ons/kernel/drivers/bin/lkpi_pcitest
   ```

4. Reboot normally. The previous boot's log is in `/var/log/syslog.old`.

### Uninstall

The same two `rm` commands, plus optionally
`rm -r /boot/system/non-packaged/data/firmware/lkpi_pcitest`.
