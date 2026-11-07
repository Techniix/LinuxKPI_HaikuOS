# LinuxKPI Manager

A native Haiku application, plus a command-line twin (`lkpictl`), for
seeing, configuring and troubleshooting LinuxKPI drivers without kernel
knowledge.

Claims are marked like the rest of the project: **[V]** verified in Haiku
source (tree at `patches/haiku/BASE_COMMIT`) or on hardware, **[H]** a
reasoned hypothesis, **[U]** unknown / still to check.

Status (2026-11-07): design done; the kernel status interface, the shared
library, `lkpictl` and a first version of the GUI are written. The userland
code passes a syntax check against Haiku's headers, and the kernel side links
in `tools/link-check.sh`. **None of it has been built with jam or run on
Haiku yet.**

## 1. Goals and non-goals

Goals:
- An ordinary user can answer "does my hardware have a LinuxKPI driver, is it
  working, and if not, why?" in one window.
- Safe changes only: everything the tool changes can be undone from the same
  tool, or from safe mode.
- The kernel layer stays independent: LinuxKPI never depends on the GUI, and
  works the same whether the tool is installed or not.
- New drivers and new LinuxKPI subsystems show up by adding data files,
  without changing the program.

Non-goals (for now): loading or unloading drivers while Haiku is running,
changing kernel state while a driver runs, editing driver code or firmware.

## 2. Architecture

```
 LinuxKPI Manager (GUI)         lkpictl (CLI)
   Interface Kit, BLayout,        printf
   BColumnListView, BTabView
            \                      /
             liblinuxkpi_manage.a          <- the only code that touches the system
   DriverCatalog  HardwareScan  KernelQuery  LogReader  SettingsStore  Report
        |              |             |           |            |
   data/linuxkpi   device manager  socket     /var/log/   ~/config/settings/
   manifests,      generic syscall ioctl      syslog      kernel/drivers/<drv>
   coverage,       (as listdev)      |                         |
   diagnostics                       v                         v (read at load)
 ------------------------------------------------------------------ kernel
                            network stack (datalink)
                                     |
                         freebsd_network compat_control()
                                     |  patch 0007: gDriverControlHook
                         LinuxKPI lkpi_control.c (READ-ONLY)
                                     |
                         LinuxKPI tunables, DMA counters, versions
```

Files:

| Part | Where |
|---|---|
| Kernel/userland ABI | `haiku/headers/private/linuxkpi/lkpi_control.h` |
| Kernel side (read-only) | `haiku/src/libs/compat/linuxkpi/haiku/lkpi_control.c` |
| Tunables from settings | `linuxkpi/haiku/include/sys/sysctl.h`, `lkpi_sysinit.c` |
| Hook in Haiku | `patches/haiku/0007-freebsd_network-driver-private-control-hook.patch` |
| Shared library | `haiku/src/apps/linuxkpimanager/core/` (`liblinuxkpi_manage.a`) |
| GUI | `haiku/src/apps/linuxkpimanager/` (`LinuxKPIManager`) |
| CLI | `haiku/src/bin/lkpictl/` |
| Build hooks | `patches/haiku/0008-build-hook-for-linuxkpi-manager-and-lkpictl.patch` |
| Data | `data/linuxkpi/{drivers/<name>,coverage,diagnostics}` |

Rule: the GUI and the CLI contain presentation only. Every decision about
what is safe, what a value may be, which file to touch, lives in the library,
so both front ends behave the same and there is one place to review.

## 3. What Haiku allows: feasibility

Each management operation the request asked for, checked against how Haiku
actually loads and runs drivers.

| Operation | Feasible? | How / why not |
|---|---|---|
| List PCI hardware | yes **[V]** | Device manager generic syscalls `DM_GET_ROOT/CHILD/NEXT_CHILD/NEXT_ATTRIBUTE`, as `listdev` and Devices use (`src/bin/listdev/dm_wrapper.c`). Read-only. PCI subsystem IDs are not published as attributes **[V]** (commented out in `listdev.cpp`). |
| Know which drivers exist and what they support | yes | Driver manifests (data files). Haiku has no driver metadata a program could read **[V]**: a legacy driver's supported IDs live only in its code. |
| Installed? | yes **[V]** | The binary in `add-ons/kernel/drivers/bin/` (user non-packaged, system non-packaged, system). |
| Enabled at boot? | yes **[V]** | devfs publishes a driver from the links in `add-ons/kernel/drivers/dev/` (`dev/net/mt7921e -> ../../bin/mt7921e`); this is how we have been switching mt7921e on and off. |
| Enable / disable at boot | yes, non-packaged only | Create or remove that link; takes effect at restart. A packaged driver's link is inside a read-only package; blocking it needs the package system's blocked-entries setting **[H]** (`/boot/system/settings/packages`, to be verified), roadmap phase 3. |
| Running? | yes **[V]** | `/dev/<publish>` exists; the network roster lists an interface named `/dev/net/<driver>/N`. |
| Load now | **no** (for now) | devfs loads on open or `rescan`, but `rescan` reuses an image already loaded in this session **[V, seen on hardware]**, and a driver that hangs in probe hangs the caller. Offered only as "enable at boot + restart". |
| Unload now | **no** (for network drivers) | The network stack keeps the device open while the interface exists, and `compat_open` allows one opener (`B_BUSY`) **[V]**. Removing the interface first and then hoping devfs unloads the image is not reliable **[U]**. |
| Interface up/down | yes **[V]** | `BNetworkInterface::SetFlags()`; already in Network preferences, so not duplicated. |
| Ask a running driver for status | yes, with patch 0007 | A socket ioctl naming the interface reaches `compat_control()` via `datalink_control()`'s default case and `ethernet_control()` **[V]**; unknown opcodes then go to `wlan_control()`, which rejects them **[V]**. Patch 0007 adds a driver hook in front of `wlan_control()`. The device node itself cannot be opened by a second program (`B_BUSY`) **[V]**. |
| Configure a driver | yes, at load time | LinuxKPI tunables (`CTLFLAG_TUN` sysctls, `module_param`s) are read from `~/config/settings/kernel/drivers/<driver>` when the driver loads (commit `fee1631`) **[V builds; U on hardware]**. Changing them while running is not offered. |
| Kernel messages | yes **[V]** | `/var/log/syslog` (+ `syslog.old`), readable by applications; driver lines carry `[<driver>]`. |
| Driver initialisation errors | partly | A driver that fails to attach publishes nothing, so it cannot be asked anything; its log lines and the "no interface" state are what we have. |
| API coverage | yes, as data | `data/linuxkpi/coverage`, maintained with `docs/STATUS.md`. Generating it from the build (roadmap phase 4) is possible but not done. |
| Permissions | — | Haiku has no privilege separation between the desktop user and system files: the first user is root **[V]**. "Safe" therefore has to come from the interfaces themselves (read-only kernel side, validated writes, confirmations), not from permission checks. |

## 4. The userland ↔ kernel interface

Defined in `lkpi_control.h`. Summary:

- **Transport:** `ioctl()` on an `AF_INET` datagram socket, with a request
  whose first 32 bytes are the interface name. The network stack finds the
  interface and passes the request down to the driver with `arg` still a
  userland pointer **[V]**.
- **Opcodes:** `LKPI_CTL_GET_INFO`, `LKPI_CTL_GET_STATS`,
  `LKPI_CTL_GET_TUNABLES`, all at `0x4c4b0001…`, far from Haiku's `SIOC*` and
  `B_*` codes. A driver built before this returns `B_BAD_VALUE`;
  `KernelQuery` reports that as "this build has no status interface".
- **Replies:** text, one `key=value` per line. New keys can be added at any
  time; readers ignore unknown keys. Only changing the request layout or the
  meaning of a key needs `LKPI_CONTROL_VERSION` to change.
- **Validation in the kernel** (`lkpi_control.c`): opcode range, magic,
  version, the request and reply buffer must be userland addresses, reply
  size capped at 64 KiB, reply built in a kernel buffer and copied with
  `user_memcpy()`. `B_BUFFER_OVERFLOW` plus `reply_size` tells the caller how
  much room it needs.
- **Read-only by design.** No opcode changes state. See §6 for how write
  operations should be added, if ever.

What a running driver reports today:

| Op | Keys |
|---|---|
| INFO | `abi`, `interface`, `driver`, `linuxkpi.version`, `linuxkpi.freebsd_import`, `linuxkpi.hz`, `loaded_for_ms` |
| STATS | `jiffies`, `dma.maps`, `dma.bounces`, `dma.bounce_pool_misses`, `threads.records_live`, `threads.records_created`, `threads.records_freed` |
| TUNABLES | each tunable's dotted key with its current value, and `<key>.flags` (`r`, `w`, `t`) |

Each LinuxKPI driver links its own copy of LinuxKPI (`LINUXKPI_LIBS` are
merge objects) **[V]**, so the interface is per driver; there is no
system-wide LinuxKPI to ask. Roadmap phase 5 covers a shared kernel module.

## 5. Data files

All use Haiku's driver_settings syntax, parsed with `load_driver_settings()`
in userland, so no new parser exists anywhere **[V: parsed with Haiku's own
parser in a host build]**. Search order: user non-packaged data, system
non-packaged data, system data; the first file of a name wins.

- **`linuxkpi/drivers/<name>` (driver manifest):** identity, origin and
  license, `publish` path, dependencies, firmware directory, supported devices
  with their firmware files, a feature list with states (works, partial,
  experimental, untested, missing), and the tunables the tool may offer, each
  with type, range, default, label, description and risk (safe, experimental,
  unsafe = never editable).
- **`linuxkpi/coverage`:** API areas with implemented, partial, stub or
  missing, and a note.
- **`linuxkpi/diagnostics`:** log substrings mapped to a severity, a
  plain-language title and advice. The first matching rule wins.

A new driver = a new manifest; a new subsystem = new coverage lines; a newly
understood failure = a new rule. None needs a program change.

## 6. Safety model

1. **Kernel side: read-only.** It cannot be used to change anything, so a bug
   in the GUI cannot hurt the system through it.
2. **Writes happen only in userland, to three kinds of file,** each under
   rules in the library:
   - *settings file:* only keys in the manifest, only values that pass
     `TunableSpec::Validate()`, `unsafe` keys never; other lines (comments,
     unknown keys) kept as they are; written as `<file>.new` then renamed.
   - *boot link:* only the path computed from the manifest, inside
     non-packaged `add-ons/kernel/drivers/dev/`, only if it is a symlink or
     absent; never touches packaged files.
   - *report:* only where the user chose in a save panel.
3. **Every change takes effect at the next driver load (restart)** and is
   confirmed first, with the recovery path in the dialog ("Disable user
   add-ons" in safe mode).
4. **Reports may contain personal data** (network names, MAC addresses in log
   lines). Both front ends say so before you share one.
5. **Future write operations to the kernel** (e.g. changing a `CTLFLAG_RW`
   value while running) must be a separate opcode range, use a whitelist
   from the driver's own tunable set (never an address from userland), check
   types and ranges in the kernel, and be logged. Not planned before the
   read-only version has been used on hardware.

## 7. The application

Tabs:

- **Overview:** plain-language summary: drivers known and running, the
  supported hardware found and its state, error count with a pointer to
  Diagnostics.
- **Drivers:** table (name, version, installed, at boot, running); details
  (description, origin, license, dependencies, supported devices, features,
  live INFO/STATS from the kernel); *Enable/Disable at boot*; *Settings…*.
- **Hardware:** PCI devices that some LinuxKPI driver supports, with type and
  state: working, driver not installed, missing firmware (names the files),
  disabled at boot, or not running.
- **Diagnostics:** log lines matched by a warning or error rule, with title
  and advice.
- **Log:** LinuxKPI lines from the syslog, with search, a problems-only
  filter, and an option to include the previous boot.
- **Compatibility:** coverage table, a bar (implemented + half of partial),
  and counts.
- **Settings window (per driver):** check boxes and number fields built from
  the manifest, with risk labels; *Save* validates all values first; values
  equal to the default are removed from the file; *Defaults* clears them all.
- **File → Export diagnostic report:** the same report as `lkpictl report`.

`lkpictl` commands: `drivers`, `hardware`, `status <driver>`,
`log [-a] [driver]`, `diagnose`, `coverage`, `settings <driver>`,
`set|unset`, `enable|disable`, `report [file]`.

## 8. Roadmap

| Phase | Content | State |
|---|---|---|
| 1 | Tunables read from the settings file; DMA counters | done (`fee1631`, `95abd05`); hardware test pending (Test B) |
| 2 | Read-only kernel interface (0007, `lkpi_control.c`); library; `lkpictl`; GUI first version; data files | **written, not yet built on Haiku** |
| 2a | First build on Haiku: fix compile/link errors, check every tab against the real system, screenshots | next |
| 3 | Packaged drivers: blocked-entries support for enable/disable **[H]**; package the tool (`.hpkg` with data files) | |
| 4 | Richer kernel data: firmware versions (from mt76 `mt76_connac_mcu` logs or a hook), interrupt and work-queue counters, last error per subsystem; coverage generated from the build (`tools/link-check.sh` already lists every undefined/stubbed symbol) | |
| 5 | System-wide view: LinuxKPI as one shared kernel module (`generic/linuxkpi/v1`) used by all LinuxKPI drivers, publishing `/dev/misc/linuxkpi` for status without any network interface. Needed anyway for DRM, where no network stack is involved | design |
| 6 | Non-network drivers (DRM/KMS): the same ABI over the module's device node; a Display section | depends on DRM work |
| 7 | Live changes for `CTLFLAG_RW` tunables, under §6.5 rules | maybe |
| 8 | Localisation (strings are already `B_TRANSLATE`d), icon, documentation for users | |

## 9. Upstream note

Haiku's `AGENTS.md` (in the Haiku source tree) says Haiku does not accept
AI-generated code or documentation. This project's code, including the
patches in `patches/haiku/` and this application, was written with an AI
assistant. It can be used in your own tree, but it cannot be submitted to
Haiku as it is. Anything meant for upstream (for example patch 0004's bug
fix, or patch 0007's hook) would have to be written by a person
independently.
