#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

# Setzt ffi_dynamic_library.cc im Dart-Repository zurueck, damit das
# Patch-Skript sauber neu ansetzen kann.
set -euo pipefail

D="$SRC/flutter/third_party/dart"
git -C "$D" checkout -- runtime/lib/ffi_dynamic_library.cc
echo "ffi_dynamic_library.cc zurueckgesetzt."
