#!/usr/bin/env bash
# Baut die ganze Kette in der einzig richtigen Reihenfolge neu.
#
# Warum es dieses Skript gibt: Der Snapshot-Hash ist ein MD5 ueber 15
# Dart-Quelldateien (dart/tools/make_version.py), darunter dart.cc,
# app_snapshot.cc und image_snapshot.cc. Wer eine davon anfasst - und sei es
# nur, um eine Debug-Marke zu entfernen -, macht jeden vorhandenen Snapshot
# ungueltig. gen_snapshot, Engine und Snapshot muessen dann aus demselben Stand
# kommen, sonst endet der naechste Lauf in
#
#   Wrong full snapshot version, expected '...' found '...'
#
# Die Reihenfolge ist deshalb nicht beliebig:
#   1. gen_snapshot  - traegt den neuen Hash
#   2. Engine        - traegt denselben Hash, erwartet ihn im Snapshot
#   3. Snapshot      - mit dem gen_snapshot aus Schritt 1 erzeugt
#   4. NRO           - linkt Engine und Snapshot zusammen
#
# Plattform: Auf Linux/WSL liegt der Host-gen_snapshot im x64-Host-Toolchain
# (clang_x64). Auf macOS uebersetzt derselbe Toolchain als arm64 (clang_arm64);
# beide werden vom nativen Compiler gebaut und emittieren Horizon/arm64-Code.
set -euo pipefail

# Gemeinsame Helfer (REPO, die, require_cmd) - siehe scripts/common.sh.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
MODE="release"
usage() {
  echo "Verwendung: $0 [--mode release|debug] BEISPIEL" >&2
  echo "  release (Vorgabe): gen_snapshot -> Engine -> app_aot.s -> .nro" >&2
  echo "  debug:             Engine -> .nro (JIT-Kern aus dem RomFS)" >&2
  exit 1
}
while [ $# -gt 0 ]; do
  case "$1" in
    --mode)   MODE="${2:?--mode braucht einen Wert}"; shift 2 ;;
    --mode=*) MODE="${1#*=}"; shift ;;
    -h|--help) usage ;;
    --*) echo "Unbekanntes Argument: $1" >&2; usage ;;
    *) break ;;
  esac
done
EXAMPLE="${1:-ui_app}"

# Je Modus ein Ausgabeverzeichnis - es muss zum Engine-Build passen, weil
# Schritt 2 genau dort libflutter_engine.a erzeugt. Ein von aussen gesetztes OUT
# (env.sh) darf den Modus nicht stillschweigend ueberschreiben.
#
# Der Vergleich laeuft ueber absolute Pfade: env.sh setzt OUT absolut
# ($SRC/out/...), der Default hier ist relativ. Ein reiner Zeilenvergleich
# wuerde denselben Katalog als "verschieden" melden.
abs_out() {
  case "$1" in
    /*) printf '%s' "$1" ;;
    *)  printf '%s' "$SRC/$1" ;;
  esac
}
case "$MODE" in
  release) default_out="out/horizon_release_arm64" ;;
  debug)   default_out="out/horizon_debug_arm64" ;;
  *) die "Unbekannter Modus: $MODE (erlaubt: release, debug)" ;;
esac
if [ -n "${OUT:-}" ] && [ "$(abs_out "$OUT")" != "$(abs_out "$default_out")" ]; then
  die "OUT=$OUT (aus der Umgebung) passt nicht zu --mode $MODE; erwartet wird $default_out. Entweder OUT leer lassen oder 'unset OUT'."
fi
# exportiert, damit build-horizon.sh und relink-example.sh dasselbe Verzeichnis
# sehen - ohne das nutzen die Kindprozesse ihren eigenen Vorgabewert.
export OUT="$default_out"
OS=$(uname -s)

# OUT darf absolut (env.sh) oder relativ zu SRC sein.
case "$OUT" in
  /*) OUT_ABS="$OUT" ;;
  *)  OUT_ABS="$SRC/$OUT" ;;
esac

# Debug laeuft im JIT: die Engine uebersetzt kernel_blob.bin zur Laufzeit. Ein
# app-aot-Snapshot wird nicht erzeugt und nicht gebraucht, also entfallen
# Schritt 1 (gen_snapshot) und Schritt 3. Uebrig bleiben Engine und NRO.
if [ "$MODE" = "debug" ]; then
  SKIP_SNAPSHOT=1
  TOTAL_STEPS=2
else
  SKIP_SNAPSHOT=0
  TOTAL_STEPS=4
fi

# Host-Toolchain-Verzeichnis und Ninja-Ziel des Host-gen_snapshot.
if [ "$OS" = "Darwin" ]; then
  HOST_TOOLCHAIN="clang_arm64"
else
  HOST_TOOLCHAIN="clang_x64"
fi
GEN_SNAPSHOT_TARGET="$HOST_TOOLCHAIN/gen_snapshot_product"
GEN_SNAPSHOT="$OUT_ABS/$GEN_SNAPSHOT_TARGET"

GENERATED="$REPO/examples/$EXAMPLE/generated"
LOG_DIR="$REPO/build-logs"

mkdir -p "$LOG_DIR"

if [ "$SKIP_SNAPSHOT" -eq 0 ]; then
  echo "==> 1/$TOTAL_STEPS gen_snapshot ($GEN_SNAPSHOT_TARGET)"
  # Auf Linux kapselt build-horizon.sh den Aufruf inkl. depot_tools-PATH; auf
  # macOS rufen wir ninja direkt auf, brauchen aber vpython3 fuer die
  # generate_version_cc_file-Action, also depot_tools ebenfalls im PATH.
  if [ "$OS" = "Darwin" ]; then
    if ! PATH="${DEPOT_TOOLS:-$HOME/depot_tools}:$PATH" \
         ninja -C "$OUT_ABS" -j"${JOBS:-4}" "$GEN_SNAPSHOT_TARGET" \
         > "$LOG_DIR/rebuild-gen-snapshot.log" 2>&1; then
      echo "FEHLGESCHLAGEN - siehe $LOG_DIR/rebuild-gen-snapshot.log"
      grep -E "error:" "$LOG_DIR/rebuild-gen-snapshot.log" | head -10
      exit 1
    fi
  else
    if ! bash "$REPO/scripts/build-horizon.sh" "$GEN_SNAPSHOT_TARGET" \
         > "$LOG_DIR/rebuild-gen-snapshot.log" 2>&1; then
      echo "FEHLGESCHLAGEN - siehe $LOG_DIR/rebuild-gen-snapshot.log"
      grep -E "error:" "$LOG_DIR/rebuild-gen-snapshot.log" | head -10
      exit 1
    fi
  fi
  tail -1 "$LOG_DIR/rebuild-gen-snapshot.log"
fi

echo "==> $((SKIP_SNAPSHOT + 1))/$TOTAL_STEPS Engine ($MODE, $OUT)"
if ! bash "$REPO/scripts/build-horizon.sh" \
     flutter/shell/platform/embedder:flutter_engine_static \
     > "$LOG_DIR/rebuild-engine.log" 2>&1; then
  echo "FEHLGESCHLAGEN - siehe $LOG_DIR/rebuild-engine.log"
  grep -E "error:" "$LOG_DIR/rebuild-engine.log" | head -10
  exit 1
fi
tail -1 "$LOG_DIR/rebuild-engine.log"

if [ "$SKIP_SNAPSHOT" -eq 0 ]; then
  echo "==> 3/$TOTAL_STEPS Snapshot aus $EXAMPLE/generated/app.dill"
  if [ ! -x "$GEN_SNAPSHOT" ]; then
    echo "FEHLER: $GEN_SNAPSHOT fehlt"
    exit 1
  fi
  if [ ! -f "$GENERATED/app.dill" ]; then
    echo "FEHLER: $GENERATED/app.dill fehlt - erst scripts/build-dart-app.sh --project examples/$EXAMPLE/dart laufen lassen"
    exit 1
  fi
  # In eine Nebendatei schreiben und erst danach umbenennen: Bricht gen_snapshot
  # ab, bleibt der alte Snapshot stehen, statt dass eine leere .s zurueckbleibt
  # und der naechste Build stillschweigend Unsinn linkt.
  if ! "$GEN_SNAPSHOT" --snapshot_kind=app-aot-assembly \
       --assembly="$GENERATED/app_aot_new.s" "$GENERATED/app.dill"; then
    echo "FEHLGESCHLAGEN: gen_snapshot"
    rm -f "$GENERATED/app_aot_new.s"
    exit 1
  fi
  if [ ! -s "$GENERATED/app_aot_new.s" ]; then
    echo "FEHLER: erzeugte Assembly ist leer"
    rm -f "$GENERATED/app_aot_new.s"
    exit 1
  fi
  mv -f "$GENERATED/app_aot_new.s" "$GENERATED/app_aot.s"
  echo "    $(wc -l < "$GENERATED/app_aot.s") Zeilen Assembly"
else
  echo "==> 2/$TOTAL_STEPS Snapshot entfaellt (JIT: kernel_blob.bin aus dem RomFS)"
fi

echo "==> $TOTAL_STEPS/$TOTAL_STEPS NRO"
bash "$REPO/scripts/relink-example.sh" "$EXAMPLE"
