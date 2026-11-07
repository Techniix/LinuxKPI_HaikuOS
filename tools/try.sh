#!/bin/sh
# try.sh <profile> <file> [n] — check one file, show first n distinct errors
export HAIKU_TOP=${HAIKU_TOP:-/home/claude/src/haiku}
cd "$(dirname "$0")/.."
rm -f /tmp/lkpi-check/*.log
tools/check-module.sh -p "$1" "$2"
grep -h "error" /tmp/lkpi-check/*.log | sed 's|/home/claude/[^ :]*/compat/||g' | awk '!seen[$0]++' | head -${3:-20} | cut -c1-220
