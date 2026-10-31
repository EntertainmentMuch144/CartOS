#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${MIYOO_SDL2_ROOT:-}" ]; then
  echo "Set MIYOO_SDL2_ROOT to a Miyoo-compatible SDL2 prefix (include/ and lib/)." >&2
  exit 2
fi
SDL2_LIB_DIR="$MIYOO_SDL2_ROOT/lib"
if [ ! -d "$SDL2_LIB_DIR" ] && [ -d "$MIYOO_SDL2_ROOT/lib32" ]; then
  SDL2_LIB_DIR="$MIYOO_SDL2_ROOT/lib32"
fi
if [ ! -d "$SDL2_LIB_DIR" ]; then
  echo "SDL2 library directory not found under $MIYOO_SDL2_ROOT (expected lib/ or lib32/)." >&2
  exit 2
fi
APP="$ROOT/packaging/OnionOS/App/CartOS"
# The SDL2 backend imports the NEON memcpy and pixel-scaler ABI from a helper
# with this soname. Build CartOS's own portable implementation rather than
# copying the helper from the upstream SDL2 checkout.
TARGET_CC="${CROSS_COMPILE:-arm-linux-gnueabihf-}gcc"
rm -f "$SDL2_LIB_DIR/libneonarmmiyoo.so"
rm -f "$MIYOO_SDL2_ROOT/licenses/NEON-LICENSE-NOT-PROVIDED.txt"
rm -f "$APP/licenses/NEON-LICENSE-NOT-PROVIDED.txt"
"$TARGET_CC" -shared -fPIC -O2 -std=c99 -Wall -Wextra \
  -Wl,-soname,libneonarmmiyoo.so \
  -o "$SDL2_LIB_DIR/libneonarmmiyoo.so" \
  "$ROOT/tools/neon_compat.c"
cmake -S "$ROOT" -B "$ROOT/build/miyoo" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/miyoo-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSDL2_ROOT="$MIYOO_SDL2_ROOT" \
  -DMIYOO_NEON_LIBRARY:FILEPATH="$SDL2_LIB_DIR/libneonarmmiyoo.so" \
  -DCMAKE_PREFIX_PATH="$MIYOO_SDL2_ROOT" \
  -DCMAKE_INSTALL_PREFIX=/
cmake --build "$ROOT/build/miyoo" --parallel
cmake --install "$ROOT/build/miyoo" --prefix "$APP"
mkdir -p "$APP/res" "$APP/licenses"
cp "$ROOT/assets/font_atlas.png" "$APP/res/font_atlas.png"
cp "$ROOT/assets/onionos_logo.png" "$APP/res/onionos_logo.png"
cp "$ROOT/assets/fonts/DejaVu-LICENSE.txt" "$APP/licenses/DejaVu-LICENSE.txt"
mkdir -p "$APP/lib"
found_sdl2=0
for lib in "$SDL2_LIB_DIR"/libSDL2-2.0.so*; do
  [ -e "$lib" ] || continue
  cp -a "$lib" "$APP/lib/"
  found_sdl2=1
done
if [ "$found_sdl2" -ne 1 ]; then
  echo "No libSDL2-2.0.so* runtime found in $SDL2_LIB_DIR." >&2
  exit 2
fi
cp -a "$SDL2_LIB_DIR/libneonarmmiyoo.so" "$APP/lib/"
# The common mmiyoo SDL2 builds may also ship these backend runtime libraries.
for pattern in 'libEGL.so*' 'libGLESv2.so*' 'libjson-c.so*'; do
  for lib in "$SDL2_LIB_DIR"/$pattern; do
    [ -e "$lib" ] || continue
    cp -a "$lib" "$APP/lib/"
  done
done
if [ -d "$MIYOO_SDL2_ROOT/licenses/SDL2" ]; then
  mkdir -p "$APP/licenses/SDL2"
  cp -R "$MIYOO_SDL2_ROOT/licenses/SDL2/." "$APP/licenses/SDL2/"
fi
chmod +x "$APP/bin/cartos"
printf 'Packaged app: %s\n' "$APP"
