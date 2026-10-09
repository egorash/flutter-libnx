#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
REPO="${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

set -uo pipefail
S="$SRC/flutter/fml/synchronization/semaphore.cc"
NM="$DEVKITA64/bin/aarch64-none-elf-nm"
OBJDUMP="$DEVKITA64/bin/aarch64-none-elf-objdump"
LIBDIR="$DEVKITA64/aarch64-none-elf/lib"

echo "=== semaphore.cc: Plattformweichen"
grep -n "#if\|#elif\|#else\|#endif\|sem_init\|sem_wait\|sem_post\|sem_trywait\|dispatch_semaphore\|CreateSemaphore" "$S"

echo
echo "=== sem_* in den Bibliotheken"
for s in sem_init sem_destroy sem_wait sem_trywait sem_post sem_timedwait; do
  found=""
  for lib in "$LIBDIR"/libc.a "$LIBDIR"/libsysbase.a "$LIBDIR"/libpthread.a "$DEVKITPRO/libnx/lib/libnx.a"; do
    [ -f "$lib" ] || continue
    "$NM" --defined-only "$lib" 2>/dev/null | grep -qE "^[0-9a-f]+ +[TW] +$s\$" && found="$found $(basename "$lib")"
  done
  [ -n "$found" ] && echo "  ja   $s <-$found" || echo "  NEIN $s"
done

echo
echo "=== was sem_init im gelinkten Programm tatsaechlich tut"
E="$REPO/examples/engine_link_test/engine_link_test.elf"
ADDR=$("$NM" "$E" 2>/dev/null | grep -E " T sem_init\$" | head -1 | cut -d' ' -f1)
if [ -n "$ADDR" ]; then
  echo "  Adresse: $ADDR"
  "$OBJDUMP" -d --start-address="0x$ADDR" --stop-address="$(printf '0x%x' $((0x$ADDR + 0x40)))" "$E" 2>/dev/null | tail -15
else
  echo "  sem_init nicht im Programm (nicht referenziert)"
fi
