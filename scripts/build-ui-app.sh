#!/usr/bin/env bash
# Erzeugt aus examples/ui_app/dart/main.dart die Artefakte, die die Engine zum
# Starten einer Dart-Anwendung braucht.
#
# macOS-Port von scripts/build-ui-app.ps1.
#
# Die Kette:
#   main.dart --gen_kernel--> app.dill --gen_snapshot--> app_aot.s
#
# Unterschied zum aot_poc: Dort genuegte vm_platform_product.dill, weil das
# Programm nur dart:core benutzte. Hier wird dart:ui gebraucht, das steckt in
# der Flutter-eigenen Plattform-Dill (flutter_patched_sdk_product).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
flutter="${FLUTTER_HOME:-$HOME/flutter-3.41.6}"
sdk="$flutter/bin/cache/dart-sdk"
engine="$flutter/bin/cache/artifacts/engine"
platform="$engine/common/flutter_patched_sdk_product/platform_strong.dill"
icu="${ICU:-$engine/darwin-arm64/icudtl.dat}"
if [ ! -f "$icu" ]; then
  found="$(find "$engine" -name icudtl.dat 2>/dev/null | head -1 || true)"
  [ -n "$found" ] && icu="$found"
fi

app="$root/examples/ui_app"
generated="$app/generated"
romfs="$app/romfs"
assets="$romfs/flutter_assets"

for p in "$sdk" "$platform" "$icu"; do
  [ -e "$p" ] || { echo "Nicht gefunden: $p" >&2; exit 1; }
done

mkdir -p "$generated" "$romfs" "$assets"
dill="$generated/app.dill"

# Sobald package:flutter im Spiel ist, reicht die Plattform-Dill nicht mehr:
# dart:ui steckt darin, das Framework liegt als Quellpaket im SDK und wird nur
# ueber die Paketaufloesung gefunden. Die erzeugt `flutter pub get` in
# examples/ui_app/dart aus der dortigen pubspec.yaml.
packages="$app/dart/.dart_tool/package_config.json"
if [ ! -f "$packages" ]; then
  echo "Nicht gefunden: $packages - erst 'flutter pub get' in $app/dart laufen lassen" >&2
  exit 1
fi

echo '==> Kernel erzeugen (Framework ueber package_config.json)'
"$sdk/bin/dartaotruntime" "$sdk/bin/snapshots/gen_kernel_aot.dart.snapshot" \
  --platform "$platform" \
  --target=flutter \
  --aot \
  --packages "$packages" \
  -o "$dill" \
  "$app/dart/lib/main.dart"

# Die Assembly entsteht bewusst NICHT hier - zustaendig ist
# scripts/rebuild-all.sh mit dem selbst gebauten clang_x64/gen_snapshot_product
# (Snapshot-Hash aus geaenderten Dart-Quellen, kein Compressed Pointers).
echo
echo "Weiter mit: $root/scripts/rebuild-all.sh ui_app"

# Die Engine erwartet ein Asset-Verzeichnis, auch wenn die Anwendung keine
# Bilder mitbringt. Fehlen die Manifeste, bricht der AssetManager beim Start ab.
echo '==> flutter_assets anlegen'
material_fonts="$flutter/bin/cache/artifacts/material_fonts"
icon_font="$material_fonts/materialicons-regular.otf"
if [ ! -f "$icon_font" ]; then
  echo "Nicht gefunden: $icon_font" >&2
  exit 1
fi

mkdir -p "$assets/fonts"
cp -f "$icon_font" "$assets/fonts/MaterialIcons-Regular.otf"

printf '%s\n' '{"": []}' > "$assets/AssetManifest.json"
# Der Familienname muss "MaterialIcons" lauten - genau den traegt IconData aus
# dem Framework. Der Pfad ist relativ zum flutter_assets-Verzeichnis.
printf '%s\n' '[{"family":"MaterialIcons","fonts":[{"asset":"fonts/MaterialIcons-Regular.otf"}]}]' \
  > "$assets/FontManifest.json"
cp -f "$icu" "$romfs/icudtl.dat"
icon_kb="$(wc -c < "$icon_font" | awk '{printf "%.0f", $1/1024}')"
echo "    MaterialIcons-Regular.otf (${icon_kb} KB)"
echo "    AssetManifest.json, FontManifest.json, icudtl.dat"

echo
echo "Fertig. Artefakte in $generated"