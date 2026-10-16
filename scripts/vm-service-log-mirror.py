#!/usr/bin/env python3
"""Зеркалит поток Dart-логов с отладочной NRO на Mac.

Зачем нужен этот скрипт
----------------------
Логи Dart (`developer.log`, в приложениях обычно GetX) уходят в Dart-VM в
служебный поток "Logging". Но VM отправляет их только если на поток подписан
хотя бы один клиент:

    // runtime/vm/service.cc, Service::SendLogEvent
    if (!Service::logging_stream.enabled()) { return; }   // запись отброшена

Это не зависит от режима сборки. В release-ветке службы нет вовсе, в debug она
есть, но без клиента запись всё равно теряется. Именно этот клиент делает логи
видимыми - после подписки они приходят событиями WebSocket.

Клиент живёт на Mac, а не внутри NRO: тот же самый сервис, но без
WebSocket-стека, JSON-разбора и лишнего сокета в памяти устройства.

Запуск
------
    python3 scripts/vm-service-log-mirror.py --host <ip-консоли>
    python3 scripts/vm-service-log-mirror.py --host <ip> --out logs/debug.log

Требуется python3 с модулем `websockets` (в системе уже установлен).
"""

from __future__ import annotations

import argparse
import asyncio
import datetime
import json
import sys

# Уровни dart:developer в числовом виде; см. package:logging.
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

# Номер изолята -> короткое имя. Без имён в протоколе видно только "main", и
# неясно, из какой изоляты пришла строка; имена берутся из getVM, которое
# вызывается перед streamListen.
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
            "ОШИБКА: не найден python3-модуль 'websockets' (pip install websockets)",
            file=sys.stderr,
        )
        sys.exit(1)

    url = f"ws://{host}:{port}/{auth_code}/ws" if auth_code else f"ws://{host}:{port}/ws"
    emit(f"==> подключение к {url}")

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

        # Имена изолят - чтобы строки лога были читаемыми.
        await call("getVM")

        # Без этого вызова logging_stream.enabled() ложно, и VM отбрасывает
        # каждую запись. В этом и заключается смысл скрипта.
        await call("streamListen", {"streamId": "Logging"})
        emit("==> подписка на поток 'Logging' - пошли строки лога")

        async for raw in ws:
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            # Ответ на getVM: запоминаем имена изолят.
            result = msg.get("result")
            if isinstance(result, dict) and "isolates" in result:
                for iso in result["isolates"]:
                    isolate_names[iso.get("id", "")] = iso.get("name", "?")
                continue

            # Событие потока Logging.
            if msg.get("method") != "event":
                # Ошибки (id + error) здесь достаточно интересны, чтобы их показать.
                if "error" in msg:
                    emit(f"[vm-service] ОШИБКА {msg['error']}")
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
                # Сообщения самой службы (например, "Подключение к ...").
                for line in str(data.get("value", "")).splitlines():
                    emit(f"[{now()}][sys] {line}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Зеркалить Dart-логи отладочной NRO на Mac")
    ap.add_argument("--host", required=True, help="IP консоли")
    ap.add_argument("--port", type=int, default=8181,
                    help="порт VM-сервиса (по умолчанию 8181)")
    ap.add_argument("--auth-code", default="",
                    help="токен в пути, если без --disable-service-auth-codes")
    ap.add_argument("--out", help="файл протокола (дополнительно к stdout)")
    args = ap.parse_args()

    out_file = open(args.out, "a", buffering=1) if args.out else None
    try:
        asyncio.run(run(args.host, args.port, args.auth_code, out_file))
    except KeyboardInterrupt:
        emit("==> прервано")
    except OSError as exc:
        print(f"ОШИБКА: {exc}", file=sys.stderr)
        sys.exit(1)
    finally:
        if out_file:
            out_file.close()


if __name__ == "__main__":
    main()