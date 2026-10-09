#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
R="$SRC/flutter/third_party/dart/runtime/bin"
echo "=== accept4 in socket_linux.cc"
grep -n -B4 -A6 "accept4" "$R/socket_linux.cc"
echo
echo "=== stat64/fstat64 in socket_base_linux.cc"
grep -n -B4 -A8 "fstat64" "$R/socket_base_linux.cc"
echo
echo "=== ENONET"
grep -rn "ENONET" "$R/socket_base_linux.cc" "$R/socket_linux.cc"
