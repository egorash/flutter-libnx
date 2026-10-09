#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
REPO="${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

set -uo pipefail
D="$DEVKITA64/aarch64-none-elf/include"

echo "=== exakte Deklarationen"
grep -rn -A2 "mkdirat\|unlinkat\|faccessat\|renameat *(\|fdopendir" "$D"/sys/stat.h "$D"/unistd.h "$D"/stdio.h "$D"/dirent.h "$D"/sys/dirent.h 2>/dev/null | grep -v "^--"

echo
echo "=== O_DIRECTORY irgendwo?"
grep -rn "O_DIRECTORY" "$D" 2>/dev/null | head
echo "(im Engine-Patchskript:)"
grep -n "O_DIRECTORY" $REPO/scripts/patch-engine-horizon.py | head

echo
echo "=== was der Engine-Baum daraus macht"
grep -rn "O_DIRECTORY" "$SRC/flutter/fml/" 2>/dev/null | head
