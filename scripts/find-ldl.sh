#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

set -uo pipefail
# Wer haengt noch -ldl an? libnx hat kein libdl.
S="$SRC"
echo '=== "dl" in libs-Listen (ohne third_party/dart/tools)'
grep -rn 'libs *= *\[ *"dl"\|libs += \[ *"dl"\|"dl",' "$S/flutter" "$S/build" \
  --include='*.gn' --include='*.gni' 2>/dev/null | head -12
echo
echo "=== im erzeugten Link-Kommando"
grep -o '\-ldl' "$S/out/horizon_release_arm64/libflutter_engine.so.rsp" 2>/dev/null | head -2
echo
echo "=== ninja-Regel fuer das Ziel"
grep -n "ldl\|-ldl" "$S/out/horizon_release_arm64/build.ninja" 2>/dev/null | head -5
