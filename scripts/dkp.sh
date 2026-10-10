#!/usr/bin/env bash
# Fuehrt einen Befehl in der devkitPro-Toolchain aus.
#
# macOS-Port von scripts/dkp.ps1: statt Docker wird hier die NATIVE
# devkitPro-Installation (/opt/devkitpro) bevorzugt; Docker (devkita64) bleibt
# als Fallback fuer Maschinen ohne lokale Toolchain.
#
# Beispiele:
#   dkp.sh examples/hello_libnx make
#   dkp.sh examples/hello_libnx make clean
#   dkp.sh . "aarch64-none-elf-gcc --version"
#
# Aufruf: dkp.sh <WorkDir> <Befehl...>
set -euo pipefail

[ $# -ge 2 ] || { echo "Verwendung: $0 <WorkDir> <Befehl...>" >&2; exit 1; }
workdir="$1"
shift

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

workdir="${workdir#./}"
workdir="${workdir%/}"
dir="$root/${workdir#/}"
[ -d "$dir" ] || { echo "Arbeitsverzeichnis fehlt: $dir" >&2; exit 1; }

run_native() {
  (
    cd "$dir"
    export DEVKITPRO DEVKITA64
    export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
    bash -lc "$*"
  )
}

if [ -x "$DEVKITA64/bin/aarch64-none-elf-gcc" ]; then
  run_native "$@"
  exit $?
fi

# Fallback: devkitPro-Container (wie im Original gepinnt).
if command -v docker >/dev/null 2>&1 && docker image inspect devkitpro/devkita64:latest >/dev/null 2>&1; then
  inner="$(printf '%s' "$workdir" | sed 's:/$::')"
  [ "$inner" = "." ] && inner=""
  container_dir="/work${inner:+/$inner}"
  docker run --rm -v "${root}:/work" -w "$container_dir" devkitpro/devkita64:latest bash -lc "$*"
  exit $?
fi

echo "devkitPro nicht gefunden: weder $DEVKITA64/bin noch Docker-Image devkitpro/devkita64." >&2
exit 1