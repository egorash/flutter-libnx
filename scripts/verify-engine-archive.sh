#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

set -uo pipefail
OUT="${OUT:-$SRC/out/horizon_release_arm64}"
A="$OUT/obj/flutter/shell/platform/embedder/libflutter_engine.a"
NM="$DEVKITA64/bin/aarch64-none-elf-nm"

echo "=== Archiv"
ls -lh "$A" | awk '{print "  " $5 "  " $9}'
echo "  Objektdateien darin: $("$DEVKITA64/bin/aarch64-none-elf-ar" t "$A" | wc -l)"

echo
echo "=== Embedder-API im Archiv"
for sym in FlutterEngineRun FlutterEngineInitialize FlutterEngineRunInitialized \
           FlutterEngineSendWindowMetricsEvent FlutterEngineSendPointerEvent \
           FlutterEngineSendPlatformMessage FlutterEngineShutdown \
           FlutterEngineRunsAOTCompiledDartCode; do
  if "$NM" --defined-only "$A" 2>/dev/null | grep -q " T $sym$"; then
    echo "  ok     $sym"
  else
    echo "  FEHLT  $sym"
  fi
done
