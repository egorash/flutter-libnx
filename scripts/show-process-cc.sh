#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
B="$SRC/flutter/third_party/dart/runtime/bin"
echo "=== in process.cc definiert (plattformneutral)"
grep -n "^[a-zA-Z_].*Process::" "$B/process.cc" | sed 's/^/  /'
echo
echo "=== in process_horizon.cc definiert"
grep -n "^[a-zA-Z_].*Process::" "$B/process_horizon.cc" | sed 's/^/  /'
