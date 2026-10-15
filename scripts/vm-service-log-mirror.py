#!/usr/bin/env python3
"""Spiegelt den Dart-Logstrom einer Debug-NRO auf dem Mac.

Warum es das Skript gibt
-----------------------
Dart-Logs (`developer.log`, in der App meist GetX) landen in der Dart-VM im
Dienststrom "Logging". Die VM schickt sie aber nur, wenn sich mindestens ein
Client angemeldet hat:

    // runtime/vm/service.cc, Service::SendLogEvent
    if (!Service::logging_stream.enabled()) { return; }   // Log wird verworfen

Das ist unabhaengig vom Buildmodus. Im Release-Bau gibt es keinen Dienst, im
Debug-Bau zwar, aber ohne Client wird der Eintrag trotzdem fallengelassen.
Erst dieser Client hier macht die Logs sichtbar - danach kommen sie als
WebSocket-Ereignisse an.

Der Client laeuft bewusst auf dem Mac und nicht in der NRO: derselbe
Dienst, aber ohne WebSocket-Stack, JSON-Parser und Sockets im Geraetespeicher
und ohne weiteren der 16 BSD-Sitzungen, die Horizon zur Verfuegung stehen.

Aufruf
------
    python3 scripts/vm-service-log-mirror.py --host <switch-ip>
    python3 scripts/vm-service-log-mirror.py --host <ip> --out logs/debug.log

Voraussetzung: python3 mit dem Modul `websockets` (im System vorhanden).
"""

from __future__ import annotations

import argparse
import asyncio
import datetime
import json
import sys

# Stufen von dart:developer als Zahlenwerte; siehe package:logging.
LEVEL_NAMES = {
    500: "FINE",
    600: "CONFIG",
    700: "INFO_FINE",
    800: "INFO",
    900: "WARNING",
    1000: "ERROR",
    1100: "SEVERE",
    1200: "SILLY",
}

# Zuordnung Isolate-Nummer -> Kurzname. Ohne Namen im Protokoll sieht man
# nur "main" und weiss nicht, aus welchem Isolate die Zeile stammt; die Namen
# kommen aus getVM, das vor streamListen kommt.
isolate_names: dict[str, str] = {}


def now() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def emit(line: str, out_file=None) -> None:
    print(line, flush=True)
    if out_file:
        out_file.write(line + "\n")
        out_file.flush()


async def run(host: str, port: int, auth_code: str, out_file) -> None:
    try:
        import websockets
    except ImportError:
        print(
            "FEHLER: python3-Modul 'websockets' fehlt (pip install websockets)",
            file=sys.stderr,
        )
        sys.exit(1)

    url = f"ws://{host}:{port}/{auth_code}/ws" if auth_code else f"ws://{host}:{port}/ws"
    emit(f"==> verbinde mit {url}")

    next_id = 0

    async with websockets.connect(url, max_size=None) as ws:

        async def call(method: str, params: dict | None = None) -> int:
            nonlocal next_id
            next_id += 1
            msg = {"jsonrpc": "2.0", "id": str(next_id), "method": method}
            if params:
                msg["params"] = params
            await ws.send(json.dumps(msg))
            return next_id

        # Isolate-Namen fuer lesbare Zeilen einsammeln.
        await call("getVM")

        # Ohne diesen Aufruf ist logging_stream.enabled() fals und die VM
        # verwirft jedes Log. Das ist der eigentliche Zweck des Skripts.
        await call("streamListen", {"streamId": "Logging"})
        emit("==> abonniert auf Stream 'Logging' - jetzt kommen die Logzeilen")

        async for raw in ws:
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            # Antwort auf getVM: Namen der Isolate merken.
            result = msg.get("result")
            if isinstance(result, dict) and "isolates" in result:
                for iso in result["isolates"]:
                    isolate_names[iso.get("id", "")] = iso.get("name", "?")
                continue

            # Ein Event des Logging-Stroms.
            if msg.get("method") != "event":
                # Fehlerantworten (id + error) sind hier interesting genug.
                if "error" in msg:
                    emit(f"[vm-service] FEHLER {msg['error']}")
                continue

            params = msg.get("params", {})
            if params.get("streamId") != "Logging":
                continue

            data = params.get("data", {})
            if params.get("event") == "Log":
                level = LEVEL_NAMES.get(data.get("level"), str(data.get("level", "?")))
                iso = isolate_names.get(data.get("isolateId", ""), "")
                where = f" [{iso}]" if iso else ""
                for line in str(data.get("message", "")).splitlines():
                    emit(f"[{now()}][{level}]{where} {line}")
            elif params.get("event") == "SystemLog":
                # Meldungen des Dienstes selbst (z.B. "Verbinde mit ...").
                for line in str(data.get("value", "")).splitlines():
                    emit(f"[{now()}][sys] {line}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Dart-Logs einer Debug-NRO auf dem Mac spiegeln")
    ap.add_argument("--host", required=True, help="IP der Switch")
    ap.add_argument("--port", type=int, default=8181,
                    help="Port des VM-Dienstes (Default 8181)")
    ap.add_argument("--auth-code", default="",
                    help="Pfad-Token, falls ohne --disable-service-auth-codes")
    ap.add_argument("--out", help="Datei fuer das Protokoll (zusaetzlich stdout)")
    args = ap.parse_args()

    out_file = open(args.out, "a", buffering=1) if args.out else None
    try:
        asyncio.run(run(args.host, args.port, args.auth_code, out_file))
    except KeyboardInterrupt:
        emit("==> abgebrochen")
    except OSError as exc:
        print(f"FEHLER: {exc}", file=sys.stderr)
        sys.exit(1)
    finally:
        if out_file:
            out_file.close()


if __name__ == "__main__":
    main()