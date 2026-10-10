#!/usr/bin/env bash
# Erzeugt aus einem Flutter-Projekt die Artefakte, die der Horizon-Embedder
# braucht: Assets fuer das RomFS und AOT-Assembly fuer den Linker.
#
# macOS-Port von scripts/build-dart-app.ps1 (getopts statt param()).
# Die Kette ist dieselbe wie im Original:
#   flutter build bundle  -> flutter_assets
#   gen_kernel_aot        -> app.dill
#   scripts/rebuild-all.sh -> app_aot.s -> .nro   (lokal, ohne WSL)
#
# Aufruf:
#   build-dart-app.sh                         -> examples/ui_app
#   build-dart-app.sh --project /pfad/app     -> beliebiges Projekt
#   build-dart-app.sh --product               -> -Ddart.vm.product=true
#
# WICHTIG zum Projektpfad: wie im Original liegen Ausgabeziele (generated/,
# romfs/) NEBEN dem Beispiel examples/ui_app, nicht im fremden Projekt - ein
# Werkzeug soll das Projekt, das es uebersetzt, nicht veraendern.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
flutter="${FLUTTER_HOME:-$HOME/flutter-3.41.6}"
sdk="$flutter/bin/cache/dart-sdk"
engine="$flutter/bin/cache/artifacts/engine"

# Die Plattform-Dill des Frameworks. dart:ui steckt darin; ohne sie scheitert
# schon die Kernel-Erzeugung an einem unbekannten Import.
platform="$engine/common/flutter_patched_sdk_product/platform_strong.dill"
# icudtl.dat: macOS-Host-Datei (im Original windows-x64). Fallback: suchen.
icu="${ICU:-$engine/darwin-arm64/icudtl.dat}"
if [ ! -f "$icu" ]; then
  found="$(find "$engine" -name icudtl.dat 2>/dev/null | head -1 || true)"
  [ -n "$found" ] && icu="$found"
fi

project=""
product=0
usage() {
  echo "Verwendung: $0 [--project PFAD] [--product]" >&2
  exit 1
}
while [ $# -gt 0 ]; do
  case "$1" in
    --project) project="${2:?--project braucht einen Pfad}"; shift 2 ;;
    --project=*) project="${1#*=}"; shift ;;
    --product) product=1; shift ;;
    -h|--help) usage ;;
    *) echo "Unbekanntes Argument: $1" >&2; usage ;;
  esac
done

if [ -z "$project" ]; then project="$root/examples/ui_app/dart"; fi
project="$(cd "$project" && pwd)"

# Das Ausgabeziel liegt neben dem Beispiel (siehe Kopf).
app="$root/examples/ui_app"
generated="$app/generated"
romfs="$app/romfs"
assets="$romfs/flutter_assets"

for p in "$sdk" "$platform" "$icu" "$project/pubspec.yaml"; do
  [ -e "$p" ] || { echo "Nicht gefunden: $p" >&2; exit 1; }
done

mkdir -p "$generated" "$romfs"
echo "==> Projekt: $project"

# --- 1. Assets --------------------------------------------------------------
echo '==> flutter build bundle'
(
  cd "$project"
  "$flutter/bin/flutter" build bundle
)
bundle="$project/build/flutter_assets"
[ -d "$bundle" ] || { echo "Nicht gefunden: $bundle" >&2; exit 1; }

# JIT-Artefakte bleiben draussen (siehe PS1-Kommentar: kernel_blob.bin alleine
# waere >40 MB in einer NRO, die sonst 17 MB wiegt).
jit_only='kernel_blob.bin vm_snapshot_data isolate_snapshot_data .last_build_id'

rm -rf "$assets"
mkdir -p "$assets"
copied=0
while IFS= read -r f; do
  rel="${f#"$bundle"/}"
  base="$(basename "$f")"
  for j in $jit_only; do [ "$base" = "$j" ] && continue 2; done
  mkdir -p "$(dirname "$assets/$rel")"
  cp -f "$f" "$assets/$rel"
  copied=$((copied + 1))
done < <(find "$bundle" -type f)

size_kb="$(du -sk "$assets" | awk '{print $1}')"
printf '    %s Dateien, %.2f MB\n' "$copied" "$(awk -v k="$size_kb" 'BEGIN{printf "%.2f", k/1024}')"

cp -f "$icu" "$romfs/icudtl.dat"

# --- 2. Kernel --------------------------------------------------------------
# package:flutter liegt als Quellpaket im SDK; `flutter build bundle` oben hat
# die Paketaufloesung erzeugt.
packages="$project/.dart_tool/package_config.json"
entry="$project/lib/main.dart"
for p in "$packages" "$entry"; do
  [ -e "$p" ] || { echo "Nicht gefunden: $p" >&2; exit 1; }
done

# --- Horizon-Registrant -----------------------------------------------------
# Wie im Original: erzeugter Einstiegspunkt, der VOR main() die
# METHOD-CHANNEL-Implementierungen registriert (die Liste waechst mit den
# unterstuetzten Paketen; Antworten kommen aus plugins_horizon.cpp des
# Embedders). Ohne ihn bleibt bei Paketen mit `static late _instance`
# (z.B. file_picker) die Plattform-Instanz ungesetzt.
app_name="$(sed -n 's/^name:[[:space:]]*\([^[:space:]]*\).*/\1/p' "$project/pubspec.yaml" | head -1)"
registrations=""
imports=""
if grep -q '"name"[[:space:]]*:[[:space:]]*"file_picker"' "$packages"; then
  imports="import 'package:file_picker/file_picker.dart' show FilePickerIO;"
  registrations="  FilePickerIO.registerWith();"
fi

if [ -n "$registrations" ]; then
  echo "==> Horizon-Registrant (1 Registrierung)"
  entry="$generated/horizon_main.dart"
  {
    echo '// Von build-dart-app.sh erzeugt - nicht von Hand pflegen.'
    echo "import 'package:${app_name:-app}/main.dart' as app;"
    [ -n "$imports" ] && echo "$imports"
    echo
    echo 'void main() {'
    echo "$registrations"
    echo '  app.main();'
    echo '}'
  } > "$entry"
fi

echo "==> Kernel erzeugen$([ "$product" -eq 1 ] && echo ' (product)')"
dill="$generated/app.dill"
defines=()
[ "$product" -eq 1 ] && defines=(-Ddart.vm.product=true)
"$sdk/bin/dartaotruntime" "$sdk/bin/snapshots/gen_kernel_aot.dart.snapshot" \
  --platform "$platform" \
  --target=flutter \
  --aot \
  --packages "$packages" \
  "${defines[@]+"${defines[@]}"}" \
  -o "$dill" \
  "$entry"

# --- 3. Weiter lokal (kein WSL) --------------------------------------------
# Die Assembly entsteht bewusst nicht hier: sie braucht den selbst gebauten
# clang_x64/gen_snapshot_product (Snapshot-Hash aus 15 geaenderten
# Dart-Quellen; kein Compressed Pointers) - siehe scripts/rebuild-all.sh.
echo
echo "Weiter mit: $root/scripts/rebuild-all.sh ui_app"