#!/usr/bin/env bash
# Holt GNU make ohne root nach ~/bin.
#
# Die WSL-Installation ist blank; make fehlt ebenso wie zuvor pkg-config.
# Quelle ist wieder das devkitPro-Image, das ohnehin lokal liegt.
set -euo pipefail

if [ "$(uname -s)" = "Darwin" ]; then
  echo "extract-make.sh extracts a Linux ELF binary from the devkitPro image (WSL)." >&2
  echo "On macOS use: make (Xcode CLT/Homebrew: brew install make)" >&2
  exit 1
fi

TAR="/mnt/e/make.tar"
BIN="$HOME/bin"

mkdir -p "$BIN"
rm -rf /tmp/makex
mkdir -p /tmp/makex
tar -xf "$TAR" -C /tmp/makex

cp -a /tmp/makex/usr/bin/make "$BIN/"
echo "installiert nach $BIN"
"$BIN/make" --version | head -1
