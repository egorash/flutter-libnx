#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

# Klaert, ob libnx' devoptab ein Verzeichnis als Dateideskriptor oeffnen kann.
set -uo pipefail
NX="$DEVKITPRO/libnx"
SRC=""
for c in "$DEVKITPRO/libnx/nx/source/runtime/devices/fs_dev.c" \
         "$HOME/src/libnx/nx/source/runtime/devices/fs_dev.c"; do
  [ -f "$c" ] && SRC="$c" && break
done

echo "=== libnx-Quelle"
if [ -n "$SRC" ]; then
  echo "  $SRC"
else
  echo "  kein Quell-Checkout gefunden"
fi

echo
echo "=== dirent-Struktur (dirent.h)"
sed -n '1,60p' "$DEVKITA64/aarch64-none-elf/include/sys/dirent.h" 2>/dev/null

echo
echo "=== DIR-Struktur (libsysbase iosupport.h)"
grep -n "struct DIR_ITER\|dirStateSize\|diropen_r\|dirnext_r" \
  "$NX/include"/*.h "$DEVKITA64/aarch64-none-elf/include/sys/iosupport.h" 2>/dev/null | head -20

echo
echo "=== open_r-Signatur im devoptab"
grep -n "open_r\|fstat_r\|dirstatesize" \
  "$DEVKITA64/aarch64-none-elf/include/sys/iosupport.h" 2>/dev/null | head -20

if [ -n "$SRC" ]; then
  echo
  echo "=== fs_dev open: Verzeichnis-Behandlung"
  grep -n "EISDIR\|FsDirEntryType_Dir\|O_DIRECTORY" "$SRC" | head -20
fi
