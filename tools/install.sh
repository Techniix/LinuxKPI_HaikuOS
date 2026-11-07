#!/bin/sh
# install.sh — drop LinuxKPI_HaikuOS into a Haiku source tree.
#
# Usage: tools/install.sh /path/to/haiku
#   1. applies patches/haiku/*.patch (skips ones already applied),
#   2. copies haiku/src/... over the tree.
# Re-running is safe. To remove: git -C /path/to/haiku checkout -- . and
# delete src/libs/compat/linuxkpi and
# src/add-ons/kernel/drivers/network/lkpi_pcitest.
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

(cd "$REPO/haiku" && find src -type f) | while read -r f; do
	mkdir -p "$H/$(dirname "$f")"
	cp "$REPO/haiku/$f" "$H/$f"
done
echo "copied $(cd "$REPO/haiku" && find src -type f | wc -l) files into $H"
