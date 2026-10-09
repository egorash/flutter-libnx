#!/usr/bin/env bash
# Prueft, welche Werkzeuge fuer den Weg "Flutter-App -> NRO" schon vorliegen.
#
# macOS-Port von scripts/probe-app-toolchain.ps1. Der Weg ist:
# flutter build bundle (Assets) + gen_kernel (app.dill) +
# gen_snapshot (AArch64-Assembly) + devkitA64 (assemblieren und linken).
set -uo pipefail

flutter="${FLUTTER_HOME:-$HOME/flutter-3.41.6}"
engine="$flutter/bin/cache/artifacts/engine"

echo "=== Flutter-SDK"
if [ -d "$flutter" ]; then
  echo "  $flutter"
  version="$(cat "$flutter/version" 2>/dev/null || true)"
  echo "  Version: ${version:-unbekannt}"
else
  echo "  nicht gefunden"
  exit 1
fi

echo
echo "=== gen_snapshot fuer arm64"
find "$engine" -type f \( -name 'gen_snapshot' -o -name 'gen_snapshot.exe' \) \
  2>/dev/null | head -10 \
  | sed "s|$engine/||" | sed 's/^/  /'

echo
echo "=== Kernel-Compiler und Plattform-Dill"
find "$engine" -type f \
  \( -name 'gen_kernel*.dart.snapshot' -o -name 'frontend_server*.dart.snapshot' \
     -o -name 'vm_platform*.dill' \) \
  2>/dev/null | head -10 \
  | sed "s|$engine/||" | sed 's/^/  /'

echo
echo "=== icudtl.dat (braucht die Engine zur Laufzeit)"
while IFS= read -r f; do
  kb="$(wc -c < "$f" | awk '{printf "%.1f", $1/1048576}')"
  echo "  ${f#"$engine"/}  (${kb} MB)"
done < <(find "$engine" -type f -name icudtl.dat 2>/dev/null | head -5)