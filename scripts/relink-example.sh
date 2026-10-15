#!/usr/bin/env bash
set -euo pipefail

# Gemeinsame Helfer (REPO, die, require_cmd) - siehe scripts/common.sh.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Erzwingt einen Neulink des Beispiels und fasst das Ergebnis zusammen.
# Das Makefile kennt libflutter_engine.a nicht als Abhaengigkeit, deshalb muss
# die ELF weg, damit ueberhaupt neu gelinkt wird.
EX="${1:-engine_link_test}"
D="$REPO/examples/$EX"
# Nicht nach /tmp: Wird die WSL-Instanz zwischen zwei Aufrufen beendet, ist das
# Protokoll weg - und ein leeres Protokoll sieht aus wie "null Fehler".
LOG="$REPO/build-logs/link-$EX.log"
mkdir -p "$(dirname "$LOG")"

# Moduswechsel kostet ein leeres build/.
#
# make kennt kein Abochen auf Compile-Flags: nach einem Wechsel zwischen
# release und debug liegen die .o-Dateien des anderen Modus noch da. Sie sind
# nicht falsch im Sinne von kaputt - sie sind falsch im Sinne von alt, und der
# Linker sucht dann die Symbole des falschen Zweigs. Genau das ergab
# undefined reference auf _kDartVmSnapshotData: main.o war im Release-Modus
# uebersetzt, das RomFS trug den JIT-Kern.
#
# Der Stamp ist ein Einzeiler in build/ und wird nur gelesen, wenn es ihn
# schon gibt - ein frisches Beispiel verliert nichts.
MODE="${MODE:-release}"
stamp="$D/build/.mode"
if [ -f "$stamp" ] && [ "$(cat "$stamp")" != "$MODE" ]; then
  echo "Moduswechsel $(cat "$stamp") -> $MODE: build/ wird geleert"
  rm -rf "$D/build"
fi
mkdir -p "$D/build"
printf '%s' "$MODE" > "$stamp"

rm -f "$D/$EX.elf"
# set -e: Ein Fehlschlag des Builds ist hier ein erwarteter Fall (RC wird unten
# ausgewertet), deshalb if/else statt nacktem Aufruf.
if bash "$REPO/scripts/build-example-wsl.sh" "$EX" > "$LOG" 2>&1; then
  BUILD_RC=0
else
  BUILD_RC=$?
fi

# Erst prüfen, ob überhaupt gelinkt wurde. Ein leeres Protokoll und ein
# fehlerfreies sehen bei einer reinen Symbolzählung gleich aus.
if [ ! -s "$LOG" ]; then
  echo "FEHLER: Protokoll ist leer - der Build lief gar nicht an."
  exit 1
fi

# Und noch davor: Ein Compilerfehler beendet make, bevor der Linker ueberhaupt
# laeuft. Die Symbolzaehlung meldete dann "0 undefinierte Symbole" - richtig
# gezaehlt und trotzdem irrefuehrend, weil die alte NRO unveraendert liegen
# blieb. Dieselbe Falle wie beim Zaehler, der nur eine Fehlerart kannte.
if [ "$BUILD_RC" -ne 0 ]; then
  echo "FEHLER: make endete mit $BUILD_RC - es wurde nichts gelinkt."
  grep -E "error:|Error [0-9]+" "$LOG" | head -10 | sed 's/^/  /'
  echo "  vollstaendig: $LOG"
  exit 1
fi
if ! grep -q "linking\|built \.\.\.\|Error\|error" "$LOG"; then
  echo "WARNUNG: keine Link- oder Fehlerzeile im Protokoll"
  tail -10 "$LOG" | sed 's/^/  /'
fi

echo "=== undefinierte Symbole"
MISSING="$(dirname "$LOG")/missing-$EX.txt"
# Leerer Treffer ist normal (Link sauber) - deshalb || true unter set -e.
grep -o "undefined reference to \`[^']*'" "$LOG" \
  | sed "s/undefined reference to \`//; s/'$//" | sort -u > "$MISSING" \
  || true
COUNT=$(wc -l < "$MISSING")
echo "  $COUNT"
[ "$COUNT" -gt 0 ] && head -15 "$MISSING" | sed 's/^/    /'

# Nicht jeder Linkfehler ist ein fehlendes Symbol. Eine reine Zaehlung der
# undefinierten Verweise meldete schon einmal "0", waehrend der Link an einer
# doppelten Definition scheiterte.
echo
echo "=== doppelte Definitionen"
DUP=$(grep -c "multiple definition of" "$LOG" || true)
echo "  $DUP"
if [ "$DUP" -gt 0 ]; then
  grep -o "multiple definition of \`[^']*'" "$LOG" \
    | sed "s/multiple definition of \`//; s/'$//" | sort -u | sed 's/^/    /' \
    || true
fi

echo
echo "=== sonstige Linkerfehler"
grep -E "^\S+ld: .*(error|Error)|collect2: error" "$LOG" \
  | grep -v "undefined reference\|multiple definition" | head -5 | sed 's/^/  /' \
  || true

echo
echo "=== Ergebnis"
grep "built \.\.\." "$LOG" | sed 's/^/  /' || echo "  nichts gebaut"

if [ -f "$D/$EX.nro" ]; then
  ls -lh "$D/$EX.nro" | awk '{print "  " $5 "  " $9}'
fi
