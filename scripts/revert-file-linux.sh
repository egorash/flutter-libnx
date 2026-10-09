#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

# Setzt file_linux.cc im Dart-Repository zurueck, damit das Patch-Skript
# sauber neu ansetzen kann.
#
# third_party/dart ist ein eigenes Git-Repository innerhalb des Checkouts,
# deshalb -C auf dieses Verzeichnis.
set -euo pipefail

D="$SRC/flutter/third_party/dart"
git -C "$D" checkout -- runtime/bin/file_linux.cc
echo "file_linux.cc zurueckgesetzt."
