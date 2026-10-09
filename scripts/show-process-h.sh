#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
sed -n '88,145p' \
  "$SRC/flutter/third_party/dart/runtime/bin/process.h"
