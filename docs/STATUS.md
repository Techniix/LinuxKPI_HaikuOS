# Status

Last updated 2026-11-06. Haiku @ `b9fdc8a9e699`, FreeBSD @ `b556d9c9ce94`.

## Stages

| Stage | Goal | Status |
|---|---|---|
| a | LinuxKPI core loads; test driver probes MT7921, maps BAR0, installs MSI | **[V] Passed on hardware 2026-11-06** (MT7922 `14c3:0616`): SYSINITs ran in order, BAR0 mapped, MSI vector 64 installed, clean detach and SYSUNINIT, device unreserved |
| b | `request_firmware` returns the MT7921 blobs | **[V] Passed on hardware 2026-11-06**: both MT7922 blobs loaded (137632 and 1233748 bytes; crc32 `7a55f5fd`, `2144df1c`) |
| c | DMA API (`linux_dma_*`), import mt76 + mt7921e, probe and MCU boot | **[V] Firmware boots on hardware 2026-11-07** (MT7922: `HW/SW Version: 0x8a108a10`, `WM Firmware Version: ____000000`); attach deadlock on Giant fixed (`c987dfc`) |
| d | `linuxkpi_wlan` (mac80211 → net80211), scan | **[V] Works on hardware 2026-11-07**: scan finds the AP |
| e | WPA2 association, DHCP, traffic | **[V] First pass 2026-11-07**: WPA2-PSK/CCMP association (802.11n, HT40), DHCP lease, RX/TX packets. Open: A-MPDU TX session setup fails (`ieee80211_ampdu_request returned 0 != 1`), 1 TX error. **Wi-Fi-only traffic verified** (Ethernet unplugged: ping to gateway and internet 0% loss, ~9-25 ms; web browsing, including this project's chat session) |
| f | Suspend/resume, stability, upstreaming | Not started |
| speed | Throughput tuning | 2026-11-07 [V]: A-MPDU TX setup fixed (patch 0006). Results depend on which mesh radio is joined: `…:e9:3e` ~617 down / ~43 up Mbps, `…:e9:3f` ~19 down / ~110 up (consistent over 3 runs). Ping 2 ms to gateway, 20 ms to 1.1.1.1. DMA bounces only ~6.5% of maps, 0 pool misses, so the copy is not the bottleneck. Next: Test B (`compat.linuxkpi.80211.hw_crypto 1`): with software crypto, LinuxKPI clears net80211's RX A-MPDU flag (`linux_80211.c:4533`) [H: cause of the per-band asymmetry] |
| mgmt | LinuxKPI Manager, `lkpictl`, read-only kernel status interface | Written 2026-11-07; kernel side links (`link-check.sh`), userland passes a syntax check against Haiku headers; **not yet built or run on Haiku** (docs/MANAGER.md) |

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
- Tasklets are not bound to CPUs.
- Shared INTx is unsafe; MSI only.
- `pci_find_*` lookups return NULL.
- `sx` shared mode is exclusive.
- `sysctl` has no tree; `CTLFLAG_TUN` values are read from the driver settings file at load (`~/config/settings/kernel/drivers/<driver>`), not changeable while running.
- The test driver declines the device after its self-test (see ARCHITECTURE/RESEARCH addendum, item 3), so no network interface appears in stage (a).
- 44 imported upstream files carry no license header; flagged in `IMPORTED.md`.
- DMA: a streaming mapping above the device's DMA mask is copied through a
  512-slot bounce pool (larger ones get their own `contigmalloc()`). Page
  allocations prefer memory below 4 GiB to make bouncing rare.
- LinuxKPI 802.11's suspend/resume helper (`linuxkpi_80211_pm.c`) is not
  built: it needs newbus identify methods Haiku's compat layer lacks.
- `sched_set_fifo*()` / raised kthread priorities map to Haiku
  `B_URGENT_DISPLAY_PRIORITY`, not a real-time class.
- Attach of a LinuxKPI 802.11 driver waits up to 20 s for the driver to
  register with mac80211 (needed by freebsd_wlan's `start_wlan()`).
