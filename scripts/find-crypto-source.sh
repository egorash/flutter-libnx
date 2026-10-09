#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
B="$SRC/flutter/third_party/dart/runtime/bin"
echo "=== crypto_* in den .gni-Dateien"
grep -rn "crypto_" "$B"/*.gni
echo
echo "=== crypto_* in BUILD.gn"
grep -n "crypto_" "$B/BUILD.gn"
