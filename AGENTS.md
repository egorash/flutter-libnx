# AGENTS.md

Руководство для ИИ-агентов (и людей), работающих в этом репозитории.
Читать целиком перед любыми изменениями.

---

## 1. О проекте

**flutter-libnx** — порт **Flutter Engine** на **Nintendo Switch (Horizon OS)**
через homebrew-тулчейн [devkitPro](https://devkitpro.org/)/libnx. Цель — взять
*немодифицированное* Flutter-приложение и получить один `.nro`, который
запускается на консоли.

- Upstream: `https://github.com/miri2577/flutter-libnx` (ветка `master`)
- Форк (наш): `https://github.com/egorash/flutter-libnx`
- Рабочая ветка macOS-порта: `macos-build-chain`

**Задача этого форка:** сделать build-цепочку полностью нативной на macOS
(Apple Silicon `arm64`, вторично `x86_64`), переписав PowerShell-скрипты на
bash/zsh. Оригинальная цепочка рассчитана на **Windows + WSL2 + PowerShell**.

Движок жёстко пиннится к **Flutter 3.41.6**.

---

## 2. Ключевые факты, которые нельзя упускать

1. **Engine checkout — тяжёлый.** `gclient sync` качает ~26 GB (по README до
   ~40 GB диска). Это разовая операция.
2. **`dlopen` на Horizon нет.** FFI-библиотеки (напр. sqlite3) статически
   линкуются и резолвятся через embedder-hook со сгенерированными таблицами
   символов.
3. **Оригинальные `.ps1` не удаляем** — переименовываем в `.ps1.bak`.
4. **Новые скрипты** — только bash: `set -euo pipefail`, shebang
   `#!/usr/bin/env bash`, **идемпотентность**.
5. **devkitPro на macOS** ставится **не через brew** (tap
   `devkitpro/devkitpro` не существует). Официальный путь — `.pkg`-установщик
   `devkitpro-pacman-installer.pkg`, затем `dkp-pacman -S switch-dev`.
   Итог — `/opt/devkitpro`.
6. **Перед реализацией каждого этапа плана** агент обязан задать пользователю
   уточняющие вопросы и дождаться ответа. Никаких «молчаливых» широких
   изменений.

---

## 3. Структура репозитория

```
embedder/            C++ embedder: include/ и src/ (engine/, platform/)
patches/flutter-engine/  Патчи к исходникам Flutter Engine (horizon)
scripts/             build-скрипты (.sh, .ps1, .py) — ~90 файлов
examples/            примеры: ui_app, aot_poc, engine_link_test, hello_libnx, thread_probe
third_party/         sqlite3 (amalgamation), mpv symbols
dart_helpers/        вспомогательные Dart-файлы
docs/                документация (status.md, porting-notes.md, ...)
```

---

## 4. Целевая build-цепочка (macOS)

```
1. Engine checkout              gclient sync (~26 GB, один раз)
2. Патчи движка                 python3 scripts/patch-engine-horizon.py   # идемпотентно
3. GN-конфиг                    scripts/gn-gen-horizon.sh                 # target_os=horizon, arm64
4. Сборка движка                scripts/build-horizon.sh ...:flutter_engine_static
5. sqlite3 + таблица символов   scripts/build-sqlite3.sh
6. Ассеты + kernel + регистрант scripts/build-dart-app.sh --project <app>  # порт из .ps1
7. AOT-снапшот → .nro           scripts/rebuild-all.sh ui_app
8. Заливка на консоль           scripts/nxlink-upload.sh --switch-ip <ip> --example ui_app
```

Шаги 1–5 — разовые. Итерации по приложению — шаги 6–7.
`scripts/log-listener.sh` принимает TCP-логи, консоль сама подключается к ПК.

---

## 5. Окружение macOS

- **devkitPro:** `/opt/devkitpro` (devkitA64), группа `switch-dev`.
- **Flutter SDK:** отдельная копия `~/flutter-3.41.6` (не системный brew-flutter).
- **depot_tools:** `~/depot_tools`.
- **Engine checkout:** `~/engine/flutter/engine/src` (ожидание
  `build-horizon.sh`/`gn-gen-horizon.sh`; переопределяется через `SRC`).
- **Переменные окружения** (добавляются в `~/.zshrc`):
  ```sh
  export DEVKITPRO=/opt/devkitpro
  export DEVKITA64=$DEVKITPRO/devkitA64
  export PATH="$HOME/depot_tools:$HOME/flutter-3.41.6/bin:$PATH"
  ```
- Установщик: `scripts/setup-macos.sh` (идемпотентный).

Проверка после установки:
```
aarch64-none-elf-gcc --version
gn --version
ninja --version
flutter --version
python3 --version
```

---

## 6. Git и несколько аккаунтов (важно!)

На машине настроены **корпоративные GitLab** (`*.krista.ru`) в глобальном
`~/.gitconfig` (с кастомными сертификатами). **Не менять глобальный конфиг.**

- GitHub по **SSH** (`~/.ssh/id_rsa`) аутентифицируется как **egorash**.
- GitHub по **HTTPS** (keychain, пользователь `24220268` = `egorash`) — токен
  со scopes `repo`, `workflow`, `read:user`, `user:email`.
- **remotes этого репозитория делаем по SSH:**
  ```
  origin   git@github.com:egorash/flutter-libnx.git
  upstream git@github.com:miri2577/flutter-libnx.git
  ```
- `user.name` / `user.email` задаём **локально** (`git config --local`), чтобы
  не путать с корпоративной идентичностью.
- Секреты и токены в коммиты не попадают. Engine checkout и артефакты сборки —
  в `.gitignore`.

---

## 7. Соглашения

- **Коммиты (Conventional Commits):** `feat(macos):`, `fix(macos):`,
  `docs(macos):`, `chore(macos):`, `refactor(macos):`.
- **Язык документации:** русский (AGENTS.md, планы) и существующая
  англо/немецкая документация upstream не переводится без надобности.
- **Скрипты:** shellcheck без `severity=error`, никаких Linux-специфичных путей
  (`/mnt/...`, `/usr/bin/python3`), различать синтаксис `nc` и `sed` между
  macOS и Linux.
- Любое изменение контракта скрипта (флаги/аргументы) отражать в
  `docs/macos-migration.md` и README.

---

## 8. Документы

| Файл | Назначение |
|---|---|
| `docs/0_init.md` | Исходный план (исторический, содержит неточности) |
| `docs/macos-plan.md` | Актуальный поэтапный план работ |
| `docs/macos-migration.md` | Аудит всех скриптов (создаётся на этапе 1) |
| `docs/status.md`, `docs/porting-notes.md` | Инженерный журнал upstream |

---

## 9. Текущий статус macOS-порта

Прогресс ведётся в `docs/macos-plan.md` (чеклисты этапов). На момент создания
этого файла выполнено: только постановка задачи и планирование.
