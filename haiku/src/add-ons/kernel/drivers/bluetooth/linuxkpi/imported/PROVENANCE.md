# Imported Linux Bluetooth driver files

Verbatim copies; do not edit. Adaptations go in LinuxKPI (headers and
implementations under `src/libs/compat/linuxkpi*`), never here.

Source: Linux, git `fa4ad798f6f681031775266044ba1b0cf3c670bd`
(v7.4-rc1, 2026-11-07), `drivers/bluetooth/`.

| File | Linux path | SPDX license | sha256 |
|---|---|---|---|
| btmtk.c | drivers/bluetooth/btmtk.c | ISC | `b748e802744cfe79374260fd8422aee86324c7ddf6f13a7bc7e1949273f8fc9a` |
| btmtk.h | drivers/bluetooth/btmtk.h | ISC | `5366c461f575ed88dd2bd8aae317e11134fd1720067e751dd4189b578d6b266b` |

Notes:

- The files carry `SPDX-License-Identifier: ISC` (copyright MediaTek Inc.).
  `btmtk.c` also says `MODULE_LICENSE("GPL")`; that macro tags the Linux
  kernel module the file is linked into and does not change the file's own
  license.
- Built with `CONFIG_BT_MTK` and `CONFIG_BT_HCIBTUSB_MTK` defined, which
  enables the USB helpers (`btmtk_usb_*`).
- `CONFIG_DEV_COREDUMP` is not defined: firmware core dumps are not
  collected on Haiku (the code paths return early).
