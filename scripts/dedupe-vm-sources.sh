#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"

# Entfernt doppelte horizon-Eintraege aus vm_sources.gni.
#
# Ursache war ein Fehler im Patch-Skript: Beim Einfuegen bleibt der Anker
# erhalten, weshalb die Idempotenzpruefung auf das Ergebnis schauen muss und
# nicht auf den Anker. Behoben; dieses Skript raeumt den Schaden weg.
set -euo pipefail

F="$SRC/flutter/third_party/dart/runtime/vm/vm_sources.gni"

awk '!(/horizon\.cc/ && $0 == prev) { print } { prev = $0 }' "$F" > "$F.tmp"
mv "$F.tmp" "$F"

echo "horizon-Eintraege danach:"
grep -n 'horizon' "$F"
