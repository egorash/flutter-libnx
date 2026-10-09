#!/usr/bin/env bash
# --- macOS: дефолты путей (env.sh задаёт те же переменные) ---
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

set -uo pipefail
NM="$DEVKITA64/bin/aarch64-none-elf-nm"
LIBDIR="$DEVKITA64/aarch64-none-elf/lib"
NXLIB="$DEVKITPRO/libnx/lib"
D="$DEVKITA64/aarch64-none-elf/include"
NXINC="$DEVKITPRO/libnx/include"

check() {
  local s="$1"
  local found=""
  for lib in "$LIBDIR"/libc.a "$LIBDIR"/libsysbase.a "$NXLIB"/libnx.a; do
    [ -f "$lib" ] || continue
    "$NM" --defined-only "$lib" 2>/dev/null | grep -qE "^[0-9a-f]+ +[TW] +$s\$" \
      && found="$found $(basename "$lib" .a)"
  done
  if [ -n "$found" ]; then
    printf "  ja   %-22s <-%s\n" "$s" "$found"
  else
    printf "  NEIN %-22s\n" "$s"
  fi
}

echo "=== Netzwerk (fuer socket_base_posix.cc)"
for s in getifaddrs freeifaddrs if_nametoindex gai_strerror inet_ntop \
         getaddrinfo freeaddrinfo getnameinfo socket bind listen accept \
         connect setsockopt getsockopt shutdown recvmsg sendmsg; do
  check "$s"
done

echo
echo "=== Zufall / Crypto"
for s in getentropy _getentropy_r getrandom csrngGetRandomBytes randomGet; do
  check "$s"
done

echo
echo "=== Datei-/Prozess-Luecken aus der Symbolliste"
for s in fstatat pread readlinkat symlinkat utimensat pipe pthread_sigmask \
         open64 fchdir dup2 getcwd chdir; do
  check "$s"
done

echo
echo "=== libnx-Zufallsquelle: Header"
grep -rn "csrngGetRandomBytes\|randomGet" "$NXINC" 2>/dev/null | head -5

echo
echo "=== bin/ifaddrs.h im Dart-Baum"
B="$SRC/flutter/third_party/dart/runtime/bin"
sed -n '1,40p' "$B/ifaddrs.h" 2>/dev/null
