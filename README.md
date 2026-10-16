# flutter-libnx

**Flutter Engine на Nintendo Switch.** Немодифицированное приложение на
Flutter → один `.nro`, который запускается на консоли.

Порт работает: на железе собран и запущен
[`examples/romashka_game`](examples/romashka_game) — со звуком, кнопками и
аналоговыми стиками.

- Восходящий проект: `https://github.com/miri2577/flutter-libnx`
- Форк: `https://github.com/egorash/flutter-libnx`
- Движок закреплён на **Flutter 3.41.6**

---

## Что работает сегодня

| Область | Состояние |
|---|---|
| Flutter-фреймворк (`runApp`, Material, анимации, шрифты) | ✅ на железе |
| GPU-рендеринг: EGL, OpenGL Core 4.3 через Mesa/nouveau | ✅ на железе |
| Сенсорный ввод, экранные кнопки | ✅ |
| Клавиатурный ввод, экранная клавиатура (applet `swkbd`) | ✅ |
| **Звук**: MP3 через `audioplayers`, 32 голоса, микширование audren | ✅ на железе |
| **Геймпад**: кнопки и стики через `gamepads`, вибрация | ✅ на железе |
| Dart AOT (release, JIT не нужен) | ✅ |
| `dart:io` — файлы, сокеты, TLS/HTTPS | ✅ |
| sqlite3 через `dart:ffi` (включая FTS5): `drift`, `sqflite_common_ffi` | ✅ |
| `path_provider`, `shared_preferences`, `url_launcher` (системный браузер), `file_picker`, `flutter_secure_storage`, `hive` | ✅ |
| Платформенные каналы: JSON и стандартный бинарный кодек | ✅ |

---

## Как это устроено (коротко)

- **Движок Flutter** (закреплён на 3.41.6) кросс-компилируется под devkitA64
  в статическую библиотеку. `horizon` добавлен как целевая ОС в GN, fml, Dart
  VM (пять файлов платформы: потоки, виртуальная память, cpuinfo …) и
  `dart:io` (опросный обработчик событий, loopback-канал пробуждения).
- **Эмбеддер** (этот репозиторий) даёт движку всё, что она ждёт от платформы:
  поверхность EGL, ввод, исполнители задач, шрифты из сервиса `pl:u`, цикл
  сообщений, слой POSIX-совместимости.
- **`dlopen` на Horizon не существует.** FFI всё равно работает: нативные
  библиотеки (sqlite3) линкуются статически и резолвятся через хук эмбеддера
  с сгенерированными таблицами символов — `DynamicLibrary.open('libsqlite3.so')`
  работает, приложение ничего не замечает.
- **Плагины** реализованы в эмбеддере против их настоящих каналов, поэтому
  немодифицированные пакеты с pub.dev работают как есть. Сгенерированный
  «Horizon-регистрант» заменяет тот регистрант, который создаёт
  `flutter build`.
- **Своё время.** `armGetSystemTick()` отдаёт тики, а не наносекунды; частота
  19,2 МГц. Внутри кода — только тики, через
  [`tick_clock.h`](embedder/include/flutter_libnx/tick_clock.h).

Инженерный журнал с разбором каждой ошибки:
[`docs/journal-audio-gamepad.md`](docs/journal-audio-gamepad.md) (звук и ввод),
[`docs/porting-notes.md`](docs/porting-notes.md) и
[`docs/status.md`](docs/status.md) (меленты 0–5, на немецком).

---

## Требования

- Nintendo Switch, способная запускать homebrew (Atmosphère), **в режиме
  приложения** (удерживать R при старте игры → hbmenu). В режиме applet
  доступно около 380 МБ памяти — фреймворк там не запустится.
- macOS на Apple Silicon. Движок кросс-компилируется под devkitA64, цепочка
  сборки — bash. Подробности: [`docs/macos-migration.md`](docs/macos-migration.md).
- ~40 ГБ на диске под checkout движка (`gclient sync`, около 26 ГБ, один раз).
- [devkitPro](https://devkitpro.org/) в `/opt/devkitpro`, группа `switch-dev`.
- Flutter SDK **3.41.6** — отдельная копия, системный brew-flutter не годится.

Подготовка окружения:

```sh
bash scripts/setup-macos.sh                       # brew-пакеты, depot_tools, Flutter
sudo scripts/install-devkitpro-macos.sh          # официальный .pkg → dkp-pacman
source ./env.sh                                   # DEVKITPRO, SRC, FLUTTER_HOME, PATH, …
```

---

## Сборка

```sh
# разовые шаги, по одному на обновление движка
python3 scripts/patch-engine-horizon.py          # патчи, идемпотентно
bash scripts/gn-gen-horizon.sh                    # GN-конфиг horizon/arm64
bash scripts/build-horizon.sh ...:flutter_engine_static
bash scripts/build-sqlite3.sh                    # sqlite3 + таблица символов

# итерация по приложению
bash scripts/build-dart-app.sh --project examples/romashka_game/dart
bash scripts/rebuild-all.sh romashka_game        # AOT-снапшот → .nro
bash scripts/nxlink-upload.sh --switch-ip <ip> --example romashka_game
```

`scripts/log-listener.sh` принимает лог консоли по TCP — подключается
консоль, поэтому NAT и брандмауэр не мешают.

**Режим сборки** задаётся флагом `--mode release|debug`. В release
собирается `out/horizon_release_arm64`, в debug — `out/horizon_debug_arm64`;
в debug дополнительно поднимается VM-сервис. Обоснование и ограничения —
[`docs/build-modes.md`](docs/build-modes.md).

---

## Своё приложение

```sh
bash scripts/build-dart-app.sh --project /путь/к/проекту
bash scripts/rebuild-all.sh <имя_приложения>
```

Приложение править не нужно. Ассеты, шрифты и манифесты приходят из
`flutter build bundle`; пакеты разрешаются через его собственный
`package_config.json`. Данные приложения лежат в
`/switch/flutter_apps/<app-id>/` на карте SD.

---

## Проверка железа без приложения

[`tools/hw_probe`](tools/hw_probe) — маленький NRO без Flutter и Dart,
который отвечает на два вопроса, в большом приложении неразделимые: выдаёт
ли `audren` вообще звук и какие биты приходят с консоли.

```sh
make -C tools/hw_probe
```

На экране: тон 440 Гц и сырая маска кнопок в шестнадцатеричном виде.
260 КБ против 50 МБ у приложения, собирается за секунды.

---

## Известные ограничения

- **Debug-`.nro` не линкуется.** Движок в debug собирается целиком, линковка
  упирается в четыре символа JIT-пути: `basename`,
  `dart::NativeSymbolResolver::AddSymbols`,
  `dart::bin::VirtualMemory::*`, `dart::ThreadInterrupter::*`. Первые три
  закрываются по образцу уже сделанных портов, четвёртый требует решения, а
  не переноса: Horizon не имеет перенесённых сигналов. Список получен из
  линкера — [`docs/build-modes.md`](docs/build-modes.md) §6. На release это
  не влияет.
- `flutter_secure_storage` хранит **открытым текстом** (в FAT нет связки
  ключей) — для homebrew приемлемо, для настоящих секретов нет.
- WebView и видео пока вне объёма (разбор — в
  [`docs/target-apps.md`](docs/target-apps.md)).
- Одно разрешение экрана, 1280×720; масштабирование в доке не сделано.
- В портативном режиме доступна половина кнопок: правый джойкон — это
  A/B/X/Y и D-Pad, левый — L/ZL/Minus/SL. Pro Controller в доке даёт полный
  набор, но перечисления игроков пока нет.

---

## Юридическое

Это независимый проект с открытым кодом для **homebrew-разработки на
принадлежащем вам железе**. Проект не связан с Nintendo и Google, не одобрен
ими и не поддерживается ими. Nintendo Switch — товарный знак Nintendo. Ни
одна часть проекта не предназначена для и не одобряет пиратство.

Лицензии: репозиторий — MIT (см. [LICENSE](LICENSE)). Собирается против
движка Flutter (BSD-3-Clause; патчи в `patches/`), включает amalgamation
SQLite (public domain), декодер minimp3 (public domain) и таблицу символов
libmpv (`third_party/mpv/mpv_symbols.inc`).