#!/usr/bin/env bash
# Nimmt die Logausgabe der Switch entgegen.
#
# macOS-Port von scripts/log-listener.ps1. BSD-nc kann nicht "auf eine
# Verbindung warten" wie GNU-socat - deshalb nutzt das Skript einen
# python3-Socket (python3 ist auf macOS ueber Xcode CLT/Homebrew vorhanden).
#
# Richtung ist Absicht: Die Konsole verbindet sich hierher, nicht umgekehrt.
# Deshalb ist es egal, ob dieser Empfaenger hinter NAT sitzt - im Gegensatz zu
# `nxlink -s`, das genau daran scheitert.
#
# Aufruf:
#   log-listener.sh [--port 28800] [--timeout 120] [--out datei.log]
set -euo pipefail

port=28800
timeout_seconds=120
out_file=""

usage() {
  echo "Verwendung: $0 [--port PORT] [--timeout SEK] [--out DATEI]" >&2
  exit 1
}
while [ $# -gt 0 ]; do
  case "$1" in
    --port) port="${2:?--port braucht eine Zahl}"; shift 2 ;;
    --port=*) port="${1#*=}"; shift ;;
    --timeout) timeout_seconds="${2:?--timeout braucht eine Zahl}"; shift 2 ;;
    --timeout=*) timeout_seconds="${1#*=}"; shift ;;
    --out) out_file="${2:?--out braucht einen Pfad}"; shift 2 ;;
    --out=*) out_file="${1#*=}"; shift ;;
    -h|--help) usage ;;
    *) echo "Unbekanntes Argument: $1" >&2; usage ;;
  esac
done

case "$port" in
  ''|*[!0-9]*) echo "Port muss eine Zahl sein: $port" >&2; exit 1 ;;
esac
case "$timeout_seconds" in
  ''|*[!0-9]*) echo "Timeout muss eine Zahl sein: $timeout_seconds" >&2; exit 1 ;;
esac

export LL_PORT="$port" LL_TIMEOUT="$timeout_seconds" LL_OUT="${out_file:-}"
python3 - "$@" <<'PY'
import os, socket, sys

port = int(os.environ["LL_PORT"])
timeout = int(os.environ["LL_TIMEOUT"])
out_file = os.environ.get("LL_OUT") or None

print(f"Warte auf Verbindung der Switch auf Port {port} (Zeitlimit {timeout}s)...", flush=True)

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("", port))
srv.listen(1)
srv.settimeout(timeout)

try:
    conn, addr = srv.accept()
except socket.timeout:
    print("Zeitlimit erreicht, keine Verbindung.", flush=True)
    sys.exit(0)

with conn:
    print(f"Verbunden: {addr}", flush=True)
    print("-" * 60, flush=True)
    lines = []
    stream = conn.makefile("r", encoding="utf-8", errors="replace")
    for line in stream:
        line = line.rstrip("\n")
        print(line, flush=True)
        lines.append(line)
    print("-" * 60, flush=True)
    print("Verbindung beendet.", flush=True)
    if out_file:
        with open(out_file, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + ("\n" if lines else ""))
        print(f"Protokoll: {out_file}", flush=True)
PY