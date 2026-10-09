#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
R="$SRC/flutter/third_party/dart/runtime"
echo "########## platform/utils_linux.h"
cat "$R/platform/utils_linux.h"
echo
echo "########## vm/os_thread_linux.h"
cat "$R/vm/os_thread_linux.h"
