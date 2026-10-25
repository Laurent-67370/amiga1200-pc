#!/usr/bin/env bash
# Ressources posées à côté de l'exécutable : celles du cœur (AROS, WHDLoad, bruits des lecteurs), la police DejaVu
# (accents et symboles de l'interface) et l'icône de la fenêtre
#   scripts/resources.sh <ressources du cœur> <destination>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
rm -rf "$2"; cp -R "$1" "$2"
mkdir -p "$2/fonts"
for f in DejaVuSans.ttf DejaVuSans-Bold.ttf; do
  src=$(fc-match -f '%{file}' "${f%.ttf}" 2>/dev/null || true)
  [ -f "/usr/share/fonts/truetype/dejavu/$f" ] && src="/usr/share/fonts/truetype/dejavu/$f"
  cp "$src" "$2/fonts/$f"
done
cp "$ROOT/amiga1200/icon-192.png" "$2/icon.png"
convert "$ROOT/amiga1200/icon-192.png" -resize 64x64 -alpha on "BMP:$2/icon.bmp" 2>/dev/null || true
