#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

set -uo pipefail
OUT="${OUT:-$SRC/out/horizon_release_arm64}"
BIN="$SRC/flutter/third_party/dart/runtime/bin"
NM="$DEVKITA64/bin/aarch64-none-elf-nm"

for o in socket_base_linux socket_linux platform_linux directory_linux file_linux; do
  O="$OUT/obj/flutter/third_party/dart/runtime/bin/common_embedder_dart_io.$o.o"
  if [ -f "$O" ]; then
    N=$("$NM" --defined-only "$O" 2>/dev/null | wc -l)
    printf "  %-22s %5s definierte Symbole  (%s Bytes)\n" "$o" "$N" "$(stat -f%z "$O")"
  else
    printf "  %-22s fehlt\n" "$o"
  fi
done

echo
echo "=== Guards in socket_base_linux.cc"
grep -n "DART_HOST_OS\|^#if\|^#endif\|^#else" "$BIN/socket_base_linux.cc" | head -30
