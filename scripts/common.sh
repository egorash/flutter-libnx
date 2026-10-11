#!/usr/bin/env bash
# Gemeinsame Helfer fuer die Build-/Setup-Skripte (flutter-libnx, macOS-Port).
#
# WICHTIG: common.sh setzt KEIN `set -euo pipefail` selbst - jedes Skript
# deklariert die Flags am Anfang (sichtbare Kontrolle) und laedt dann
# common.sh. Bereitgestellt werden:
#
#   REPO          absolute Wurzel des Repos (nur aus BASH_SOURCE[0], kein cd)
#   SCRIPTS       "$REPO/scripts"
#   die()         Meldung nach stderr + exit 1
#   require_cmd() command -v mit klarer Fehlermeldung (+ optionalem Hinweis)
#
# Idempotent: mehrfaches Sourcen ist unschaedlich - bereits gesetzte Variablen
# (z.B. REPO aus der Umgebung) werden nicht ueberschrieben.

if [ -z "${REPO:-}" ]; then
  REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
# SCRIPTS ist Teil der common.sh-API, an die Skripte per $SCRIPTS kommen.
# Passt: die Variable wird dort benutzt, nicht in common.sh selbst.
# shellcheck disable=SC2034
SCRIPTS="$REPO/scripts"

die() {
  echo "FEHLER: $*" >&2
  exit 1
}

# require_cmd NAME [Hinweis]
require_cmd() {
  local cmd="$1" hint="${2:-}"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    if [ -n "$hint" ]; then
      die "fehlendes Werkzeug '$cmd' - ${hint}"
    fi
    die "fehlendes Werkzeug '$cmd' (nicht in PATH)"
  fi
}