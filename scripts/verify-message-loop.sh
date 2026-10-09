#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
REPO="${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

set -uo pipefail
NM="$DEVKITA64/bin/aarch64-none-elf-nm"
E="$REPO/examples/engine_link_test/engine_link_test.elf"

echo "=== MessageLoopHorizon im Programm"
"$NM" -C --defined-only "$E" | grep "MessageLoopHorizon::" | sed 's/^/  /'

echo
echo "=== keine Linux-Schleife hineingeraten?"
if "$NM" -C --defined-only "$E" | grep -q "MessageLoopLinux::"; then
  echo "  ACHTUNG: MessageLoopLinux ist ebenfalls im Programm"
else
  echo "  ok, MessageLoopLinux fehlt wie erwartet"
fi

echo
echo "=== Groesse"
ls -lh "$E" "${E%.elf}.nro" | awk '{print "  " $5 "  " $9}'
