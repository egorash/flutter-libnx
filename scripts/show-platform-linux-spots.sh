#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
F="$SRC/flutter/third_party/dart/runtime/bin/platform_linux.cc"
echo "=== Zeilen 9-24 (Includes)"
sed -n '9,24p' "$F"
echo
echo "=== Umgebung der Treffer"
grep -n -B3 -A6 "utsname\|gethostname\|sysconf\|/proc/" "$F" | head -60
