#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
R="$SRC/flutter/third_party/dart/runtime"
echo "########## vm/cpuinfo.h"
cat "$R/vm/cpuinfo.h"
echo
echo "########## vm/native_symbol.h (Deklarationen)"
grep -n "static\|class" "$R/vm/native_symbol.h" | head -14
