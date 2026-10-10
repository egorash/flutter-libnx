#!/usr/bin/env bash
# Erzeugt aus examples/aot_poc/dart/hello.dart einen Dart-AOT-Snapshot als
# AArch64-Assembly und baut daraus eine .nro.
#
# macOS-Port von scripts/build-aot-poc.ps1.
#
# Die Kette:
#   hello.dart --gen_kernel--> hello.dill --gen_snapshot--> hello_aot.s
#              --devkitA64--> hello_aot.o --link--> aot_poc.nro
#
# gen_snapshot stammt aus den arm64-Artefakten des gepinnten Flutter-SDK und
# laeuft auf dem Host (macOS darwin-arm64 statt windows-x64 im Original).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
flutter="${FLUTTER_HOME:-$HOME/flutter-3.41.6}"
sdk="$flutter/bin/cache/dart-sdk"

# gen_snapshot muss mit DART_TARGET_OS_HORIZON gebaut sein: Nur dann ist die
# app-aot-assembly GNU/ELF-Syntax (aarch64-none-elf-as versteht kein Mach-O,
# wie das Flutter-SDK-Artefakt es erzeugt). Er liegt daher im Host-Toolchain
# des Engine-Outs, nicht in den SDK-Artefakten - siehe rebuild-all.sh, Schritt 1.
engine_src="${ENGINE_SRC:-$HOME/engine/flutter/engine/src}"
engine_out="${ENGINE_OUT:-$engine_src/out/horizon_release_arm64}"
if [ "$(uname -s)" = "Darwin" ]; then
  host_tc="clang_arm64"
else
  host_tc="clang_x64"
fi
gen_snap="${GEN_SNAPSHOT:-$engine_out/$host_tc/gen_snapshot_product}"

poc="$root/examples/aot_poc"
generated="$poc/generated"

for p in "$sdk" "$gen_snap"; do
  [ -n "$p" ] && [ -e "$p" ] || { echo "Nicht gefunden: $p" >&2; exit 1; }
done

mkdir -p "$generated"
dill="$generated/hello.dill"

echo '==> Kernel erzeugen (gen_kernel)'
"$sdk/bin/dartaotruntime" "$sdk/bin/snapshots/gen_kernel_aot.dart.snapshot" \
  --platform "$sdk/lib/_internal/vm_platform_product.dill" \
  --aot -o "$dill" "$poc/dart/hello.dart"

echo '==> AArch64-Assembly erzeugen (gen_snapshot)'
"$gen_snap" --snapshot_kind=app-aot-assembly --assembly="$generated/hello_aot.s" "$dill"

lines="$(wc -l < "$generated/hello_aot.s" | tr -d ' ')"
echo "    $lines Zeilen Assembly"

echo '==> NRO bauen (devkitA64)'
bash "$root/scripts/dkp.sh" examples/aot_poc make

echo '==> Symbole in der ELF'
bash "$root/scripts/dkp.sh" examples/aot_poc \
  '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm --defined-only aot_poc.elf | grep -i "kDart.*Snapshot"'

echo 'Fertig.'