# Bluetooth through LinuxKPI

Goal: run Linux Bluetooth controller drivers (`drivers/bluetooth`, starting
with USB: `btusb` and the vendor setup helpers it uses) on Haiku, so that
controllers Haiku's own `h2generic` cannot bring up (firmware download,
vendor quirks) work with Haiku's existing Bluetooth stack.

Status (2026-11-08): **foundation written, compiles, links; not yet run.**
The driver itself waits on a licensing decision (see "Driver choice").

Claims are marked [V] verified in source or on hardware, [H] hypothesis,
[U] unknown.

## What Haiku has, and what it lacks

Haiku's Bluetooth stack [V, Haiku `b9fdc8a`]:

- `bluetooth_server` watches the subdirectories of `/dev/bluetooth/h2`,
  opens every device node there and drives the controller with ioctls from
  `<bluetooth/HCI/btHCI_transport.h>`: `GET_HCI_ID`, `BT_UP`,
  `ISSUE_BT_COMMAND` (a raw HCI command), `GET_STATS`.
- The kernel module `bluetooth/hci/v1` (`src/add-ons/kernel/bluetooth/hci`)
  is the transport interface: a driver calls `RegisterDriver()` with its
  send hooks (`SendCommand`, `SendACL`, `SendSCO`), and posts received
  events and ACL data with `PostTransportPacket()`.
- One USB transport driver, `h2generic`: standard controllers only (USB
  class E0/01/01), no firmware loading, no vendor setup.

Gaps that matter for Linux drivers, and how this layer fills them without
changing Haiku:

| Gap in Haiku | Solution here | Why it should last |
|---|---|---|
| No Linux USB driver model (URBs, anchors, `usb_driver` binding) | **LinuxKPI USB** (`src/libs/compat/linuxkpi_usb`): written for Haiku on the public USB bus manager API `bus_managers/usb/v3` (`<USB3.h>`). FreeBSD's `linux_usb.c` is not usable (needs FreeBSD's USB stack). | `USB3.h` is Haiku's stable driver API since R1/beta1; only documented calls are used. |
| No per-transfer cancel (Haiku cancels a whole pipe) | Unlinking one URB cancels its pipe; IN URBs cancelled as a side effect are queued again transparently, OUT/control ones complete with `-ECONNRESET` and are logged. | Pure LinuxKPI-side logic. If Haiku ever adds per-transfer cancel, only `lkpi_hk_cancel()` changes. |
| USB speed not exposed | Inferred from bulk endpoint sizes (64/512/1024 bytes are fixed per speed by the USB spec). | Spec-defined; no Haiku internals. |
| No Linux HCI core | **LinuxKPI Bluetooth** (`src/libs/compat/linuxkpi_bt`): the transport-driver half of `net/bluetooth/hci_core.h` (struct `hci_dev`, `hci_recv_frame`, `__hci_cmd_sync*`, `hci_cmd_sync_queue`, quirks, flags). Haiku's stack stays the host stack. | Drivers see the Linux API; Haiku sees an ordinary HCI transport, the same contract `h2generic` uses. |
| `freebsd_network` only publishes network devices | **Generic driver glue** (`LKPI_HAIKU_DRIVER_GLUE`, `lkpi_driver.c`, `lkpi_devfs.c`): any LinuxKPI driver class can publish its own device nodes. | Uses only the legacy driver entry points and `devfs_rescan_driver()` (the call Haiku's USB stack itself uses). |
| Driver loading on hotplug | The add-on registers its Linux ID table with the USB stack as `usb_support_descriptor`s; Haiku then (re)loads the add-on when a matching device appears [V: `Stack::NotifyDeviceChange`/`RescanDrivers`]. If nothing matches, the add-on unloads. | Haiku's documented mechanism for USB drivers. |
| Missing helpers (`microtime`, `skb_pull_data`, `skb_clone`, `__skb_put_padto`, `EREMOTEIO`, `SX_SYSINIT`/`DEFINE_MUTEX`, one-argument `MODULE_VERSION`) | Small overlays under `linuxkpi/haiku/include`, each documented. | If a later FreeBSD import adds one, the build fails on the duplicate and the overlay is deleted. |
| Two USB drivers can bind the same device (no exclusive claim) | See "Coexistence with h2generic". | Interim: a user setting. Long term: a small Haiku change or replacing `h2generic`. |

## Architecture

```
 bluetooth_server ── ioctl ──► /dev/bluetooth/h2/<driver>/<n>
                                   │
 Haiku "bluetooth/hci/v1"  ◄───────┤ lkpi_bt_haiku.cpp  (Haiku side)
   PostTransportPacket ◄── RX      │   RegisterDriver, SendCommand/SendACL
                                   ▼
                       lkpi_bt_bridge.h   (plain C)
                                   ▼
                        lkpi_hci.c         (LinuxKPI side)
                          struct hci_dev, hci_recv_frame, __hci_cmd_sync
                                   ▼
                   Linux driver: btusb / btmtk / ...
                                   ▼
                        lkpi_usb.c         (LinuxKPI side)
                          usb_driver, URBs, anchors, usb_control_msg
                                   ▼
                       lkpi_usb_bridge.h  (plain C)
                                   ▼
                     lkpi_usb_haiku.c      (Haiku side)
                          bus_managers/usb/v3: queue_bulk, queue_request …
```

Each subsystem has two halves, as in the rest of the project: the LinuxKPI
side is built with the LinuxKPI preinclude (Linux types, FreeBSD errno
values); the Haiku side without it. Only plain C types cross the bridge
headers; errors cross as small codes that each side maps to its own errno
space.

### Bring-up sequence

1. A matching USB device appears; Haiku loads the add-on (or it is loaded
   when `bluetooth_server` opens `/dev/bluetooth/h2`).
2. `init_driver()` runs LinuxKPI's SYSINITs: the driver's `module_init()`
   registers its `usb_driver`; LinuxKPI USB then registers with Haiku's USB
   stack and binds present devices (`probe()`).
3. The driver calls `hci_register_dev()`; LinuxKPI Bluetooth publishes
   `/dev/bluetooth/h2/<driver>/<n>`.
4. `bluetooth_server` opens the node (`RegisterDriver()` with Haiku's HCI
   module) and sends `BT_UP`: LinuxKPI runs the driver's `open()` and, the
   first time, `setup()` (firmware download for MediaTek, Intel, Realtek …).
5. From then on HCI commands and ACL data flow both ways. Events that answer
   the driver's own commands (during setup) are consumed; everything else
   goes to Haiku.

### Semantics kept from Linux [V against Linux v7.4-rc1 source]

- URB completion order (`__usb_hcd_giveback_urb`): unanchor, status,
  `complete()`, `use_count`, wake killers; anchors' `suspend_wakeups`.
- `usb_kill_urb()` (reject, unlink `-ENOENT`, wait), poison/unpoison,
  `usb_kill_anchored_urbs()` loop, `usb_get_from_anchor()`,
  `usb_scuttle_anchored_urbs()`.
- `usb_driver_release_interface()` calls the driver's `disconnect()`
  (btusb relies on it for its SCO and diagnostic interfaces).
- ID matching per `usb_match_one_id()`, including the vendor-specific
  device class rule.
- `usb_control_msg_recv()` treats a short read as `-EREMOTEIO`.
- `hci_recv_frame()`: accepts frames only in `HCI_UP`/`HCI_INIT`, never
  sleeps; `hci_send_frame()` frees the skb when `send()` fails.
- `__hci_cmd_sync*()`: Command Complete returns the return parameters;
  failed Command Status returns its error; a successful Command Status with
  no event requested returns `-ENODATA`; timeout `-ETIMEDOUT`;
  `hci_cmd_sync_cancel(err)` ends the pending request with `-err`.
- `__hci_cmd_send()` only for vendor commands (OGF 0x3f).
- `setup()` once (or every open with `HCI_QUIRK_NON_PERSISTENT_SETUP`);
  `shutdown()` before `close()` unless unregistering.
- `hci_conn_num()` and `notify(HCI_NOTIFY_CONN_ADD/DEL)` work: connections
  are tracked from Connection Complete / Disconnection Complete / LE
  connection events (btusb switches its SCO alternate setting on them).

### Differences (documented in the sources)

- `usb_reset_device()` is not supported and fails with `-EIO`. Haiku's
  `reset_port()` only resets the port: the device comes back at address 0
  while Haiku keeps its old address, and nothing re-enumerates a device
  that stays connected [V: `Hub::ResetPort`, `Hub::Explore`]. Failing keeps
  the controller usable; btusb/btmtk log it and carry on.
- `usb_unlink_urb()` cancels from a work item (Haiku may run cancelled
  transfers' completions in the cancelling thread [H]); `usb_kill_urb()`
  cancels directly.
- `usb_set_interface()` kills the old alternate's URBs first: Haiku drops
  the transfers of a deleted pipe without completing them [H].
- Cancelling one bulk IN URB re-queues its IN siblings, but a sibling that
  had received part of a multi-packet transfer loses that part. btusb and
  btmtk only kill whole anchors, so they are not affected.
- The USB match list given to Haiku is fixed at start: replacing it while
  the add-on runs races with Haiku's explore thread, and narrowing it would
  hide device removals [V: `Stack::RegisterDriver`,
  `Device::ReportDevice`].
- Isochronous transfers are not implemented: `usb_submit_urb()` returns
  `-EOPNOTSUPP` for them. Haiku's Bluetooth stack has no SCO audio path to
  feed yet, so nothing is lost today.
- `skb_clone()` is a full copy (contents, header fields and `cb[]` are
  equal; drivers use clones only to parse).
- No device core dumps (`hci_devcd_*` return `-EOPNOTSUPP`), no debugfs,
  no Linux mgmt interface (Haiku's stack is the host).
- Haiku's `GET_STATS` counters are 8 bit; they wrap.

## Driver choice (decision needed)

Licenses of the candidate Linux files [V, Linux `fa4ad79`]:

| File | License | What it does |
|---|---|---|
| `btusb_main.c` | GPL-2.0-or-later | The Linux USB transport, device table for all vendors |
| `btusb_qcom.c` | GPL-2.0-only | Qualcomm parts of btusb |
| `btintel.c`, `btrtl.c`, `btbcm.c` | GPL-2.0-or-later | Intel, Realtek, Broadcom setup |
| `btmtk.c`/`btmtk.h` | **ISC** | MediaTek setup (MT7921/7922/7925 …), USB helpers |
| `include/net/bluetooth/*` | GPL-2.0 | Not used: LinuxKPI Bluetooth has its own MIT headers |

Options:

- **A. Port Linux `btusb` as is.** Broadest hardware support (Intel,
  Realtek, Broadcom, Qualcomm, MediaTek, Marvell …). The add-on binary is
  then GPL; LinuxKPI stays MIT. Haiku's main tree does not take GPL kernel
  add-ons, so it would ship as a separate package.
- **B. Our own MIT USB transport + MediaTek's ISC `btmtk`.** Everything
  permissive and upstreamable to Haiku. Covers standard controllers (what
  `h2generic` covers, with Linux's robustness) plus MediaTek combo cards
  (including the MT7922 in the test machine). Other vendors need their own
  setup code later (clean-room MIT or optional GPL add-ons).
- **C. Both.** B as the permissive default; A as an optional package for
  the remaining vendors.

All three use the same foundation (LinuxKPI USB and Bluetooth), which is
written and checked. The imported `btmtk` is needed by all three.

## Coexistence with h2generic

Haiku reports each USB device to every driver that registered for it; there
is no exclusive claim [V: `Device::ReportDevice`]. `h2generic` matches every
standard controller (class E0/01/01), including MediaTek combo cards, but
cannot load their firmware. With both drivers installed, both would publish
a node and `bluetooth_server` would open both.

Until Haiku has an exclusive-claim mechanism (a small bus-manager change we
can propose upstream), block `h2generic` while testing:

```
# /boot/system/settings/packages
Package haiku {
	BlockedEntries {
		add-ons/kernel/drivers/bin/h2generic
		add-ons/kernel/drivers/dev/bluetooth/h2/h2generic
	}
}
```

then reboot. packagefs `BlockedEntries` [V:
`packagefs/volume/PackageSettings.cpp`] hides files of an installed package;
the paths are relative to `/boot/system`. That `h2generic` lives at these
two paths in the `haiku` package is [H]; check with
`ls -l /boot/system/add-ons/kernel/drivers/dev/bluetooth/h2/` before
relying on it. Blocking the binary alone is enough to stop it loading.
Delete the block and reboot to undo. The LinuxKPI Manager can manage this
setting later.

## Files

| Path | Side | Contents |
|---|---|---|
| `linuxkpi_usb/include/linux/usb.h`, `usb/ch9.h`, `usb/quirks.h` | LinuxKPI | Linux USB API (MIT, replaces FreeBSD's `linux/usb.h`) |
| `linuxkpi_usb/lkpi_usb.c` | LinuxKPI | driver binding, URBs, anchors, messages, reset |
| `linuxkpi_usb/lkpi_usb_bridge.h` | both | bridge to Haiku's USB API |
| `linuxkpi_usb/haiku/lkpi_usb_haiku.c` | Haiku | `bus_managers/usb/v3` |
| `linuxkpi_bt/include/net/bluetooth/{bluetooth,hci,hci_core}.h` | LinuxKPI | Linux HCI API (MIT; values from the Bluetooth Core Specification) |
| `linuxkpi_bt/lkpi_hci.c` | LinuxKPI | HCI core for transport drivers |
| `linuxkpi_bt/lkpi_bt_bridge.h` | both | bridge to Haiku's Bluetooth stack |
| `linuxkpi_bt/haiku/lkpi_bt_haiku.cpp` | Haiku | device node, `bluetooth/hci/v1` |
| `linuxkpi/haiku/lkpi_driver.{c,h}`, `lkpi_devfs.c` | Haiku | entry points and device nodes for non-network LinuxKPI add-ons |
| `add-ons/kernel/drivers/bluetooth/linuxkpi/imported/btmtk.{c,h}` | Linux | verbatim, ISC (`PROVENANCE.md` there) |

Build: `UseLinuxKPI <version> : bt ;` in a driver Jamfile, link
`$(LINUXKPI_BT_LIBS)`, and use `LKPI_HAIKU_DRIVER_GLUE(<add-on name>)` in
the glue (example: `tools/drivers/bt-foundation/glue.c`).

## Verification so far

| Check | Result |
|---|---|
| All new sources compile with Haiku x86_64 kernel flags (`tools/check-module.sh`, profiles `usb`, `bt`, `hk`, `shim`) | [V] |
| Imported `btmtk.c` compiles unchanged against LinuxKPI Bluetooth/USB | [V] |
| `tools/link-check.sh tools/drivers/bt-foundation`: every undefined symbol is a Haiku kernel export | [V] 0 unresolved |
| Wi-Fi add-on unaffected (`link-check.sh` mt7921e) | [V] 0 unresolved |
| Independent code review | [V] done 2026-11-08; 9 findings, all fixed (resubmit from a completion handler, submit/kill race, unlink context, alternate-setting switch, port reset, match-list updates, HCI close ordering, Haiku-handle lifetime, error paths) |
| Built with jam on Haiku | [U] |
| Runs on hardware | [U] |

## Roadmap

1. Decide A/B/C; add the driver add-on (`btusb` or the MIT transport) with
   its firmware name map and package files.
2. First hardware run on the MT7922 (firmware
   `mediatek/BT_RAM_CODE_MT7922_1_1_hdr.bin` from linux-firmware).
3. USB device reset: find and verify a way to make Haiku re-enumerate a
   port (e.g. disabling it and letting the hub explore re-attach), then
   implement `usb_reset_device()`.
4. Isochronous URBs (needs Haiku's `queue_isochronous`) once Haiku's stack
   can carry SCO audio.
5. Exclusive USB claim: propose a minimal Haiku bus-manager change, or
   replace `h2generic` upstream with the permissive driver.
6. Bluetooth page in the LinuxKPI Manager (controller state, firmware,
   `h2generic` blocking).
