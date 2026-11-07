# Architecture

Versions: FreeBSD `main` @ `b556d9c9ce94`, Haiku @ `b9fdc8a9e699`.

```
 Linux driver (mt76 / lkpi_pcitest)        LINUXKPI_VERSION=70000
 ───────────────────────────────────────
 LinuxKPI core   common/src/*.c (verbatim) + generated lkpi_compat.c, lkpi_rcu.c
 Haiku shims     haiku/*.c, haiku/*.cpp, overlay headers haiku/include/
 ───────────────────────────────────────
 Haiku freebsd_network  (+ freebsd_wlan / net80211 later)
 ───────────────────────────────────────
 Haiku kernel
```

## Include order [V]

1. `haiku/include` — Haiku overlays of FreeBSD headers that freebsd_network
   lacks or gets wrong for LinuxKPI (`sys/proc.h`, `sys/sx.h`, `machine/atomic.h`, `vm/*` …)
2. `freebsd/include`, `freebsd/ck/include` — verbatim FreeBSD headers
3. `common/include` — LinuxKPI `linux/*`, `asm/*`
4. `haiku/`, `src/libs`, then the freebsd_network compat dirs, then system headers

Every LinuxKPI unit is compiled with `-include lkpi_haiku_preinclude.h`, which:

- renames tokens that clash with Haiku's kernel API (`atomic_set` → `lkpi_atomic_set`,
  `mutex` → `lkpi_mutex` after `typedef mutex haiku_mutex`, `tm`, `umode_t`,
  `add_timer`, `timer_delete`);
- switches errno values to FreeBSD's (positive) numbers;
- wraps the freebsd_network functions LinuxKPI calls so their (negative,
  Haiku) errors are translated back (`lkpi_haiku_wrap.h`).

## Initialisation [V]

1. `init_hardware`: freebsd_network probes the bridge driver
   `DRIVER_MODULE_NAME(lkpi, pci)` (`lkpi_pci.c`). It matches PCI IDs against
   every `MODULE_DEVICE_TABLE` collected in the `__lkpi_pnp` linker set. No
   match returns `-1` (Haiku treats `>= 0` as a match).
2. `init_driver`: Haiku's `init_sysinit` runs our single `__freebsd_sysinit`
   entry (`lkpi_sysinit.c`). It sets up sleepqueues and DPCPU, then runs the
   LinuxKPI SYSINITs from `__lkpi_sysinit` sorted by (subsystem, order).
   `module_init` runs here and calls `pci_register_driver`.
3. Bridge attach calls the Linux driver's `probe`.

## Subsystem mapping

| LinuxKPI / FreeBSD need | Haiku implementation | File |
|---|---|---|
| `curthread`, sleepqueue, `pause` | per-thread record in a hash keyed by `thread_id`, Haiku `ConditionVariable` | `lkpi_thread.cpp` |
| `sx` locks | Haiku mutex (shared = exclusive) | `lkpi_sx.c` |
| UMA zones | `object_cache` | `lkpi_uma.cpp` |
| pages, `kmem`, `ioremap` | contiguous areas + `vm_page` descriptors in a pfn hash; `map_physical_memory` | `lkpi_page.c` |
| RCU | ConcurrencyKit `ck_epoch` with a spinlock-guarded reader | `lkpi_rcu.c` (generated) |
| DPCPU | `set_lkpi_dpcpu` section copied per CPU | `lkpi_misc.c` |
| gtaskqueue / tasklets | freebsd_network taskqueues | `lkpi_gtaskqueue.c` |
| PCI (BARs, MSI, regions, pcim) | freebsd_network `pci_*` + Haiku `pci_info` | `lkpi_pci.c`, `lkpi_haiku_pci.c` |
| newbus extras | devclass table, `device_probe_and_attach` | `lkpi_newbus.c` |
| `SYSINIT` ordering | own linker set + sort | `lkpi_sysinit.c` |
| interrupts | freebsd_network threaded handlers, MSI only | `lkpi_pci.c`, driver glue |
| firmware | freebsd_network `firmware(9)` → `/boot/system/[non-packaged/]data/firmware/<driver>/` | glue name map |

## Haiku changes

Kept in `patches/haiku/` against `BASE_COMMIT`:

| Patch | Why |
|---|---|
| 0001 | `taskqueue_enqueue_flags`, `taskqueue_poll_is_busy` (LinuxKPI work queues) |
| 0002 | net80211 key accessors used by `linuxkpi_wlan` |
| 0003 | build hooks for `linuxkpi` and `lkpi_pcitest` |
| 0004 | **upstream bug:** compat `atomic_set_*` must be OR (atomic(9)), not store |

## Generated files

| File | Generator | Source |
|---|---|---|
| `haiku/lkpi_compat.c` | `tools/extract-compat.py` | `linux_compat.c` (keeps 66 functions; cdev/file/vma parts dropped) |
| `haiku/lkpi_rcu.c` | `tools/derive.py` | `linux_rcu.c` |
| `haiku/include/lkpi_errno_redefine.h` | from `lkpi_errno_list.h` | — |
| `IMPORTED.md` | `tools/import-freebsd.sh` | — |
