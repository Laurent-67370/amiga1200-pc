#!/usr/bin/env bash
# Cœur Amiberry (base WinUAE 6) pour PC et son frontal : native/out/<cible>/libcore.a
#
#   native/build-core.sh linux      (sur Linux x64 : gcc, g++, make, zlib)
#   native/build-core.sh windows    (sur Linux x64, compilation croisée MinGW-w64 : x86_64-w64-mingw32-g++-posix)
#
# Les sources d'Amiberry sont clonées dans native/src (même version que les versions web, Android et Mac), adaptées
# par native/patches/*.patch, puis compilées par le Makefile libretro d'Amiberry. Sur PC x64 ce Makefile compile le
# cœur sans JIT (le 68040 est interprété). Le frontal est celui de la version web (amiga1200/wasm/frontend.c,
# sous-module), compilé tel quel : native/compat remplace les quelques fonctions d'Emscripten qu'il appelle.
# native/bridge.c le relie à l'application.
set -euo pipefail
TARGET=${1:-linux}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
WEB="$ROOT/amiga1200"
AMB_REPO=https://github.com/BlitterStudio/amiberry.git
AMB_REV=ca2332df8cf914ec97c4a7c6ace38adcb3543b25   # Amiberry 8.3, comme amiga1200/wasm-amiberry/build.sh
SRC="$HERE/src-$TARGET"
OUT="$HERE/out/$TARGET"
JOBS=${JOBS:-$(nproc)}
[ -f "$WEB/wasm/frontend.c" ] || { echo "sous-module amiga1200 absent : git submodule update --init" >&2; exit 1; }

case "$TARGET" in
  linux)
    MAKEARGS=(platform=unix ARCH=x86_64)
    CC=gcc; AR=ar; NM=nm; OBJCOPY=objcopy; COMPAT="-I$HERE/compat" ;;
  windows)
    # Variante « posix » de MinGW-w64 : les fils POSIX (winpthreads) dont le cœur et le pont ont besoin
    CC=x86_64-w64-mingw32-gcc-posix; AR=x86_64-w64-mingw32-ar; NM=x86_64-w64-mingw32-nm; OBJCOPY=x86_64-w64-mingw32-objcopy
    # native/compat-win : iconv de MinGW adapté à la forme POSIX qu'attend Amiberry
    COMPAT="-I$HERE/compat -I$HERE/compat-win"
    MAKEARGS=(platform=win ARCH=x86_64 CC=$CC CXX=x86_64-w64-mingw32-g++-posix AR=$AR) ;;
  *) echo "cible inconnue : $TARGET (linux ou windows)" >&2; exit 1 ;;
esac

if [ ! -d "$SRC/.git" ]; then
  git clone --filter=blob:none "$AMB_REPO" "$SRC"
fi
git -C "$SRC" fetch -q origin "$AMB_REV" 2>/dev/null || true
git -C "$SRC" checkout -q -f "$AMB_REV"
git -C "$SRC" clean -fdq -e '*.o' -e .patches.sum -e deps
git -C "$SRC" submodule update --init libretro/libco libretro/libretro-common
for p in "$HERE"/patches/*.patch; do git -C "$SRC" apply "$p"; done
# Fonctions C++ d'Amiberry appelées par le frontal (lecteurs de disquettes…) : compilées avec le cœur, mêmes options
cp "$WEB/wasm-amiberry/amb_glue.cpp" "$SRC/libretro/amb_glue.cpp"
sed -i 's|^SOURCES_CXX := \(.*\)$(SOURCES_MISC)$|SOURCES_CXX := \1$(SOURCES_MISC) $(LIBRETRO_DIR)/amb_glue.cpp|' "$SRC/libretro/Makefile"
grep -q 'amb_glue.cpp' "$SRC/libretro/Makefile" || { echo "Makefile d'Amiberry : liste des sources introuvable" >&2; exit 1; }
# Le Makefile ne suit pas les en-têtes : si les correctifs ont changé, les objets d'une compilation précédente sont jetés
PATCH_SUM=$(cat "$HERE"/patches/*.patch "$WEB/wasm-amiberry/amb_glue.cpp" | sha256sum | cut -c1-16)
if [ "$(cat "$SRC/.patches.sum" 2>/dev/null)" != "$PATCH_SUM" ]; then
  find "$SRC" -name '*.o' -delete
  echo "$PATCH_SUM" > "$SRC/.patches.sum"
fi

mkdir -p "$OUT"
# zlib : celle du système (Linux) ou celle de MinGW ; native/compat (faux en-têtes Emscripten pour amb_glue.cpp) passe
# par ZLIB_CFLAGS, ajouté aux chemins d'en-têtes
(cd "$SRC/libretro" && make -j"$JOBS" "${MAKEARGS[@]}" ZLIB_CFLAGS="$COMPAT" ZLIB_LIBS=-lz >"$OUT/make.log" 2>&1) \
  || { tail -80 "$OUT/make.log"; exit 1; }

# Le cœur n'exporte que l'API libretro : on garde ses objets, réunis avec le frontal et le pont en une bibliothèque
cd "$OUT"
FLAGS=(-O3 -DFE_AMIBERRY -I"$HERE/compat" -I"$SRC/libretro" -I"$SRC/libretro/libretro-common/include")
# Le pont intercepte retro_set_environment (journal du cœur) : le frontal appelle notre fonction à sa place
$CC "${FLAGS[@]}" -Dretro_set_environment=amb_frontend_set_environment -c "$WEB/wasm/frontend.c" -o frontend.o
$CC "${FLAGS[@]}" -Wall -c "$HERE/bridge.c" -o bridge.o
find "$SRC/src" "$SRC/libretro" "$SRC/external" -name '*.o' | sort >objs.txt
[ "$(wc -l <objs.txt)" -ge 300 ] || { echo "compilation incomplète, voir $OUT/make.log" >&2; exit 1; }
# Le cœur apporte ses propres fonctions SDL factices (libretro/sdl_stub.cpp, noms de SDL 3) : renommées en AMB_SDL_*
# dans tous ses objets, pour ne pas se mêler à la vraie bibliothèque SDL de l'application
$NM --defined-only "$SRC/libretro/sdl_stub.o" | awk '$3 ~ /^SDL_/ {print $3" AMB_"$3}' | sort -u >sdl-renames.txt
[ -s sdl-renames.txt ] || { echo "sdl_stub.o : aucune fonction SDL trouvée" >&2; exit 1; }
rm -rf objs; mkdir objs
i=0
while read -r o; do
  i=$((i + 1))
  $OBJCOPY --redefine-syms=sdl-renames.txt "$o" "objs/$i-$(basename "$o")"
done <objs.txt
cp frontend.o bridge.o objs/
rm -f libcore.a
$AR rcs libcore.a objs/*.o
# Vérification : plus aucune fonction SDL définie par le cœur
if $NM --defined-only libcore.a 2>/dev/null | awk '$2 ~ /[TDBR]/ && $3 ~ /^SDL_/' | grep -q .; then
  echo "des fonctions SDL du cœur n'ont pas été renommées" >&2; exit 1
fi

# Ressources : ROM AROS libres (démarrage sans Kickstart), WHDLoad d'Amiberry, bruits des lecteurs
RES="$OUT/resources"
rm -rf "$RES"; mkdir -p "$RES/aros"
cp "$WEB/vendor/aros/aros-20260820-rom.bin" "$RES/aros/aros-rom.bin"
cp "$WEB/vendor/aros/aros-20260820-ext.bin" "$RES/aros/aros-ext.bin"
cp -R "$SRC/whdboot" "$RES/whdboot"
cp -R "$SRC/data/floppy_sounds" "$RES/floppy_sounds"
echo "OK : $OUT/libcore.a ($(stat -c %s libcore.a) octets)"
