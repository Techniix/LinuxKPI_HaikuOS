#!/bin/sh
# link-check.sh — build every object of a LinuxKPI add-on with the host
# compiler (tools/check-module.sh profiles), merge them the way the Jamfiles
# do (ld -r), and print the symbols that remain undefined. Each of those must
# be exported by the Haiku kernel; tools/kernel-exports.sh checks that.
#
# Usage: HAIKU_TOP=... tools/link-check.sh [driver-dir]
#   driver-dir defaults to haiku/src/add-ons/kernel/drivers/network/lkpi_pcitest
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
		on && $1 !~ /^#/ && NF { print $1 }' "$1"
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
	echo "built $obj ($# sources)"
}

S=$(for f in $(jam_sources "$LK/Jamfile" linuxkpi.o); do
	find_src "$f" "$LK/common/src" "$LK/haiku" "$LK/freebsd/src" "$LK/freebsd/ck/src"; done)
build linuxkpi linuxkpi.o $S

S=$(for f in $(jam_sources "$LK/haiku/Jamfile" linuxkpi_haiku.o); do
	find_src "$f" "$LK/haiku"; done)
build shim linuxkpi_haiku.o $S

name=$(basename "$DRV")
S=$(for f in $(jam_sources "$DRV/linux/Jamfile" "${name}_linux.o"); do
	find_src "$f" "$DRV"; done)
build driver "${name}_linux.o" $S
build fbsd glue.o "$DRV/glue.c"

# Haiku's own freebsd_network (with patches/haiku applied to HAIKU_TOP)
FN=$H/src/libs/compat/freebsd_network
S=$(awk '/KernelStaticLibrary libfreebsd_network.a/{on=1;next} on&&/;/{on=0} on&&NF{print $1}' "$FN/Jamfile" | sed "s|^|$FN/|")
build fbsd libfreebsd_network.o $S

ld -r -o "$OUT/addon.o" "$OUT/glue.o" "$OUT/${name}_linux.o" \
	"$OUT/linuxkpi.o" "$OUT/linuxkpi_haiku.o" "$OUT/libfreebsd_network.o"
nm -u "$OUT/addon.o" | awk '{print $2}' | sort -u > "$OUT/undefined.txt"
echo "undefined symbols (must be Haiku kernel exports): $(wc -l < "$OUT/undefined.txt")"
echo "list: $OUT/undefined.txt"

# Phase 2: every undefined symbol must be (a) a linker-set boundary,
# (b) already imported by Haiku's own libfreebsd_network (so proven to be
# a kernel export by shipping drivers), or (c) defined in Haiku's kernel
# sources (needs src/system/kernel, src/system/kernel/lib and the libroot
# posix sources they pull in, in HAIKU_TOP).
nm -u "$OUT/libfreebsd_network.o" "$OUT/glue.o" | awk '{print $2}' | sort -u > "$OUT/haiku-uses.txt"
unresolved=0
: > "$OUT/new-imports.txt"
for s in $(cat "$OUT/undefined.txt"); do
	case $s in __start_set_*|__stop_set_*) continue ;; esac
	grep -qx -- "$s" "$OUT/haiku-uses.txt" && continue
	case $s in
	_Znw*|_Zna*) name='operator new' ;;
	_Zdl*|_Zda*) name='operator delete' ;;
	_Z*) name=$(echo "$s" | c++filt | sed 's/(.*//; s/.*:://') ;;
	*) name=$s ;;
	esac
	where=$(grep -rlE "(^|[ *])$name[ ]*\(|^$name$|std::$name" \
		"$H/src/system/kernel" "$H/src/system/libroot/posix/string" \
		"$H/src/system/libroot/posix/musl/string" 2>/dev/null | head -1)
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
	'$(LINUXKPI_LIBS)')
		set -- $(awk '/^LINUXKPI_LIBS =/{a=1} a{print} a&&/;/{exit}' "$rules" |
			sed 's/^LINUXKPI_LIBS =//;s/;//') ;;
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
