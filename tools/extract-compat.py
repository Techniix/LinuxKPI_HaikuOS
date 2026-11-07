#!/usr/bin/env python3
"""
extract-compat.py - generate haiku/src/libs/compat/linuxkpi/haiku/lkpi_compat.c
from FreeBSD's imported linux_compat.c.

FreeBSD's linux_compat.c mixes generic LinuxKPI services (devices/classes,
timers, completions, kasprintf, hexdump, notifiers, list_sort, ...) with the
Linux character-device/file/mmap layer, which depends on FreeBSD's
cdev/vnode/file/VM-object subsystems (DRM territory, absent on Haiku).
This keeps the former verbatim (by function name), drops the latter, and
replaces linux_compat_init() with a Haiku version (lkpi_compat_haiku.c).
Re-run after every import; review the diff.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "haiku/src/libs/compat/linuxkpi/common/src/linux_compat.c"
DST = ROOT / "haiku/src/libs/compat/linuxkpi/haiku/lkpi_compat.c"

KEEP = """linux_remap_address __lkpi_current panic_cmp linux_device_release linux_class_show
linux_class_store linux_class_release linux_dev_release linux_dev_show
linux_dev_store device_create device_create_groups_vargs lkpi_class_create
linux_copyin linux_copyout linux_clear_user linux_access_ok
devm_kvasprintf kvasprintf lkpi_devm_kasprintf kasprintf
__lkpi_hexdump_printf __lkpi_hexdump_sbuf_printf lkpi_hex_dump hdtb_cb
lkpi_hex_dump_to_buffer linux_timer_callback_wrapper linux_timer_jiffies_until
mod_timer add_timer add_timer_on timer_delete timer_delete_sync
timer_shutdown_sync lkpi_gcd_64 linux_timer_init linux_complete_common
linux_wait_for_common linux_wait_for_timeout_common
linux_try_wait_for_completion linux_completion_done
linux_handle_ifnet_link_event linux_handle_ifnet_arrival_event
linux_handle_ifnet_departure_event linux_handle_iflladdr_event
linux_handle_ifaddr_event register_netdevice_notifier
register_inetaddr_notifier unregister_netdevice_notifier
unregister_inetaddr_notifier linux_le_cmp list_sort linux_wbinvd_on_all_cpus
linux_on_each_cpu linux_in_atomic linux_dump_stack linuxkpi_net_ratelimit
io_mapping_create_wc device_can_wakeup linuxkpi_device_set_wakeup_capable
devm_device_group_remove lkpi_devm_device_add_group
lkpi_get_static_single_cpu_mask lkpi_xen_initial_domain lkpi_xen_pv_domain
linuxkpi_x86_match_cpu""".split()

# FreeBSD headers for subsystems we do not have (file/cdev/vnode/VM objects)
DROP_INCLUDES = """sys/sglist.h sys/fcntl.h sys/file.h sys/filio.h sys/stack.h
sys/syscall.h sys/sysent.h sys/user.h vm/vm_object.h vm/vm_pager.h vm/vm_radix.h
linux/cdev.h linux/file.h linux/fs.h linux/poll.h linux/seq_file.h
linux/interval_tree.h linux/interval_tree_generic.h machine/cputypes.h
xen/xen-os.h""".split()

src = SRC.read_text().splitlines(keepends=True)

# license block
i = 0
lic = []
while not src[i].startswith(" */"):
    lic.append(src[i]); i += 1
LICLEN = i + 1

# preamble: from after the license to the first function (keep includes,
# sysctls, MALLOC_DEFINE, globals; drop file/cdev-only declarations)
first_fn = None
for n, l in enumerate(src):
    if re.match(r"^[a-z_][a-z_0-9]*\(", l) and n > 0 and not src[n-1].startswith("#"):
        first_fn = n - 1
        break
pre = []
for l in src[LICLEN:first_fn]:
    m = re.match(r"#include [<\"]([^>\"]+)[>\"]", l)
    if m and m.group(1) in DROP_INCLUDES:
        continue
    if re.search(r"linux_destroy_dev|linux_cdev_deref|linux_cdev_handle_find|"
                 r"linux_vma_lock|cdev_pager|linuxcdevsw|linuxfileops", l):
        continue
    pre.append(l)

# Walk the rest of the file once, collecting kept spans in source order:
#  - function definitions named in KEEP (plus comment block above and a
#    SYSINIT/SYSUNINIT right after),
#  - top-level structure definitions,
#  - top-level variable definitions (except file/cdev-layer ones),
#  - top-level #define/#undef lines.
VARDEF = re.compile(r"^(static )?(const )?(struct [a-z_0-9]+|u?int[0-9]*_t|"
                    r"unsigned( long| int)?|int|long|bool|char) \**[a-z_][a-z_0-9]*"
                    r"(\[[^]]*\])?( = .*|;)$")
DROPVAR = re.compile(r"cdev|file|kq|vma|pager|ldev|fop")
spans = []          # (start, text)
found = set()
n = first_fn
while n < len(src):
    line = src[n]
    m = re.match(r"^([a-z_][a-z_0-9]*)\(", line)
    if m and src[n-1].strip() and not src[n-1].startswith(("#", " ", "\t", "}")):
        name = m.group(1)
        start = n - 1
        while start > 0 and src[start-1].startswith((" *", "/*")):
            start -= 1
        end = n
        while not src[end].startswith("}"):
            end += 1
        if end + 1 < len(src) and src[end+1].startswith(("SYSINIT", "SYSUNINIT")):
            end += 1
        if name in KEEP:
            spans.append((start, "".join(src[start:end+1]) + "\n"))
            found.add(name)
        n = end + 1
        continue
    if re.match(r"^(static )?(const )?struct [a-z_0-9]+ \{", line) or \
       (VARDEF.match(line) and not DROPVAR.search(line)) or \
       (line.rstrip().endswith("= {") and re.match(r"^(static )?(const )?struct ", line)
        and not DROPVAR.search(line)):
        end = n
        if line.rstrip().endswith("{"):
            while not src[end].startswith("};"):
                end += 1
        text = "".join(src[n:end+1])
        if not DROPVAR.search(text):
            spans.append((n, text + "\n"))
        n = end + 1
        continue
    if re.match(r"^#(define|undef)\s", line):
        end = n
        while src[end].rstrip().endswith("\\"):
            end += 1
        spans.append((n, "".join(src[n:end+1])))
        n = end + 1
        continue
    n += 1

missing = [k for k in KEEP if k not in found]
if missing:
    sys.exit("extract-compat: not found in linux_compat.c: " + " ".join(missing))

out = []
out += lic
out.append(""" *
 * ---------------------------------------------------------------------------
 * GENERATED by tools/extract-compat.py from FreeBSD
 * sys/compat/linuxkpi/common/src/linux_compat.c (see IMPORTED.md for the
 * commit). Function bodies are verbatim; the Linux cdev/file/mmap layer is
 * omitted and linux_compat_init()/uninit() are replaced by
 * lkpi_compat_haiku.c. Do not edit; edit the generator instead.
 */
""")
out += pre
out.append("\n/* ---- kept items, in FreeBSD's order ---- */\n\n")
for _, text in sorted(spans):
    out.append(text)
out.append('/* Haiku init/uninit and globals (uses this file\'s statics). */\n'
           '#include "lkpi_compat_haiku.c"\n')
DST.write_text("".join(out))
print(f"wrote {DST.relative_to(ROOT)}: {len(KEEP)} functions")
