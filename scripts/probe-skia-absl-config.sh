#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
S="$SRC"

echo "=== flutter/skia/BUILD.gn, Kopf der Konfiguration"
sed -n '1,60p' "$S/flutter/skia/BUILD.gn"

echo
echo "=== low_level_alloc.h: Bedingung fuer ABSL_LOW_LEVEL_ALLOC_MISSING"
sed -n '25,50p' "$S/third_party/abseil-cpp/absl/base/internal/low_level_alloc.h"
