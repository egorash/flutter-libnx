#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
R="$SRC/flutter/third_party/dart/runtime/bin"
echo "########## eventhandler.h – Plattformauswahl"
grep -n -B4 -A14 "Unknown target os" "$R/eventhandler.h"
echo
echo "########## socket_base.h – Plattformauswahl"
grep -n -B4 -A14 "Unknown target os" "$R/socket_base.h"
echo
echo "########## Welche bin-Dateien baut das Ziel ueberhaupt?"
grep -n "eventhandler_linux\|socket_linux\|file_linux\|process_linux" "$R/BUILD.gn" | head -10
