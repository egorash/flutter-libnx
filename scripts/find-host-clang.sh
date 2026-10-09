#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
S="$SRC"
echo "=== buildtools-Verzeichnisse"
find "$S" -maxdepth 3 -type d -name buildtools 2>/dev/null
echo
echo "=== clang++ im Checkout"
find "$S" -maxdepth 6 -type f -name "clang++" 2>/dev/null | head -5
echo
echo "=== erwarteter Pfad laut Buildbefehl"
ls -l "$S/buildtools/linux-x64/clang/bin/clang++" 2>/dev/null || echo "  fehlt"
