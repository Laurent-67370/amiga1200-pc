#!/usr/bin/env bash
# Application Windows x64, compilée depuis Linux (MinGW-w64), en .zip portable : build/amiga1200-windows-x64.zip
#   SDL2_MINGW=<SDL2-x.y.z/x86_64-w64-mingw32> scripts/package-windows.sh     (après native/build-core.sh windows)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${VERSION:-0.1}
: "${SDL2_MINGW:?chemin de SDL2 pour MinGW (paquet SDL2-devel-*-mingw, dossier x86_64-w64-mingw32)}"
B="$ROOT/build/windows"
mkdir -p "$B"
convert "$ROOT/amiga1200/icon-512.png" -define icon:auto-resize=256,64,48,32,16 "$B/amiga1200.ico"
cmake -S "$ROOT" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64.cmake" -DSDL2_MINGW="$SDL2_MINGW" \
  -DSDL2_DIR="$SDL2_MINGW/lib/cmake/SDL2" -DCORE_TARGET=windows -DAMIGA1200_VERSION="$VERSION" >/dev/null
cmake --build "$B" -j"$(nproc)"
OUT="$ROOT/build/Amiga1200"
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/amiga1200.exe" "$OUT/Amiga1200.exe"
"$ROOT/scripts/resources.sh" "$ROOT/native/out/windows/resources" "$OUT/resources"
x86_64-w64-mingw32-strip "$OUT/Amiga1200.exe"
(cd "$ROOT/build" && rm -f amiga1200-windows-x64.zip && zip -qr amiga1200-windows-x64.zip Amiga1200)
echo "OK : build/amiga1200-windows-x64.zip"
