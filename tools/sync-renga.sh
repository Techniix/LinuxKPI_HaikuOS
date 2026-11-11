#!/bin/sh
# sync-renga.sh — keep Renga's in-tree copy of LinuxKPI in step with this
# repository.
#
# Renga (github.com/Techniix/haiku, branch ce/main) carries LinuxKPI in its
# tree at the same paths as haiku/ here, built with the image and tested by
# its QEMU harness (decision 2026-11-11, Renga RENGA_MASTER_ROADMAP.md F-26).
# This repository stays the upstream for the LinuxKPI layer, the imported
# FreeBSD/Linux code and the tools. Differences to install.sh:
#   - patches/haiku/*.patch are NOT applied: in Renga each of them is an
#     ordinary commit. The script only checks that each one is already in
#     the Renga tree and says which are not.
#   - the Bluetooth driver directory (imported btmtk.c, licence decision
#     pending, never built) is not copied.
#   - data/linuxkpi goes to data/system/data/linuxkpi (the system package
#     takes it from there).
#
# Usage:
#   tools/sync-renga.sh --check /path/to/renga   list differences, change nothing
#   tools/sync-renga.sh /path/to/renga           copy this repository into Renga
# After copying, build and run Renga's harness (3rdparty/ce/README.md) and
# commit there with explicit paths; record the commit of this repository in
# the Renga commit message.
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
CHECK=0
if [ "${1:-}" = "--check" ]; then CHECK=1; shift; fi
R=${1:?usage: tools/sync-renga.sh [--check] /path/to/renga}
[ -f "$R/3rdparty/ce/harness/ce-test.py" ] || {
	echo "$R is not a Renga tree (no 3rdparty/ce/harness)" >&2; exit 1; }

OWNED="src/libs/compat/linuxkpi src/libs/compat/linuxkpi_wlan
	src/libs/compat/linuxkpi_usb src/libs/compat/linuxkpi_bt
	src/add-ons/kernel/drivers/network/lkpi_pcitest
	src/add-ons/kernel/drivers/network/wlan/mt7921e
	src/apps/linuxkpimanager src/bin/lkpictl
	headers/private/linuxkpi"

missing=0
for p in "$REPO"/patches/haiku/*.patch; do
	if git -C "$R" apply --reverse --check "$p" 2>/dev/null; then
		:
	else
		echo "not in Renga yet: $(basename "$p") (make it a Renga commit)"
		missing=$((missing + 1))
	fi
done

if [ $CHECK = 1 ]; then
	status=0
	for d in $OWNED; do
		diff -r -q "$REPO/haiku/$d" "$R/$d" || status=1
	done
	diff -r -q "$REPO/data/linuxkpi" "$R/data/system/data/linuxkpi" || status=1
	[ $status = 0 ] && echo "Renga's copy matches this repository"
	exit $((status + missing))
fi

for d in $OWNED; do
	rm -rf "${R:?}/$d"
	mkdir -p "$R/$d"
	cp -R "$REPO/haiku/$d/." "$R/$d/"
done
rm -rf "$R/data/system/data/linuxkpi"
mkdir -p "$R/data/system/data/linuxkpi"
cp -R "$REPO/data/linuxkpi/." "$R/data/system/data/linuxkpi/"
echo "copied into $R; $missing patch(es) not yet in Renga"
echo "repository commit: $(git -C "$REPO" rev-parse --short HEAD)"
