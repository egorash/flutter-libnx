#!/usr/bin/env bash
# Holt die Referenzquellen nach third_party/ (nicht versioniert).
# Gepinnte Versionen siehe README.md / docs/feasibility.md.
#
# macOS-Port von scripts/fetch-reference.ps1 (curl statt Invoke-WebRequest).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
flutter_rev='db50e20168db8fee486b9abf32fc912de3bc5b6a'
# Passend zur libnx-Version im Container-Image devkitpro/devkita64 (4.12.0-1).
libnx_rev='v4.12.0'

mkdir -p "$root/third_party/reference"

echo "==> embedder.h @ $flutter_rev"
curl -fL --retry 3 -o "$root/third_party/reference/embedder.h" \
  "https://raw.githubusercontent.com/flutter/flutter/$flutter_rev/engine/src/flutter/shell/platform/embedder/embedder.h"

echo "==> libnx @ $libnx_rev"
libnx="$root/third_party/libnx"
if [ ! -d "$libnx/.git" ]; then
  git clone -q https://github.com/switchbrew/libnx "$libnx"
fi
git -C "$libnx" fetch -q origin
git -C "$libnx" checkout -q "$libnx_rev"

echo "Fertig."