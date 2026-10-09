#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
S="$SRC/flutter/fml"
echo "=== file_posix.cc ($(wc -l < "$S/platform/posix/file_posix.cc") Zeilen)"
cat -n "$S/platform/posix/file_posix.cc"
echo
echo "=== mapping_horizon.cc: fd-Nutzung"
grep -n "fstat\|::open\|lseek" "$S/platform/horizon/mapping_horizon.cc"
