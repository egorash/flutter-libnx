# Режимы сборки и отладка на Switch

Практическое руководство. Обоснование и история решений — в
[`build-modes-plan.md`](build-modes-plan.md).

Работает только с портом на macOS (bash-скрипты, `/opt/devkitpro`).

---

## 1. Два режима, коротко

| | release (по умолчанию) | debug |
|---|---|---|
| Dart | AOT-снапшот, вшит в `.nro` | JIT, `kernel_blob.bin` в RomFS |
| Движок | `out/horizon_release_arm64` | `out/horizon_debug_arm64` |
| Сборка ядра | `gen_kernel_aot --aot` + `gen_snapshot` | не нужна |
| Dart-VM | AOT | JIT, **VM-сервис поднят** |
| Размер RomFS (ui_app) | ~1.7 МБ | ~52 МБ |
| Логи `developer.log` | теряются | теряются **без клиента**, видны с клиентом |

> ⚠️ Про логи в release: `developer.log` не доходит до SD-лога **в любом
> режиме**. В Dart-VM запись уходит в служжебный поток `Logging`, а он активен
> только при подключённом клиенте (`runtime/vm/service.cc`, `SendLogEvent`).
> Единственный канал, работающий всегда, — `print()`, который движок
> заворачивает в `[engine:flutter]`.

## 2. Сборка

### 2.1 Разовая подготовка debug-движка

```sh
source ./env.sh
bash scripts/gn-gen-horizon.sh --mode debug       # ~1 мин
OUT=out/horizon_debug_arm64 JOBS=4 \
  bash scripts/build-horizon.sh flutter/shell/platform/embedder:flutter_engine_static
```

`gn gen` для release **не нужно** повторять: у режимов отдельные каталоги.

### 2.2 Приложение + `.nro`

```sh
# release (обычная работа)
bash scripts/build-dart-app.sh --project /pfad/zur/app
bash scripts/rebuild-all.sh ui_app

# debug
bash scripts/build-dart-app.sh --mode debug --project /pfad/zur/app
bash scripts/rebuild-all.sh --mode debug ui_app
```

Скрипт в конце сам печатает точную следующую команду — можно копировать её.

`build-dart-app.sh` в debug делает три вещи, которых в release нет:

1. запускает `flutter pub get` **до** Horizon-регистранта — иначе у
   регистранта не было бы `package_config.json`;
2. передаёт `flutter build bundle --debug -t <registrant>`: точка входа
   должна быть именно сгенерированным регистрантом, иначе теряются
   регистрации `file_picker`, `image_picker`, `webview_flutter`;
3. удаляет `generated/app_aot.s` и `generated/app.dill`, оставшиеся от
   предыдущего AOT-сборки, — `Makefile` подхватывает любой `*.s` из
   `generated/` и иначе ассемблировал бы 450 тысяч строк впустую.

## 3. Подключение к VM-сервису

Сервис слушает `0.0.0.0:8181` — порт фиксированный, иначе клиент его не
найдёт. На Mac:

```sh
# просто логи в файл (то, что нужно для анализа)
python3 scripts/vm-service-log-mirror.py --host <ip-консоли> --out logs/debug.log

# плюс DevTools / точки останова / hot reload
flutter attach --debug-url http://<ip-консоли>:8181
```

**Подключаться нужно до запуска приложения** — либо приложение запускают
после старта клиента. Пока клиента нет, `logging_stream.enabled()` ложно и VM
логи выбрасывает на входе.

Строка приходит в файле так:

```
[18:42:03.114][WARNING] [main] [GET] /api/... 500
```

## 4. Что нужно знать

- **Perfetto и Dart-проfiler на Horizon выключены** (`dart_support_perfetto=false`).
  В release это происходило само (`runtime_args.gni` привязывает его к
  `dart_runtime_mode == "release"`), в debug пришлось отключить явно: без этого
  сборка падает на `perfetto/base/build_config.h: «OS not supported»`.
  Таймлайн-события и профилирование в debug недоступны.
- **mDNS не работает**, поэтому `flutter attach` находит сервис только по
  явному `--debug-url`. Обычный `flutter attach` без адреса не сработает.
- **Размер.** Debug-RomFS `ui_app` — около 52 МБ (`kernel_blob.bin` 40 МБ,
  `isolate_snapshot_data` 11 МБ), итоговая `.nro` заметно больше обычных
  17 МБ. Для `my_nexus` ожидается ещё больше.
- **Скорость.** JIT стартует медленнее AOT и потребляет больше памяти; на
  720p с программным растеризатором это заметно.
- **hot restart не поддерживается** сознательно. Hot reload — см. раздел 5.
- **Не публиковать.** В debug поднимается сетевой сервис без
  аутентификации (`--disable-service-auth-codes`). Такие сборки не годятся
  для распространения.

## 5. Статус проверок на железе

| Проверка | Статус |
|---|---|
| Сборка release-цепочки после изменений | ✅ скрипты и линковка (2026-10-14) |
| `gn gen --mode debug`, сборка debug-движка | ✅ (2026-10-14) |
| Debug-`.nro` запускается на консоли | ⏳ не проверено |
| VM-сервис виден с Mac | ⏳ не проверено |
| Dart-логи (GetX) видны в зеркале | ⏳ не проверено |
| hot reload (`flutter attach` → `r`) | ⏳ не проверено |