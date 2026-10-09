#!/usr/bin/env bash
# setup-macos.sh — установка окружения macOS-порта flutter-libnx.
#
# Идемпотентен: повторный запуск ничего не ломает.
# devkitPro требует sudo и ставится скриптом
# scripts/install-devkitpro-macos.sh — автоматически при интерактивном
# запуске или вручную.
#
# Использование:
#   ./scripts/setup-macos.sh [--no-devkitpro]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLUTTER_VERSION="3.41.6"
FLUTTER_HOME="${FLUTTER_HOME:-$HOME/flutter-3.41.6}"
WITH_DEVKITPRO=1

for arg in "$@"; do
  case "$arg" in
    --no-devkitpro) WITH_DEVKITPRO=0 ;;
    -h|--help) echo "usage: $0 [--no-devkitpro]"; exit 0 ;;
    *) echo "неизвестный аргумент: $arg" >&2; exit 2 ;;
  esac
done

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mВНИМАНИЕ:\033[0m %s\n' "$*"; }
ok()   { printf '  \033[1;32mok\033[0m: %s\n' "$*"; }

# 1. Homebrew -------------------------------------------------------------
if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew не найден. Установи его и повтори:" >&2
  # shellcheck disable=SC2016
  echo '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"' >&2
  exit 1
fi
log "Homebrew: $(brew --version | head -1)"

# 2. Пакеты ---------------------------------------------------------------
log "Homebrew-пакеты"
BREW_PKGS=(cmake ninja python3 git pkg-config bison shellcheck wget)
for p in "${BREW_PKGS[@]}"; do
  if brew list --versions "$p" >/dev/null 2>&1; then
    ok "$p"
  else
    brew install "$p"
  fi
done

# 3. depot_tools ----------------------------------------------------------
if [ -d "$HOME/depot_tools/.git" ]; then
  ok "depot_tools: $HOME/depot_tools"
else
  log "Клонирую depot_tools в $HOME/depot_tools"
  git clone --depth=1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git \
    "$HOME/depot_tools"
fi

# 4. Flutter SDK ----------------------------------------------------------
if [ -d "$FLUTTER_HOME/bin" ]; then
  ok "Flutter SDK: $FLUTTER_HOME"
else
  log "Клонирую Flutter $FLUTTER_VERSION в $FLUTTER_HOME"
  git clone https://github.com/flutter/flutter.git -b "$FLUTTER_VERSION" "$FLUTTER_HOME"
fi

# 5. devkitPro ------------------------------------------------------------
if [ -d /opt/devkitpro/devkitA64 ]; then
  ok "devkitPro: /opt/devkitpro"
elif [ "$WITH_DEVKITPRO" -eq 1 ]; then
  if [ -t 0 ]; then
    log "Устанавливаю devkitPro (потребуется пароль sudo)"
    sudo "$ROOT/scripts/install-devkitpro-macos.sh"
  else
    warn "devkitPro не установлен. Запусти вручную:"
    echo "    sudo $ROOT/scripts/install-devkitpro-macos.sh"
  fi
else
  warn "devkitPro пропущен (--no-devkitpro)"
fi

# 6. Окружение ------------------------------------------------------------
log "Применение окружения"
echo "    source $ROOT/env.sh"

# 7. Чеклист версий -------------------------------------------------------
log "Проверка версий"
FLUTTER_LIBNX_QUIET=1
# shellcheck disable=SC1091
source "$ROOT/env.sh" || true

check() {
  local name="$1"; shift
  if command -v "$name" >/dev/null 2>&1; then
    printf '  %-24s %s\n' "$name" "$(command -v "$name")"
  else
    printf '  %-24s \033[1;31mMISSING\033[0m\n' "$name"
  fi
}
check brew
check cmake
check ninja
check gn
check bison
check shellcheck
check python3
check flutter
check aarch64-none-elf-gcc
check nxlink

echo
echo "Версии:"
aarch64-none-elf-gcc --version 2>/dev/null | head -1 || true
ninja --version 2>/dev/null | sed 's/^/  ninja /' || true
python3 --version 2>/dev/null || true
flutter --version 2>/dev/null | head -1 || true

log "Готово."
