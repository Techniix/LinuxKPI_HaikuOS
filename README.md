# LinuxKPI_HaikuOS

A LinuxKPI layer for [Haiku](https://www.haiku-os.org/), modelled on FreeBSD's
`sys/compat/linuxkpi` and stacked on Haiku's existing `freebsd_network` /
`freebsd_wlan` compatibility layers. The goal is to run Linux kernel drivers
on Haiku with minimal changes to Haiku itself.

- **First target:** MediaTek MT7921 Wi-Fi (`mt7921e`, Linux mt76) on x86-64 real hardware.
- **Later:** DRM/KMS, connecting to the separate Renguin project.

> **Status:** stage (a) — the LinuxKPI core plus a PCI test driver — compiles
> and link-checks with 0 unresolved symbols against Haiku. **It has not run
> on hardware yet.** See [docs/STATUS.md](docs/STATUS.md).

## Layout

The `haiku/` tree mirrors a Haiku source checkout, so it can be copied
straight in:

```
haiku/src/libs/compat/linuxkpi/
  LinuxKPIRules            jam rule UseLinuxKPI, LINUXKPI_LIBS
  Jamfile                  linuxkpi.o (KernelMergeObject)
  common/                  FreeBSD LinuxKPI, verbatim (see IMPORTED.md)
  freebsd/                 other verbatim FreeBSD code (ConcurrencyKit, crc32, sys/tree.h)
  haiku/                   Haiku shims, overlay headers, generated files
haiku/src/add-ons/kernel/drivers/network/lkpi_pcitest/
                           stage (a)/(b) PCI test driver
patches/haiku/             small patches to Haiku itself (BASE_COMMIT)
tools/                     import, generate, compile-check, link-check, install
docs/                      architecture, status, testing, research report
IMPORTED.md                provenance + license of every imported file (generated)
```

Imported FreeBSD code is never edited in place. Where a FreeBSD file has to
change, a tool generates a Haiku version from it (`tools/extract-compat.py`,
`tools/derive.py`) so the change can be re-applied after an upstream refresh.

## Quick start (on Haiku)

```sh
tools/install.sh /path/to/haiku     # apply patches, copy haiku/ into the tree
cd /path/to/haiku/generated.x86_64  # your build directory
jam -q -j4 lkpi_pcitest
```

Then follow [docs/TESTING.md](docs/TESTING.md) for installing and testing on hardware safely.

## Documents

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the layers fit together
- [docs/STATUS.md](docs/STATUS.md) — what works, what doesn't, staged plan
- [docs/TESTING.md](docs/TESTING.md) — exact test and recovery steps
- [docs/RESEARCH.md](docs/RESEARCH.md) — Phase 1 report plus Phase 2 addendum

Claims in the docs are marked **[V]** verified in source, **[H]** hypothesis,
or **[U]** unverified.

## Licensing

Original code is MIT (see `LICENSE`). Imported FreeBSD code keeps its own
licenses (mostly BSD-2-Clause; ConcurrencyKit is BSD-2-Clause); every file is
listed with its detected license in `IMPORTED.md`. Upstream files that carry
no license text are flagged there for review.
