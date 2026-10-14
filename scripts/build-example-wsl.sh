#!/usr/bin/env bash
# Baut ein Beispiel in WSL statt im Container.
#
# Noetig, sobald ein Beispiel gegen die Engine linkt: Der Engine-Checkout liegt
# im WSL-Dateisystem und ist im Container nicht sichtbar. devkitA64 wurde
# dafuer ohnehin schon nach ~/devkitpro entpackt.
#
#   build-example-wsl.sh engine_link_test
set -euo pipefail

EXAMPLE="${1:-engine_link_test}"
shift || true   # der Rest geht an make weiter, der Name nicht
# Gemeinsame Helfer (REPO, die, require_cmd) - siehe scripts/common.sh.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="$DEVKITPRO/devkitA64"
# ~/bin enthaelt die ohne root beschafften Werkzeuge (make, pkg-config).
export PATH="$HOME/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
export ENGINE_SRC="${ENGINE_SRC:-$HOME/engine/flutter/engine/src}"

# MODE bestimmt, welches libflutter_engine.a gelinkt wird - und ob die
# C++-Seite den JIT-Zweig uebersetzt. rebuild-all.sh setzt ihn; ohne MODE
# bleibt es beim Release-Build. Ein von aussen gesetztes ENGINE_OUT darf den
# Modus nicht stillschweigend ueberschreiben - sonst linkt der Debug-Build
# gegen die Release-Engine.
MODE="${MODE:-release}"
mode_out="$ENGINE_SRC/out/horizon_${MODE}_arm64"
if [ -n "${ENGINE_OUT:-}" ] && [ "$ENGINE_OUT" != "$mode_out" ]; then
  echo "WARNUNG: ENGINE_OUT=$ENGINE_OUT passt nicht zu MODE=$MODE; verwende $mode_out" >&2
fi
export MODE
export ENGINE_OUT="$mode_out"

require_cmd make "Xcode CLT (xcode-select --install) oder ~/bin (extract-make.sh)"
require_cmd pkg-config "x86_64- bzw. macOS-pkg-config (siehe extract-pkgconf.sh)"

cd "$REPO/examples/$EXAMPLE"
echo "==> make in $(pwd)"
echo "    DEVKITPRO=$DEVKITPRO"
echo "    MODE=$MODE"
echo "    ENGINE_OUT=$ENGINE_OUT"
make "$@"
