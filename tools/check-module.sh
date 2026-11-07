#!/bin/sh
# check-module.sh — compile (syntax + type check, -c) kernel sources against a
# Haiku checkout with the *host* gcc/g++, emulating Haiku's x86_64 kernel-mode
# flags. Used because the Haiku cross toolchain may be unavailable.
#
# Usage: HAIKU_TOP=/path/to/haiku tools/check-module.sh [-p profile] file.c...
#   profiles: fbsd      — freebsd_network/freebsd_wlan code (Haiku Jamfile flags)
#             shim      — Haiku-side LinuxKPI shims: overlays, no preinclude
#             wlan      — freebsd_wlan/net80211 (FBSD_DRIVER=1)
#             linuxkpi  — LinuxKPI core (fbsd + linuxkpi include paths)
#             lkpi_wlan — LinuxKPI 802.11 + net80211
#             driver    — imported Linux driver (mt76)
# Flags are copied from build/jam/ArchitectureRules (HAIKU_KERNEL_*),
# src/libs/compat/freebsd_network/Jamfile and the wlan driver Jamfiles.
set -eu

HAIKU_TOP=${HAIKU_TOP:?set HAIKU_TOP to a Haiku checkout}
REPO=$(cd "$(dirname "$0")/.." && pwd)
PROFILE=fbsd
OUT=${OUT:-/tmp/lkpi-check}
VERBOSE=${VERBOSE:-0}
if [ "${1:-}" = "-p" ]; then PROFILE=$2; shift 2; fi
mkdir -p "$OUT"

H=$HAIKU_TOP
SHIM=$REPO/tools/hostshim
CC=${CC:-gcc}
CXX=${CXX:-g++}
GCCINC=$($CC -print-file-name=include)

# Haiku x86_64 kernel base flags (ArchitectureRules: ccBaseFlags, x86_64 case).
BASE="-nostdinc -isystem $GCCINC -ffreestanding -finline -fno-semantic-interposition \
 -fno-tree-vectorize -fno-pic -mcmodel=kernel -mno-red-zone -fno-omit-frame-pointer \
 -fno-builtin -fno-stack-protector -fshort-wchar \
 -U__linux__ -U__linux -Ulinux -U__gnu_linux__ -U__unix__ -U__unix -Uunix \
 -D__HAIKU__ -D__HAIKU_ARCH_X86_64 -D_KERNEL_MODE -D__GLIBC_PREREQ(a,b)=0 \
 -include $H/headers/config/HaikuConfig.h"
CXXONLY="-fno-exceptions -fno-rtti -fno-use-cxa-atexit -nostdinc++ -I$SHIM"

# HAIKU_PRIVATE_KERNEL_HEADERS + public headers, in Haiku's order.
SYSINC="-I$H/headers -I$H/headers/config -I$H/headers/os -I$H/headers/os/kernel \
 -I$H/headers/os/drivers -I$H/headers/os/drivers/bus -I$H/headers/os/support \
 -I$H/headers/os/storage -I$H/headers/os/app -I$H/headers/os/net -I$H/headers/posix \
 -I$H/headers/private/kernel -I$H/headers/private/libroot -I$H/headers/private/shared \
 -I$H/headers/private -I$H/headers/private/kernel/arch/x86 -I$H/headers/private/kernel/boot/platform/efi \
 -I$H/headers/private/system -I$H/headers/private/system/arch/x86_64 \
 -I$H/headers/private/net -I$H/headers/private/drivers -I$H/build/config_headers \
 -I$SHIM/generated"

C=$H/src/libs/compat
FBSD="-I$C/freebsd_network/compat -I$C/freebsd_network -I$C/freebsd_wlan -D_KERNEL=1 -D_XOPEN_SOURCE \
 -Wno-unused -Wno-sign-compare -Wno-error=incompatible-pointer-types -Wno-multichar"
LK=$REPO/haiku/src/libs/compat/linuxkpi
LKW=$REPO/haiku/src/libs/compat/linuxkpi_wlan
LKINC="-Wno-pointer-sign -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types -I$LK/haiku/include -I$LK/freebsd/include -I$LK/freebsd/ck/include -I$LK/common/include -I$LK/dummy/include -I$LK/haiku -I$REPO/haiku/src/libs -include $LK/haiku/lkpi_haiku_preinclude.h"

case $PROFILE in
	fbsd)      PFLAGS="$FBSD" ;;
	shim)      PFLAGS="-I$LK/haiku/include -I$LK/freebsd/include -I$LK/haiku $FBSD -I$C/freebsd_network" ;;
	wlan)      PFLAGS="-I$C/freebsd_wlan/net80211 $FBSD -DFBSD_DRIVER=1 -Wno-format -Wno-uninitialized" ;;
	linuxkpi)  PFLAGS="$LKINC $FBSD -I$C/freebsd_wlan -DFBSD_DRIVER=1 " ;;
	lkpi_wlan) PFLAGS="$LKINC $FBSD -I$C/freebsd_wlan -I$LKW -DFBSD_DRIVER=1 -idirafter $C/freebsd_wlan/net80211" ;;
	driver)    PFLAGS="$LKINC $FBSD -I$C/freebsd_wlan -DFBSD_DRIVER=1 -DLINUXKPI_VERSION=70000 ${DRIVER_FLAGS:-}" ;;
	*) echo "unknown profile $PROFILE" >&2; exit 2 ;;
esac

fail=0
for f in "$@"; do
	case $f in
		*.cpp) cmd="$CXX -std=gnu++17 $BASE $CXXONLY $PFLAGS $SYSINC" ;;
		*)     cmd="$CC -std=gnu11 $BASE $PFLAGS $SYSINC" ;;
	esac
	o=$OUT/$(echo "$f" | tr '/' '_').o
	if [ "$VERBOSE" = 1 ]; then echo "$cmd -c $f -o $o"; fi
	if $cmd -Wall ${EXTRA_FLAGS:-} -c "$f" -o "$o" 2>"$o.log"; then
		echo "OK   $f"
	else
		echo "FAIL $f  ($(grep -c 'error' "$o.log") errors, log: $o.log)"
		fail=$((fail+1))
	fi
done
exit $fail
