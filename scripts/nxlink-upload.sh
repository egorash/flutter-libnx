#!/usr/bin/env bash
# Laedt eine NRO ueber das Netz auf die Switch (hbmenu-Netloader).
#
# macOS-Port von scripts/nxlink-upload.ps1: statt docker --network host wird
# der NATIVE nxlink aus /opt/devkitpro/tools/bin benutzt - auf macOS mit
# lokalem devkitPro ist Docker ueberfluessig.
#
# Aufruf:
#   nxlink-upload.sh --switch-ip 192.168.1.50 [--example ui_app] [-- args...]
#
# WICHTIG: Den Netloader-Port (28280) niemals vorher antasten (Test-NetConnection
# o.ae.). hbmenu nimmt genau eine Verbindung an; eine Testverbindung verbraucht
# sie, und der eigentliche Upload laeuft ins Leere.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
nxlink="${NXLINK:-$DEVKITPRO/tools/bin/nxlink}"

switch_ip=""
example="engine_link_test"
args=()

usage() {
  echo "Verwendung: $0 --switch-ip IP [--example NAME] [-- args...]" >&2
  exit 1
}
while [ $# -gt 0 ]; do
  case "$1" in
    --switch-ip) switch_ip="${2:?--switch-ip braucht eine IP}"; shift 2 ;;
    --switch-ip=*) switch_ip="${1#*=}"; shift ;;
    --example) example="${2:?--example braucht einen Namen}"; shift 2 ;;
    --example=*) example="${1#*=}"; shift ;;
    --) shift; args=("$@"); break ;;
    -h|--help) usage ;;
    *) echo "Unbekanntes Argument: $1" >&2; usage ;;
  esac
done

[ -n "$switch_ip" ] || usage
[ -x "$nxlink" ] || {
  echo "nxlink nicht gefunden: $nxlink (devkitPro-Tools, siehe scripts/install-devkitpro-macos.sh)" >&2
  exit 1
}

nro="$root/examples/$example/$example.nro"
if [ ! -f "$nro" ]; then
  echo "Nicht gefunden: $nro" >&2
  exit 1
fi

size_bytes="$(wc -c < "$nro" | tr -d ' ')"
printf 'Sende %s (%.1f MB) an %s\n' "$nro" \
  "$(awk -v b="$size_bytes" 'BEGIN{printf "%.1f", b/1048576}')" "$switch_ip"

if [ "${#args[@]}" -gt 0 ]; then
  "$nxlink" -a "$switch_ip" "$nro" -- "${args[@]}"
else
  "$nxlink" -a "$switch_ip" "$nro"
fi