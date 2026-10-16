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
#   build-dart-app.sh --mode debug            -> JIT-Kernel fuer den Debug-Build
#
# Modi (siehe docs/build-modes-plan.md):
#   release (Vorgabe)  flutter build bundle + gen_kernel_aot --aot, Ergebnis
#                      app.dill -> rebuild-all.sh -> app_aot.s -> .nro
#   debug               flutter build bundle --debug, Kernel bleibt als
#                      kernel_blob.bin im RomFS. rebuild-all.sh ueberspringt
#                      gen_snapshot; die Engine laeuft im JIT-Modus.
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

# Gemeinsame Helfer (REPO, die, require_cmd) - siehe scripts/common.sh.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
root="$REPO"
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
mode="release"
usage() {
  echo "Verwendung: $0 [--project PFAD] [--example NAME] [--mode release|debug]" >&2
  echo "  --mode release (Vorgabe): AOT, app.dill -> app_aot.s -> .nro" >&2
  echo "  --mode debug:             JIT, kernel_blob.bin im RomFS" >&2
  exit 1
}
while [ $# -gt 0 ]; do
  case "$1" in
    --project) project="${2:?--project braucht einen Pfad}"; shift 2 ;;
    --project=*) project="${1#*=}"; shift ;;
    --example) example="${2:?--example braucht einen Namen}"; shift 2 ;;
    --example=*) example="${1#*=}"; shift ;;
    --mode)   mode="${2:?--mode braucht einen Wert}"; shift 2 ;;
    --mode=*) mode="${1#*=}"; shift ;;
    # Frueher hiess der Weg in den Release-Build "--product". Der Auslieferungs-
    # build setzt das define dart.vm.product inzwischen selbst (siehe unten),
    # deshalb ist der Schalter nur noch ein Synonym fuer --mode release.
    --product)
      echo "WARNUNG: --product ist deprecated, benutze --mode release" >&2
      mode="release"; shift ;;
    -h|--help) usage ;;
    *) echo "Unbekanntes Argument: $1" >&2; usage ;;
  esac
done

case "$mode" in
  release|debug) ;;
  *) echo "Unbekannter Modus: $mode (erlaubt: release, debug)" >&2; exit 1 ;;
esac

if [ -z "$project" ]; then project="$root/examples/$example/dart"; fi
project="$(cd "$project" && pwd)"

# Das Ausgabeziel liegt neben dem gewaehlten Beispiel (siehe Kopf).
app="$root/examples/$example"
generated="$app/generated"
romfs="$app/romfs"
assets="$romfs/flutter_assets"

# Die Produkt-Dill wird nur fuer den AOT-Kern gebraucht. Im Debug-Modus
# uebersetzt der Flutter-Compiler selbst, und zwar mit der Nicht-Produkt-Dill
# aus dem Bundle - die braucht dieses Skript gar nicht.
for p in "$sdk" "$icu" "$project/pubspec.yaml"; do
  [ -e "$p" ] || { echo "Nicht gefunden: $p" >&2; exit 1; }
done
if [ "$mode" = "release" ]; then
  [ -e "$platform" ] || { echo "Nicht gefunden: $platform" >&2; exit 1; }
fi
[ -x "$flutter/bin/flutter" ] || die \
  "Flutter-SDK fehlt unter $flutter (FLUTTER_HOME) - erst scripts/setup-macos.sh"

mkdir -p "$generated" "$romfs"
echo "==> Projekt: $project"
pick_nro_icon "$project" "$app"

# --- 1. Paketaufloesung -----------------------------------------------------
# package:flutter liegt als Quellpaket im SDK; `flutter build bundle` wuerde
# die Aufloesung zwar auch mit erzeugen - aber der Registrant (Schritt 2)
# braucht package_config.json VOR dem Bundle, sonst fehlen die Plattform-
# Registrierungen im Debug-Kern. Desbuch laeuft `flutter pub get` hier explizit
# vor. Der Aufruf ist idempotent und kostet bei unveraendertem pubspec.lock
# nur eine Zeile.
echo '==> flutter pub get'
(
  cd "$project"
  "$flutter/bin/flutter" pub get
)

packages="$project/.dart_tool/package_config.json"
entry="$project/lib/main.dart"
for p in "$packages" "$entry"; do
  [ -e "$p" ] || { echo "Nicht gefunden: $p" >&2; exit 1; }
done

# --- SDK-Kontrolle ----------------------------------------------------------
# Projekte mit fvm tragen eine .fvm-Verknuepfung auf ein anderes Flutter
# (hier 3.47.7 gegen unsere 3.41.6). Sobald dort eine fvm-Session lief, zeigt
# package_config.json auf deren SDK - und dann kompiliert `flutter build
# bundle` mit dem einen Framework, der Kernel-Schritt mit dem anderen. Der
# Fehler erscheint dann erst spaet als "Type 'ui.HitTestRequest' is not found"
# aus dem fremden Framework, ohne jeden Hinweis auf die Ursache (das kostete
# am 2026-10-16 einen ganzen Bauzyklus).
#
# Deshalb einmal nachsehen und Klartext sagen, statt stillzuschweigen.
python3 - "$packages" "$flutter" "$project" <<'PY' || exit 1
import json
import os
import sys
from urllib.parse import unquote, urlparse

config_path, flutter_home, project = sys.argv[1], sys.argv[2].rstrip("/"), sys.argv[3]

with open(config_path, encoding="utf-8") as handle:
    config = json.load(handle)

root = ""
for package in config.get("packages", []):
    if package.get("name") == "flutter":
        root = unquote(urlparse(package.get("rootUri", "")).path)
        break

if not root:
    print(f"FEHLER: package 'flutter' fehlt in {config_path}", file=sys.stderr)
    sys.exit(1)

expected = os.path.join(flutter_home, "packages", "flutter")
if os.path.realpath(root) != os.path.realpath(expected):
    print(f"FEHLER: package:flutter zeigt auf {root}", file=sys.stderr)
    print(f"       erwartet wird das SDK aus FLUTTER_HOME: {expected}",
          file=sys.stderr)
    print("       Wahrscheinlich hat eine fvm-Session zuletzt in diesem "
          "Projekt gearbeitet.", file=sys.stderr)
    print("       Abhilfe:", file=sys.stderr)
    print(f"         (cd {project} && "
          f"{os.path.join(flutter_home, 'bin', 'flutter')} pub get)",
          file=sys.stderr)
    sys.exit(1)
PY

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

# --- 3. Assets --------------------------------------------------------------
# Im Debug-Modus ist der Einstiegspunkt der Registrant: `flutter build bundle`
# uebersetzt dann selbst zum JIT-Kern (kernel_blob.bin), den die Engine zur
# Laufzeit aus dem RomFS laedt. Ohne -t wuerde lib/main.dart kompiliert und
# die Registrierungen fehlten.
#
# Belegt: ein Ziel ausserhalb des Paketverzeichnisses ist erlaubt. Gegenprobe
# mit einem kaputten Import darin bricht den Build ab, das -t wird also
# tatsaechlich ausgewertet und nicht stillschweigend verworfen.
echo "==> flutter build bundle ($mode)"
(
  cd "$project"
  if [ "$mode" = "debug" ]; then
    "$flutter/bin/flutter" build bundle --debug -t "$entry"
  else
    "$flutter/bin/flutter" build bundle
  fi
)
bundle="$project/build/flutter_assets"
[ -d "$bundle" ] || { echo "Nicht gefunden: $bundle" >&2; exit 1; }

# Im AOT-Build bleiben die JIT-Artefakte draussen (siehe PS1-Kommentar:
# kernel_blob.bin alleine waere >40 MB in einer NRO, die sonst 17 MB wiegt) -
# sie werden von der Engine nicht gelesen. Im Debug-Build ist kernel_blob.bin
# gerade die Anwendung.
if [ "$mode" = "debug" ]; then
  jit_only='.last_build_id'
else
  jit_only='kernel_blob.bin vm_snapshot_data isolate_snapshot_data .last_build_id'
fi

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

# --- 4. Kernel --------------------------------------------------------------
if [ "$mode" = "release" ]; then
  # Defines wie in Flutter's compile.dart (buildModeOptions, BuildMode.release).
  # dart.vm.product ist das, woran kReleaseMode/kDebugMode im Framework hängen:
  #   kReleaseMode = bool.fromEnvironment('dart.vm.product')   (constants.dart:25)
  #   kDebugMode   = !kReleaseMode && !kProfileMode            (constants.dart:64)
  # Ohne dieses define war die bisherige AOT-NRO im Framework "debug" - die
  # Verzweigung hat keine Auswirkung auf die Sichtbarkeit von Logausgaben
  # (siehe docs/build-modes-plan.md, Abschnitt 3), aber sie passte nicht zur
  # Bezeichnung.
  echo "==> Kernel erzeugen (AOT, dart.vm.product)"
  dill="$generated/app.dill"
  "$sdk/bin/dartaotruntime" "$sdk/bin/snapshots/gen_kernel_aot.dart.snapshot" \
    --platform "$platform" \
    --target=flutter \
    --aot \
    --packages "$packages" \
    -Ddart.vm.profile=false \
    -Ddart.vm.product=true \
    -o "$dill" \
    "$entry"

  # --- 5. Weiter lokal (kein WSL) ------------------------------------------
  # Die Assembly entsteht bewusst nicht hier: sie braucht den selbst gebauten
  # clang_arm64/gen_snapshot_product (Snapshot-Hash aus 15 geaenderten
  # Dart-Quellen; kein Compressed Pointers) - siehe scripts/rebuild-all.sh.
  echo
  echo "Weiter mit: $root/scripts/rebuild-all.sh $example"
else
  # Im Debug-Modus entfaellt app.dill: die Engine laeuft im JIT und liest den
  # Kern direkt aus dem RomFS. gen_snapshot wird darum nicht gebraucht und in
  # rebuild-all.sh uebersprungen.
  #
  # Wichtig: eine liegengebliebene app_aot.s vom letzten AOT-Lauf wird hier
  # entfernt. Das Makefile uebernimmt jede *.s aus generated/ und wuerde sie
  # sonst mitassemblieren - 450k Zeilen Fuellmaterial fuer einen Build, der
  # die Symbole gar nicht referenziert.
  rm -f "$generated/app_aot.s" "$generated/app.dill"
  echo
  echo "Weiter mit: $root/scripts/rebuild-all.sh --mode debug $example"
fi