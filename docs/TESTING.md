# Testing on real hardware

Target: x86-64 machine with a MediaTek MT7921 (PCI vendor `14c3`; the test
driver matches devices `7961`, `0608`, `0616`, `7920`, `7922`).

The test driver is a kernel add-on. A bad kernel add-on can panic the
machine at boot, so **read the recovery section before installing.**

## 0. Before you start

- Have **wired ethernet or a USB tether** available. Haiku has no native
  MT7921 driver, so stock Haiku has no Wi-Fi on this card anyway, but you'll
  want a network to pull logs off the machine.
- Know how to reach the boot menu: hold **Shift** (or press Space) right
  after the firmware/BIOS screen.
- Record your exact card:

  ```sh
  listdev | grep -B2 -A2 -i 14c3
  ```

  Note the `device` ID. If it isn't one of the five above, stop and report it.

- Make sure **no other driver claims the card**. Only one driver can reserve a
  PCI device; if another one has it, syslog shows
  `pci_reserve_device for driver lkpi_pcitest failed: Name in use`.
  Find and move aside any other MT7921/MT7922 driver, then reboot:

  ```sh
  find /boot -iname "*mediatek*" -path "*drivers*" 2>/dev/null
  ```

- `rescan` is in Haiku's source tree but not installed by default. Build it once:

  ```sh
  gcc /path/to/haiku/src/bin/rescan.c -o ~/rescan
  ```

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

Firmware for stage (b) comes from `linux-firmware` (`mediatek/`). The
names depend on the chip, exactly as in Linux mt7921:

| PCI device | Chip | Files |
|---|---|---|
| `0616`, `7922` | MT7922 | `WIFI_MT7922_patch_mcu_1_1_hdr.bin`, `WIFI_RAM_CODE_MT7922_1.bin` |
| `7961`, `0608` | MT7921 | `WIFI_MT7961_patch_mcu_1_2_hdr.bin`, `WIFI_RAM_CODE_MT7961_1.bin` |

```sh
F=/boot/system/non-packaged/data/firmware/lkpi_pcitest
mkdir -p $F && cd $F
U=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek
curl -LO $U/WIFI_MT7922_patch_mcu_1_1_hdr.bin    # MT7922; use the MT7961 names for MT7921
curl -LO $U/WIFI_RAM_CODE_MT7922_1.bin
```

Without firmware, stage (a) still runs; stage (b) logs an error.

## 3. Run

```sh
~/rescan lkpi_pcitest
sleep 2
grep -E "stage|linuxkpi|lkpi" /var/log/syslog | tail -40
```

### Expected output

```
... stage a: probe 14c3:0616 (subsys ....:....) rev ..
... stage a: BAR0 start 0x... len 0x... mapped at 0x...
... stage a: irq N installed (msi=1)
... stage b: mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin: NNNN bytes, crc32 ........
... stage b: mediatek/WIFI_RAM_CODE_MT7922_1.bin: NNNN bytes, crc32 ........
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
   # stage (c): the same two files are called mt7921e
   rm /boot/system/non-packaged/add-ons/kernel/drivers/dev/net/mt7921e
   rm /boot/system/non-packaged/add-ons/kernel/drivers/bin/mt7921e
   ```

4. Reboot normally. The previous boot's log is in `/var/log/syslog.old`.

### Uninstall

The same two `rm` commands, plus optionally
`rm -r /boot/system/non-packaged/data/firmware/lkpi_pcitest`.

---

# Stage (c): the real mt7921e driver

Branch `stage-c`. This is the first stage where the card does DMA into
memory, so a bug can crash the machine or corrupt memory. Save your work,
keep ethernet plugged in, and know the recovery steps above.

**Unlike `lkpi_pcitest`, `mt7921e` stays loaded and Haiku loads it at every
boot** (the network stack opens `/dev/net`). If it crashes during boot, use
*Select safe mode options → Disable user add-ons* and remove it (section 4).

**Until stage (c) works, do not leave `dev/net/mt7921e` in place across a
reboot.** Before commit 'mt7921e: do not hang probe' a failed init made probe wait
forever, which hung the boot at the rocket icon (fixed: probe now fails after
the driver's own retries, roughly 30-40 s, but that still delays boot).
Remove the link before rebooting and load the driver by hand afterwards:

```sh
D=/boot/system/non-packaged/add-ons/kernel/drivers
mv $D/dev/net/mt7921e ~/mt7921e.link        # before reboot
mv ~/mt7921e.link $D/dev/net/mt7921e        # after boot, then ~/rescan mt7921e
```

`rescan` only loads a driver binary the first time in a session; after
replacing the binary, reboot (with the link removed) before testing again.

## c.1 Remove the stage (a)/(b) test driver

Both drivers match the same card; only one can own it.

```sh
D=/boot/system/non-packaged/add-ons/kernel/drivers
rm $D/dev/net/lkpi_pcitest $D/bin/lkpi_pcitest
```

## c.2 Build

```sh
cd ~/hk/LinuxKPI_HaikuOS && git fetch && git checkout stage-c && git pull
tools/install.sh ~/hk/haiku
cd ~/hk/haiku/generated.x86_64
jam -q -j12 mt7921e
```

## c.3 Install (non-packaged) and firmware

```sh
D=/boot/system/non-packaged/add-ons/kernel/drivers
cp objects/haiku/x86_64/release/add-ons/kernel/drivers/network/wlan/mt7921e/mt7921e $D/bin/
ln -sf ../../bin/mt7921e $D/dev/net/mt7921e

F=/boot/system/non-packaged/data/firmware/mt7921e
mkdir -p $F
cp /boot/system/non-packaged/data/firmware/lkpi_pcitest/WIFI_* $F/
ls -l $F
```

(MT7921 parts, `7961`/`0608`, need the `WIFI_MT7961_*` files instead; see the
table in section 2.)

## c.4 Run

```sh
~/rescan mt7921e
sleep 5
grep -E "mt7921|mt76|linuxkpi|LinuxKPI|lkpi|wlan" /var/log/syslog | tail -60
```

### What success looks like

In roughly this order (values differ):

```
... ASIC revision: 7922....
... HW/SW Version: 0x8a108a10, Build Time: 2025...
... WM Firmware Version: ____000000, Build Time: 2025...
... LinuxKPI 802.11: lkpi0 registered after NNN ms
... start_wlan: wlan started.
```

The **WM Firmware Version** line is the stage (c) milestone: the card's
microcontroller booted the firmware we sent over DMA and answered.
`ifconfig` should then list a wireless interface; scanning and connecting
are stages (d) and (e) and may not work yet.

### What to send back

- the whole `grep` output above (or `tail -150 /var/log/syslog`),
- the KDL `bt` photo if it crashed,
- `listdev | grep -B2 -A2 -i 14c3` and `uname -a` if anything looks off.

If it says `no wireless device registered within 20 s`, the firmware was not
found or did not start; the lines before it say which.

## Tunables and speed diagnostics

FreeBSD's LinuxKPI has boot-time tunables (`CTLFLAG_TUN` sysctls such as
`compat.linuxkpi.80211.hw_crypto`). On Haiku they are read from the driver's
settings file, one `key value` per line, before the driver attaches:

```sh
mkdir -p ~/config/settings/kernel/drivers
cat > ~/config/settings/kernel/drivers/mt7921e <<'X'
compat.linuxkpi.80211.hw_crypto 1
X
```

Values: a number, or `true`/`false` (`yes`/`no`, `on`/`off`). Every tunable
that was applied is logged as `linuxkpi: tunable <key> = <value>`. To undo,
delete the line (or the file) and reboot. If a setting stops the machine
from booting, boot with "Disable user add-ons" and remove the file.

Useful keys:

| Key | Default | Effect |
|---|---|---|
| `compat.linuxkpi.80211.hw_crypto` | 0 | Let the card do CCMP encryption/decryption. Off means net80211 does it in software, one frame at a time, on the CPU. |
| `compat.linuxkpi.80211.debug` | 0 | LinuxKPI 802.11 debug bitmask (only in builds with `LINUXKPI_DEBUG_80211`). |
| `compat.linuxkpi.mt7921_pci_disable_aspm` | 0 | mt7921e: turn off PCIe power saving (ASPM). |
| `compat.linuxkpi.mt7921_disable_clc` | 0 | mt7921e: ignore the firmware's country-location table. |

The DMA layer logs a summary every 16,384 copies made because a buffer is
above the card's 32-bit DMA limit:

```
lkpi_dma: <bounces> bounces, <misses> pool misses, <maps> maps total
```

`bounces` close to `maps` means nearly every transfer is copied; any `pool
misses` mean the 512-slot pool ran out and fell back to a slower allocation.

## Speed tests A and B (2026-11-07)

Both use the build from commit `95abd05` (the one with the
`linuxkpi: tunable` and `pool misses` strings). **Do not rebuild between
A and B**, so the only difference is the setting.

Your mesh has several radios called "Singleton Fiber"; the driver may join a
different one after each restart, and results differ a lot between them. So
note the access point for every test:

```sh
ifconfig /dev/net/mt7921e/0 | grep Network
```

Test A (software encryption, default) is done: on `…:e9:3f`, about 19 down
and 110 up; on `…:e9:3e`, about 617 down and 43 up.

**Test B (card does the encryption):**

1. Create the settings file:
   ```sh
   mkdir -p ~/config/settings/kernel/drivers
   echo "compat.linuxkpi.80211.hw_crypto 1" > ~/config/settings/kernel/drivers/mt7921e
   cat ~/config/settings/kernel/drivers/mt7921e
   ```
2. Restart.
3. Check that it was applied:
   ```sh
   grep "tunable" /var/log/syslog
   ```
   Expect `linuxkpi: tunable compat.linuxkpi.80211.hw_crypto = 1`.
4. Check that Wi-Fi connected (network menu: `mt7921e/0: Connected`) and
   which access point you are on (`ifconfig` line above).
   - Not connected: save `grep -E "mt7921|mt76|lkpi|linuxkpi|wlan|key" /var/log/syslog | tail -80 > ~/testB-fail.txt` and go to step 8.
5. Speed test (Ethernet unplugged), twice. Note both results.
6. Save the logs:
   ```sh
   grep -E "lkpi_dma|tunable" /var/log/syslog | tail -20 > ~/testB.txt
   grep -iE "ampdu|addba|ba_|aggr" /var/log/syslog | tail -30 > ~/testB-ba.txt
   ```
7. Compare with Test A *on the same access point*. If B landed on the other
   one, restart until it lands on the one you want (or note it and test
   both).
8. To undo: `rm ~/config/settings/kernel/drivers/mt7921e` and restart. If it
   does not start: boot menu → safe mode options → *Disable user add-ons*,
   then run the `rm` and restart normally.

Send: speed numbers, the `Network` line, `~/testB.txt`, `~/testB-ba.txt`
(or `~/testB-fail.txt`).

## LinuxKPI Manager and lkpictl (first build; optional)

Do this **after** Test B. It rebuilds the driver with the new read-only
status interface (patch 0007) and builds the two tools for the first time;
expect compile errors that we fix together.

```sh
cd ~/hk/LinuxKPI_HaikuOS && git pull
tools/install.sh ~/hk/haiku          # also applies patches 0007 and 0008
cd ~/hk/haiku/generated.x86_64
R=objects/haiku/x86_64/release
rm -rf $R/add-ons/kernel/drivers/network/wlan/mt7921e \
       $R/libs/compat/linuxkpi $R/libs/compat/linuxkpi_wlan \
       $R/libs/compat/freebsd_network
jam -q -j12 mt7921e LinuxKPIManager lkpictl 2>&1 | tee ~/manager-build.txt | tail -40
```

Install the tools and their data (the driver as in section c.3):

```sh
N=/boot/system/non-packaged
mkdir -p $N/bin $N/apps $N/data/linuxkpi
cp $R/apps/linuxkpimanager/LinuxKPIManager $N/apps/
cp $R/bin/lkpictl/lkpictl $N/bin/
cp -r ~/hk/LinuxKPI_HaikuOS/data/linuxkpi/* $N/data/linuxkpi/
lkpictl drivers
lkpictl hardware
lkpictl status mt7921e
```

(If the binaries are not at those paths, `find $R -name lkpictl -type f`.)
Then start `/boot/system/non-packaged/apps/LinuxKPIManager`. Send
`~/manager-build.txt` if the build fails, or screenshots of each tab if it
runs.

### Checking the thread-record leak fix (same build)

Before 2026-11-07 every thread that touched the driver left about a
kilobyte behind for good [H: size estimated, not measured]. With the fix,
records are freed 30 s after their thread exits. To see it:

```sh
lkpictl status mt7921e | grep threads
for i in $(seq 50); do ifconfig /dev/net/mt7921e/0 > /dev/null; done
sleep 35; ifconfig /dev/net/mt7921e/0 > /dev/null
lkpictl status mt7921e | grep threads
```

`records_created` may rise; `records_live` should come back to about where
it started, and `records_freed` should rise. The syslog also shows
`linuxkpi: N thread records created, L live, F freed` every 256 records.
If you see `cannot watch thread exits`, the fix is not active.
