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
#   build-dart-app.sh --example my_nexus --project /pfad/app
#                                             -> Ausgabe nach examples/my_nexus
#   build-dart-app.sh --product               -> -Ddart.vm.product=true
#
# WICHTIG zum Projektpfad: wie im Original liegen Ausgabeziele (generated/,
# romfs/) NEBEN dem gewaehlten Beispiel (--example, Standard examples/ui_app),
# nicht im fremden Projekt - ein Werkzeug soll das Projekt, das es uebersetzt,
# nicht veraendern.
set -euo pipefail

# --- Иконка NRO --------------------------------------------------------------
# libnx bettet eine 256x256-JPEG in die .nro ein; das devkitPro-Makefile der
# Beispiele nimmt sie von selbst auf (icon.jpg oder <TARGET>.jpg im
# Beispielverzeichnis). Quelle ist die Icon des Flutter-Projekts selbst:
# zuerst assets/icons/logo.png bzw. assets/logo.png (dasselbe Bild, das auch
# flutter_launcher_icons nutzt), sonst die groesste Android-mipmap
# (xxxhdpi, 192px). PNG->JPEG 256x256 macht sips (macOS); Ausweichwege:
# ImageMagick (magick/convert) oder python3+Pillow. Fehlt beides, baut die .nro
# mit der Standard-Icon - die Meldung ist nur ein Hinweis, kein Abbruch.
pick_nro_icon() {
  local project="$1" app="$2"
  local src=""
  local c d

  for c in \
    "$project/assets/icons/logo.png" \
    "$project/assets/logo.png" \
    "$project/assets/app_icon.png" \
    "$project/assets/icon.png"; do
    [ -f "$c" ] && { src="$c"; break; }
  done
  if [ -z "$src" ]; then
    # Reihenfolge = Dichte: xxxhdpi(192) > xxhdpi(144) > xhdpi(96) > hdpi(72).
    for d in xxxhdpi xxhdpi xhdpi hdpi mdpi; do
      c="$project/android/app/src/main/res/mipmap-$d/ic_launcher.png"
      [ -f "$c" ] && { src="$c"; break; }
    done
  fi
  if [ -z "$src" ]; then
    echo "    keine Icon-Quelle im Projekt - .nro bekommt die Standard-Icon" >&2
    return 0
  fi

  local out="$app/icon.jpg"
  if command -v sips >/dev/null 2>&1; then
    sips -s format jpeg -z 256 256 "$src" --out "$out" >/dev/null 2>&1 \
      || rm -f "$out"
  elif command -v magick >/dev/null 2>&1; then
    magick "$src" -resize 256x256! -quality 90 "$out" || rm -f "$out"
  elif command -v convert >/dev/null 2>&1; then
    convert "$src" -resize 256x256! -quality 90 "$out" || rm -f "$out"
  elif python3 -c 'import PIL' >/dev/null 2>&1; then
    python3 - "$src" "$out" <<'PY' || rm -f "$out"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
im = im.resize((256, 256), Image.LANCZOS)
im.save(sys.argv[2], "JPEG", quality=90)
PY
  else
    echo "    kein Konverter (sips/magick/convert/PIL) - Standard-Icon" >&2
    return 0
  fi

  if [ -s "$out" ]; then
    size_b="$(wc -c < "$out" | tr -d ' ')"
    echo "    NRO-Icon: $src -> $out (${size_b} Bytes)"
  else
    echo "    Icon-Konvertierung fehlgeschlagen - Standard-Icon" >&2
    rm -f "$out"
  fi
}

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
example="ui_app"
product=0
usage() {
  echo "Verwendung: $0 [--project PFAD] [--example NAME] [--product]" >&2
  exit 1
}
while [ $# -gt 0 ]; do
  case "$1" in
    --project) project="${2:?--project braucht einen Pfad}"; shift 2 ;;
    --project=*) project="${1#*=}"; shift ;;
    --example) example="${2:?--example braucht einen Namen}"; shift 2 ;;
    --example=*) example="${1#*=}"; shift ;;
    --product) product=1; shift ;;
    -h|--help) usage ;;
    *) echo "Unbekanntes Argument: $1" >&2; usage ;;
  esac
done

if [ -z "$project" ]; then project="$root/examples/$example/dart"; fi
project="$(cd "$project" && pwd)"

# Das Ausgabeziel liegt neben dem gewaehlten Beispiel (siehe Kopf).
app="$root/examples/$example"
generated="$app/generated"
romfs="$app/romfs"
assets="$romfs/flutter_assets"

for p in "$sdk" "$platform" "$icu" "$project/pubspec.yaml"; do
  [ -e "$p" ] || { echo "Nicht gefunden: $p" >&2; exit 1; }
done

mkdir -p "$generated" "$romfs"
echo "==> Projekt: $project"
pick_nro_icon "$project" "$app"

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
# Plattform-Registrierungen ausfuehrt - Method-Channels aus
# plugins_horizon.cpp (file_picker, ...) und Dart-Shims aus
# dart_helpers/horizon_plugins/ (image_picker, image_cropper,
# webview_flutter). Ohne Registranten bleibt bei Paketen mit statischer
# Plattform-Instanz die Instanz ungesetzt (z.B. file_picker) oder das Paket
# faellt mit einem Null-Check in Dart (z.B. webview_flutter: `instance!`).
# Der Registrant wird daher IMMER erzeugt, sobald EINES der unterstuetzten
# Pakete im Projekt steckt - auch wenn noch nichts zu registrieren ist,
# damit die Kommentarzeilen im erzeugten File zeigen, was noch fehlt.
#
# Dart-Shims: fuer jedes erkannte Paket wird
# dart_helpers/horizon_plugins/shim_<paket>.dart gesucht. Eingebunden wird
# NUR ein fertiger Shim - er muss den Marker  // HORIZON_SHIM: READY
# enthalten (dann: Kopie nach generated/ + Import + registerHorizonShim()).
# Fehlt der Marker oder die Datei, landet stattdessen eine Kommentarzeile
# im erzeugten File.
app_name="$(sed -n 's/^name:[[:space:]]*\([^[:space:]]*\).*/\1/p' "$project/pubspec.yaml" | head -1)"

# detected: mindestens eines der unterstuetzten Pakete ist im Projekt.
detected=0
imports=()
regs=()
notes=()
shims=()
# shim_pkgs: erweitern, wenn ein weiteres Paket einen Dart-Shim bekommt.
shim_pkgs='image_picker image_cropper webview_flutter'

if grep -q '"name"[[:space:]]*:[[:space:]]*"file_picker"' "$packages"; then
  detected=1
  imports+=("import 'package:file_picker/file_picker.dart' show FilePickerIO;")
  regs+=("  FilePickerIO.registerWith();")
fi

for p in $shim_pkgs; do
  grep -q "\"name\"[[:space:]]*:[[:space:]]*\"${p}\"" "$packages" || continue
  detected=1
  shim_file="$root/dart_helpers/horizon_plugins/shim_$p.dart"
  if [ -f "$shim_file" ] && grep -q '// HORIZON_SHIM: READY' "$shim_file"; then
    shims+=("$shim_file")
    imports+=("import 'shim_$p.dart' as shim_$p;")
    regs+=("  shim_$p.registerHorizonShim();")
  elif [ -f "$shim_file" ]; then
    notes+=("// $p: Shim vorhanden (dart_helpers/horizon_plugins/shim_$p.dart), aber nicht READY")
  else
    notes+=("// $p: kein Shim unter dart_helpers/horizon_plugins/")
  fi
done

if [ "$detected" -eq 1 ]; then
  for s in "${shims[@]+"${shims[@]}"}"; do
    cp -f "$s" "$generated/$(basename "$s")"
  done
  echo "==> Horizon-Registrant (${#regs[@]} Registrierungen, ${#shims[@]} Shims)"
  entry="$generated/horizon_main.dart"
  {
    echo '// Von build-dart-app.sh erzeugt - nicht von Hand pflegen.'
    echo "import 'package:${app_name:-app}/main.dart' as app;"
    for i in "${imports[@]+"${imports[@]}"}"; do echo "$i"; done
    [ "${#notes[@]}" -gt 0 ] && echo
    for note in "${notes[@]+"${notes[@]}"}"; do echo "$note"; done
    echo
    echo 'void main() {'
    for r in "${regs[@]+"${regs[@]}"}"; do echo "$r"; done
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
echo "Weiter mit: $root/scripts/rebuild-all.sh $example"