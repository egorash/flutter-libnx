#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
G="$SRC/flutter/third_party/dart/runtime/platform/globals.h"
echo "=== 118-145"
sed -n '118,145p' "$G"
echo "=== 400-430"
sed -n '400,430p' "$G"
echo "=== 735-760"
sed -n '735,760p' "$G"
echo "=== 790-815"
sed -n '790,815p' "$G"
