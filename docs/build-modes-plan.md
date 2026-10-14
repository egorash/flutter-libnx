# План: режим `debug` и отладка на Switch (VM-сервис, логи, hot reload)

Отдельный спринт (условно «Спринт 4»). Не входит в `docs/infra-plan.md`,
хотя тематически близок (инфраструктура эмбеддера).

Статус: планирование. Язык — русский, как новые материалы форка.

Решения по объёму (2026-10-15):
- режим — **только `debug`** (profile/release-варианты не делаем, release
  остаётся дефолтом и не меняется);
- **Dart-VM не трогаем** (никаких патчей `runtime/vm/*`); приложение по
  умолчанию тоже не правим;
- логи приложения нужны **в файле** `sdmc:/switch/flutter-libnx/<app>.log` — и
  **только в debug** (по файлу идёт анализ);
- **hot restart не нужен**; hot reload — по возможности.

---

## 1. Зачем

Сейчас цепочка всегда собирает **release/AOT**, режим нигде не задаётся:

- приложение не отличает отладку от прода;
- `kDebugMode == false` → отладочное логирование приложения не выполняется;
- **Dart-логи не видны вообще** (и это отдельная причина, не только режим, §3);
- нет инспектора/DevTools/профилировщика/hot reload.

Цель спринта: уметь собирать и запускать приложение в **debug** и завести
отладочный канал (VM-сервис), чтобы видеть логи и, если технически выйдет,
иметь hot reload.

## 2. Что есть сейчас (факты)

- `scripts/build-dart-app.sh:146` — `flutter build bundle` **без флага**
  (дефолт Flutter — release);
- `scripts/build-dart-app.sh:151-153` — JIT-артефакты
  (`kernel_blob.bin`, `vm_snapshot_data`, `isolate_snapshot_data`) **намеренно
  вырезаются** из assets;
- `scripts/build-dart-app.sh:255-262` — ядро через `gen_kernel_aot --aot`,
  `flutter_patched_sdk_product`, `--product` → `-Ddart.vm.product=true`;
- `scripts/gn-gen-horizon.sh:44` — движок `is_debug=false` (release, AOT-VM);
- `scripts/rebuild-all.sh:43` — `gen_snapshot_product`;
- `examples/ui_app/source/main.cpp:873-876` — в `DartProject` кладутся только
  AOT-снапшоты; JIT/kernel-ветки «вручную» нет (но она и не нужна, см. §4);
- в чек-ауте движка есть только `out/horizon_release_arm64`.

Итог: сборка только release/AOT.

## 3. Почему одних режимов мало (важно)

`kDebugMode` — compile-time флаг (`dart.vm.product`/`dart.vm.profile`), он
решает лишь, **вызывается ли** код логирования.

GetX-логгер по умолчанию пишет в `developer.log`, а тот в Dart-VM уходит в
сервисный стрим `Logging`:

```cpp
// runtime/vm/service.cc, Service::SendLogEvent
if (!Service::logging_stream.enabled()) { return; }   // лог выброшен
```

`logging_stream.enabled()` истинно только когда к VM-сервису подключён клиент
(на десктопе — `flutter run`/DevTools). На Horizon клиента нет → **`developer.log`
теряется в любом режиме**.

Сейчас до SD-лога доходит только Dart `print()` (движок заворачивает его в
`[engine:flutter]`): так устроен `NetProbe` (`lib/app/shared/utils/net_probe.dart`)
и `docs/horizon-tls-roots.md` §5.

**Вывод:** режим нужен, чтобы код логирования выполнялся, а логи становятся
видны при **подключённом клиенте VM-сервиса**. Именно поэтому цель спринта — не
«включить режим», а «поднять VM-сервис» (§4, M4). VM при этом не патчим.

## 4. Блоки работ

### M0. Оценка выполнимости (сделано вчерне)

Хорошая новость: **C-API эмбеддера уже всё умеет**, менять C++ эмбеддера, скорее
всего, не придётся.

- Движок берёт свитчи из `FlutterProjectArgs.command_line_argc/argv`
  (`embedder.cc:2056-2064`) → ими включается VM-сервис.
- `enable_vm_service` включается **по умолчанию** (`switches.cc:240`,
  `!DisableVMService`); хост/порт — `--vm-service-host` / `--vm-service-port`
  (`switches.cc:247-267`), есть `--disable-service-auth-codes`.
- При **не-precompiled** VM движок сам уходит в JIT и ждёт `kernel_blob.bin` в
  assets (`embedder.cc:2078-2100`) — то есть JIT-путь в API есть.

Оценка:
- **Сборка+установка (как сейчас):** есть, деградации не будет.
- **VM-сервис (логи, DevTools, брейкпоинты):** технически выполнимо. Нужен
  debug-движок, JIT-ассеты и свитчи; сервис слушает `0.0.0.0:<port>` и доступен
  с Mac по Wi-Fi. Логи `developer.log` при этом видны **в подключённом клиенте**
  (DevTools/`flutter attach`) — VM для этого не трогаем.
- **hot reload:** стоит на VM-сервисе (`reloadSources`), поэтому в принципе
  возможен. Но требуется, чтобы `flutter attach` нашёл сервис (mDNS-объявление
  на Horizon, вероятно, не работает → URL задавать вручную). Это **наименее
  гарантированная** часть; проверяется эмпирически.

### M1. Режим в сборке приложения (скрипты)
- [ ] `build-dart-app.sh --mode release|debug` (дефолт `release` — текущее
      поведение не меняется).
- [ ] `flutter build bundle --debug` для debug.
- [ ] Ядро: release — как сейчас (`gen_kernel_aot`, product SDK); debug —
      JIT-ядро (без `--aot`), non-product SDK
      (`flutter_patched_sdk/platform_strong.dill`).
- [ ] Assets: для debug **не вырезать** JIT-артефакты; для release — как сейчас.

### M2. Движок: debug-сборка
- [ ] `gn-gen-horizon.sh`: режим → `is_debug=true`, out-каталог
      `out/horizon_debug_arm64`.
- [ ] Собрать debug-движок (JIT-VM) и debug host-`gen_snapshot`.
- [ ] `rebuild-all.sh`: для debug — согласованный `gen_snapshot` (не
      `_product`) и, при необходимости, `gen_snapshot`-вариант для JIT.
      Разобраться, откуда берутся JIT `vm_snapshot_data`/`isolate_snapshot_data`
      (bundle vs символы) и как их подать в `FlutterProjectArgs`.

### M3. Эмбеддер: JIT и свитчи
- [ ] Убедиться, что при не-precompiled VM нет нужды менять `main.cpp`
      (движок сам читает kernel из assets). Если символы AOT мешают — добавить
      ветку выбора снапшотов (минимально).
- [ ] Прокинуть `command_line_argc/argv` (сейчас не задаются) со свитчами:
      `--enable-vm-service`, `--vm-service-host=0.0.0.0`,
      `--vm-service-port=<n>`, `--disable-service-auth-codes` (только debug).
- [ ] Makefile примеров: AOT- и JIT-варианты линковки не должны ломать release.

### M4. Отладочный канал: VM-сервис и логи в файл (debug)
Требование: в debug логи приложения должны попадать в файл
`sdmc:/switch/flutter-libnx/<app>.log` — по нему идёт анализ.

- [ ] В debug поднять VM-сервис, доступный с Mac (`http://<switch-ip>:<port>`),
      с учётом 16 BSD-сессий.
- [ ] `flutter attach` (URL задаётся вручную) / DevTools — живые логи и
      брейкпоинты (удобно для интерактива).
- [ ] **Файл:** зеркалировать сервисный стрим `Logging` в SD-лог. Реализация —
      минимальный клиент сервис-протокола внутри эмбеддера: подключиться к
      своему VM-сервису на loopback, `streamListen(Logging)`, писать события в
      тот же лог-синк. Правок VM и приложения не требует.
      Альтернатива (если клиент окажется дорогим): debug-gated `print()` в
      логгере приложения — но это правка приложения, по умолчанию не берём.
- [ ] Убедиться, что вывод не раздувает файл (уровни/дедуп; ротация уже есть).

### M5. hot reload (если M4 получится)
- [ ] Проверить `flutter attach` → `r` (reloadSources) на железе.
- [ ] Если mDNS не работает — задавать VM-service URL вручную.
- [ ] Зафиксировать результат: работает / не работает и почему.

### M6. Документация и регрессии
- [ ] `docs/build-modes.md`: как собрать/запустить debug, как подключиться,
      что видно, ограничения.
- [ ] Регресс release: `ui_app` и `my_nexus` собираются и работают как раньше.
- [ ] Обновить `docs/macos-migration.md` / README (контракт `build-dart-app.sh`).

## 5. Не входит в спринт
- Режим `profile`.
- Патчи Dart-VM (`runtime/vm/*`).
- Правки исходников целевых приложений (my_nexus и др.).
- Разбор конкретной ошибки загрузки API (отдельная задача; после появления
  VM-сервиса делается быстро).

## 6. Риски
- Debug-движок заметно тяжелее и дольше в сборке; расход диска.
- JIT-запуск на Horizon: память/время старта; неизвестные ограничения.
- VM-сервис слушает TCP внутри процесса — упирается в 16 BSD-сессий.
- mDNS-объявление сервиса на Horizon, скорее всего, не работает →
  `flutter attach` только по явному URL.
- Линковка снапшотов (AOT vs JIT) в Makefile — легко сломать release.
- hot reload может упереться в специфику Horizon (файлы/ассеты) — не гарантируем.

## 7. Критерии успеха
- [ ] `--mode` поддержан; дефолт `release` не меняет текущих артефактов.
- [ ] `debug`-сборка запускается на железе.
- [ ] VM-сервис виден с Mac.
- [ ] В debug Dart-логи приложения (GetX) попадают **в файл** `sdmc:/...` и
      видны в клиенте (DevTools / `flutter attach`).
- [ ] Зафиксирован статус hot reload (да/нет + причина).
- [ ] Release-путь не деградировал (регресс пройден).
- [ ] Есть `docs/build-modes.md` и обновлён контракт скриптов.

## 8. Решённые вопросы (2026-10-15)
- Логи приложения нужны **в файле на SD**, только в debug (M4).
- Hot restart **не нужен**; hot reload — по возможности (M5).
- Dart-VM **не патчим**; приложение по умолчанию **не правим** (если зеркало
  `Logging` в эмбеддере окажется слишком дорогим — вернуться к этому решению).
