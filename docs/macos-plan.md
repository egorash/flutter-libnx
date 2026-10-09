# Поэтапный план: macOS build-цепочка flutter-libnx

**Цель:** полностью нативная сборка Flutter-приложения в `.nro` для Nintendo
Switch — на macOS (Apple Silicon arm64), без Windows и WSL2.

**Область:** полный цикл до готового `.nro`.

**Принципы:**
- План поэтапный и с явными критериями приёмки.
- **Перед реализацией каждого этапа** агент задаёт вопросы и ждёт ответа.
- Каждый этап завершается проверяемым результатом и коммитом
  (`feat(macos):` / `fix(macos):` / `docs(macos):`).
- Оригинальные `.ps1` не удаляются (`.ps1.bak`), новые скрипты идемпотентны.

**Решения, зафиксированные с заказчиком:**
- Форк — в аккаунт **egorash**; remote'ы по SSH.
- AGENTS.md и планы — на русском.
- devkitPro — официальный `.pkg` (`dkp-pacman`), `/opt/devkitpro`.
- Flutter SDK — отдельная копия **3.41.6** в `~/flutter-3.41.6`.
- Рабочая ветка — `macos-build-chain`; `docs/0_init.md` сохраняем.
- Switch есть, тесты на железе — **позже** (этап 9 откладывается).

---

## Этап 0. Форк и bootstrap репозитория

**Шаги**
1. Форк `miri2577/flutter-libnx` → `egorash/flutter-libnx` (через API с уже
   сохранённым токеном egorash либо `gh`).
2. `git init` в `/Users/egorshepelev/Documents/dev/switch-flutter`.
3. remote `origin` (SSH, форк) и `upstream` (SSH, upstream).
4. `git fetch upstream && git checkout -b macos-build-chain upstream/master`
   (существующие `docs/0_init.md`, `AGENTS.md`, `docs/macos-plan.md`
   сохраняются).
5. Локальные `user.name` / `user.email` (не трогая глобальный конфиг).
6. `.gitignore`: исключить macOS-мусор, engine checkout, артефакты.

**Критерий приёмки:** `git remote -v` показывает origin=egorash, upstream=miri2577;
ветка `macos-build-chain` создана; рабочее дерево содержит upstream-файлы и наши
документы; первый коммит сделан.

**Зависимости:** SSH-доступ к GitHub (есть), токен egorash (есть).

---

## Этап 1. Аудит скриптов

**Шаги**
1. Прочитать все `scripts/*.sh` (~90), `scripts/*.ps1` (8), `scripts/*.py`.
2. Для каждого: назначение, внешние зависимости (утилиты, переменные
   окружения), WSL/Linux-специфика, macOS-совместимость, требуемый рефакторинг.
3. Свести в таблицу `docs/macos-migration.md`.

**Критерий приёмки:** `docs/macos-migration.md` содержит полную таблицу и
список приоритетных правок.

**Зависимости:** Этап 0.

---

## Этап 2. Установка окружения на macOS (`scripts/setup-macos.sh`)

**Шаги**
1. Homebrew-зависимости: `cmake ninja python3 git pkg-config bison shellcheck`
   (проверить, что не конфликтуют с pyenv-версиями).
2. devkitPro: скачать `devkitpro-pacman-installer.pkg` из
   `github.com/devkitPro/pacman/releases`, `sudo installer -pkg ... -target /`,
   затем `sudo dkp-pacman -Syu` и `sudo dkp-pacman -S --needed switch-dev`.
3. depot_tools: клон в `~/depot_tools` (если нет).
4. Flutter 3.41.6: клон `-b 3.41.6` в `~/flutter-3.41.6` (если нет).
5. Дописать в `~/.zshrc` блок env (идемпотентно, с маркерами).
6. Вывести чеклист версий: `aarch64-none-elf-gcc`, `gn`, `ninja`, `flutter`,
   `python3`.

**Критерий приёмки:** на чистой macOS скрипт проходит и все проверки версий
успешны; повторный запуск ничего не ломает.

**Риски:** devkitPro `.pkg` требует `sudo`; `bison` из brew может понадобиться
раньше системного в PATH; pyenv-`python3` может мешать `gclient`.

**Зависимости:** Этап 0.

---

## Этап 3. Проверка существующих `.sh` на macOS-совместимость

**Шаги**
1. `shellcheck` по всем скриптам.
2. Заменить Linux-специфику: `readlink -f` → `realpath`/python-фолбэк,
   `sed -i` → `sed -i ''`, `nc -l -p` → ветвление по `$OSTYPE`,
   `/usr/bin/python3` → `$(command -v python3)`, хардкод `/mnt/e/...`.
3. Привести shebang к `#!/usr/bin/env bash`.
4. Исправления — коммитами `fix(macos): ...`.

**Критерий приёмки:** `shellcheck` без `severity=error`; скрипты не падают с
`command not found`.

**Зависимости:** Этап 2 (нужен shellcheck и частично окружение).

---

## Этап 4. Engine checkout

**Шаги**
1. Настроить `gclient` (`.gclient`), `gclient sync` движка Flutter
   (`~/engine/flutter/engine/src`), пин на коммит, соответствующий 3.41.6.
2. `python3 scripts/patch-engine-horizon.py` — применить Horizon-патчи
   (идемпотентно).

**Критерий приёмки:** checkout на нужном коммите, патчи применились без ошибок.

**Риски:** ~26 GB загрузки, длительное время, pyenv/python для `gclient`.

**Зависимости:** Этап 2 (depot_tools, devkitPro, Flutter).

---

## Этап 5. Сборка движка и sqlite3

**Шаги**
1. `scripts/gn-gen-horizon.sh` — GN-конфиг `target_os=horizon`,
   `target_cpu=arm64`, `flutter_runtime_mode=release`.
2. `scripts/build-horizon.sh ...:flutter_engine_static` — сборка статической
   библиотеки движка.
3. `scripts/build-sqlite3.sh` — sqlite3 + таблица символов.

**Критерий приёмки:** получена `flutter_engine_static` и sqlite3-артефакты.

**Зависимости:** Этап 4.

---

## Этап 6. Порт PowerShell → bash

**Шаги**
1. `build-dart-app.ps1` → `build-dart-app.sh` (ключевой: `flutter build bundle`,
   kernel, Horizon-регистрант, копирование ассетов). Оригинал → `.ps1.bak`.
2. `nxlink-upload.ps1` → `nxlink-upload.sh`.
3. `log-listener.ps1` → `log-listener.sh` (netcat, ветвление macOS/Linux,
   trap, цветной вывод).
4. По итогам аудита — остальные `.ps1`
   (`build-aot-poc`, `build-ui-app`, `dkp`, `fetch-reference`,
   `probe-app-toolchain`) и/или их bash-аналоги при необходимости.

**Критерий приёмки:** `build-dart-app.sh --project ./examples/ui_app` даёт те же
артефакты, что PS-версия; nxlink/log-listener работают с понятными ошибками.

**Зависимости:** Этап 3, Этап 5.

---

## Этап 7. Сборка приложения до `.nro`

**Шаги**
1. `scripts/build-dart-app.sh --project ./examples/ui_app`.
2. `scripts/rebuild-all.sh ui_app` → AOT-снапшот → `.nro`.

**Критерий приёмки:** `ui_app.nro` собран; размер в разумных пределах
(ориентир ~50–150 МБ).

**Зависимости:** Этап 6.

---

## Этап 8. Документация

**Шаги**
1. README: секция «macOS Build», таблица поддерживаемых хост-ОС, убрать WSL2 как
   единственный вариант.
2. `docs/macos-migration.md` — финализировать.
3. `CONTRIBUTING.md` (если нет): окружение macOS/Linux/Windows, стиль коммитов,
   тестирование, PR в upstream.

**Критерий приёмки:** документы актуальны и воспроизводимы с нуля.

**Зависимости:** Этапы 2–7.

---

## Этап 9. Smoke-тест и CI

**Шаги**
1. Полный прогон на macOS: setup → patch → gn → build → dart-app → rebuild;
   зафиксировать время этапов, ошибки/решения, размер `.nro`.
2. (Опционально) `.github/workflows/build-macos.yml` (`macos-14`, кэш,
   таймаут 6 ч).
3. Тесты на железе Switch через `nxlink-upload.sh` / `log-listener.sh` —
   **позже**, по готовности.

**Критерий приёмки:** воспроизводимая сборка;
`docs/macos-migration.md`/отчёт со временем и результатом.

**Зависимости:** Этап 8.

---

## Граф зависимостей

```
0 ─┬─> 1
   └─> 2 ─> 3 ─┐
               ├─> 6 ─> 7 ─> 8 ─> 9
       2 ─> 4 ─> 5 ─┘
```

## Прогресс

- [x] Этап 0. Форк и bootstrap
- [ ] Этап 1. Аудит скриптов
- [ ] Этап 2. Установка окружения
- [ ] Этап 3. Проверка `.sh`
- [ ] Этап 4. Engine checkout
- [ ] Этап 5. Сборка движка и sqlite3
- [ ] Этап 6. Порт PS → bash
- [ ] Этап 7. Сборка до `.nro`
- [ ] Этап 8. Документация
- [ ] Этап 9. Smoke-тест и CI
