#!/usr/bin/env bash
# gn gen für das Horizon-Target "horizon".
#
# Aufruf:
#   gn-gen-horizon.sh                  -> release (AOT), out/horizon_release_arm64
#   gn-gen-horizon.sh --mode debug     -> debug (JIT-VM + VM-Dienst),
#                                         out/horizon_debug_arm64
#
# Der Modus steckt an genau zwei Stellen: is_debug sowie flutter_runtime_mode /
# dart_runtime_mode. Alles andere (Target, CPU, Skia, Buildtools) bleibt in
# beiden Ausprägungen gleich - ein Debug-Build ist also keine Sonderwelt,
# sondern derselbe Code mit anderen Schaltern.
set -uo pipefail

# Gemeinsame Helfer (REPO, die, require_cmd) - siehe scripts/common.sh.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

SRC="${SRC:-$HOME/engine/flutter/engine/src}"
GN="$SRC/flutter/third_party/gn/gn"

MODE="release"
mode_explicit=0
usage() {
  echo "Verwendung: $0 [--mode release|debug]" >&2
  exit 1
}
while [ $# -gt 0 ]; do
  case "$1" in
    --mode)   MODE="${2:?--mode braucht einen Wert}"; mode_explicit=1; shift 2 ;;
    --mode=*) MODE="${1#*=}"; mode_explicit=1; shift ;;
    -h|--help) usage ;;
    *) echo "Unbekanntes Argument: $1" >&2; usage ;;
  esac
done

# Pro Modus: der is_debug-Schalter, die beiden Runtime-Modi und das
# Ausgabeverzeichnis.
#
# dart_runtime_mode ist NICHT "debug": das ist der Flutter-Modus, fuer den
# Dart gilt develop/profile/release (third_party/dart/runtime/BUILD.gn:25).
# Die Zuordnung ist dieselbe wie in Flutter's eigenem tools/gn (Zeile 585):
# flutter_runtime_mode=debug -> dart_runtime_mode=develop.
case "$MODE" in
  release)
    gn_is_debug=false
    gn_flutter_mode="release"
    gn_dart_mode="release"
    default_out="out/horizon_release_arm64"
    gn_extra=""
    ;;
  debug)
    # debug heisst hier zwei Dinge zugleich: der Framework-Code laeuft mit
    # Assertions, und die Dart-VM ist eine JIT-VM (dart_runtime_mode=develop).
    # Nur Letzteres liefert den VM-Dienst und damit Logs/DevTools.
    gn_is_debug=true
    gn_flutter_mode="debug"
    gn_dart_mode="develop"
    default_out="out/horizon_debug_arm64"
    # Perfetto gibt es auf Horizon nicht: base/build_config.h kennt kein
    # Zielos und bricht mit "OS not supported" ab. In release ist es ohnehin
    # aus (runtime_args.gni:98 koppelt es an dart_runtime_mode == "release"),
    # im Debug-Modus nicht - dort muss es ausdruecklich abgeschaltet werden.
    # Nebeneffekt: der Dart-Profiler faellt mit weg (Kommentar dort:92).
    gn_extra="dart_support_perfetto=false"
    ;;
  *) die "Unbekannter Modus: $MODE (erlaubt: release, debug)" ;;
esac

# env.sh setzt OUT fest auf das Release-Verzeichnis. Ein ausdruecklich
# gewaehlter --mode hat Vorrang und ueberschreibt OUT - mit deutlicher Warnung.
# Ohne diese Logik schreibt `gn-gen-horizon.sh --mode debug` dorthin, wo der
# Release-Build liegt, und macht ihn kaputt: gn schreibt args.gn VOR der
# Auswertung, ein Fehler hinterlaesst also die Debug-Argumente liegen.
#
# Vergleich ueber absolute Pfade: env.sh setzt OUT absolut, der Default hier ist
# relativ - ein Zeilenvergleich wuerde denselben Katalog als fremd melden.
if [ -n "${OUT:-}" ]; then
  case "$OUT" in /*) out_gegeben="$OUT" ;; *) out_gegeben="$SRC/$OUT" ;; esac
  case "$default_out" in /*) out_erwartet="$default_out" ;; *) out_erwartet="$SRC/$default_out" ;; esac
  if [ "$out_gegeben" != "$out_erwartet" ]; then
    if [ "$mode_explicit" -eq 1 ]; then
      echo "HINWEIS: OUT=$OUT wird wegen --mode $MODE ignoriert; benutzt wird $default_out" >&2
    else
      die "OUT=$OUT (aus der Umgebung) passt nicht zum Default-Modus $MODE; erwartet wird $default_out. Entweder OUT leer lassen oder 'unset OUT'."
    fi
  fi
fi
OUT="$default_out"

# GN ruft Hilfsskripte über `vpython3` auf, das aus depot_tools kommt.
# ~/bin enthält das ohne root beschaffte pkg-config (siehe extract-pkgconf.sh).
export PATH="$HOME/bin:${DEPOT_TOOLS:-$HOME/depot_tools}:$PATH"
export DEPOT_TOOLS_UPDATE=0

cd "$SRC" || exit 1

echo "==> gn gen $OUT"
echo "    target_os=horizon target_cpu=arm64 mode=$MODE (is_debug=$gn_is_debug)"
echo

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
# Diese vier Angaben verlangt //flutter/shell/version/version.gni per assert.
# Sie landen nur in Artefaktnamen und in der Versionsausgabe der Engine, sind
# also für den Build selbst folgenlos – müssen aber gesetzt sein.
ENGINE_VERSION="${ENGINE_VERSION:-db50e20168db8fee486b9abf32fc912de3bc5b6a}"
SKIA_VERSION="${SKIA_VERSION:-a183ded9ad67d998a5b0fe4cd86d3ef5402ffb45}"
DART_VERSION="${DART_VERSION:-02abc57898bebc334a997e609ce5827c8ef207d7}"

# flutter_runtime_mode=release ist die Kernentscheidung des Projekts: Dart AOT
# statt JIT. Nebenbei schaltet Dart dadurch Perfetto und den Profiler ab
# (runtime/runtime_args.gni:98), die auf Horizon ohnehin nicht übersetzen.
# Im Modus debug ist es genau umgekehrt - JIT-VM und VM-Dienst, den es ohne
# subscribenden Client ohnehin nicht zu sehen gibt.
#
# dart_use_fallback_root_certificates bindet die Wurzelzertifikate fest ins
# Programm ein. Auf Horizon ist das keine Notlösung, sondern der einzige Weg:
# Es gibt keinen Systemzertifikatspeicher und kein /etc/ssl, aus dem sich einer
# lesen ließe. Ohne das Flag bleibt `root_certificates_pem_length` undefiniert
# und jede HTTPS-Verbindung wäre unmöglich.
"$GN" gen "$OUT" \
  --args="target_os=\"horizon\" target_cpu=\"arm64\" is_debug=$gn_is_debug \
flutter_runtime_mode=\"$gn_flutter_mode\" dart_runtime_mode=\"$gn_dart_mode\" \
dart_use_compressed_pointers=false \
dart_use_fallback_root_certificates=true \
$gn_extra \
buildtools_path=\"//flutter/buildtools\" \
shell_enable_gl=true test_enable_gl=false test_enable_vulkan=false \
devkitpro_root=\"$DEVKITPRO\" engine_version=\"$ENGINE_VERSION\" \
content_hash=\"$ENGINE_VERSION\" skia_version=\"$SKIA_VERSION\" \
dart_version=\"$DART_VERSION\"" \
  2>&1 | tail -40

echo
echo "==> Exitcode der Pipeline: ${PIPESTATUS[0]:-unbekannt}"
