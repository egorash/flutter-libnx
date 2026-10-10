# Аудит macOS-совместимости скриптов flutter-libnx

Аудит выполнен для перевода build-цепочки с Windows + WSL2 + PowerShell на
нативный macOS (Apple Silicon `arm64`, bash/zsh, Homebrew, devkitPro в
`/opt/devkitpro`).

**Область:** все скрипты `scripts/` — 81 `.sh`, 8 `.ps1`, 6 `.py` (95 файлов).
**Режим:** только анализ. Правки кода — на этапах 3 и 6 (см.
`docs/macos-plan.md`).

> **Статус Этапа 3 (выполнен, коммиты `fix(macos):` 2cc92a6..9e006c1):**
> все `.sh` параметризованы (`REPO/SRC/OUT/DEVKITPRO/DEVKITA64` с дефолтами),
> `/mnt/e/...` → `$REPO`, `$HOME/devkitpro` → `/opt/devkitpro`, GNU-флаги →
> BSD (`stat -f%z`, без `ls --time-style`, кавычки `--include='*.…'`),
> `set -uo/-euo pipefail`, macOS-guard'ы в `setup-devkitpro.sh`/
> `extract-{make,pkgconf}.sh`. `bash -n` и `shellcheck -S error` — чисто.
> Пункты 1–5 и 7 раздела «Рекомендации» ниже закрыты; пункт 6 — Этап 6.

## Как читать таблицы

- **Зависимости** — внешние утилиты, инструменты devkitPro и переменные
  окружения (`SRC`, `OUT`, `REPO`, `DEVKITPRO`, `DEVKITA64`, `DEPOT_TOOLS`,
  `HOME`).
- **Приоритет:**
  - `высокий` — ломается на macOS и критичен для цепочки;
  - `средний` — работает частично/зависит от окружения или выдаёт неверный
    результат;
  - `низкий` — тонкая обёртка над `grep`/`sed`/`nm`; на macOS достаточно
    поправить пути/переменные.

## Сводка

| Приоритет | Кол-во |
|---|---|
| высокий | 24 |
| средний | 15 |
| низкий | 56 |
| **всего** | **95** |

Системные источники несовместимости (повторяются у многих скриптов):

1. **WSL-путь `/mnt/e/flutter-libnx`** вместо repo-relative / `$REPO`.
2. **`$HOME/devkitpro`** вместо `/opt/devkitpro` (`$DEVKITPRO`) — путь из
   старой WSL-схемы; на macOS devkitPro ставится в `/opt/devkitpro`.
3. **`sudo apt-get` / `install-devkitpro-pacman`** — Debian-only
   (`setup-devkitpro.sh`); на macOS нужен `.pkg`-установщик `dkp-pacman`.
4. **Linux-ELF-артефакты** (`extract-make.sh`, `extract-pkgconf.sh`) — на
   macOS неисполнимы, не нужны (есть Xcode CLT/Homebrew).
5. **GNU-специфика:** `stat -c%s` (BSD `stat -f%z`), `ls --time-style`
   (BSD `ls` не поддерживает), `sed -i` без `''`, `readlink -f`,
   `LD_LIBRARY_PATH` (нужен `DYLD_LIBRARY_PATH`), `nproc`, `sha256sum`.
6. **Python3** как зависимость (`fix-socket-base-guard.sh`,
   `patch-engine-horizon.py`) — на macOS доступен после `xcode-select
   --install` / Homebrew.
7. **Docker** (`dkp.ps1`, `build-aot-poc.ps1`, `nxlink-upload.ps1`) — при
   нативном devkitPro не нужен.
8. **Хардкод `$HOME/engine/flutter/engine/src`** без override `SRC` — в
   большинстве диагностических скриптов; на macOS совпадает по расположению,
   но не параметризовано.

---

## 1. Ядро build-цепочки (`.sh`, 11)

| Скрипт | Назначение | Зависимости | macOS-риски | Приоритет |
|---|---|---|---|---|
| `build-horizon.sh` | Запускает ninja-сборку одного target движка (по умолчанию `flutter/fml`) в `out/horizon_release_arm64`. | `ninja` (сначала `$SRC/flutter/third_party/ninja/ninja`, иначе `command -v ninja`); env: `SRC`, `OUT`, `JOBS`, `DEPOT_TOOLS`, `HOME`, `PATH` | Пути `$HOME`-относительные; `set -u` без `-e`/`pipefail`; ninja-бинарь из checkout может отсутствовать, тогда берётся host-ninja | низкий |
| `gn-gen-horizon.sh` | Первый `gn gen` для target `horizon` (arm64, release/AOT). | `gn` из `$SRC/flutter/third_party/gn/gn`, `vpython3`/`python` из depot_tools; env: `SRC`, `GN`, `OUT`, `DEVKITPRO`, `ENGINE_VERSION`, `SKIA_VERSION`, `DART_VERSION`, `DEPOT_TOOLS`, `HOME` | `DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"` — на macOS devkitPro в `/opt/devkitpro`, дефолт неверен; `2>&1 \| tail -40` глушит крупный вывод ошибок | средний |
| `build-sqlite3.sh` | Компилирует sqlite3 amalgamation в `libsqlite3.a` и генерирует `sqlite3_symbols.inc` для FFI. | `aarch64-none-elf-gcc`/`-ar`/`-nm`, `du`, `cut`, `awk`, `sort`, `grep`; env: `HOME` (пути захардкожены) | `REPO=/mnt/e/flutter-libnx` (WSL); `DEVKITA64="$HOME/devkitpro/devkitA64"` и `-I$HOME/devkitpro/libnx/include` вместо `/opt/devkitpro` | высокий |
| `rebuild-all.sh` | Полная пересборка: gen_snapshot → engine → AOT-snapshot → NRO. | Вызывает `build-horizon.sh`, `relink-example.sh`, `gen_snapshot_product`, `mkdir`, `mv`, `wc`, `grep`, `tail`; env: `SRC`, `OUT` | `REPO=/mnt/e/flutter-libnx` (WSL) — не находит `examples/*/generated` и `build-logs`; в подсказке — непортированный `build-ui-app.ps1`; `set -u` без `-e` | высокий |
| `build-example-wsl.sh` | Собирает пример (`make`) против движка, подставляя пути движка и devkitA64. | `make`; env: `REPO`, `DEVKITPRO`, `DEVKITA64`, `ENGINE_SRC`, `ENGINE_OUT`, `HOME`, `PATH` | `REPO="${REPO:-/mnt/e/flutter-libnx}"` (WSL); `DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"`; по имени/смыслу — только WSL | высокий |
| `setup-engine-checkout.sh` | Клонирует depot_tools и flutter, пишет `.gclient` и делает `gclient sync` (пин `db50e20`). | `git`, `gclient`/depot_tools, `mkdir`, `du`; env: `ENGINE_DIR`, `DEPOT_TOOLS`, `PATH`, `HOME` | Linux-специфики нет, `$HOME/engine` и `$HOME/depot_tools` совпадают с macOS; комментарии про WSL, логика переносима | низкий |
| `setup-devkitpro.sh` | Ставит devkitPro/devkitA64 + libnx через apt/dkp-pacman на Debian/Ubuntu. | `sudo`, `apt-get`, `wget`, `gdebi-core`, `build-essential`, `p7zip-full`, `dkp-pacman`, `mktemp` | Полностью Linux: `sudo apt-get`, `wget install-devkitpro-pacman`; на macOS — `.pkg`-установщик + `dkp-pacman -S switch-dev` | высокий |
| `relink-example.sh` | Удаляет ELF, заново линкует пример и разбирает лог (undefined/multiple definition). | `rm`, `grep`, `sed`, `sort`, `wc`, `awk`, `ls`, `head`, `tail`, `mkdir` | `D=/mnt/e/flutter-libnx/examples/$EX`, `LOG=/mnt/e/...`, вызов `/mnt/e/.../build-example-wsl.sh`; хрупкое экранирование backtick в `grep -o` (BSD vs GNU) | высокий |
| `build-mpv-symbols.sh` | Генерирует `mpv_symbols.inc` из `libmpv.a` для FFI-резолва. | `aarch64-none-elf-nm`, `awk`, `sort`, `grep`; env: `HOME` | `REPO=/mnt/e/flutter-libnx`; `LIB="$HOME/devkitpro/portlibs/switch/lib/libmpv.a"`, `NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"`; `nm --defined-only` — GNU-флаг | высокий |
| `extract-pkgconf.sh` | Достаёт pkgconf без root в `~/bin` из tar-архива. | `tar`, `cp`, `chmod`, `rm`, `mkdir`, `sh`; env: `HOME` | `TAR=/mnt/e/pkgconf.tar`; копирует Linux x86_64 `.so`; wrapper использует `LD_LIBRARY_PATH` (на macOS — `DYLD_LIBRARY_PATH`); на macOS проще brew `pkg-config` | высокий |
| `extract-make.sh` | Достаёт GNU make без root в `~/bin` из tar-архива. | `tar`, `cp`, `head`; env: `HOME` | `TAR=/mnt/e/make.tar`; копирует Linux ELF `make`, неисполнимый на macOS; не нужен (Xcode CLT/brew) | высокий |

### Детали

**`gn-gen-horizon.sh`**
- Строка 23: `DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"` — на macOS devkitPro
  в `/opt/devkitpro`, дефолт приведёт к неверному `devkitpro_root`.
- Строка 50: `2>&1 | tail -40` обрезает вывод `gn gen`; реальная причина
  падения может потеряться.
- Строка 46: **`shell_enable_gl` обязан быть `true`.** С `false` движок
  собирается без GL-рендерера: `FlutterEngineInitialize` возвращает
  `kInternalInconsistency`, в логе —
  `[ERROR] This Flutter Engine does not support OpenGL rendering.
  (embedder.cc:518)`, на экране чёрный кадр. Это соответствует upstream
  Milestone 5 (`docs/status.md`, `docs/porting-notes.md`: `shell_enable_gl=true`
  + `skia_use_gl=true`, Impeller выключен). `skia_use_gl` для Horizon задаёт
  патч `flutter/skia/BUILD.gn` (`patch_skia_config`), отдельный GN-аргумент
  не нужен.

**`build-sqlite3.sh`**
- Строка 32: `REPO=/mnt/e/flutter-libnx` — отсутствует на macOS.
- Строки 34/46: `DEVKITA64="$HOME/devkitpro/devkitA64"` и
  `-I"$HOME/devkitpro/libnx/include"` — нужно `DEVKITPRO=/opt/devkitpro`.

**`rebuild-all.sh`**
- Строка 20: `REPO=/mnt/e/flutter-libnx` — производные пути в WSL-дерево.
- Строка 55: подсказка про `scripts/build-ui-app.ps1` (не портирован).
- Строки 31/40: вызов `$REPO/scripts/build-horizon.sh` по `/mnt/e`-пути.

**`build-example-wsl.sh`**
- Строка 13: `REPO="${REPO:-/mnt/e/flutter-libnx}"` — WSL-путь.
- Строка 15: `DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"`.
- Строка 18: `PATH="$HOME/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"`.

**`setup-devkitpro.sh`**
- Строки 12–13: `sudo apt-get update` / `sudo apt-get install -y ...` —
  Debian/Ubuntu-only.
- Строка 18: `wget ... install-devkitpro-pacman`; строка 20 —
  `sudo "$tmp/install-devkitpro-pacman"` — Linux-путь.
- Строки 27–28: `sudo dkp-pacman -Syu` / `-S switch-dev`.

**`relink-example.sh`**
- Строка 7: `D="/mnt/e/flutter-libnx/examples/$EX"`.
- Строка 10: `LOG="/mnt/e/flutter-libnx/build-logs/link-$EX.log"`.
- Строка 14: `bash /mnt/e/flutter-libnx/scripts/build-example-wsl.sh ...`.

**`build-mpv-symbols.sh`**
- Строка 12: `REPO=/mnt/e/flutter-libnx`.
- Строка 14: `LIB="$HOME/devkitpro/portlibs/switch/lib/libmpv.a"`.
- Строка 15: `NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"`.

**`extract-pkgconf.sh`**
- Строка 11: `TAR="/mnt/e/pkgconf.tar"`.
- Строка 26: копирование Linux `.so`; строка 32: `LD_LIBRARY_PATH`.

**`extract-make.sh`**
- Строка 8: `TAR="/mnt/e/make.tar"`.
- Строка 16: `cp -a .../usr/bin/make` — Linux ELF.

### macOS-обновление контрактов (Этап 7)

- **`install-portlibs-macos.sh`** (новый) — ставит портлибы, нужные `ui_app`
  (`switch-mesa`, `switch-libdrm_nouveau`, `switch-libmpv`, `switch-ffmpeg`,
  `switch-libass`, `switch-libfribidi`, `switch-libplacebo`, `switch-dav1d`,
  `switch-libarchive`, `switch-libzstd`, `switch-sdl2`, `switch-bzip2`,
  `switch-liblzma`, `switch-zlib`), из группы `switch-portlibs`. Запуск через
  `sudo`, идемпотентен (`--needed`). Обходит сломанный gpg (нет `gpg` на
  macOS arm64) копией `pacman.conf` с `SigLevel = Never`. Дополняет
  `install-devkitpro-macos.sh`, который ставит только группу `switch-dev`.

- **`rebuild-all.sh`** — определяется платформа (`uname -s`). Host-`gen_snapshot`
  выбирается по host-тулчейну: `clang_arm64/gen_snapshot_product` на Darwin,
  `clang_x64/gen_snapshot_product` на Linux. На Darwin шаг 1 вызывается
  напрямую через `ninja` (с `depot_tools` в `PATH` ради `vpython3`), а не
  через `build-horizon.sh`; шаг 2 (`flutter_engine_static`) — через
  `build-horizon.sh` в обоих случаях. `REPO` берётся из расположения скрипта;
  подсказка указывает на `build-dart-app.sh` (вместо `.ps1`).
- **`build-dart-app.sh`** — раскрытие массива `defines` защищено
  (`${defines[@]+...}`), иначе пустой массив под `set -u` роняет kernel-шаг.
  Добавлен флаг **`--example NAME`** (дефолт `ui_app`): выходные каталоги
  (`generated/`, `romfs/`) кладутся в `examples/NAME`, а не всегда в
  `examples/ui_app`. Дефолтный `--project` — `examples/NAME/dart`. Подсказка
  в конце указывает на `rebuild-all.sh NAME`. Так собирается произвольное
  приложение в отдельную цель (`examples/my_nexus`) — см. ниже.
- **`examples/my_nexus`** (новый пример-цель) — сборка произвольного
  Flutter-проекта в `my_nexus.nro`. `Makefile` — копия `ui_app` с другим
  `TARGET`/`APP_TITLE`; `source/main.cpp` — **симлинк** на общий раннер
  `examples/ui_app/source/main.cpp` (раннер app-независим). Раннер теперь
  берёт `FLUTTER_LIBNX_APP_ID` из `-DFLUTTER_LIBNX_APP_ID="$(TARGET)"`
  (`examples/ui_app/source/main.cpp`): от неё зависят путь лога
  (`sdmc:/switch/flutter-libnx/<id>.log`) и текст `LOG_INFO`. Цепочка:
  `build-dart-app.sh --example my_nexus --project <путь> && rebuild-all.sh my_nexus`.
- **`patch-engine-horizon.py` (macOS-хост)** — пиновый `buildtools`-clang
  старше SDK 27. Для host-тулчейна (`current_os == mac`) патчер снимает
  `-Werror` (`-Wno-error`) в `build/config/compiler/BUILD.gn`; Horizon-кросс
  и так без `-Werror`. Дополняет `-fuse-ld=<Xcode ld>` для mac-линкера.
- **`relink-example.sh` / `build-example-wsl.sh`** — контракт не меняется:
  `build-example-wsl.sh` уже платформо-нейтрален (задаёт devkitPro и `make`),
  поэтому на macOS вызывается как есть.

---

## 2. PowerShell (порт на bash) и Python (14)

> **Статус порта (Этап 6, коммиты `feat(macos):`):** все 8 `.ps1` портированы
> в bash-скрипты рядом (`build-dart-app.sh`, `build-ui-app.sh`,
> `build-aot-poc.sh`, `dkp.sh`, `nxlink-upload.sh`, `log-listener.sh`,
> `fetch-reference.sh`, `probe-app-toolchain.sh`). Оригиналы `.ps1`
> **не удаляются и не переименовываются** — Windows/WSL-поток сохранён.
> Изменения контрактов (getopts вместо `param()`):
> - `build-dart-app.sh --project PFAD [--example NAME] |--product` (дефолт
>   примера `examples/ui_app`, проекта — `examples/<example>/dart`);
> - `build-ui-app.sh` — без аргументов (как оригинал);
> - `build-aot-poc.sh` — без аргументов; `GEN_SNAPSHOT` для override;
> - `dkp.sh <WorkDir> <Befehl...>` — нативный devkitPro, docker — fallback;
> - `nxlink-upload.sh --switch-ip IP [--example NAME] [-- args...]`;
> - `log-listener.sh [--port N] [--timeout N] [--out DATEI]` — python3-socket
>   (BSD-nc не умеет ждать соединение);
> - `fetch-reference.sh`, `probe-app-toolchain.sh` — как оригиналы.
> Windows-специфика заменена: `.exe` убраны, `C:\Users\...\flutter` →
> `$FLUTTER_HOME` (дефолт `~/flutter-3.41.6`), `windows-x64\icudtl.dat` →
> `darwin-arm64/icudtl.dat` (+поиск), финал «WSL bash /mnt/e/...» → локальный
> `$root/scripts/rebuild-all.sh`.

| Скрипт | Назначение | Зависимости | macOS-риски | Приоритет |
|---|---|---|---|---|
| `build-aot-poc.ps1` | Собирает пример `aot_poc`: gen_kernel → gen_snapshot (AArch64) → `make` в devkitA64, печатает defined-символы. | dartaotruntime + `gen_kernel_aot.dart.snapshot`, `gen_snapshot.exe`, `dkp.ps1` (docker), `aarch64-none-elf-nm`; env: `USERPROFILE`, `PSScriptRoot`, `LASTEXITCODE`; жёсткий `C:\Users\mirkorichter\flutter` | PS-конструкции (`Split-Path`, `Test-Path`, `New-Item`, `Write-Host`, `&`, `$LASTEXITCODE`, `\`-пути, `.exe`); вызов `dkp.ps1` (docker). Нужен `build-aot-poc.sh` + `dkp.sh` | высокий |
| `build-dart-app.ps1` | Универсально собирает Flutter-проект: `flutter build bundle` → assets → Horizon-регистрант → gen_kernel → `app.dill`, затем WSL. | flutter(.bat), dartaotruntime, `icudtl.dat`, WSL bash + `rebuild-all.sh`; env: `USERPROFILE`, `PSScriptRoot` | `param(-Project/-Product)` → getopts; `Resolve-Path`, `Test-Path`, `New-Item`, `Copy-Item`, `Remove-Item`, `Get-ChildItem`, `Select-String`, `Set-Content`, `Push/Pop-Location`; `$env:USERPROFILE\flutter`, `windows-x64\icudtl.dat`, `flutter.bat`; финал — `wsl bash /mnt/e/...`. Нужен `build-dart-app.sh` | высокий |
| `build-ui-app.ps1` | Собирает `examples/ui_app`: gen_kernel из `main.dart` + вручную flutter_assets. | dartaotruntime, Flutter SDK (material_fonts, icudtl.dat), WSL + `rebuild-all.sh`; env: `USERPROFILE`, `PSScriptRoot` | Те же PS-команды, `Set-Content -Encoding UTF8`, here-strings; `$env:USERPROFILE\flutter`, `windows-x64\icudtl.dat`; `wsl bash /mnt/e/...`. Дублирует `build-dart-app` | высокий |
| `dkp.ps1` | Выполняет команду в toolchain devkitPro через `docker run devkitpro/devkita64` с монтированием репо в `/work`. | docker (образ `devkitpro/devkita64:latest`); env: `PSScriptRoot`, `LASTEXITCODE` | `param` + `ValueFromRemainingArguments` → `"$@"`; `-replace '\\','/'` → трансляция путей; docker; при нативном devkitPro не нужен; `$LASTEXITCODE` → `$?` | высокий |
| `fetch-reference.ps1` | Скачивает reference `embedder.h` по пинну и клонирует/чекаутит libnx в `third_party`. | `Invoke-WebRequest`, git; env: `PSScriptRoot`; ревизии `db50e201…`, `v4.12.0` | `Invoke-WebRequest -OutFile` → `curl -fL -o`; `New-Item -Force`, `Test-Path` → `mkdir -p`/`test`; `git -C` переносим | низкий |
| `log-listener.ps1` | TCP-приёмник логов Switch на порту 28800 с таймаутом и записью в файл. | .NET `TcpListener`/`StreamReader`; параметры `-Port/-TimeoutSeconds/-OutFile` | Переписать на `nc`/`socat`/python3-socket (BSD `nc` не ждёт соединения); таймаут, `tee`; файл отсутствует (нужен `log-listener.sh`) | средний |
| `nxlink-upload.ps1` | Загружает `.nro` на Switch через nxlink. | docker (`devkitpro/devkita64`) + `/opt/devkitpro/tools/bin/nxlink`; `param` `SwitchIp`, `-Example`, `-Args` | docker `--network host` уходит; `param`/`RemainingArguments` → getopts + `"$@"`; `Test-Path`/`Get-Item` → `test`/`stat`; `" {0:N1} MB"` → printf. Нужен `nxlink-upload.sh` | высокий |
| `probe-app-toolchain.ps1` | Диагностика: наличие Flutter SDK, gen_snapshot, kernel-компилятора, platform-dill, `icudtl.dat`. | Flutter SDK; env: `USERPROFILE` | `Get-ChildItem -Recurse -Filter/-Include`, `.Replace`, `Test-Path`, `Get-Content`, `Write-Host` → `find`/`grep`/`cat`/`$HOME`. Нужен `probe-app-toolchain.sh` | низкий |
| `analyze-dart-io-guards.py` | Проверяет, можно ли расширить guard `DART_HOST_OS_LINUX` в `*_linux.cc` dart:io. | python3 stdlib (os, re, sys); env `SRC` (default `~/engine/flutter/engine/src`) | Windows/WSL-специфики нет; forward-slash, `expanduser`; read-only, идемпотентен | низкий |
| `analyze-dart-io-posix.py` | То же для POSIX-слоя `*_posix.cc` dart:io. | python3 stdlib; env `SRC` | Как выше; `os.listdir` без проверки падает при неверном `SRC`; read-only | низкий |
| `dedupe-horizon-branches.py` | Удаляет повторно вставленные Horizon-ветки из `BUILD.gn`/`globals.h`/`skia/BUILD.gn`. | python3 stdlib; env `SRC` | Пишет в движок; пути корректны, LF; идемпотентность частичная | низкий |
| `find-silent-fallbacks.py` | Ищет `#if`-цепочки с молчаливым `#else`, не покрывающие Horizon. | python3 stdlib; env `SRC`; аннотация `-> list` (py3.9+) | Read-only, forward-slash; менять нечего | низкий |
| `patch-engine-horizon.py` | Главный идемпотентный патчер движка (`current_os=horizon`, toolchain, guards, копирование Horizon-исходников). | python3 stdlib (os, re, sys, shutil); env `SRC` (default `~/engine/...`), `TRACE=1` | Пишет в `~/engine/...`, репо-путь через `dirname(...)`; чистая stdlib + forward-slash → macOS ок. Критично: верный `$SRC`, LF, py3.9+; идемпотентность через `replace_once`/проверки вхождения | высокий |
| `scan-missing-cstring.py` | Ищет engine-исходники, использующие `<cstring>`/`<climits>` без соответствующего include. | python3 stdlib (os, re, sys); env `SRC` (default `~/engine/flutter/engine/src`) | Read-only, forward-slash, `encoding="utf-8", errors="replace"`; на macOS работает как есть; нужен верный `$SRC` | низкий |

### Детали

**`build-aot-poc.ps1`**
- Строки 14–19: `Split-Path -Parent $PSScriptRoot` и жёсткий
  `C:\Users\mirkorichter\flutter`; `gen_snapshot.exe`/`dartaotruntime.exe`.
- Строки 41–47: вызов `dkp.ps1` (docker) и `/opt/devkitpro/.../nm | grep`.
- План: `build-aot-poc.sh` с `ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." &&
  pwd)"`, SDK из `$FLUTTER_HOME` (default `~/flutter-3.41.6`), хост-бинарники
  без `.exe`, сборка через `dkp.sh`, проверка `if ! cmd; then`.

**`build-dart-app.ps1`**
- Строки 26–33: `param([string]$Project,[switch]$Product)`; 48 `Resolve-Path`;
  57–61 `Test-Path`/`New-Item`; 67–73 `Push-Location`/`flutter.bat`;
  85–100 `Remove-Item`/`Copy-Item`/`Join-Path`/`Get-ChildItem`; 123
  `Select-String`; 143 `Set-Content`; 166–167 печать `wsl bash /mnt/e/...`.
- План: `build-dart-app.sh` с getopts `--project/--product`,
  `$HOME/flutter-3.41.6`, macOS-артефакт `icudtl.dat`; `test`/`mkdir -p`/
  `cp -R`/`rm -rf`/`find`; `cd` в subshell; финал — локальный
  `rebuild-all.sh` (без WSL).

**`build-ui-app.ps1`**
- Строки 15–23: `$env:USERPROFILE\flutter`, `windows-x64\icudtl.dat`;
  34–38 `Test-Path`/`New-Item`; 51–58 gen_kernel; 86–98 `Copy-Item`
  MaterialIcons, `Set-Content -Encoding UTF8`; 75 `wsl bash /mnt/e/...`.
- План: отдельный `build-ui-app.sh` либо слить в один параметризованный
  скрипт; манифесты через heredoc; финал — локальный `rebuild-all.sh ui_app`.

**`dkp.ps1`**
- Строки 10–13: `param(... ValueFromRemainingArguments)`; 19 пиннутый образ;
  22–24 `-replace`, `/work`; 26 `docker run --rm -v ... -w ... bash -lc`;
  27 `exit $LASTEXITCODE`.
- План: `dkp.sh` с `WorkDir="$1"; shift; exec "$@"`; при нативном devkitPro —
  просто `cd` + `PATH` из `/opt/devkitpro/{devkitA64,tools}/bin`; docker —
  fallback.

**`nxlink-upload.ps1`**
- Строки 11–15: `param`; 22 `Test-Path`; 26–29 `Get-Item.Length` +
  `/opt/devkitpro/tools/bin/nxlink`; 31–35 `docker run --network host`.
- План: `nxlink-upload.sh` с getopts `--switch-ip/--example`, `"$@"` после
  `--`; нативный nxlink; размер через `stat -f%z`; `printf`.

**`log-listener.ps1`**
- Строки 14–16: `[System.Net.Sockets.TcpListener]`, `Start`, `Write-Host`;
  20–28 неблокирующее ожидание `Pending()` с дедлайном; 36–43 чтение строк и
  `Set-Content -Encoding UTF8`.
- План: `log-listener.sh` на `nc`/`socat`/python3-socket с
  `--port/--timeout/--out`, таймаут через python3/perl-alarm, запись через
  `tee`.

**`patch-engine-horizon.py`**
- Строка 14: `SRC = os.environ.get("SRC", os.path.expanduser("~/engine/..."))`
  — уже macOS-совместимо; 197–216 `replace_once` (идемпотентность через
  `new in text`); 3647–3763 `main()`.
- План: без переписывания; запуск с `SRC=...`, python ≥3.9, LF; после прогона
  проверить идемпотентность повторным запуском.

---

## 3. Диагностика: `probe-*` и `find-*` (23)

| Скрипт | Назначение | Зависимости | macOS-риски | Приоритет |
|---|---|---|---|---|
| `probe-absl-sync-target.sh` | Инспекция abseil `synchronization/BUILD.gn` и `thread_identity`. | bash, grep, head, ls; `$HOME` | Хардкод `$HOME/engine/...` и `out/horizon_release_arm64`, не слушает `SRC`; GNU-флагов нет | низкий |
| `probe-absl-users.sh` | Поиск пользователей abseil/synchronization и собранных объектов. | bash, grep, find, sed, sort, wc; `$HOME` | `--include` поддерживается BSD grep; хардкод пути движка | низкий |
| `probe-archive.sh` | Диагностика `libflutter_engine.a`: члены, `SocketBase::Read`. | `ar`, `nm`, ls, awk, grep, wc; `$HOME` | Тулкит `$HOME/devkitpro/...` вместо `/opt/devkitpro` | средний |
| `probe-at-consts.sh` | Поиск `*at`/`fdopendir`/`O_DIRECTORY` в newlib и патче. | grep; `$HOME` | Хардкод WSL-пути `/mnt/e/flutter-libnx/scripts/patch-engine-horizon.py`; `$HOME/devkitpro` | средний |
| `probe-at-funcs.sh` | Проверка `*at`/dir-функций в libc/libnx через `nm`. | `nm`, grep, basename; `$HOME` | `$HOME/devkitpro` вместо `/opt/devkitpro` | низкий |
| `probe-dirfd.sh` | Разбор dirent/DIR devoptab libnx. | grep, sed; `$HOME` | Хардкод `$HOME/devkitpro`, `$HOME/libnx`, `$HOME/src/libnx` | низкий |
| `probe-freetype-absl-certs.sh` | Поиск FreeType-объектов и root_certificates. | find, grep, ls, wc; `$HOME` | Тонкая обёртка; хардкод пути движка | низкий |
| `probe-freetype-absl2.sh` | Skia-порт FreeType и состояние `low_level_alloc.o`. | find, grep, ls, awk, nm, wc; `$HOME` | `$HOME/devkitpro/.../nm`; иначе grep/find-обёртка | низкий |
| `probe-last-two.sh` | Анализ `sysconf`, `_SC_*`, fallback-root-certs по логам/GN. | grep; `/tmp`, `$HOME` | Читает `/tmp/link.log` (может отсутствовать; temp иной); хардкод пути | низкий |
| `probe-message-loop.sh` | Инспекция `MessageLoopImpl::Create` и timerfd/epoll в ELF. | grep, ls, nm; `$HOME` | Хардкод WSL-пути `/mnt/e/.../elf`; `$HOME/devkitpro` | средний |
| `probe-net-crypto.sh` | Проверка net/crypto/file-символов в libc/libnx. | nm, grep, printf, sed; `$HOME` | `$HOME/devkitpro` и `$HOME/devkitpro/libnx` | низкий |
| `probe-semaphore.sh` | Разбор semaphore.cc и дизасм `sem_init`. | nm, objdump, grep, cut, printf; `$HOME` | Хардкод WSL-пути `/mnt/e/.../elf`; `$HOME/devkitpro` | средний |
| `probe-skia-absl-config.sh` | Вывод головы `skia/BUILD.gn` и условий `low_level_alloc.h`. | sed; `$HOME` | Чисто sed-обёртка; хардкод пути движка | низкий |
| `probe-socket-base-obj.sh` | Символы/размер dart_io-объектов и guard-ов socket_base_linux. | nm, stat, grep, wc, printf; `$HOME` | `stat -c%s` — GNU, на macOS `stat -f%z`; `$HOME/devkitpro` | высокий |
| `find-buildtools-decl.sh` | Поиск `buildtools_path` и build_overrides. | grep, ls; `$HOME` | grep/ls-обёртка; хардкод пути | низкий |
| `find-buildtools-path.sh` | Как linux-тулчейн находит clang. | grep; `$HOME` | grep-обёртка; ищет только linux-тулчейн | низкий |
| `find-clang-base-path.sh` | Поиск `clang_base_path` и buildtools clang++. | grep, ls; `$HOME` | Хардкод `buildtools/linux-x64/...`; на macOS хост иной | низкий |
| `find-crypto-source.sh` | Поиск `crypto_*` в gni/BUILD.gn dart runtime. | grep; `$HOME` | grep-обёртка; хардкод пути | низкий |
| `find-dart-bin-sources.sh` | Список gni, поиск `eventhandler_linux.cc`. | ls, grep, wc, sort; `$HOME` | Тонкая обёртка; хардкод пути | низкий |
| `find-host-clang.sh` | Поиск buildtools и clang++ в дереве движка. | find, ls; `$HOME` | find-обёртка; проверяет `linux-x64`, не `mac-*` | низкий |
| `find-ldl.sh` | Поиск `-ldl` в GN и link-команде. | grep; `$HOME` | grep-обёртка; хардкод пути и `.rsp` | низкий |
| `find-missing-impls.sh` | Поиск реализаций Utils/Syslog/OS. | grep; `$HOME` | grep-обёртка; хардкод пути | низкий |
| `find-sync-impl.sh` | Поиск `Mutex::Lock` и synchronization-файлов. | grep, ls; `$HOME` | grep/ls-обёртка; хардкод пути | низкий |

### Детали

**`probe-socket-base-obj.sh`**
- `stat -c%s "$O"` — GNU; на macOS BSD `stat` падает (нужен `stat -f%z`).
- `NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"` — нужно
  `/opt/devkitpro`/`$DEVKITPRO`.

**`probe-archive.sh`**
- `AR`/`NM` жёстко `$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-{ar,nm}`.
- `LIB`/`OUT` хардкодят `$HOME/engine/.../out/horizon_release_arm64`.

**`probe-at-consts.sh`**
- `grep ... /mnt/e/flutter-libnx/scripts/patch-engine-horizon.py` — WSL-путь.
- `D="$HOME/devkitpro/devkitA64/aarch64-none-elf/include"` — заголовки не
  найдутся.

**`probe-message-loop.sh`**
- ELF `/mnt/e/flutter-libnx/examples/engine_link_test/engine_link_test.elf` —
  WSL-хардкод.
- `NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"`.

**`probe-semaphore.sh`**
- `E="/mnt/e/flutter-libnx/examples/engine_link_test/engine_link_test.elf"`.
- `NM`/`OBJDUMP` под `$HOME/devkitpro/...`.

---

## 4. Диагностика: `show-*` (25)

| Скрипт | Назначение | Зависимости | macOS-риски | Приоритет |
|---|---|---|---|---|
| `show-cpuinfo-decl.sh` | Показать `vm/cpuinfo.h` и объявления в `native_symbol.h`. | cat, grep, head; `HOME` | Хардкод `$HOME/engine/...` без override; BSD grep ок | низкий |
| `show-dart-bin-guards.sh` | Linux-гварды в `bin/*_linux.cc`. | grep, cut, sort, tr; `HOME` | BRE-альтернация `\|` работает; хардкод пути | низкий |
| `show-dart-bin-headers.sh` | Гварды в bin-заголовках и epoll в `eventhandler_linux.h`. | grep, wc, head; `HOME` | Баг: `$f` без префикса `$R/`; хардкод пути | низкий |
| `show-dart-bin.sh` | Выбор платформы в `eventhandler.h`/`socket_base.h`. | grep, head; `HOME` | Хардкод пути; BSD grep ок | низкий |
| `show-dart-io-config.sh` | `io_impl_sources.gni`, собранные dart_io-объекты, `args.gn`. | grep, find, sed, sort, cat; `HOME` | Завязан на готовый `out/horizon_release_arm64`; `find` с `-o` без скобок; хардкод `OUT` | средний |
| `show-dart-linux-variants.sh` | Вывести `utils_linux.h` и `os_thread_linux.h`. | cat; `HOME` | Хардкод пути | низкий |
| `show-dart-platform.sh` | Linux/android-гварды в platform/vm. | grep, head, ls; `HOME` | Хардкод пути; BRE ок | низкий |
| `show-eventhandler-apis.sh` | API TimerUtils/SimpleHashMap/TimeoutQueue. | grep; `HOME` | Хардкод пути | низкий |
| `show-file-linux-mmap.sh` | `mmap`/`File::Map` в `file_linux.cc`. | grep; `HOME` | Хардкод пути | низкий |
| `show-fml-file.sh` | `*at`/`opendir`, `paths_linux.cc`, BUILD.gn в fml. | grep, sed, ls, cat; `HOME` | `ls */paths_*.cc 2>/dev/null` глушит ошибки; хардкод пути | низкий |
| `show-freetype-dlfcn.sh` | `dlsym`/`dlopen`/RTLD в `SkFontHost_FreeType.cpp`. | sed, grep; `HOME` | Хардкод пути | низкий |
| `show-globals.sh` | Фрагменты `globals.h` (строки 118–815). | sed; `HOME` | Жёсткие номера строк хрупки; хардкод пути | низкий |
| `show-guard-sites.sh` | Головы/хвосты posix/linux bin-файлов. | sed, tail, grep; `HOME` | Хардкод пути | низкий |
| `show-ldl-sites.sh` | Фрагменты BUILD.gn для skia и dart bin. | sed; `HOME` | Жёсткие номера строк; хардкод пути | низкий |
| `show-lla-sites.sh` | Фрагменты `low_level_alloc.cc` abseil. | sed; `HOME` | Жёсткие номера строк; хардкод пути | низкий |
| `show-low-level-alloc.sh` | Размер, mmap/sbrk, `ABSL_HAVE_MMAP`. | wc, grep, sed; `HOME` | BRE ок; хардкод пути | низкий |
| `show-message-loop-linux.sh` | `message_loop_impl.h` и `message_loop_linux.{h,cc}`. | cat; `HOME` | `cat -n` ок; хардкод пути | низкий |
| `show-paths.sh` | `paths_qnx/posix`, is_horizon-блок, GetCachesDirectory. | grep, cat, sed; `HOME` | Unquoted glob `--include=*.cc` (ошибка в zsh); обход дерева ~26 ГБ | средний |
| `show-platform-linux-spots.sh` | utsname/gethostname/sysconf в `platform_linux.cc`. | sed, grep, head; `HOME` | BRE ок; хардкод пути | низкий |
| `show-process-cc.sh` | Сравнение `Process::` в process.cc и process_horizon.cc. | grep, sed; `HOME` | Хардкод пути | низкий |
| `show-process-decl.sh` | Объявления process.h и определения process_linux.cc. | grep, sed, head; `HOME` | Очень длинный regex; хардкод пути | низкий |
| `show-process-h.sh` | Строки 88–145 `process.h`. | sed; `HOME` | Жёсткие номера строк; хардкод пути | низкий |
| `show-socket-spots.sh` | accept4/fstat64/ENONET в `socket*_linux.cc`. | grep; `HOME` | Хардкод пути | низкий |
| `show-socket-un.sh` | `socket_base_linux.h`, RawAddr, `sockaddr_un` в libnx. | cat, grep, ls; `HOME`, `$HOME/devkitpro` | Хардкод `$HOME/devkitpro`; `$DEVKITPRO` не используется; `2>/dev/null` маскирует ошибку | средний |
| `show-stdio-linux.sh` | Статистика, функции, includes `stdio_linux.cc`. | grep, wc; `HOME` | Хардкод пути | низкий |

### Детали

**`show-dart-io-config.sh`**
- Жёстко зашит `$OUT=.../out/horizon_release_arm64` и `args.gn`; без
  GN-конфига вернёт пусто. Заменить на `OUT="${OUT:-$SRC/out/...}"`, `find`
  обернуть в `\( ... \)`.

**`show-paths.sh`**
- `grep -rn ... --include=*.cc --include=*.h` — при запуске через zsh
  unquoted glob ломается; кавычить: `--include='*.cc'`.
- Рекурсивный обход дерева ~26 ГБ на macOS медленный.

**`show-socket-un.sh`**
- `$HOME/devkitpro` вместо `/opt/devkitpro`; `2>/dev/null` маскирует
  неверный путь → ложный результат. Заменить на `D="${DEVKITPRO:-/opt/devkitpro}"`.

---

## 5. Проверки/утилиты: `check-*`, `verify-*`, `revert-*`, `dedupe-*`, прочие (22)

| Скрипт | Назначение | Зависимости | macOS-риски | Приоритет |
|---|---|---|---|---|
| `check-at-and-skdebug.sh` | Наличие `*at`-символов в newlib и реализаций `SkDebugf`. | grep, head, ls; `$HOME` | `D="$HOME/devkitpro"` вместо `/opt/devkitpro`; жёсткий `S="$HOME/engine/..."` | высокий |
| `check-engine-checkout.sh` | Проверяет каталоги/файлы engine-чекаута. | echo, `[ -e ]`; `SRC` | Уже поддерживает `SRC`; POSIX-совместим | средний |
| `check-horizon-guards.sh` | Проверяет guard `DART_HOST_OS_HORIZON` в Dart-файлах. | grep; `$HOME` | Жёсткий `cd "$HOME/engine/..."`, нет override `SRC` | средний |
| `check-poll-pipe.sh` | Какие poll/pipe/socketpair-символы есть в libnx. | grep, head, `aarch64-none-elf-nm`, sort; `$HOME` | `D="$HOME/devkitpro"` неверен; libnx должен быть `$DEVKITPRO/libnx` | высокий |
| `check-undefined-symbols.sh` | Считает undefined-символы Dart VM, группирует по префиксам. | `aarch64-none-elf-nm`, sed, sort, wc, grep; `$HOME` | `NM`/`OBJ` захардкожены в `$HOME/devkitpro`/`$HOME/engine` | высокий |
| `verify-engine-archive.sh` | Проверяет Embedder-API в `libflutter_engine.a`. | `aarch64-none-elf-nm`, `-ar`, ls, awk; `$HOME` | Инструменты и `OUT` захардкожены | высокий |
| `verify-engine-nro.sh` | Проверяет `.nro`/`.elf` и наличие символов. | nm, readelf, dd, sed, grep, ls, awk; `$HOME` | `/mnt/e/...` (WSL); `ls -lh --time-style=+%H:%M` — GNU-only, BSD `ls` падает | высокий |
| `verify-engine-objects.sh` | Инвентаризация `.o`/`.a` и ключевых символов. | nm, readelf, find, du, basename; `$HOME` | `NM`/`READELF`/`OUT` захардкожены | высокий |
| `verify-message-loop.sh` | `MessageLoopHorizon` и отсутствие `MessageLoopLinux` в ELF. | nm, grep, sed, ls; `$HOME` | `/mnt/e/...` (WSL); `NM` в `$HOME/devkitpro` | высокий |
| `revert-ffi-dynamic-library.sh` | `git checkout ffi_dynamic_library.cc`. | git; `$HOME` | Жёсткий путь `$HOME/engine/...`; git переносим | низкий |
| `revert-file-linux.sh` | `git checkout file_linux.cc`. | git; `$HOME` | Жёсткий путь | низкий |
| `revert-stdio-linux.sh` | `git checkout stdio_linux.cc` + подсчёт HORIZON. | git, grep; `$HOME` | Жёсткий путь | низкий |
| `dedupe-io-sources.sh` | Удаляет дубли horizon-строк в `io_impl_sources.gni`. | grep, awk, mv; `$HOME` | Жёсткий путь; awk/mv переносимы | средний |
| `dedupe-vm-sources.sh` | Удаляет дубли horizon-строк в `vm_sources.gni`. | awk, grep, mv; `$HOME` | Жёсткий путь; переносимо | средний |
| `dump-fml-file-posix.sh` | Печатает `file_posix.cc` и fd-использование. | cat, wc, grep; `$HOME` | Жёсткий путь | низкий |
| `fix-socket-base-guard.sh` | Снимает лишний HORIZON-guard, отключает git-хуки. | git, python3; `$HOME` | `python3` может отсутствовать до Xcode CLT; путь захардкожен | средний |
| `group-missing-symbols.sh` | Группирует undefined-символы из лога линковки. | grep, sed, sort, wc, cat | Дефолт `/tmp/link.log`; BSD grep/sed совместимы | низкий |
| `inspect-buildtools.sh` | Показывает `flutter/buildtools` и DEPS по clang. | ls, grep; `$HOME` | Опирается на `linux-x64`, на macOS-хосте будет `mac-*` | средний |
| `list-missing-symbols.sh` | Реально отсутствующие символы (undefined минус defined). | nm, sed, sort, comm, wc, mktemp; `$HOME` | `NM`/`OBJ` захардкожены | высокий |
| `remove-posix-at-dart.sh` | Удаляет `posix_at_horizon.cc` из Dart-дерева и gni. | grep, mv, rm; `$HOME` | Жёсткий путь | средний |
| `resolve-crash.sh` | Резолвит адреса краша через `addr2line` по `ui_app.elf`. | nm, addr2line, awk, find, printf; `$HOME` | `ELF=/mnt/e/...` (WSL); devkitPro-инструменты из `$HOME/devkitpro` | высокий |
| `survey-platform-linux.sh` | Считает частоты Linux-вызовов в `platform_linux.cc`. | grep, cut, sort, uniq; `$HOME` | Жёсткий путь; утилиты переносимы | низкий |

### Детали (высокий/средний)

**`check-at-and-skdebug.sh`**
```bash
D="${DEVKITPRO:-/opt/devkitpro}"
S="${SRC:-$HOME/engine/flutter/engine/src}"
```

**`check-poll-pipe.sh`**
```bash
D="${DEVKITPRO:-/opt/devkitpro}"
NM="$D/devkitA64/bin/aarch64-none-elf-nm"
# libnx: "$D/libnx/include/poll.h", "$D/libnx/lib/libnx.a"
```

**`check-undefined-symbols.sh`**
```bash
NM="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm"
OBJ="${OUT:-$HOME/engine/flutter/engine/src/out/horizon_release_arm64}/obj/flutter/third_party/dart/runtime/vm"
```

**`verify-engine-archive.sh`**
```bash
OUT="${OUT:-$HOME/engine/flutter/engine/src/out/horizon_release_arm64}"
NM="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm"
AR="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-ar"
```

**`verify-engine-objects.sh`**
```bash
OUT="${OUT:-$HOME/engine/flutter/engine/src/out/horizon_release_arm64}"
NM="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm"
READELF="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-readelf"
```

**`verify-engine-nro.sh`**
```bash
E="${E:-$PWD/examples/engine_link_test}"
# GNU-флаг убрать:  ls -lh "$E"/*.nro "$E"/*.elf
# либо: stat -f '%Sm %z %N' -t '%H:%M' "$E"/engine_link_test.nro "$E"/engine_link_test.elf
```

**`verify-message-loop.sh`**
```bash
E="${E:-$PWD/examples/engine_link_test/engine_link_test.elf}"
NM="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm"
```

**`list-missing-symbols.sh`**
```bash
NM="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm"
OBJ="${OUT:-$HOME/engine/flutter/engine/src/out/horizon_release_arm64}/obj/flutter/third_party/dart/runtime/vm"
```

**`resolve-crash.sh`**
```bash
ELF="${ELF:-$PWD/examples/ui_app/ui_app.elf}"
TOOLS="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin"
```

**`check-engine-checkout.sh`**
```bash
SRC="${SRC:-${ENGINE_ROOT:-$HOME/engine/flutter}/engine/src}"
```

**`check-horizon-guards.sh`**
```bash
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
cd "$SRC/flutter/third_party/dart/runtime/bin" || exit 1
```

**`inspect-buildtools.sh`**
```bash
S="${SRC:-$HOME/engine/flutter/engine/src}"
ls -la "$S/flutter/buildtools/mac-arm64" 2>/dev/null   # macOS-хост, не linux-x64
```

**`fix-socket-base-guard.sh`**
```bash
E="${ENGINE_ROOT:-$HOME/engine/flutter}"
F="$E/engine/src/flutter/third_party/dart/runtime/bin/socket_base.h"
# python3 — после xcode-select --install
```

**`dedupe-io-sources.sh`**
```bash
DART="${DART_DIR:-$HOME/engine/flutter/engine/src/flutter/third_party/dart}"
F="$DART/runtime/bin/io_impl_sources.gni"
```

**`dedupe-vm-sources.sh`**
```bash
DART="${DART_DIR:-$HOME/engine/flutter/engine/src/flutter/third_party/dart}"
F="$DART/runtime/vm/vm_sources.gni"
```

**`remove-posix-at-dart.sh`**
```bash
BIN="${DART_DIR:-$HOME/engine/flutter/engine/src/flutter/third_party/dart}/runtime/bin"
GNI="$BIN/io_impl_sources.gni"
```

---

## Рекомендации (для этапов 3 и 6)

Общие правки, применимые к большинству скриптов (не выполнялись — это аудит):

1. **Ввести переменные с дефолтами** в начале скриптов:
   ```bash
   REPO="${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
   SRC="${SRC:-$HOME/engine/flutter/engine/src}"
   OUT="${OUT:-$SRC/out/horizon_release_arm64}"
   DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
   ```
2. **Заменить `/mnt/e/flutter-libnx`** на `$REPO` / repo-relative пути.
3. **Заменить `$HOME/devkitpro`** на `$DEVKITPRO` (`/opt/devkitpro`).
4. **Портировать `setup-devkitpro.sh`** под macOS (`dkp-pacman` `.pkg`),
   `extract-make.sh`/`extract-pkgconf.sh` — не нужны на macOS.
5. **Кроссплатформенные замены:** `stat -c%s` → `stat -f%z`; убрать
   `ls --time-style`; `readlink -f` → `realpath`/python-фолбэк; кавычить
   `--include='*.cc'`; `LD_LIBRARY_PATH` → `DYLD_LIBRARY_PATH`.
6. **`.ps1` → bash** (этап 6): приоритет `build-dart-app`, `build-ui-app`,
   `build-aot-poc`, `dkp`, `nxlink-upload`, `log-listener`. Оригиналы →
   `.ps1.bak`.
7. Диагностические `show-*`/`find-*`/`probe-*` — низкий приоритет: достаточно
   параметризовать пути (`SRC`, `OUT`, `DEVKITPRO`), логика переносима.
