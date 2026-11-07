#!/bin/sh
# install.sh — drop LinuxKPI_HaikuOS into a Haiku source tree.
#
# Usage: tools/install.sh /path/to/haiku
#   1. applies patches/haiku/*.patch (skips ones already applied),
#   2. replaces the directories this project owns (OWNED below) with the
#      repository's copy, so files removed here are removed there too.
# Re-running is safe. To remove: git -C /path/to/haiku checkout -- . and
# delete the OWNED directories.
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
H=${1:?usage: tools/install.sh /path/to/haiku}
[ -f "$H/build/jam/KernelRules" ] || { echo "$H is not a Haiku tree" >&2; exit 1; }

want=$(cat "$REPO/patches/haiku/BASE_COMMIT")
have=$(git -C "$H" rev-parse HEAD 2>/dev/null || echo unknown)
[ "$have" = "$want" ] || echo "note: patches made against $want, tree is at $have"

for p in "$REPO"/patches/haiku/*.patch; do
	if git -C "$H" apply --reverse --check "$p" 2>/dev/null; then
		echo "already applied: $(basename "$p")"
	elif git -C "$H" apply --check "$p"; then
		git -C "$H" apply "$p"
		echo "applied: $(basename "$p")"
	else
		echo "FAILED to apply $(basename "$p"); resolve by hand" >&2
		exit 1
	fi
done

# Directories that exist only because of this project (never Haiku's own).
OWNED="src/libs/compat/linuxkpi src/libs/compat/linuxkpi_wlan
	src/add-ons/kernel/drivers/network/lkpi_pcitest
	src/add-ons/kernel/drivers/network/wlan/mt7921e
	src/apps/linuxkpimanager src/bin/lkpictl
	headers/private/linuxkpi"
for d in $OWNED; do
	[ -d "$REPO/haiku/$d" ] || { echo "install.sh: $d missing in repo" >&2; exit 1; }
	rm -rf "${H:?}/$d"
done

(cd "$REPO/haiku" && find src headers -type f) | while read -r f; do
	mkdir -p "$H/$(dirname "$f")"
	cp "$REPO/haiku/$f" "$H/$f"
done
echo "copied $(cd "$REPO/haiku" && find src headers -type f | wc -l) files into $H"
