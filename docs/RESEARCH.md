# LinuxKPI for Haiku — Phase 1 research report

Target: **MediaTek MT7921 (PCIe)** on x86-64 Haiku. Date: 2026-11-06.

The facts here were checked against source code, at these trees:

- **Haiku** `haiku/haiku` master `b9fdc8a9e699` (2026-11-06)
- **FreeBSD** `freebsd/freebsd-src` main `b556d9c9ce94` (2026-11-06)

Tags:

- **[V]** read in source, a commit log, a man page or an official FreeBSD
  status report
- **[H]** hypothesis or design proposal
- **[U]** unverified, or only reported second-hand

---

## 1. Summary

1. **The starting hypothesis holds.** LinuxKPI stacked on Haiku's
   `freebsd_network` + `freebsd_wlan` is viable, and cheaper than expected.
   - Haiku already provides about 127 of the FreeBSD KPIs that LinuxKPI
     calls. [V, probe §3]
   - Haiku's net80211 is missing only **6 symbols** that `linux_80211.c`
     uses. [V, §3.3]
   - The hard gaps are concentrated in a few subsystems:
     - sleepqueue
     - UMA
     - per-CPU data and CPU sets
     - epoch / RCU
     - `vm_page` allocation
     - dynamic newbus driver registration
2. **No native Haiku driver exists for your card.** The only MediaTek wifi
   driver in the tree is MT7601U, a USB chip handled by `ralinkwifi`. [V]
   Nothing needs blocklisting.
3. **MT7921 on FreeBSD works, but only on main, and only recently.**
   - It first appeared in FreeBSD 14.0. [V, mt7921.4]
   - In 2026Q2, mt7921 "passes packets with acceptable throughput for the
     first cut". At the same time, hardware crypto broke TX and PCI-detach
     panics were still in review. [V, Q2-2026 status draft]
   - We therefore import from a **pinned main commit**.
4. **Licensing is clean.**
   - Every source file in the mt76 core and mt7921 that we need carries
     `BSD-3-Clause-Clear`. [V]
   - LinuxKPI's `.c` files are BSD-2-Clause, except `linux_siphash.c`,
     which is dual GPL-2.0 / BSD-3-Clause and usable under BSD-3. [V]
5. **The biggest structural mismatch is in driver registration.**
   - Haiku's glue expects a *static* `driver_t` symbol
     (`HAIKU_FBSD_WLAN_DRIVER_GLUE` → `DRIVER_MODULE_NAME(name, busname)`,
     `compat/sys/haiku-module.h:172`). [V]
   - LinuxKPI builds `pdrv->bsddriver` at runtime and registers it with
     `devclass_add_driver()` (`linux_pci.c:1678`). Haiku doesn't have that
     function. [V]
   - This needs a small, well-understood shim (§5).

---

## 2. What Haiku's compat layers are

### 2.1 freebsd_network [V]

Files in `src/libs/compat/freebsd_network/`:

- bus/PCI: `bus.cpp`, `bus_dma.cpp`, `pci.cpp`
- timing and synchronisation: `callout.cpp`, `condvar.cpp`, `synch.cpp`
- device model: `device.c`, `driver.c`, `device_hooks.c`
- other services: `firmware.c`, `kthread.cpp`, `malloc.cpp`, `mutex.c`,
  `taskqueue.c` + `fbsd_subr_taskqueue.c`, `sysinit.c`, `smp.c`, `unit.cpp`,
  `usb.cpp`
- imported FreeBSD sources: `fbsd_*.c` (mbuf, if, ether, sbuf, bufring)

It builds as `libfreebsd_network.a` (`KernelStaticLibrary`, `-D_KERNEL=1`).

The mechanisms that matter for us:

- **Firmware** (`firmware.c:54`): `firmware_get_flags()` opens
  `{B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, B_SYSTEM_DATA_DIRECTORY}/firmware/<gDriverName>/<name>`,
  i.e. `/boot/system/non-packaged/data/firmware/<driver>/…`, then
  `/boot/system/data/firmware/<driver>/…`. Names pass through
  `HAIKU_FIRMWARE_NAME_MAP`.
  LinuxKPI's `linux_firmware.c:93-121` calls `firmware_get_flags()` with the
  Linux name. If that fails, it retries with only the basename, then with
  `/`, `.` and `-` flattened to `_`.
  So `mediatek/WIFI_RAM_CODE_MT7961_1.bin` resolves to
  `…/data/firmware/<driver>/mediatek/WIFI_RAM_CODE_MT7961_1.bin`, or the
  basename one directory up. **It works without code changes** [H on runtime
  behaviour; the paths are V].
- **Interrupts and MSI** (`pci.cpp:263-336`): `pci_msi_count`,
  `pci_alloc_msi`, `pci_release_msi`, `pci_msix_count`, `pci_alloc_msix` and
  `pci_msix_table_bar` exist. They call the PCI bus manager
  (`gPci->configure_msi` / `configure_msix`).
- **bus_dma** (`bus_dma.cpp`): honours `lowaddr` (DMA32), has bounce
  buffers, and `bus_dmamem_alloc` allocates below `lowaddr` without
  bouncing (lines 140, 229-273). That covers mt76's DMA32 needs.
- **Locks**: `sx_*` are macros over Haiku's kernel `rw_lock_*`
  (`compat/sys/sx.h:19-22`). `sx_init_flags`, `sx_xlock_sig` and
  `sx_slock_sig` are missing. There is **no rwlock(9)** (`rw_init` and
  friends).
- **Epoch** (`compat/sys/epoch.h`): `NET_EPOCH_ENTER/EXIT` are **no-ops**.
  The `ck_epoch` API is absent.
- **Driver lifecycle**:
  - The `HAIKU_FBSD_DRIVER_GLUE` / `HAIKU_FBSD_WLAN_DRIVER_GLUE` macros
    generate `__haiku_init_hardware()`, which calls
    `_fbsd_init_hardware_pci(drivers[])` with a static, NULL-terminated
    `driver_t*` list.
  - Taskqueue flavours are chosen with `HAIKU_DRIVER_REQUIREMENTS`.
    `FBSD_WLAN` gives net80211 plus the thread taskqueue.
    `OBSD_WLAN` gives net80211 plus the fast taskqueue
    (`haiku-module.h:221-231`).
- **The version marker is stale.**
  `compat/sys/param.h:21` still says `__FreeBSD_version 1200086`
  (12.x-era), while FreeBSD main is at `1600031`.
  The FreeBSD 15.0 driver sync that Phoronix reported (Dec 2025) [U] didn't
  bump it. **Don't rely on `__FreeBSD_version`** in shims. LinuxKPI tests it
  in places, so define it explicitly in our layer.
- **`__FreeBSD__` is not defined** by the compat layer. FreeBSD's copy of
  mt76 has **139** `__FreeBSD__` conditional blocks. [V] Our driver Jamfile
  must define `__FreeBSD__` for the imported LinuxKPI and mt76 objects only
  [H, decide in stage a].

### 2.2 freebsd_wlan [V]

- `net80211/` holds 79 files, with `ieee80211_haiku.cpp/.h` replacing
  `ieee80211_freebsd.c/.h`.
- Compared with FreeBSD main, it lacks `ieee80211_crypto_gcm{.c,.h}`,
  `ieee80211_crypto_gcmp.c` and the new `*.md` docs. The GCM cipher IDs are
  present in `ieee80211_crypto.h`; only the GCMP module is missing.
- Line-diff sizes against main:
  - `ieee80211_var.h`: 40
  - `ieee80211_proto.h`: 6
  - `ieee80211_node.h`: 4
  - `ieee80211_output.c`: 19
  - `ieee80211_vht.c`: 73
  - `ieee80211_crypto.h`: 274, mostly the doc and key-accessor additions
    in main
- This is a **recent** net80211, consistent with a FreeBSD 15.0 sync.

### 2.3 Existing wlan drivers [V]

- `iaxwifi200` (`iwx`) and `idualwifi7260` (`iwm`) now use
  `HAIKU_DRIVER_REQUIREMENTS(OBSD_WLAN)` and link `openbsd_wlan.a` +
  `libfreebsd_network.a`, so they are OpenBSD-derived.
- Ten drivers use `FBSD_WLAN`.
- A glue file is about 20 lines: the GLUE macro, MII and interrupt hooks,
  and the firmware version and name map. `idualwifi7260/glue.c` is the
  template. The Jamfile builds a `KernelAddon` from the driver sources plus
  `glue.c`, linked against the static compat libs.

---

## 3. Dependency map and coverage matrix

### 3.1 Scope

These are the LinuxKPI sources a PCIe wifi driver needs.

- From FreeBSD's `sys/modules/linuxkpi/Makefile`: `linux_compat`,
  `current`, `devres`, `domain`, `firmware`, `hrtimer`, `idr`, `interrupt`,
  `kmod`, `kobject`, `kthread`, `lock`, `netdev`, `page`,
  `linuxkpi_page_pool`, `pci`, `radix`, `rcu`, `schedule`, `skbuff`,
  `slab`, `tasklet`, `timesub`, `work` and `xarray`.
- From `linuxkpi_wlan`: `linux_80211.c` (10,309 lines),
  `linux_80211_macops.c` and `linuxkpi_80211_pm.c`.
- Not needed for wifi: i2c, hdmi, aperture, shmemfs, usb, acpi, dmi and
  mhi.

Sizes:

- LinuxKPI `common/src`: 32,879 lines in total
- LinuxKPI headers: 310
- mt76 core + connac + mt792x + mt7921 PCI: about 18,100 lines [V, wc]

### 3.2 FreeBSD KPIs that LinuxKPI calls, against Haiku freebsd_network/wlan

**Method:** every FreeBSD-KPI-shaped call in the files above, about 300
symbols, was grepped across Haiku's `freebsd_network` and `freebsd_wlan`.
Linux-side false positives were filtered by hand. Probe output is in the
appendix. "Present" means the identifier exists in Haiku's layer. **It does
not prove matching semantics**; stage (a) compile and runtime tests settle
that.

**Present [V]:**

- mtx, condvar (`cv_timedwait`), `msleep`, `mtx_sleep`, `wakeup`, `pause`
- `callout_reset`, `callout_drain`
- taskqueue, including `taskqueue_enqueue_timeout` and
  `taskqueue_drain_all`
- `kthread_add`
- `malloc(9)`, `contigmalloc`, `contigfree`
- the full `bus_dma` API
- `bus_alloc_resource_any`, `bus_setup_intr`, `device_add_child`
- PCI config space, MSI, MSI-X
- `firmware_get` / `firmware_get_flags` / `firmware_put`
- `SYSINIT`, `EVENTHANDLER_REGISTER`, `sysctl_add_oid`,
  `SYSCTL_ADD_NODE`
- `counter_u64_alloc`, `ticks`, `mp_ncpus`
- mbuf, ifnet, sbuf
- net80211, except the 6 accessors in §3.3

**Missing, grouped by subsystem, with the decision for each [V gap / H
decision]:**

| Subsystem | Missing symbols (exact) | Used by | Decision |
|---|---|---|---|
| sleepqueue | `sleepq_lock/add/wait/wait_sig/timedwait/timedwait_sig/set_timeout/signal/broadcast/release` | `linux_schedule.c` (wait_event, schedule_timeout, completions) | **Shim in linuxkpi/haiku/**: implement on Haiku `ConditionVariable` + hash of wait channels. LinuxKPI-only. |
| thread/proc | `tdfind`, `tdsignal`, `PROC_LOCK/UNLOCK`, `thread_reap_barrier`, `kproc_create` | `linux_current.c`, `linux_kthread.c`, signals | Shim: map `task_struct` ↔ Haiku `thread_id`; signals return "not pending" initially. |
| UMA | `uma_zcreate/zdestroy/zalloc/zalloc_arg/zfree/zfree_arg/zcache_create/zone_reserve/prealloc` | `linux_slab.c` (kmem_cache), `linux_idr.c` | **Extend freebsd_network**: a `compat/vm/uma.h` built on Haiku `object_cache` [H: verify Haiku's slab API]. Generic, upstreamable. |
| per-CPU / CPU sets | `DPCPU_DEFINE_STATIC/GET/ID_GET/ID_PTR`, `CPU_FOREACH/SET/COPY/ABSENT`, `sched_pin/unpin/bind/unbind/is_bound`, `smp_rendezvous` | `linux_rcu.c`, `linux_current.c` | Extend freebsd_network (arrays sized by `smp_get_num_cpus()` [H]). `sched_bind` can approximate with pinning in Haiku [H: check `thread_pin_to_current_cpu`]. |
| epoch / RCU | `ck_epoch_init/register/begin/end/synchronize_wait` | `linux_rcu.c` | Shim: implement RCU directly (counter + grace-period wait) instead of porting ConcurrencyKit. Correct but slower. |
| VM pages | `vm_page_alloc_noobj[_contig[_domain]]`, `vm_page_free`, `vm_page_*` (wire, busy, dirty, fake), `kmem_alloc_contig`, `kmem_free`, `PHYS_TO_DMAP`, `pmap_qenter/qremove/mapdev_attr/unmapdev/...`, `vm_wait`, `vm_free_count` | `linux_page.c` (alloc_pages, which `page_pool` needs), `linuxkpi_page_pool.c` | Shim: `struct page` backed by Haiku areas (`create_area_etc` with physical restrictions [H]). Only the alloc/free/virt/phys subset is needed for wifi. mmap, pager and `vm_object` parts are **DRM-only, defer**. |
| newbus dynamic | `devclass_create`, `devclass_add_driver`, `devclass_delete_driver`, `device_add`, `device_del`, `device_initialize`, `device_find_child`, `device_get_driver`, `bus_attach_children`, `bus_identify_children` | `linux_pci.c` registration | Shim plus glue change (§5). Adding `devclass_add_driver` to freebsd_network may be upstreamable. |
| rwlock(9), sx extras | `rw_init`, `rw_destroy`, `sx_init_flags`, `sx_xlock_sig`, `sx_slock_sig` | `linux_lock.c`, `linux_compat.c` | Extend freebsd_network `compat/sys/rwlock.h` and `sx.h` on Haiku `rw_lock`. Generic. |
| refcount(9) | `refcount_set/acquire/acquire_if_not_zero/release/release_if_last` | many | Extend freebsd_network (header-only, BSD source available). |
| sbt time | `callout_reset_sbt`, `callout_schedule_sbt`, `pause_sbt`, `callout_deactivate`, `sbinuptime` | `linux_hrtimer.c`, `linux_schedule.c` | Extend freebsd_network: sbintime is 32.32 fixed point; map it to `system_time()` µs. |
| pctrie / xarray | `pctrie_init/is_empty/iter_reset/node_size`, `VM_RADIX_FOREACH_FROM` | `linux_xarray.c`, `linux_page.c` | Import `sys/kern/subr_pctrie.c` (BSD) into the new layer [H: check its dependencies]. |
| misc | `bus_get_domain`, `contigmalloc_domainset`, `bus_dma_id_mapped`, `bus_translate_resource`, `pci_get_subclass`, `pci_find_bsf`, `pci_disable_io`, `device_set_descf`, `device_set_wakeup_capable`, `taskqueue_poll_is_busy`, `TASK_INITIALIZER`, `SYSCTL_ADD_ROOT_NODE`, `SYSCTL_COUNTER_U64`, `atomic_fcmpset_int`, `atomic_cmpset_acq_long` | various | Small; add to freebsd_network where generic (`pci_get_subclass`, atomics). Otherwise stub. |
| cdev / devfs / mmap | `destroy_dev`, `vm_mmap_object`, `vm_pager_allocate`, `vm_object_*`, `vmmap_add/remove`, `vm_fault_quick_hold_pages` | `linux_compat.c` (char devices, mmap) | **Stub for wifi; real work only for DRM.** Split `linux_compat.c` or `#ifdef` it. |

### 3.3 net80211 delta [V]

Of 496 net80211 identifiers used by `linux_80211*.c` / `linux_80211.h`,
the only ones defined in FreeBSD main's net80211 but absent from Haiku's are
these six:

```
ieee80211_crypto_get_key_data      ieee80211_crypto_get_key_len
ieee80211_crypto_get_key_rxmic_data ieee80211_crypto_get_key_rxmic_len
ieee80211_crypto_get_key_txmic_data ieee80211_crypto_get_key_txmic_len
```

They are trivial key-accessor inlines added to FreeBSD's
`ieee80211_crypto.h`. Backport them into Haiku's freebsd_wlan as one small,
upstreamable patch. Hardware crypto is off by default anyway (`hw_crypto`
sysctl). This method only catches *missing* identifiers, not changed struct
layouts or semantics; the compile in stage (a)/(d) is the real test.

---

## 4. Driver choice

Your card: MT7921, PCI vendor `14c3`. `mt7921/pci.c` matches device
`0x7961`, `0x0608`, `0x0616`, `0x7920` and `0x7922` (also ITTIM `0x7922`).
[V] Please confirm yours with `listdev`.

Firmware:

- `mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin` (ROM patch)
- `mediatek/WIFI_RAM_CODE_MT7961_1.bin` (WM), or `…_1a.bin` on MT7920
  (`mt792x.h:44-50`) [V]

Both are from linux-firmware, under MediaTek's redistributable licence [V].

| | **mt7921 (yours)** | rtw88 | rtw89 | iwlwifi |
|---|---|---|---|---|
| FreeBSD state | WIP; passes packets on main; hw crypto, detach and IDR fixes pending [V] | shipping [V] | shipping, assoc regressions after Linux 7.0 sync [V] | shipping [V] |
| License | BSD-3-Clause-Clear, all files we need [V] | dual GPL/BSD [H] | dual GPL/BSD [H] | dual GPL/BSD [H] |
| Native Haiku driver? | none [V] | none [H] | none [H] | partly covered by OpenBSD-derived `iwm`/`iwx` [V drivers exist; H coverage] |
| Extra LinuxKPI surface | page_pool, DMA32, IDR, napi | smaller [H] | large [H] | largest [H] |
| Code size (driver) | ~18k lines incl. mt76 core [V] | [U] | [U] | [U] |

**Decision: MT7921 is the first target.** It's your hardware, nothing else
on Haiku supports it, the license is clean, and the LinuxKPI surface it
needs is the same core every other driver needs anyway.

The page_pool and DMA32 extras are bounded:

- Haiku's `bus_dma` already does DMA32 [V].
- `linuxkpi_page_pool.c` is 1 file.

**Optional backup card:** a cheap rtw88 M.2 card (RTL8821CE or RTL8822CE).
It would let us separate "layer bug" from "mt76 bug" at stages (c)–(e). It
isn't needed for stages (a)–(b).

---

## 5. Architecture

```
src/libs/compat/linuxkpi/                 → liblinuxkpi.a (KernelStaticLibrary)
    common/include/   imported from FreeBSD sys/compat/linuxkpi/common/include
    common/src/       imported linux_*.c (wifi subset, §3.1)
    haiku/            Haiku-only shims: sleepq, rcu, page, task_struct, newbus-dyn
    IMPORTED.md       per-file origin, FreeBSD commit, SPDX
src/libs/compat/linuxkpi_wlan/            → liblinuxkpi_wlan.a
    linux_80211.c, linux_80211_macops.c, linuxkpi_80211_pm.c (imported)
src/add-ons/kernel/drivers/network/wlan/mediatekwifi/
    glue.c            Haiku glue (§5.1)
    mt76/             imported from FreeBSD sys/contrib/dev/mediatek/mt76 @ pinned commit
    Jamfile           KernelAddon mediatekwifi : mt76 sources glue.c :
                        liblinuxkpi_wlan.a liblinuxkpi.a freebsd_wlan.a libfreebsd_network.a
patches/haiku/        freebsd_network + freebsd_wlan extensions (generic gaps, §3.2; 6 net80211 accessors)
```

### 5.1 Glue: bridging dynamic registration [H design, V constraints]

- Haiku calls `__haiku_init_hardware()` and expects a `driver_t*` array.
- LinuxKPI only creates its `driver_t` (`pdrv->bsddriver`) inside
  `linux_pci_register_driver()`, which then calls `devclass_add_driver()`.
- `mediatekwifi/glue.c` will therefore not use
  `HAIKU_FBSD_WLAN_DRIVER_GLUE`. Instead it writes the same expansion by
  hand:
  1. Run the LinuxKPI `SYSINIT` and `module_init` chain. That calls
     `pci_register_driver(&mt7921_pci_driver)`.
  2. Our `devclass_add_driver()` shim records `&pdrv->bsddriver`.
  3. Pass that pointer in the `drivers[]` array to
     `_fbsd_init_hardware_pci()`, and use `HAIKU_FBSD_WLAN_DRIVERS_GLUE`
     for the rest.
- This keeps Haiku's device-hook model untouched.

### 5.2 Other design points

- **Interrupts:** LinuxKPI's `request_irq` → `bus_setup_intr` (present).
  Use `HAIKU_CHECK_DISABLE_INTERRUPTS` returning 1 under MSI or MSI-X, as
  `idualwifi7260` does.
- **Taskqueues:** `HAIKU_DRIVER_REQUIREMENTS(FBSD_WLAN)`, because LinuxKPI
  802.11 uses the thread taskqueue for its TX and RX tasks.
- **Upstreaming** happens in three independent series:
  1. generic freebsd_network/wlan additions
  2. the linuxkpi libraries
  3. the mediatekwifi driver

  Series 1 has value on its own and goes to Gerrit first.

---

## 6. Version strategy and risk

**Strategy:** import LinuxKPI and mt76 from **FreeBSD main at a pinned
commit** (currently `b556d9c9ce94`). Backport the net80211 delta into Haiku
(6 accessors, plus whatever compile-time struct differences stage (d)
reveals). [V that the delta is small]

The old risk, "Haiku's net80211 is years behind", is **disproved**.

### Risks

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | net80211 struct or semantic drift beyond missing symbols | low–medium | compile in stage (d); diff of var.h is only 40 lines [V] |
| R2 | mt7921 itself immature on FreeBSD (hw crypto TX bug, detach panics) | medium | software crypto; re-pin to newer main as fixes land |
| R3 | `vm_page` / page_pool shim correctness (DMA32, page refcounts) | high | stage (c) exercises it directly; stress test RX |
| R4 | sleepq and RCU emulation (deadlocks, lost wakeups) | high | unit-style kernel tests in stage (a); KDL tracing |
| R5 | `__FreeBSD__` ifdefs in mt76 assume a real FreeBSD (139 blocks) | medium | define `__FreeBSD__` only for imported objects; review each hit at stage (c) |
| R6 | LinuxKPI churn (it moves weekly) | ongoing | pinned commits in `IMPORTED.md`; a sync script |
| R7 | no network fallback for this card | — | keep ethernet or USB tethering during tests |
| R8 | some LinuxKPI headers have no license text (e.g. `linux/jhash.h`, `llist.h`, `ratelimit.h`) [V] | low | per-file review before import; jhash is Bob Jenkins public domain [H] |

---

## 7. Licensing per reused component [V unless noted]

| Component | License | OK for Haiku |
|---|---|---|
| LinuxKPI `common/src/*.c` | BSD-2-Clause (Mellanox, FreeBSD Foundation and others); `linux_siphash.c` is GPL-2.0-only OR BSD-3-Clause | yes (take siphash under BSD-3; not needed for wifi anyway) |
| LinuxKPI `common/include` | mostly BSD-2; some headers lack a license block | review each header before import |
| mt76 core, connac, mt792x, mt7921 | `BSD-3-Clause-Clear` (83 SPDX lines across the files we need; relicensed upstream 2025, FreeBSD import 2026-01-23) | yes; "Clear" = no patent grant, compatible with MIT distribution |
| mt76x0, two mt7615 files | GPL-2.0 (per upstream commit) | not imported |
| MT7961 firmware | MediaTek redistributable licence | binary; Haiku firmware-package policy [U] |
| Haiku shims (ours) | MIT | yes |

---

## 8. Staged plan

| Stage | Pass criterion | Main work |
|---|---|---|
| **a0** | `tools/check-module.sh` compiles every imported file against Haiku master headers in kernel mode; `tools/coverage-probe.sh` reproduces §3 automatically | port Renguin's script; add compat include paths and `_KERNEL=1` from the Jamfiles above |
| **a** | `liblinuxkpi.a` builds; a toy Linux-style `pci_driver` matching `14c3:7961` attaches, logs BAR0 size and IRQ vector, detaches without KDL | gap shims: newbus-dyn, sleepq, rwlock, refcount, sbt, per-CPU, task_struct |
| **b** | toy driver `request_firmware()`s both MT7961 blobs and logs size + CRC | install blobs under `/boot/system/non-packaged/data/firmware/mediatekwifi/` |
| **c** | mt7921e probes: ROM patch + WM firmware load, MCU ready, MAC address in syslog | UMA, page/page_pool shim, IDR, napi |
| **d** | `ifconfig /dev/net/mediatekwifi/0 scan` lists SSIDs | `liblinuxkpi_wlan.a` against freebsd_wlan; 6 net80211 accessors |
| **e** | WPA2 join and DHCP via NetworkStatus | software crypto path |
| **f** | iperf 1 h, then 24 h; driver unload/reload clean; suspend later | detach fixes from FreeBSD |

---

## 9. Previous Haiku attempts [V]

- **GSoC 2017 (Vivek):** a Linux i915 port using DragonFly's Linux compat
  headers. It stalled while adding headers and dependencies one at a time
  (weekly report 4). Trac #13665 records it as not completed.
- **pulkomandy** rebased Hamish Morrison's `haiku-drm` onto Gerrit
  changes 430–436 and marked Vivek's patches obsolete.
- **Trac #16272:** pulkomandy's later comment points at Gerrit 436 and
  suggests KMS (modesetting) first.

**Lessons for us:**

- Don't bulk-copy headers.
- Drive the layer from one real driver.
- Measure gaps mechanically (the §3 probe).
- Reuse the existing FreeBSD layer instead of a parallel one.

All the earlier attempts bypassed that layer.

## 10. Later: DRM/KMS and Renguin [H]

**What DRM adds:** mainly the cdev/mmap/`vm_object` row of §3.2 (stubbed
for wifi), plus `dma-buf`, `ww_mutex`, `shmem`, ACPI and DMI. Then import
drm-kmod (amdgpu or i915), KMS first.

**The Renguin link:**

- Expose DRM nodes in Haiku devfs, for example `/dev/graphics/drm/...`.
  #16272 already asks for a name-based replacement for libdrm's
  major/minor scheme [V].
- Renguin maps the Linux `/dev/dri/card0` and `renderD128` to those nodes,
  and forwards DRM ioctls (fixing 32/64-bit layouts).
- One kernel driver then serves both Haiku-native Mesa (via libdrm) and
  Linux Mesa running under Renguin.

---

## Appendix: raw probe output

Missing in Haiku freebsd_network/freebsd_wlan (after removing Linux-side
false positives):

```
CPU_ABSENT CPU_COPY CPU_FOREACH CPU_SET DPCPU_DEFINE_STATIC DPCPU_GET DPCPU_ID_GET DPCPU_ID_PTR PHYS_TO_DMAP PROC_LOCK PROC_LOCK_ASSERT PROC_UNLOCK SYSCTL_ADD_ROOT_NODE SYSCTL_COUNTER_U64 TASK_INITIALIZER VM_OBJECT_ASSERT_WLOCKED VM_OBJECT_RLOCK VM_OBJECT_RUNLOCK VM_OBJECT_WLOCK VM_OBJECT_WUNLOCK VM_RADIX_FOREACH_FROM atomic_cmpset_acq_long atomic_fcmpset_int bus_attach_children bus_dma_id_mapped bus_get_domain bus_identify_children bus_translate_resource callout_deactivate callout_reset_sbt callout_schedule_sbt ck_epoch_begin ck_epoch_end ck_epoch_init ck_epoch_register ck_epoch_synchronize_wait contigmalloc_domainset destroy_dev devclass_add_driver devclass_create devclass_delete_driver device_add device_del device_find_child device_get_driver device_initialize device_set_descf device_set_wakeup_capable kmem_alloc_contig kmem_free pause_sbt pci_disable_io pci_find_bsf pci_get_subclass pctrie_init pctrie_is_empty pctrie_iter_reset pctrie_node_size pmap_extract_and_hold pmap_invalidate_cache pmap_mapdev_attr pmap_page_is_mapped pmap_page_set_memattr pmap_qenter pmap_qremove pmap_remove_all pmap_unmapdev refcount_acquire refcount_acquire_if_not_zero refcount_release refcount_release_if_last refcount_set rw_destroy rw_init sched_bind sched_is_bound sched_pin sched_unbind sched_unpin sleepq_add sleepq_broadcast sleepq_lock sleepq_release sleepq_set_timeout sleepq_signal sleepq_timedwait sleepq_timedwait_sig sleepq_wait sleepq_wait_sig smp_rendezvous sx_init_flags sx_slock_sig sx_xlock_sig taskqueue_poll_is_busy tdfind tdsignal thread_handler thread_reap_barrier uma_prealloc uma_zalloc uma_zalloc_arg uma_zcache_create uma_zcreate uma_zdestroy uma_zfree uma_zfree_arg uma_zone_reserve vm_fault_quick_hold_pages vm_free_count vm_map_range_valid vm_mmap_object vm_object_deallocate vm_object_set_memattr vm_page_alloc_noobj vm_page_alloc_noobj_contig vm_page_alloc_noobj_contig_domain vm_page_busy_acquire vm_page_clear_dirty_mask vm_page_dirty vm_page_free vm_page_getfake vm_page_grab_iter vm_page_initfake vm_page_iter_init vm_page_iter_insert vm_page_iter_limit_init vm_page_reclaim_contig vm_page_remove vm_page_replace vm_page_unwire vm_page_unwire_noq vm_page_updatefake vm_page_valid vm_page_xunbusy vm_pager_allocate vm_pager_page_unswapped vm_wait vmmap_add vmmap_remove
```

---

## Phase 2 addendum (stage a build-out, Haiku @ b9fdc8a9e699, FreeBSD @ b556d9c9ce94)

Findings made while building stage (a). They correct or sharpen the report above.

1. **[V] Upstream Haiku bug: compat `atomic_set_32` is a store.** FreeBSD atomic(9) defines
   `atomic_set_*` as bitwise OR. Haiku's freebsd_network `machine/atomic.h` maps it to Haiku's
   `atomic_set` (a store). net80211's `ieee80211_com_vdetach` reference wait relies on OR
   semantics. Fixed in `patches/haiku/0004`; worth sending upstream on its own.
2. **[V] Probe return convention.** Haiku's compat treats probe results `>= 0` as a match
   (`BUS_PROBE_DEFAULT` is +20 here), so the LinuxKPI PCI bridge returns `-1` (not `ENXIO`) for
   "no match".
3. **[V] `init_driver` needs an ifnet.** freebsd_network fails `init_driver` when
   `gDeviceCount == 0`, and that error path does not detach devices already attached. Stage (a)
   therefore runs its self-test inside probe and declines the device (`-ENODEV`) on purpose.
4. **[V] Errno sign.** Haiku errnos are negative; LinuxKPI assumes positive FreeBSD values. LinuxKPI
   units are compiled with FreeBSD values (`lkpi_errno_redefine.h`) and translated at every call
   into freebsd_network (`lkpi_haiku_wrap.h`).
5. **[V] Linker sets need merge objects.** Objects reached only through SYSINIT linker sets (e.g.
   `linux_current.c`) are dropped from `.a` archives, so the core is built as KernelMergeObjects.
6. **[V] Interrupt handlers are threaded** under the compat layer (no filter stage), like FreeBSD
   ithreads. `HAIKU_CHECK_DISABLE_INTERRUPTS` cannot quiesce an arbitrary Linux device, so stage (a)
   uses MSI only. Shared INTx remains unsafe.
7. **[V] `critical_enter` only pins the thread on Haiku**, so the derived RCU reader
   (`lkpi_rcu.c`) brackets ck_epoch begin/end with a spinlock and interrupts disabled.
8. **[V] `LINUXKPI_VERSION`.** The core must be built without it (the 61900 value broke the
   `list_sort` prototype); drivers such as mt76 use 70000.
9. **[V] No `__divti3`** in the Haiku kernel; sbt arithmetic was rewritten without 128-bit division.
10. **[V] Link check.** The stage (a) add-on links against 18 kernel imports beyond what
    freebsd_network already uses, each verified in Haiku sources; 0 unresolved symbols.
