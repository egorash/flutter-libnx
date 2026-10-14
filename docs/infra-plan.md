# План инфраструктуры (фундамент)

Статус: черновик. Дата: 2026-10-11.
Ветка: master (дефолт форка egorash/flutter-libnx).

Цель: довести инфраструктуру эмбеддера до состояния «меньше ручной работы при
добавлении плагинов, меньше хрупких мест, проще проверять/собирать».

Документ опирается на: `docs/plugins-plan.md`, `docs/work-status.md`,
`docs/plugin-plan-my-nexus.md`, `docs/macos-migration.md`.

## 1. Что есть сейчас
- Диспетчеризация каналов в `embedder/src/engine/plugins_horizon.cpp` — `if/strcmp`.
- EventChannel-помощник есть частично (только `geolocator`).
- `build-dart-app.sh` генерирует регистрант `horizon_main.dart` (детект `file_picker`).
- Ряд багфиксов/портов bash (macOS-совместимость), частичная документация.

## 2. Цельные блоки работ
| Блок | Приоритет | Зачем |
|---|---|---|
| A. Унифицированный диспетчер каналов (реестр map) | P0 | Добавление плагина — 1 строка, меньше ошибок |
| B. Унифицированный EventChannel-хелпер | P0 | Стримы не роняют/не спамят, единообразно |
| C. Подстановка иконок `.nro` (иконка homebrew) | P0 | Рабочий узнаваемый `.nro` в hbmenu |
| D. Улучшение `build-dart-app.sh` (регистрант + детект плагинов) | P0 | Автоматизация при подключении Dart-шимов |
| E. Упрощение добавления плагинов (шаблон) | P1 | Быстрое добавление A/B-типов |
| F. Автоматизация скриптов | P1 | Меньше ручных ошибок, понятнее |
| G. Документация по инфраструктуре | P1 | Сохранять знания |
| H. Поиск проблем производительности | P1 | Стабильность, FPS, память |
| I. Уменьшение шума в логах | P2 | Диагностика быстрее |

## 3. Подробное планирование

### A. Реестр каналов (map)
Файл: `embedder/src/engine/plugins_horizon.cpp`
- Заменить цепочку `if (channel == "..." ) { Handle...; return true; }` на
  `std::unordered_map<std::string, std::function<bool(...)>>` или `const struct`.
- Регистрировать пары «канал → хендлер» в `static`-конструкторе/инициализаторе.
- Сохранить обратную совместимость (все текущие каналы).
- Логировать «необработанный канал» единообразно.

Критерий: добавление нового хендлера — +1 строка в реестр.

### B. EventChannel-помощник
- Единая функция: `HandleEventChannel(engine,message,handler_state)` с
  обработкой `listen`/`cancel` (StandardMethodCodec).
- Поддержка состояния стрима (ID/флаг).
- Перевести `geolocator` на него, предусмотреть шаблон для будущих стримов.
- Логгинг без лишнего шума.

### C. Иконка `.nro` (homebrew)
Надо научиться подставлять правильные иконки в `.nro`.
Области поиска:
- `scripts/rebuild-all.sh`, `scripts/build-dart-app.sh` (генерация/линковка)
- `makefile`/патчи в `embedder/` (nxlink/upload не связано, но NRO)
- Существующие ассеты: `examples/*/icon.*`, `assets/icon*`
- libnx: `icon.jpg`/`icon.png` → встраивается в NRO? Проверить текущую сборку.
Цель: при сборке `my_nexus` используется осмысленная иконка (а не дефолтная).
Действие: найти как именно формируется `.nro` и как пробрасывается иконка.

### D. `build-dart-app.sh` — регистрант и авто-детект
- Детектить `image_picker`, `image_cropper`, `webview_flutter` помимо `file_picker`.
- Генерировать правильный `horizon_main.dart` с нужными `*.instance = ...` (подготовка
  к Dart-шимам по `plugins-plan.md`).
- Безопасный фолбэк, идемпотентный.
- Короткий комментарий в сгенерированном файле.

### E. Шаблон для плагинов
Создать минимальный шаблон: C++ хендлер + пара строк в реестре + Dart-шим-заготовка
(опц.) + проверочный сценарий. Упростить копирование.

### F. Автоматизация скриптов
Список кандидатов (по `docs/macos-migration.md`):
- Убрать избыточные subshell, унифицировать `set -euo pipefail`.
- Единые проверки `command -v`.
- Меньше дублирования путей (`SRC/OUT/ROOT`).
- Более предсказуемые сообщения об ошибках.
- Проверка наличия `devkitPro/Flutter/depot_tools` до тяжёлых шагов.
- `shellcheck`-проход по критичным скриптам (без излишнего шума).

### G. Документация
`docs/infra.md` (краткий гайд): как добавить новый канал, EventChannel, Dart-шим,
как работает реестр, как подставляется иконка, типичные ловушки.

### H. Производительность
Цель: выявить узкие места без преждевременной оптимизации.
Области:
- Цикл платформенного потока/таск-раннер (`embedder/src/engine/task_runner*`)
- EGL/GL вызовы (`platform/egl_horizon.cpp`)
- Виртуальный курсор/оверлеи
- Память: аллокации в хендлерах, строковые копии в JSON
- Логи: избыточный `LOG_INFO` в горячих путях
- Скан Альбома (если появится браузер фото) — уже учтено в плане
Методика: измерения на железе (логи + субъективный FPS) + локальный анализ.
Наблюдения и таблицы — `docs/perf-notes.md`.

### I. Меньше шума в логах
Снизить уровень/кол-во спамящих логов при нормальной работе, оставить
диагностику для отладки.

## 4. Спринты

### Спринт 1 (фундамент) — фокус: A, B, C (иконка + ввод)
Цель: убрать ручной «if-лес», унифицировать EventChannel, сделать осмысленную иконку NRO, улучшить управление контроллером.

Задачи:
- [x] A1. Спроектировать реестр каналов (структура + инициализация)
- [x] A2. Перенести все текущие каналы на реестр (без функциональных изменений)
- [x] A3. Единый лог «Unknown channel»
- [x] B1. EventChannel-хелпер (listen/cancel + состояние)
- [x] B2. Перевести `geolocator` на хелпер
- [x] C1. Найти вызов `elf2nro` и как берётся иконка (исследование: rebuild-all/build-dart-app/Makefile)
- [x] C2b. Авто-иконка: `build-dart-app.sh` (logo → mipmap → 256x256 JPEG → icon.jpg)
- [x] C3. Разбор `switch_platform.cpp`: pad init, виртуальный курсор, deadzone/sensitivity
- [x] C4. Улучшить маппинг крестовины/A/B, фокус, убрать шум логов ввода, проверить ложные нажатия при старте
      (X→Space, Y→Tab; константы курсора + ускорение; тач гасит курсор; шум срезан;
      ложные нажатия — проверено по коду, на железе)

Критерий готовности: билд проходит, поведение каналов не меняется, `.nro`
имеет иконку в hbmenu (визуально).

### Спринт 2 (автоматизация + шаблон) — D, E, F
Цель: меньше копипаста при добавлении плагинов + мелкие улучшения скриптов.

Задачи:
- [x] D1. Расширить `build-dart-app.sh`: детект `image_picker/image_cropper/webview_flutter`
- [x] D2. Генерация `horizon_main.dart` с учётом списка детектов (без ломки file_picker)
- [x] E1. Шаблон `plugin_template/` (C++ + реестр + Dart-шим-заготовка)
- [x] F1. Единые проверки `command -v` в ключевых скриптах
- [x] F2. `set -euo pipefail` единообразно
- [x] F3. shellcheck по 2–3 критичным скриптам (`build-dart-app.sh`, `rebuild-all.sh`)

### Спринт 3 (производительность + документация + шум) — G, H, I
Цель: зафиксировать текущее состояние, снизить шум, описать инфраструктуру.

Задачи:
- [x] G1. `docs/infra.md` (реестр, EventChannel, иконка, добавление плагина) —
      подробный туториал: поток платформенного сообщения, реестр `raw`/`method`,
      StandardMethodCodec/`StdValue`, EventChannel-хелпер (на примере geolocator),
      регистрант и Dart-шимы (маркер `HORIZON_SHIM: READY`), иконка `.nro`,
      идентификатор приложения, типичные ловушки, раздел «смежное».
      Канонический чеклист «добавить плагин» остаётся в
      `examples/plugin_template/README.md`.
- [x] H1. Обзор `task_runner*`, `egl_horizon.cpp` на предмет «горячих» мест
      (горячие точки найдены: петля 250 Гц, media_kit-путь с `glReadPixels`
      каждые 120 кадров; `TaskRunner`/EGL — не критично — `docs/perf-notes.md`)
- [x] H2. Список подозрительных `LOG_INFO` в частых путях
      (таблица «место × частота × оценка»; 59× `Heap-Scan` на уровне `ERROR`;
      `fflush` на строку — `docs/perf-notes.md`)
- [ ] H3. Минимальные замеры (железо) — зафиксировать наблюдения
      (2026-10-11: FPS 55–60, worst gap 343–403 мс, DNS падал.
      2026-10-15, «сеть есть», DEBUG: FPS 23.7–60.1 (≈50), worst gap до 479 мс,
      DNS-гипотеза не воспроизвелась, видео не запускалось —
      `docs/perf-notes.md` §H3. Остаётся: прогон с реальным видео и «сеть нет»)
- [x] I1. Снизить шум логов (уровни) там, где безопасно
      (`min_level=kInfo` по умолчанию; периодика и Heap-Scan-регионы → `DEBUG`;
      хелпер `LogEnabled` + медиа-`glReadPixels` только при отладке —
      `docs/perf-notes.md` §I1)
- [x] I2. Проверка регрессий (сборка + базовый прогон)
      (сборка `ui_app` — 0 undefined / 0 дублей; прогон на железе — чистый
      старт/работа/выход, 0 `ERROR`, `DEBUG`-шум отсутствует —
      `docs/perf-notes.md` §I1)
- [ ] I3 (отклонено). Остаточный `WARN`-шум на старте (5× сводка `Heap-Scan`,
      4× `MissingPluginException` по ядровым каналам) признан приемлемым —
      решение: оставляем как есть (`docs/perf-notes.md` §I1)

## 5. Риски
- Рефакторинг диспетчера может сломать редкие каналы → покрыть сравнением логов
- Подстановка иконки зависит от libnx toolchain (`elf2nro`?) — надо выяснить
- Генерация регистранта — аккуратно, не сломать текущий `file_picker`-путь
- shellcheck-правки без фанатизма (macOS-специфика)

## 6. Критерии успеха
- [x] Реестр каналов работает, все текущие каналы проходят
      (ветка `feat/channel-registry`, сборка зелёная)
- [x] EventChannel-хелпер единый, `geolocator` переведён
      (ветка `feat/eventchannel-helper`)
- [x] `.nro` `my_nexus` имеет осмысленную иконку в hbmenu
      (ветка `feat/nro-icon`; иконка в NRO проверена; визуально — на железе)
- [x] `build-dart-app.sh` детектит нужные плагины, регистрант корректен
      (Спринт 2, блок D; ветка `feat/registrant-plugins`)
- [x] Есть `docs/infra.md` + шаблон плагина
      (шаблон `plugin_template/` — Спринт 2 E1; `docs/infra.md` — Спринт 3 G1)
- [x] Критичные скрипты чище, меньше ручных проверок
      (shellcheck по build-dart-app/rebuild-all/relink-example — чисто)
- [ ] Зафиксированы наблюдения по производительности (без «оптимизаций ради оптимизаций»)
      (Спринт 3, блок H)

## Дополнение: иконка NRO и ввод контроллера (добавлено 2026-10-11)

См. `docs/input-icon-plan.md` для деталей.

### C2b. Подстановка иконки .nro
- Найти вызов `elf2nro` (`scripts/rebuild-all.sh`, `Makefile`, примеры)
- Определить формат (256x256 jpg/png)
- Добавить `examples/my_nexus/icon.png`
- Пробросить `--icon` при генерации `.nro`
- Сделать автоматическим (брать из примера если есть)

### C3. Управление кнопками/стиками
- Разбор `switch_platform.cpp` (pad init, cursor, deadzone)
- Улучшить маппинг DPAD/A/B, фокус
- Настроить deadzone/sensitivity виртуального курсора
- Снизить шум логов ввода
- Проверить ложные нажатия при старте из hbmenu

Критерии в `docs/input-icon-plan.md`.

## Дополнение: регистрант и Dart-шимы (Спринт 2 D1/D2, добавлено 2026-10-11)

Ветка `feat/registrant-plugins`. Контракт — в `docs/macos-migration.md`
(раздел «macOS-обновление контрактов»), шимы — в `docs/plugin-plan-my-nexus.md`
(раздел 5, п. 3).

### D1. Детект плагинов в `build-dart-app.sh`
- Детект по `package_config.json` (grep `"name": "<pkg>"`), без ложных
  срабатываний на транзитивных (`image_picker_platform_interface` и т.п.
  не матчатся — закрывающая кавычка сразу после имени).
- Пакеты: `file_picker` (legacy, `FilePickerIO.registerWith()`),
  `image_picker`, `image_cropper`, `webview_flutter` (Dart-шимы).
- `detected>=1` → `generated/horizon_main.dart` генерируется всегда (даже
  только с комментариями) — разработчик видит в файле, что ещё не готово.

### D2. Включение Dart-шимов
- Шим: `dart_helpers/horizon_plugins/shim_<pkg>.dart`; у каждого — функция
  `void registerHorizonShim()`, которая сама делает `XPlatform.instance = ...`.
- Включается ТОЛЬКО файл с маркером `// HORIZON_SHIM: READY` → копия в
  `generated/`, в `main()` эмитятся `import 'shim_<pkg>.dart' as shim_<pkg>;`
  и `shim_<pkg>.registerHorizonShim();` (префиксный импорт — чтобы несколько
  шимов не конфликтовали именами функций).
- Каркасы трёх шимов лежат БЕЗ маркера (не ломают рантайм my_nexus);
  image_picker на текущих версиях работает через C++-канал
  `plugins.flutter.io/image_picker` и шим не требует вовсе.

## Дополнение: шаблон плагина (Спринт 2 E1, добавлено 2026-10-11)

Ветка `feat/plugin-template`. Собираемый пример-цель
`examples/plugin_template/` (минимальный вариант `my_nexus`):

- `Makefile` — копия `ui_app`, только `TARGET`/`APP_TITLE`;
- `source/main.cpp` — симлинк на общий раннер `examples/ui_app/source/main.cpp`;
- `source/handler_stub.cpp` — компилируемый C++-референс хендлера
  (`plugins.flutter.io/plugin_template`, StandardMethodCodec); инертен:
  в `.elf` не линкуется, пока не подключён через реестр;
- `dart/` — минимальный пакет: демо-кнопка вызывает канал и честно
  показывает `MissingPluginException` (ожидаемое поведение до подключения);
- `README.md` — чеклист подключения плагина: путь A (C++-хендлер +
  одна строка в `MakeRegistry()` у `plugins_horizon.cpp`) и путь B
  (Dart-шим в `dart_helpers/horizon_plugins/` с маркером из D).

Сборка зелёная: `build-dart-app.sh --example plugin_template` +
`rebuild-all.sh plugin_template` → `plugin_template.nro`.

## Дополнение: общий модуль скриптов (Спринт 2 F1–F3, добавлено 2026-10-11)

Ветка `feat/scripts-polish`. Новый `scripts/common.sh` — модуль-хелпер для
всех публичных скриптов build-цепочки (см. контракт в `docs/macos-migration.md`):

- `REPO` — корень репозитория (из `BASH_SOURCE[0]`, env-override уважается),
  `SCRIPTS` — `$REPO/scripts`;
- `die MSG` — сообщение в stderr + `exit 1`;
- `require_cmd NAME [HINWEIS]` — единая проверка `command -v` (F1);
- сам модуль `set`-флаги НЕ ставит — каждый скрипт декларирует
  `set -euo pipefail` и затем делает `source common.sh`.

F1. Единые `require_cmd`: make/pkg-config (`build-example-wsl`), python3
    (`log-listener`), curl+git (`fetch-reference`, `install-devkitpro`),
    brew (`setup-macos`), sudo (`install-portlibs`), git
    (`setup-engine-checkout`); проверка тулчейна `[ -x "$CC" ] || die …`
    в `build-sqlite3`.
F2. `set -euo pipefail` единообразно. Найденные ловушки учтены:
    `relink-example.sh` — `BUILD_RC` через if/else, grep-пайплайны анализа
    прикрыты `|| true` (иначе успешный линк «0 undefined» убивал скрипт);
    `gn-gen-horizon.sh` сознательно остаётся на `-uo` (ожидаемый первый
    провал `gn gen` и чтение `PIPESTATUS`), как и диагностические
    `probe-*/find-*/show-*` (SIGPIPE у `find | head`).
F3. `shellcheck -S warning` чист по всем изменённым скриптам (единственный
    жёлтый SC2034 в `common.sh` — документированный API-экспорт `SCRIPTS`;
    pre-existing `FLUTTER_LIBNX_QUIET` в `setup-macos.sh` не трогали:
    переменную читает `env.sh` в том же процессе).

Мигрированы 17 скриптов: `build-dart-app`, `rebuild-all`,
`build-example-wsl`, `relink-example`, `build-horizon`, `gn-gen-horizon`,
`build-sqlite3`, `build-ui-app`, `build-aot-poc`, `dkp`, `fetch-reference`,
`nxlink-upload`, `log-listener`, `setup-macos`, `setup-engine-checkout`,
`install-devkitpro-macos`, `install-portlibs-macos`. Публичные CLI-контракты
не менялись; локальные `root=`/`REPO=$(cd …)` → `root="$REPO"`.
Диагностические скрипты (`probe-*`, `find-*`, `show-*`) — вне скоупа F.
