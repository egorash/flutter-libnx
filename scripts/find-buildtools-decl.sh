#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
S="$SRC"
echo "=== Zuweisungen an buildtools_path"
grep -rn "buildtools_path *=" "$S" --include='*.gni' --include='*.gn' 2>/dev/null \
  | grep -v "third_party/dart" | head -10
echo
echo "=== build_overrides"
ls "$S/build_overrides" 2>/dev/null
grep -rn "buildtools" "$S/build_overrides" 2>/dev/null | head -6
