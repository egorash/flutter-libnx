

# 🛠️ План работ: macOS-порт build-цепочки flutter-libnx

## Контекст для агента

```
Ты работаешь над форком репозитория https://github.com/miri2577/flutter-libnx
Это проект, портирующий Flutter Engine на Nintendo Switch (Horizon OS) через devkitPro/libnx.

Оригинальная build-цепочка рассчитана на Windows + WSL2 и использует PowerShell-скрипты.
Цель: сделать сборку полностью нативной на macOS (Apple Silicon и Intel),
переписав все PowerShell-скрипты на bash/zsh.

ОС: macOS (Sonoma/Sequoia)
Архитектура: arm64 (Apple Silicon) — основная, x86_64 — вторичная
Пакетный менеджер: Homebrew
```

---

## Фаза 0: Подготовка и форк

### Задача 0.1 — Форк и клонирование
```
1. Форкнуть https://github.com/miri2577/flutter-libnx в свой GitHub-аккаунт
2. Клонировать форк локально:
   git clone git@github.com:<YOUR_USERNAME>/flutter-libnx.git
3. Добавить upstream:
   git remote add upstream https://github.com/miri2577/flutter-libnx.git
4. Создать ветку:
   git checkout -b macos-build-chain
```
**Критерий приемки:** Репозиторий клонирован, upstream настроен, рабочая ветка создана.

### Задача 0.2 — Аудит существующих скриптов
```
Проанализировать все скрипты в директории scripts/:

PowerShell (требуют переписывания):
  - scripts/build-dart-app.ps1
  - scripts/nxlink-upload.ps1
  - scripts/log-listener.ps1

Bash (проверить macOS-совместимость):
  - scripts/gn-gen-horizon.sh
  - scripts/build-horizon.sh
  - scripts/build-sqlite3.sh
  - scripts/rebuild-all.sh

Python:
  - scripts/patch-engine-horizon.py

Для каждого скрипта:
  1. Прочитать исходный код
  2. Составить список внешних зависимостей (утилиты, переменные окружения)
  3. Определить WSL-специфичные вызовы (например, wsl bash ...)
  4. Задокументировать в docs/macos-migration.md
```
**Критерий приемки:** Файл `docs/macos-migration.md` содержит таблицу всех скриптов с анализом зависимостей и macOS-совместимости.

---

## Фаза 1: Установка зависимостей на macOS

### Задача 1.1 — Скрипт установки зависимостей
```
Создать scripts/setup-macos.sh, который:

1. Проверяет наличие Homebrew, устанавливает если нет
2. Устанавливает системные зависимости:
   brew install cmake ninja python3 git pkg-config bison
   brew install --cask powershell  # опционально, для fallback

3. Устанавливает devkitPro:
   brew tap devkitpro/devkitpro
   brew install devkitA64 devkitarm general-tools

4. Проверяет/устанавливает depot_tools:
   if [ ! -d "$HOME/depot_tools" ]; then
     git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$HOME/depot_tools"
   fi
   # Добавляет в PATH в ~/.zshrc если не добавлен

5. Проверяет/устанавливает Flutter SDK 3.41.6:
   if [ ! -d "$HOME/flutter-3.41.6" ]; then
     git clone https://github.com/flutter/flutter.git -b 3.41.6 "$HOME/flutter-3.41.6"
   fi

6. Устанавливает переменные окружения (добавляет в ~/.zshrc):
   export DEVKITPRO=/opt/devkitpro
   export DEVKITARM=$DEVKITPRO/devkitARM
   export DEVKITA64=$DEVKITPRO/devkitA64
   export PATH="$HOME/depot_tools:$HOME/flutter-3.41.6/bin:$PATH"

7. В конце выводит чеклист с проверкой версий:
   - aarch64-none-elf-gcc --version
   - gn --version
   - ninja --version
   - flutter --version
   - python3 --version

Скрипт должен быть идемпотентным (повторный запуск не ломает ничего).
```
**Критерий приемки:** `scripts/setup-macos.sh` запускается на чистой macOS и все проверки версий проходят успешно.

### Задача 1.2 — Проверка bash-скриптов на macOS
```
Пройтись по каждому .sh скрипту и:
1. Запустить shellcheck для поиска проблем
2. Заменить Linux-специфичные пути на macOS-эквиваленты:
   - /usr/bin/python3 → $(which python3)
   - hardcoded /mnt/e/... → переменные окружения
   - readlink -f → realpath или python3 -c "import os; ..."
3. Убедиться что shebang = #!/usr/bin/env bash (не /bin/bash)
4. Проверить что нет GNU-специфичных флагов (например, sed -i '' на macOS vs sed -i на Linux)

Исправления коммитить с префиксом: fix(macos): ...
```
**Критерий приемки:** `shellcheck` не выдает ошибок severity=error, скрипты запускаются без "command not found".

---

## Фаза 2: Переписывание PowerShell → Bash

### Задача 2.1 — Переписать `build-dart-app.ps1` → `build-dart-app.sh`

```
Это ключевой скрипт. Он:
  - Принимает путь к Flutter-проекту (-Project)
  - Запускает flutter build bundle для получения ассетов
  - Компилирует Dart-код в AOT-снапшот
  - Генерирует Horizon-регистрант плагинов
  - Копирует ассеты в структуру NRO

Что нужно сделать:
  1. Прочитать оригинальный build-dart-app.ps1
  2. Создать scripts/build-dart-app.sh с эквивалентной логикой
  3. Использовать getopt или позиционные аргументы вместо PowerShell-параметров:
     ./build-dart-app.sh --project /path/to/flutter_app [--product]
  4. Заменить PowerShell-конструкции:
     - Resolve-Path → realpath или cd + pwd
     - Test-Path → [ -d ... ] / [ -f ... ]
     - Copy-Item → cp -r
     - Remove-Item → rm -rf
     - Write-Host → echo / printf
     - $PSScriptRoot → $(dirname "$0")
     - Invoke-Expression → eval или прямой вызов
  5. Добавить set -euo pipefail в начало
  6. Добавить проверку зависимостей (flutter, dart)
  7. Сохранить оригинальный .ps1 как build-dart-app.ps1.bak или удалить
     (предпочтительно удалить и указать в README)
```
**Критерий приемки:** `./scripts/build-dart-app.sh --project ./examples/ui_app` создает те же артефакты, что и PowerShell-версия.

### Задача 2.2 — Переписать `nxlink-upload.ps1` → `nxlink-upload.sh`

```
Этот скрипт загружает .nro файл на Switch через сеть (nxlink).
Он:
  - Принимает IP-адрес Switch (-SwitchIp)
  - Принимает имя примера (-Example)
  - Запускает nxlink для отправки файла

Что нужно сделать:
  1. Создать scripts/nxlink-upload.sh:
     ./scripts/nxlink-upload.sh --switch-ip 192.168.1.100 --example ui_app
  2. nxlink — это утилита из devkitPro, проверить что она доступна:
     which nxlink || echo "nxlink not found, install devkitPro"
  3. Логика тривиальна — по сути обертка над:
     nxlink -a <IP> build/<example>.nro
  4. Добавить таймаут и обработку ошибок подключения
```
**Критерий приемки:** Скрипт находит nxlink, подключается к Switch и загружает .nro (или выдает понятную ошибку если Switch не в сети).

### Задача 2.3 — Переписать `log-listener.ps1` → `log-listener.sh`

```
Этот скрипт принимает TCP-логи с консоли Switch.
Он:
  - Слушает TCP-порт
  - Выводит логи в терминал
  - Консоль сама подключается к ПК (reverse connection)

Что нужно сделать:
  1. Создать scripts/log-listener.sh:
     ./scripts/log-listener.sh [--port 12345]
  2. Использовать nc (netcat) для прослушивания:
     nc -l -p $PORT  # Linux
     nc -l $PORT     # macOS (другой синтаксис!)
  3. Учесть разницу в синтаксисе netcat между macOS и Linux:
     if [[ "$OSTYPE" == "darwin"* ]]; then
       nc -l $PORT
     else
       nc -l -p $PORT
     fi
  4. Добавить цветовую раскраску логов (опционально):
     - ERROR → красный
     - WARNING → желтый
     - INFO → зеленый
  5. Добавить graceful shutdown по Ctrl+C (trap)
```
**Критерий приемки:** Скрипт слушает порт, принимает TCP-подключение от Switch и выводит логи в реальном времени. Ctrl+C корректно завершает процесс.

---

## Фаза 3: Обновление документации

### Задача 3.1 — Обновить README.md
```
1. Добавить секцию "macOS Build" с инструкциями:
   - Требования (macOS 14+, Homebrew, ~40 GB места)
   - Быстрый старт:
     ./scripts/setup-macos.sh
     ./scripts/build-dart-app.sh --project ./examples/ui_app
     ./scripts/rebuild-all.sh ui_app
   - Отличия от Windows/WSL2-инструкций
2. Обновить таблицу "Что работает" — добавить macOS в поддерживаемые хост-ОС
3. Убрать упоминания WSL2 как единственного варианта
```

### Задача 3.2 — Создать CONTRIBUTING.md (если нет)
```
Описать:
  - Как настроить окружение на macOS / Linux / Windows
  - Стиль коммитов (conventional commits)
  - Как тестировать изменения
  - Как отправить PR в upstream
```

---

## Фаза 4: Тестирование и CI

### Задача 4.1 — Smoke-тест на macOS
```
Выполнить полный цикл сборки:
  1. ./scripts/setup-macos.sh
  2. python3 scripts/patch-engine-horizon.py
  3. scripts/gn-gen-horizon.sh
  4. scripts/build-horizon.sh ...:flutter_engine_static
  5. ./scripts/build-dart-app.sh --project ./examples/ui_app
  6. ./scripts/rebuild-all.sh ui_app

Задокументировать результат:
  - Время каждого этапа
  - Ошибки и их решения
  - Размер итогового .nro
```
**Критерий приемки:** Файл `ui_app.nro` успешно собран и имеет разумный размер (ожидается ~50–150 МБ).

### Задача 4.2 — GitHub Actions для macOS (опционально)
```
Создать .github/workflows/build-macos.yml:
  - runs-on: macos-14 (Apple Silicon)
  - Кэширование: depot_tools, Flutter SDK, devkitPro
  - Шаги: setup → patch → gn → build → upload artifact
  - Таймаут: 6 часов (сборка движка долгая)
```

---

## Порядок выполнения (зависимости)

```
Фаза 0 (подготовка)
  └→ Фаза 1.1 (setup-скрипт)
       └→ Фаза 1.2 (проверка .sh скриптов)
            └→ Фаза 2.1 (build-dart-app.sh) ← самая сложная задача
                 └→ Фаза 2.2 (nxlink-upload.sh)
                 └→ Фаза 2.3 (log-listener.sh)
                      └→ Фаза 3 (документация)
                           └→ Фаза 4 (тестирование)
```

---

## Промпт для код-агента (copy-paste)

```markdown
# Задача

Ты работаешь с форком репозитория flutter-libnx (порт Flutter Engine на Nintendo Switch).
Оригинальная build-цепочка использует Windows + WSL2 + PowerShell.
Твоя цель — сделать сборку полностью нативной на macOS.

## Ограничения
- ОС: macOS (Apple Silicon, arm64)
- Пакетный менеджер: Homebrew
- Все новые скрипты на bash с `set -euo pipefail`
- Shebang: `#!/usr/bin/env bash`
- Скрипты должны быть идемпотентными
- Не удаляй оригинальные .ps1 файлы — переименовывай в .ps1.bak
- Коммить с conventional commits: feat(macos):, fix(macos):, docs(macos):

## Первый шаг
Начни с аудита: прочитай все скрипты в scripts/ и составь
docs/macos-migration.md с анализом каждого скрипта.
Затем создай scripts/setup-macos.sh для установки зависимостей.

## Приоритет
1. setup-macos.sh (установка зависимостей)
2. build-dart-app.sh (переписывание из .ps1)
3. nxlink-upload.sh (переписывание из .ps1)
4. log-listener.sh (переписывание из .ps1)
5. Проверка существующих .sh скриптов на macOS-совместимость
6. Обновление README.md
```

---

Хочешь, чтобы я углубился в какую-то конкретную задачу? Например, могу детально разобрать `build-dart-app.ps1` и написать черновик bash-версии, если ты дашь мне его содержимое (или я могу попробовать достать его из репозитория).