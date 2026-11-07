# Status

Last updated 2026-11-06. Haiku @ `b9fdc8a9e699`, FreeBSD @ `b556d9c9ce94`.

## Stages

| Stage | Goal | Status |
|---|---|---|
| a | LinuxKPI core loads; test driver probes MT7921, maps BAR0, installs MSI | **Builds, links (0 unresolved). Awaiting hardware test.** |
| b | `request_firmware` returns the MT7921 blobs | Code in test driver; awaiting hardware |
| c | DMA API (`linux_dma_*`), import mt76 + mt7921e, probe and MCU boot | Not started |
| d | `linuxkpi_wlan` (mac80211 → net80211), scan | Files imported, not compiled or wired in |
| e | WPA2 association, DHCP, traffic | Not started |
| f | Suspend/resume, stability, upstreaming | Not started |

## Verification so far

| Check | Result |
|---|---|
| Every source compiled with Haiku x86_64 kernel flags (`tools/check-module.sh`) | [V] pass |
| `ld -r` of the add-on; every undefined symbol checked against freebsd_network imports or Haiku kernel source (`tools/link-check.sh`) | [V] 0 unresolved; 18 new kernel imports |
| Patches apply to a clean tree; `install.sh` idempotent | [V] pass |
| Built with Haiku's own jam toolchain | [U] not yet — needs a Haiku build host |
| Runs on hardware | [U] not yet |

New kernel imports beyond freebsd_network's: `call_all_cpus_sync`,
`create_object_cache_etc`, `debug_debugger_running`, `get_cpuid`,
`get_system_info`, `mutex_trylock`, `object_cache_reserve`,
`object_cache_set_minimum_reserve`, `realloc`, `send_signal_etc`,
`smp_get_current_cpu`, `smp_get_num_cpus`, `strchr`, `thread_yield`,
`user_strlcpy`, and C++ `new(nothrow)`/`delete`.

## Known limits

- Per-thread records are never freed (small leak per thread that touches LinuxKPI).
- `malloc_usable_size` returns 0.
- Memory attributes (write-combining, uncached) are not applied to page allocations.
- One Haiku area per page allocation — fine for a NIC, wasteful at scale.
- DMA API not implemented (stage c).
- Tasklets are not bound to CPUs.
- Shared INTx is unsafe; MSI only.
- `pci_find_*` lookups return NULL.
- `sx` shared mode is exclusive.
- `sysctl` is a no-op.
- The test driver declines the device after its self-test (see ARCHITECTURE/RESEARCH addendum, item 3), so no network interface appears in stage (a).
- 44 imported upstream files carry no license header; flagged in `IMPORTED.md`.
