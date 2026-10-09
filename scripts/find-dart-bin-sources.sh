#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
R="$SRC/flutter/third_party/dart/runtime/bin"
echo "=== .gni-Dateien in bin/"
ls "$R" | grep '\.gni$'
echo
echo "=== wo eventhandler_linux.cc gelistet wird"
grep -rln "eventhandler_linux.cc" "$R" 2>/dev/null | head -5
echo
echo "=== Groesse der Linux-Varianten (Zeilen)"
wc -l "$R"/*_linux.cc | sort -n
