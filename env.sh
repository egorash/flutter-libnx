#!/usr/bin/env bash
# Окружение macOS-порта flutter-libnx.
#
# Применение (в текущей оболочке bash/zsh):
#
#     source ./env.sh
#
# Ничего не устанавливает, не меняет ~/.zshrc и не содержит секретов.
# Идемпотентен: повторный `source` безопасен.

# Расположение этого файла (работает и в bash, и в zsh при source).
_env_self="${BASH_SOURCE[0]:-$0}"
export FLUTTER_LIBNX_ROOT="${FLUTTER_LIBNX_ROOT:-$(cd "$(dirname "$_env_self")" && pwd)}"
unset _env_self

# --- devkitPro -----------------------------------------------------------
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

# --- Depot Tools / Flutter SDK ------------------------------------------
export DEPOT_TOOLS="${DEPOT_TOOLS:-$HOME/depot_tools}"
export FLUTTER_HOME="${FLUTTER_HOME:-$HOME/flutter-3.41.6}"

# --- Engine checkout / build --------------------------------------------
export SRC="${SRC:-$HOME/engine/flutter/engine/src}"
export OUT="${OUT:-$SRC/out/horizon_release_arm64}"
export JOBS="${JOBS:-4}"

# depot_tools не обновляем самовольно (важна воспроизводимость сборки).
export DEPOT_TOOLS_UPDATE="${DEPOT_TOOLS_UPDATE:-0}"

# --- PATH ----------------------------------------------------------------
# depot_tools и Flutter — впереди системных; bison keg-only из Homebrew.
_bison_bin="/opt/homebrew/opt/bison/bin"
export PATH="$DEPOT_TOOLS:$FLUTTER_HOME/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
if [ -d "$_bison_bin" ]; then
  case ":$PATH:" in
    *":$_bison_bin:"*) ;;
    *) export PATH="$_bison_bin:$PATH" ;;
  esac
fi
unset _bison_bin

# --- Необязательный тихий режим -----------------------------------------
if [ "${FLUTTER_LIBNX_QUIET:-0}" != "1" ]; then
  echo "flutter-libnx env: DEVKITPRO=$DEVKITPRO"
  echo "                  FLUTTER_HOME=$FLUTTER_HOME"
  echo "                  SRC=$SRC"
fi
