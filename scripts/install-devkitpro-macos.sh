#!/usr/bin/env bash
# install-devkitpro-macos.sh — установка devkitPro (devkitA64 + libnx) на macOS.
#
# Это macOS-замена Debian-скрипта scripts/setup-devkitpro.sh.
# Требует root — запускать через sudo:
#
#     sudo scripts/install-devkitpro-macos.sh
#
# Идемпотентен: повторный запуск обновляет пакеты.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Запусти через sudo: sudo $0" >&2
  exit 1
fi

PKG_URL="https://github.com/devkitPro/pacman/releases/latest/download/devkitpro-pacman-installer.pkg"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> Скачиваю devkitPro pacman installer"
curl -fL -o "$TMP/devkitpro-pacman-installer.pkg" "$PKG_URL"

echo "==> Устанавливаю пакет (в /opt/devkitpro)"
installer -pkg "$TMP/devkitpro-pacman-installer.pkg" -target /

DKP=""
for candidate in /usr/local/bin/dkp-pacman /opt/devkitpro/pacman/bin/pacman; do
  if [ -x "$candidate" ]; then DKP="$candidate"; break; fi
done
if [ -z "$DKP" ]; then
  echo "dkp-pacman не найден после установки пакета" >&2
  exit 1
fi
echo "==> Использую $DKP"

echo "==> Обновляю базы пакетов"
"$DKP" -Syu --noconfirm

echo "==> Устанавливаю группу switch-dev"
"$DKP" -S --needed --noconfirm switch-dev

echo
echo "==> Готово. Проверь:"
echo "    aarch64-none-elf-gcc --version"
echo "    ls /opt/devkitpro/libnx/include/switch.h"
echo "Применить окружение: source ./env.sh"
