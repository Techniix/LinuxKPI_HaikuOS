#!/bin/sh
# link-check.sh — build every object of a LinuxKPI add-on with the host
# compiler (tools/check-module.sh profiles), merge them the way the Jamfiles
# do (ld -r), and print the symbols that remain undefined. Each of those must
# be exported by the Haiku kernel; tools/kernel-exports.sh checks that.
#
# Usage: HAIKU_TOP=... tools/link-check.sh [driver-dir]
#   driver-dir defaults to haiku/src/add-ons/kernel/drivers/network/lkpi_pcitest
#   A driver with more than one Linux merge object, or that needs LinuxKPI
#   802.11, USB or Bluetooth, is described by tools/drivers/<name>.sh (see
#   mt7921e.sh, bt-foundation.sh). Such a script may set GLUE_PROFILE.
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
H=${HAIKU_TOP:?}
DRV=${1:-$REPO/haiku/src/add-ons/kernel/drivers/network/lkpi_pcitest}
OUT=${OUT:-/tmp/lkpi-link}
LK=$REPO/haiku/src/libs/compat/linuxkpi
rm -rf "$OUT"; mkdir -p "$OUT"

# sources of "KernelMergeObject <name> :" ... ";" in a Jamfile
jam_sources() {
	awk -v obj="$2" '
		$1 == "KernelMergeObject" && $2 == obj { on = 1; next }
		on && /;/ { on = 0 }
		on && $1 !~ /^#/ && NF { for (i = 1; i <= NF; i++) print $i }' "$1"
}
find_src() {	# name dir...
	n=$1; shift
	for d in "$@"; do [ -f "$d/$n" ] && { echo "$d/$n"; return; }; done
	echo "link-check: source $n not found" >&2; exit 1
}
build() {	# profile objname srcs...
	prof=$1; obj=$2; shift 2
	OUT=$OUT/$obj.d "$REPO/tools/check-module.sh" -p "$prof" "$@" >"$OUT/$obj.log" 2>&1 || {
		cat "$OUT/$obj.log"; echo "link-check: compile failed for $obj"; exit 1; }
	ld -r -o "$OUT/$obj" "$OUT/$obj.d"/*.o
	unset DRIVER_FLAGS EXTRA_FLAGS
	echo "built $obj ($# sources)"
}

S=$(for f in $(jam_sources "$LK/Jamfile" linuxkpi.o); do
	find_src "$f" "$LK/common/src" "$LK/haiku" "$LK/freebsd/src" "$LK/freebsd/ck/src"; done)
build linuxkpi linuxkpi.o $S

S=$(for f in $(jam_sources "$LK/haiku/Jamfile" linuxkpi_haiku.o); do
	find_src "$f" "$LK/haiku"; done)
build shim linuxkpi_haiku.o $S

name=$(basename "$DRV")
WLAN=0
if [ -f "$REPO/tools/drivers/$name.sh" ]; then
	. "$REPO/tools/drivers/$name.sh"
	driver_objects
else
	S=$(for f in $(jam_sources "$DRV/linux/Jamfile" "${name}_linux.o"); do
		find_src "$f" "$DRV"; done)
	build driver "${name}_linux.o" $S
	DRIVER_OBJS="${name}_linux.o"
fi
if [ "$WLAN" = 1 ]; then
	LKW=$REPO/haiku/src/libs/compat/linuxkpi_wlan
	S=$(for f in $(jam_sources "$LKW/Jamfile" linuxkpi_wlan.o); do
		find_src "$f" "$LKW/imported" "$LKW/haiku"; done)
	build lkpi_wlan linuxkpi_wlan.o $S
	# Haiku's own net80211 (freebsd_wlan.o)
	FW=$H/src/libs/compat/freebsd_wlan/net80211
	S=$(jam_sources "$FW/Jamfile" freebsd_wlan.o | while read f; do
		find_src "$f" "$FW" "$FW/../crypto/rijndael"; done)
	build wlan freebsd_wlan.o $S
	build wlan glue.o "$DRV/glue.c"
	WLAN_OBJS="linuxkpi_wlan.o freebsd_wlan.o"
else
	build "${GLUE_PROFILE:-fbsd}" glue.o "$DRV/glue.c"
	WLAN_OBJS=
fi

# Haiku's own freebsd_network (with patches/haiku applied to HAIKU_TOP)
FN=$H/src/libs/compat/freebsd_network
S=$(awk '/KernelStaticLibrary libfreebsd_network.a/{on=1;next} on&&/;/{on=0} on&&NF{print $1}' "$FN/Jamfile" | sed "s|^|$FN/|")
build fbsd libfreebsd_network.o $S

ld -r -o "$OUT/addon.o" "$OUT/glue.o" \
	$(for o in $DRIVER_OBJS $WLAN_OBJS; do echo "$OUT/$o"; done) \
	"$OUT/linuxkpi.o" "$OUT/linuxkpi_haiku.o" "$OUT/libfreebsd_network.o"
nm -u "$OUT/addon.o" | awk '{print $2}' | sort -u > "$OUT/undefined.txt"
echo "undefined symbols (must be Haiku kernel exports): $(wc -l < "$OUT/undefined.txt")"
echo "list: $OUT/undefined.txt"

# Phase 2: every undefined symbol must be (a) a linker-set boundary,
# (b) already imported by Haiku's own libfreebsd_network (so proven to be
# a kernel export by shipping drivers), or (c) defined in Haiku's kernel
# sources (needs src/system/kernel, src/system/kernel/lib and the libroot
# posix sources they pull in, in HAIKU_TOP).
nm -u "$OUT/libfreebsd_network.o" "$OUT/glue.o" $(for o in $WLAN_OBJS; do
	[ "$o" = freebsd_wlan.o ] && echo "$OUT/$o"; done) |
	awk '{print $2}' | sort -u > "$OUT/haiku-uses.txt"
unresolved=0
: > "$OUT/new-imports.txt"
for s in $(cat "$OUT/undefined.txt"); do
	case $s in __start_set_*|__stop_set_*) continue ;; esac
	# freebsd_network's hooks into the driver (haiku-module.h macros:
	# HAIKU_FIRMWARE_*, NO_HAIKU_FBSD_MII_DRIVER, ...): the glue must
	# define them, the kernel does not
	case $s in __haiku_*)
		echo "NOT DEFINED (driver glue must provide): $s"
		unresolved=$((unresolved + 1))
		continue ;;
	esac
	grep -qx -- "$s" "$OUT/haiku-uses.txt" && continue
	case $s in
	# C++ ABI support: the kernel links libsupc++ (KernelLd) and exports all
	# symbols (kernel_versions: "global: *"); add-ons resolve them via kernel.so
	_ZTVN10__cxxabiv1*|_ZTIN10__cxxabiv1*|__cxa_*|__gxx_*)
		echo "$s	libsupc++ in the kernel (exported)" \
			>> "$OUT/new-imports.txt"
		continue ;;
	esac
	qual=
	case $s in
	_Znw*|_Zna*) name='operator new' ;;
	_Zdl*|_Zda*) name='operator delete' ;;
	# vtable / typeinfo: emitted where the class's first out-of-line
	# virtual is defined; look for any "Class::" definition
	_ZTV*|_ZTI*) name=$(echo "$s" | c++filt | sed 's/^vtable for //; s/^typeinfo for //')
		qual="^$name::" ;;
	_Z*) qual=$(echo "$s" | c++filt | sed 's/(.*//; s/<.*//')
		name=$(echo "$qual" | sed 's/.*:://')
		case $qual in *::*) qual="^$qual[ ]*\(" ;; *) qual= ;; esac ;;
	*) name=$s ;;
	esac
	where=$(grep -rlE "(^|[ *])$name[ ]*\(|^$name$|std::$name${qual:+|$qual}" \
		"$H/src/system/kernel" "$H/src/system/libroot/posix/string" \
		"$H/src/system/libroot/posix/musl/string" \
		"$H/src/system/libroot/os/driver_settings.cpp" 2>/dev/null | head -1)
	if [ -z "$where" ]; then
		echo "NOT FOUND in Haiku kernel: $s"
		unresolved=$((unresolved + 1))
	else
		echo "$s	${where#$H/}" >> "$OUT/new-imports.txt"
	fi
done
echo "new kernel imports (verified in Haiku source): $(wc -l < "$OUT/new-imports.txt") -> $OUT/new-imports.txt"
# Phase 3: mimic Haiku's LinkAgainst (build/jam/MainBuildRules) on the
# KernelAddon library list: a name without dirname/grist that neither starts
# with "lib" nor ends in .a/.so becomes "-l<name>" and fails to link.
libs=$(awk '/^KernelAddon /{a=1} a{print} a&&/;/{exit}' "$DRV/Jamfile" |
	awk 'BEGIN{RS=":"} NR==3' | tr -s ' \t\n;' '\n')
rules=$REPO/haiku/src/libs/compat/linuxkpi/LinuxKPIRules
badlibs=0
for l in $libs; do
	case $l in
	'$(LINUXKPI_LIBS)'|'$(LINUXKPI_WLAN_LIBS)'|'$(LINUXKPI_BT_LIBS)')
		v=${l#??}; v=${v%?}
		set -- $(awk -v v="$v" '$1==v && $2=="="{a=1} a{print} a&&/;/{exit}' "$rules" |
			sed "s/^$v =//;s/;//") ;;
	*) set -- "$l" ;;
	esac
	for x; do
		case $x in
		\<*\>*|*/*|lib*|*.a|*.so) ;;
		*) echo "LinkAgainst would pass -l$x (use <nogrist>$x)"
		   badlibs=$((badlibs + 1)) ;;
		esac
	done
done
echo "KernelAddon libraries jam would turn into -l: $badlibs"
echo "unresolved: $unresolved"
exit $((unresolved + badlibs))
