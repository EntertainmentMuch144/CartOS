#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -ne 1 ]; then
  echo "Usage: $0 /path/to/sdl2_miyoo-source-checkout" >&2
  exit 2
fi
SOURCE=$(CDPATH= cd -- "$1" && pwd)
if [ ! -x "$SOURCE/build-scripts/mk_miyoo.sh" ]; then
  echo "Expected build-scripts/mk_miyoo.sh under $SOURCE; use the XK9274/sdl2_miyoo source tree." >&2
  exit 2
fi

# The Union Dockerfile installs an x86_64-only CMake binary. On Apple Silicon,
# build and run that image as amd64 under Docker emulation instead of arm64.
if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] && [ -z "${DOCKER_DEFAULT_PLATFORM:-}" ]; then
  DOCKER_DEFAULT_PLATFORM=linux/amd64
  export DOCKER_DEFAULT_PLATFORM
fi

# Use CartOS's own ABI header and skip the upstream helper, whose provenance
# and licensing are unclear. The matching portable implementation is compiled
# from tools/neon_compat.c by build-miyoo.sh. GLES is unnecessary because this
# launcher uses SDL's software renderer.
CARTOS_NEON_HEADER="$ROOT/tools/neon_compat.h"
if [ ! -f "$CARTOS_NEON_HEADER" ]; then
  echo "Missing CartOS compatibility header: $CARTOS_NEON_HEADER" >&2
  exit 2
fi
cp "$CARTOS_NEON_HEADER" "$SOURCE/include/neon.h"
(cd "$SOURCE" && ./build-scripts/mk_miyoo.sh --docker --disable-gles --skip-neon --config-arg --enable-oss)

PREFIX="$ROOT/build/miyoo-sdl2-prefix"
LIBDIR="$PREFIX/lib"
INCDIR="$PREFIX/include/SDL2"
OUTDIR="$SOURCE/output"
mkdir -p "$LIBDIR" "$INCDIR" "$PREFIX/licenses/SDL2"
# Remove any helper copied by an earlier prototype. The CartOS build script
# creates its own MIT-licensed ABI-compatible replacement in this prefix.
rm -f "$LIBDIR/libneonarmmiyoo.so"
rm -f "$PREFIX/licenses/NEON-LICENSE-NOT-PROVIDED.txt"

HEADER_DIR=""
for candidate in "$SOURCE/include" "$SOURCE/sdl2/include"; do
  if [ -f "$candidate/SDL.h" ]; then HEADER_DIR="$candidate"; break; fi
done
if [ -z "$HEADER_DIR" ]; then
  echo "Could not find SDL.h in the SDL2 source tree." >&2
  exit 2
fi
CONFIG_HEADER=""
for candidate in "$HEADER_DIR/SDL_config.h" "$SOURCE/sdl2/include/SDL_config.h"; do
  if [ -f "$candidate" ]; then CONFIG_HEADER="$candidate"; break; fi
done
if [ -z "$CONFIG_HEADER" ]; then
  echo "Could not find generated SDL_config.h. Check the SDL2 build output before packaging." >&2
  exit 2
fi
cp "$HEADER_DIR"/*.h "$INCDIR/"
cp "$CONFIG_HEADER" "$INCDIR/SDL_config.h"

if [ ! -d "$OUTDIR" ]; then
  echo "The Miyoo SDL2 build completed without the documented output/ directory: $OUTDIR" >&2
  exit 2
fi
found=0
for lib in "$OUTDIR"/libSDL2-2.0.so*; do
  [ -e "$lib" ] || continue
  cp -a "$lib" "$LIBDIR/"
  found=1
done
if [ "$found" -ne 1 ]; then
  echo "No libSDL2-2.0.so* found under $OUTDIR." >&2
  exit 2
fi
# Add the unversioned link name needed by the application linker.
if [ ! -e "$LIBDIR/libSDL2-2.0.so" ]; then
  runtime=$(find "$LIBDIR" -maxdepth 1 -name 'libSDL2-2.0.so.*' -type f | head -n 1)
  if [ -z "$runtime" ]; then
    echo "Could not locate the versioned SDL2 runtime in $LIBDIR." >&2
    exit 2
  fi
  ln -s "$(basename "$runtime")" "$LIBDIR/libSDL2-2.0.so"
fi
for pattern in 'libEGL.so*' 'libGLESv2.so*' 'libjson-c.so*'; do
  for lib in "$OUTDIR"/$pattern; do
    [ -e "$lib" ] || continue
    cp -a "$lib" "$LIBDIR/"
  done
done
for license in "$SOURCE"/LICENSE "$SOURCE"/LICENSE.txt "$SOURCE"/COPYING "$SOURCE"/COPYING.txt; do
  [ -f "$license" ] || continue
  cp "$license" "$PREFIX/licenses/SDL2/"
done
printf 'Prepared target SDL2 prefix: %s\n' "$PREFIX"
