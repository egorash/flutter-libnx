#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
F="$SRC/flutter/third_party/dart/runtime/bin/stdio_linux.cc"
echo "=== termios-Treffer: $(grep -c 'termios\|tcgetattr\|tcsetattr' "$F")"
echo "=== Zeilen gesamt: $(wc -l < "$F")"
echo
echo "=== Funktionen"
grep -n "^[A-Za-z].*::.*(" "$F"
echo
echo "=== Includes"
grep -n "#include" "$F"
