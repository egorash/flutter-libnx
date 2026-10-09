#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
R="$SRC/flutter/third_party/dart/runtime"
echo "=== TimerUtils (bin/utils.h)"
grep -n -A8 "class TimerUtils" "$R/bin/utils.h"
echo
echo "=== SimpleHashMap Iteration (platform/hashmap.h)"
grep -n "Start()\|Next(" "$R/platform/hashmap.h"
echo
echo "=== TimeoutQueue (bin/eventhandler.h)"
grep -n "HasTimeout\|CurrentTimeout\|CurrentPort\|RemoveCurrent\|UpdateTimeout" "$R/bin/eventhandler.h" | head -8
