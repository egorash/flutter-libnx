#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
F="$SRC/flutter/third_party/dart/runtime/bin/file_linux.cc"
echo "=== mmap-Nutzung"
grep -n "mmap\|munmap\|PROT_\|MAP_" "$F"
echo
echo "=== File::Map"
grep -n -A30 "void\* File::Map" "$F" | head -40
