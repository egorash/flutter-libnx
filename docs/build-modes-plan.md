# План: режимы сборки (release/profile/debug) и вывод Dart-логов

Отдельный спринт (условно «Спринт 4»). Не входит в `docs/infra-plan.md`,
хотя тематически близок (инфраструктура эмбеддера).

Статус: планирование. Язык — русский, как новые материалы форка.

---

## 1. Зачем

Сейчас цепочка всегда собирает **release/AOT**, режим нигде не задаётся. Из-за
этого:

- приложение не отличает отладку от прода (нет `debug`/`profile`);
- `kDebugMode == false`, поэтому отладочное логирование приложения не
  выполняется;
- **Dart-логи не видны на консоли вообще** — и это отдельная причина, не только
  режим (см. §3);
- нет способа запустить инспектор/DevTools или профилировщик.

Цель спринта: сделать режим сборки **явным и управляемым**
(`release`/`profile`/`debug`) и получить рабочий канал вывода Dart-логов, чтобы
диагностировать приложение прямо на железe.

## 2. Что есть сейчас (факты)

- `scripts/build-dart-app.sh:146` — `flutter build bundle` **без флага**
  (дефолт Flutter — release);
- `scripts/build-dart-app.sh:151-153` — JIT-артефакты
  (`kernel_blob.bin`, `vm_snapshot_data`, `isolate_snapshot_data`) **намеренно
  вырезаются** из assets;
- `scripts/build-dart-app.sh:255-262` — ядро через `gen_kernel_aot --aot`,
  платформа `flutter_patched_sdk_product`, `--product` → `-Ddart.vm.product=true`;
- `scripts/gn-gen-horizon.sh:44` — движок `is_debug=false` (release, AOT-VM);
- `scripts/rebuild-all.sh:43` — `gen_snapshot_product`;
- `examples/ui_app/source/main.cpp:873-876` — в `DartProject` кладутся только
  AOT-снапшоты (`vm_snapshot_*`, `isolate_snapshot_*`), JIT/kernel-ветки нет;
- в чек-ауте движка есть только `out/horizon_release_arm64`.

Итог: **сборка только release/AOT**. Комбинации «режим × вид снапшота» не
разведены.

## 3. Почему одних режимов мало (важно)

`kDebugMode` — это compile-time флаг (`dart.vm.product` / `dart.vm.profile`) и
решает лишь, **вызывается ли** код логирования.

Дальше работает GetX-логгер по умолчанию — `developer.log`
(`get_core/src/log.dart`), а он в Dart-VM попадает в сервисный стрим `Logging`:

```cpp
// runtime/vm/service.cc, Service::SendLogEvent
if (!Service::logging_stream.enabled()) { return; }   // лог выброшен
```

`logging_stream.enabled()` истинно только когда к VM-сервису подключён клиент
(на десктопе это делает `flutter run`/DevTools). На Horizon клиента нет →
**`developer.log` теряется в любом режиме**.

Единственный канал, который доходит до SD-лога сейчас, — Dart `print()`
(движок заворачивает его в `[engine:flutter]`): именно так работает `NetProbe`
(`lib/app/shared/utils/net_probe.dart`) и `docs/horizon-tls-roots.md` §5.

Вывод спринта: нужны **оба** слагаемых — (1) режим, чтобы код логирования
выполнялся, и (2) **сток** для `developer.log`.

## 4. Блоки работ

### M1. Режим в сборке приложения (скрипты)
- [ ] `build-dart-app.sh --mode release|profile|debug` (дефолт `release` —
      текущее поведение не меняется).
- [ ] `flutter build bundle --<mode>`.
- [ ] Ядро по режиму: release/profile — `gen_kernel_aot` (+ `-Ddart.vm.product`
      / `-Ddart.vm.profile`); debug — JIT-кернел (не `--aot`), non-product SDK
      (`flutter_patched_sdk/platform_strong.dill`). **Уточнить** при реализации,
      какой patched SDK корректен для `profile`.
- [ ] Assets: для `debug` **не вырезать** JIT-артефакты; для release/profile —
      как сейчас. Управляет тот же `--mode`.

### M2. Движок: debug/profile-сборка
- [ ] `gn-gen-horizon.sh`: параметр режима → `is_debug`/`is_profile_build` и
      отдельные out-каталоги (`out/horizon_debug_arm64`,
      `out/horizon_profile_arm64`).
- [ ] Собрать debug-движок (JIT-VM).
- [ ] `rebuild-all.sh`: выбор `gen_snapshot`/`gen_snapshot_profile`/
      `gen_snapshot_product` по режиму согласованно с M1.

### M3. Эмбеддер: загрузка JIT и режимы
- [ ] `main.cpp`: ветка — если в assets есть `kernel_blob.bin`, конфигурировать
      `DartProject` под JIT (снапшот-данные из bundle, вход через kernel);
      иначе — текущий AOT-путь.
- [ ] Makefile примеров: согласовать, какие символы снапшота линкуются для
      AOT и что делать в JIT-режиме.
- [ ] Проверить старт JIT на Horizon (память, время инициализации).

### M4. Сток для Dart-логов (`developer.log` → SD-лог)
Варианты (выбрать при реализации, возможно оба):
- [ ] В debug: поднять VM-сервис в эмбеддере и подписаться на стрим `Logging`,
      форвардить в наш `log`-синк.
- [ ] Фолбэк в движке: если слушателя нет, печатать событие `Logging` в stderr
      (тогда логи видны и в release — но это меняет поведение Dart-VM; только по
      явному решению).
- [ ] Убедиться, что вывод не раздувает лог (уровни, дедуп).

### M5. Инструменты (опционально)
- [ ] Доступ к VM-сервису по сети (консоль в Wi-Fi) и подключение DevTools с
      Mac; инспектор, timeline.
- [ ] Горячая перезагрузка (hot reload) — оценить реалистичность на текущем
      эмбеддере.

### M6. Документация и регрессии
- [ ] `docs/build-modes.md`: как собирать в каждом режиме, что видно, ограничения.
- [ ] Регресс release: `ui_app` и `my_nexus` собираются и работают как раньше
      (режим по умолчанию — не меняется).
- [ ] Обновить `docs/macos-migration.md` / README (контракт `build-dart-app.sh`).

## 5. Не входит в спринт
- Правки исходников целевых приложений (my_nexus и др.).
- Разбор конкретной ошибки загрузки API (отдельная задача; после появления
  видимых логов — тривиально).

## 6. Риски
- Debug-движок заметно тяжелее и дольше в сборке; расход диска.
- JIT-запуск на Horizon: память/время старта; неизвестные ограничения JIT-VM.
- VM-сервис на Horizon требует слушающего TCP внутри процесса — 16 BSD-сессий.
- Линковка снапшотов в Makefile (AOT vs JIT) — легко сломать release.
- `developer.log`-фолбэк в VM — глобальное изменение поведения, осторожно.

## 7. Критерии успеха
- [ ] `--mode` поддержан; дефолт `release` не меняет текущих артефактов.
- [ ] `debug`-сборка запускается на железе.
- [ ] Dart-логи приложения (в т.ч. GetX) видны в
      `sdmc:/switch/flutter-libnx/<app>.log`.
- [ ] Release-путь не деградировал (регресс пройден).
- [ ] Есть `docs/build-modes.md` и обновлён контракт скриптов.

## 8. Открытые вопросы
1. Объём: только `debug` или ещё `profile`?
2. Нужен ли инспектор/DevTools/hot reload (M5) или достаточно логов?
3. Сток `developer.log`: подписка на `Logging` в debug-эмбеддере vs фолбэк в
   VM (в т.ч. для release)?
