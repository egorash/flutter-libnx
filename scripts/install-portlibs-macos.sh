#!/usr/bin/env bash
# install-portlibs-macos.sh — devkitPro-Portlibs fuer die Beispiele (macOS).
#
# Ergaenzt install-devkitpro-macos.sh: Die Gruppe switch-dev enthaelt NUR die
# Toolchain (devkitA64, libnx, switch-tools, pkg-config) - KEINE Multimedia-
# oder GPU-Portlibs. Die Beispiele linken aber gegen Mesa (EGL/glapi),
# libmpv/FFmpeg, SDL2 usw.; die liegen in der Gruppe switch-portlibs und
# muessen separat installiert werden.
#
# Braucht root - aufrufen mit sudo:
#
#     sudo scripts/install-portlibs-macos.sh
#
# Idempotent: --needed ueberspringt bereits installierte Pakete.
#
# Workaround: Das mitgelieferte gnupg (2.3.4) hat unter macOS arm64 keinen
# funktionierenden crypto-Engine (kein gpg-Binary), daher scheitert jede
# Signaturpruefung der Repo-Datenbank mit "GPGME error: Invalid crypto
# engine" bzw. "database ... is not valid". Deshalb laeuft pacman hier mit
# einer Kopie der Konfiguration, in der SigLevel global auf Never steht. Die
# Server-Eintraege bleiben unangetastet.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Запусти через sudo: sudo $0" >&2
  exit 1
fi

# Was ui_app (examples/ui_app/Makefile) an Portlibs zieht. pacman loest die
# Abhaengigkeiten (switch-freetype, switch-harfbuzz, switch-libpng, ...) selbst
# mit auf - sie muessen nicht einzeln genannt werden.
PORTLIBS=(
  switch-mesa
  switch-libdrm_nouveau
  switch-libmpv
  switch-ffmpeg
  switch-libass
  switch-libfribidi
  switch-libplacebo
  switch-dav1d
  switch-libarchive
  switch-libzstd
  switch-sdl2
  switch-bzip2
  switch-liblzma
  switch-zlib
)

PACMAN=""
for candidate in /opt/devkitpro/pacman/bin/pacman /usr/local/bin/dkp-pacman; do
  if [ -x "$candidate" ]; then PACMAN="$candidate"; break; fi
done
if [ -z "$PACMAN" ]; then
  echo "pacman nicht gefunden (erst install-devkitpro-macos.sh laufen lassen)" >&2
  exit 1
fi

CONF_ORIG="/opt/devkitpro/pacman/etc/pacman.conf"
if [ ! -f "$CONF_ORIG" ]; then
  echo "$CONF_ORIG fehlt" >&2
  exit 1
fi

# Kopie der Konfiguration mit globalem SigLevel = Never.
tmpconf="$(mktemp)"
trap 'rm -f "$tmpconf"' EXIT
sed 's/^#SigLevel = Optional$/SigLevel = Never/' "$CONF_ORIG" > "$tmpconf"
if ! grep -q '^SigLevel = Never' "$tmpconf"; then
  # Fallback, falls die Zeile anders aussieht: direkt in [options] einfuegen.
  awk '{ print } /^\[options\]$/ { print "SigLevel = Never" }' "$CONF_ORIG" > "$tmpconf"
fi

echo "==> pacman: $PACMAN"
echo "==> Signaturpruefung deaktiviert (Workaround fuer kaputtes gpg)"
echo "==> Datenbank aktualisieren"
"$PACMAN" --config "$tmpconf" -Sy --noconfirm

echo "==> Portlibs installieren"
"$PACMAN" --config "$tmpconf" -S --needed --noconfirm "${PORTLIBS[@]}"

echo
echo "==> Fertig. Pruefen:"
echo "    ls /opt/devkitpro/portlibs/switch/lib/libmpv.a"
echo "    ls /opt/devkitpro/portlibs/switch/lib/libEGL.a"
echo "    ls /opt/devkitpro/portlibs/switch/include/EGL/egl.h"
