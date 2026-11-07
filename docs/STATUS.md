# Status

Last updated 2026-11-06. Haiku @ `b9fdc8a9e699`, FreeBSD @ `b556d9c9ce94`.

## Stages

| Stage | Goal | Status |
|---|---|---|
| a | LinuxKPI core loads; test driver probes MT7921, maps BAR0, installs MSI | **[V] Passed on hardware 2026-11-06** (MT7922 `14c3:0616`): SYSINITs ran in order, BAR0 mapped, MSI vector 64 installed, clean detach and SYSUNINIT, device unreserved |
| b | `request_firmware` returns the MT7921 blobs | **[V] Passed on hardware 2026-11-06**: both MT7922 blobs loaded (137632 and 1233748 bytes; crc32 `7a55f5fd`, `2144df1c`) |
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
| Built with Haiku's own jam toolchain | [V] builds on Haiku (gcc 13.3 cross tools) |
| Runs on hardware | [V] stages a and b, x86-64, MT7922 `14c3:0616` |

New kernel imports beyond freebsd_network's: `call_all_cpus_sync`,
`create_object_cache_etc`, `debug_debugger_running`, `get_cpuid`,
`get_system_info`, `mutex_trylock`, `object_cache_reserve`,
`object_cache_set_minimum_reserve`, `realloc`, `send_signal_etc`,
`smp_get_current_cpu`, `smp_get_num_cpus`, `strchr`, `thread_yield`,
`user_strlcpy`, and C++ `new(nothrow)`/`delete`.

## First hardware run (2026-11-06)

- `pci_enable_io(4) failed.` is logged by freebsd_network when LinuxKPI's
  `pci_enable_device` also enables I/O-port decoding; the MT7922 has no I/O
  BAR, so this is expected and harmless (memory decoding succeeded).
- "0 interrupts seen" is expected: the test driver never enables an interrupt
  source on the device.
- Bugs found and fixed during the run: jam `LinkAgainst` turned the merge
  objects into `-l` flags (`06c0d1f`); MT7922 needs its own firmware names
  (`471192a`); an earlier out-of-tree MT7921 driver held the PCI device.

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
