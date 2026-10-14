# plugin_template — собираемый шаблон для добавления плагинов

Минимальный рабочий пример-цель: тот же раннер, что у `ui_app` (симлинк),
та же цепочка сборки, но с двумя заготовками для нового плагина —

- `source/handler_stub.cpp` — **C++-хендлер-референс** (компилируется, но
  не вызывается): образец ответа на `StandardMethodCodec`-канал и подсказка,
  куда копировать функцию;
- `dart/lib/main.dart` — **Dart-демо**: кнопка вызывает канал
  `plugins.flutter.io/plugin_template` и честно показывает результат.

Собирается как любой пример:

```sh
source ./env.sh
bash scripts/build-dart-app.sh --example plugin_template          # упаковка assets/kernel
bash scripts/build-example-wsl.sh plugin_template                # make -> plugin_template.nro
```

Пока хендлер не подключён, кнопка показывает «Kanal nicht implementiert»
(`MissingPluginException`) — это **ожидаемое** поведение, а не баг.

---

## Как добавить новый плагин (чеклист)

Есть два пути (подробно — `docs/infra.md`; планы —
`docs/plugins-plan.md`, `docs/plugin-plan-my-nexus.md`, `docs/macos-migration.md`).

### Путь A — C++-хендлер канала (простой протокол)

1. Написать хендлер (образец — `source/handler_stub.cpp`) и **скопировать
   его в** `embedder/src/engine/plugins_horizon.cpp` (в `namespace flutter_libnx`).
2. Добавить **одну строку** в реестр каналов (`MakeRegistry()`,
   `plugins_horizon.cpp`), связывающую имя канала с функцией:
   ```cpp
   r.method["plugins.flutter.io/plugin_template"] = flutter_libnx_plugin_template_handler;
   ```
   (для каналов со своим кодеком/EventChannel — `r.raw[...]`, см. `file_picker`/`geolocator`).
3. Пересобрать движок-пример: `bash scripts/rebuild-all.sh plugin_template`.
4. Dart-сторона: `MethodChannel('<тот же канал>')` + демо-кнопка (как в
   `dart/lib/main.dart`). Убедиться, что неизвестные методы возвращают
   `false` (честная `MissingPluginException`, а не молчание).

### Путь B — Dart-шим (подмена platform-interface)

Плагины вроде `image_picker`/`image_cropper`/`webview_flutter` живут за
static-инстансом `XPlatform.instance`; на Horizon нет устройства их
регистрации, поэтому подменяем инстанс шимом:

1. Создать `dart_helpers/horizon_plugins/shim_<пакет>.dart` — каркас можно
   скопировать с существующих (`shim_image_picker.dart` и т.д.): класс-наследник
   платформенного интерфейса + `void registerHorizonShim() { XPlatform.instance = ...; }`.
2. Добавить **маркер** `// HORIZON_SHIM: READY` — без него `build-dart-app.sh`
   файл не подключит (защита: недоделанный шим не ломает рантайм).
3. Положить пакет в зависимости `dart/pubspec.yaml` и запустить `flutter pub get` —
   детект по `package_config.json` произойдёт сам.
4. Пересобрать: `build-dart-app.sh --example plugin_template` — регистрант
   скопирует шим в `generated/` и вызовет `registerHorizonShim()` перед `main()`.
5. Шим обязан быть «честным»: осмысленный ответ, либо `PlatformException`/
   `MissingPluginException` — никакого молчаливого `false`/`0`.

### Правила (AGENTS.md)

- `set -euo pipefail`, идемпотентность, shellcheck без `severity=error`.
- Контракт скрипта меняется — запись в `docs/macos-migration.md`.
- Комментарии и документация — по-русски (см. AGENTS.md §7).
- `.ps1` не удаляем.