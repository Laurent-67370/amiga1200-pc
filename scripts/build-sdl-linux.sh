#!/usr/bin/env bash
# SDL2 pour l'AppImage, compilée depuis ses sources en bibliothèque statique : X11, Wayland, PulseAudio, PipeWire,
# ALSA… y sont chargés à l'exécution s'ils sont là (dlopen), au lieu d'être exigés de la machine qui lance l'AppImage.
#   scripts/build-sdl-linux.sh     → build/sdl2-linux (il faut les en-têtes de développement de ces bibliothèques)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
V=2.30.9
OUT="$ROOT/build/sdl2-linux"
[ -f "$OUT/lib/cmake/SDL2/SDL2Config.cmake" ] && { echo "SDL2 déjà là : $OUT"; exit 0; }
W="$ROOT/build/sdl2-src"
rm -rf "$W"; mkdir -p "$W"
curl -sSL "https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL2-$V.tar.gz" | tar xz -C "$W"
cmake -S "$W/SDL2-$V" -B "$W/b" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OUT" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF \
  -DSDL_X11_SHARED=ON -DSDL_WAYLAND_SHARED=ON -DSDL_PULSEAUDIO_SHARED=ON -DSDL_PIPEWIRE_SHARED=ON \
  -DSDL_ALSA_SHARED=ON -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_KMSDRM_SHARED=ON -DSDL_LIBSAMPLERATE=OFF >"$W/cmake.log"
cmake --build "$W/b" -j"$(nproc)" >"$W/build.log"
cmake --install "$W/b" >/dev/null
echo "OK : $OUT"
