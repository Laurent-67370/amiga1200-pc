#!/usr/bin/env bash
# Application Linux x64 en AppImage (un seul fichier exécutable) : build/amiga1200-linux-x86_64.AppImage
#   scripts/package-linux.sh        (après native/build-core.sh linux ; il faut appimagetool, sinon il est téléchargé)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${VERSION:-0.1}
B="$ROOT/build/linux"
"$ROOT/scripts/build-sdl-linux.sh"
cmake -S "$ROOT" -B "$B" -DCORE_TARGET=linux -DAMIGA1200_VERSION="$VERSION" -DSDL2_DIR="$ROOT/build/sdl2-linux/lib/cmake/SDL2" >/dev/null
cmake --build "$B" -j"$(nproc)"
"$ROOT/scripts/resources.sh" "$ROOT/native/out/linux/resources" "$B/resources"

APP="$ROOT/build/AppDir"
rm -rf "$APP"; mkdir -p "$APP/usr/bin" "$APP/usr/share/applications" "$APP/usr/share/icons/hicolor/512x512/apps"
cp "$B/amiga1200" "$APP/usr/bin/"
cp -R "$B/resources" "$APP/usr/bin/resources"
cp "$ROOT/amiga1200/icon-512.png" "$APP/usr/share/icons/hicolor/512x512/apps/amiga1200.png"
cp "$ROOT/amiga1200/icon-512.png" "$APP/amiga1200.png"
cat >"$APP/amiga1200.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Amiga 1200
Comment=Émulateur Amiga (cœur Amiberry)
Exec=amiga1200
Icon=amiga1200
Categories=Game;Emulator;
DESK
cp "$APP/amiga1200.desktop" "$APP/usr/share/applications/"
cat >"$APP/AppRun" <<'RUN'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/usr/bin/amiga1200" "$@"
RUN
chmod +x "$APP/AppRun"

# AppImage de type 2 : l'amorce (runtime) suivie du dossier en SquashFS (ce que fait appimagetool)
RT="$ROOT/build/runtime-x86_64"
[ -s "$RT" ] || curl -sSL -o "$RT" https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
mksquashfs "$APP" "$ROOT/build/AppDir.sqfs" -root-owned -noappend -comp zstd -quiet >/dev/null
cat "$RT" "$ROOT/build/AppDir.sqfs" >"$ROOT/build/amiga1200-linux-x86_64.AppImage"
chmod +x "$ROOT/build/amiga1200-linux-x86_64.AppImage"
rm -f "$ROOT/build/AppDir.sqfs"
echo "OK : build/amiga1200-linux-x86_64.AppImage"
