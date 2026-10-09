#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
F="$SRC/flutter/third_party/skia/src/ports/SkFontHost_FreeType.cpp"
echo "=== Zeilen 75-135"
sed -n '75,135p' "$F"
echo
echo "=== alle dlsym/dlopen-Stellen"
grep -n "dlsym\|dlopen\|RTLD" "$F"
