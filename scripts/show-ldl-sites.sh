#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
S="$SRC"
echo "=== flutter/skia/BUILD.gn:740-755"
sed -n '740,755p' "$S/flutter/skia/BUILD.gn"
echo
echo "=== dart/runtime/bin/BUILD.gn:18-35"
sed -n '18,35p' "$S/flutter/third_party/dart/runtime/bin/BUILD.gn"
