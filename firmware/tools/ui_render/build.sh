#!/usr/bin/env bash
# Builds the host screen renderer: the firmware's drawing code on a real M5GFX
# canvas (M5GFX's SDL platform headers; no window is opened).
# Usage: tools/ui_render/build.sh [BUILD_DIR]  ->  BUILD_DIR/ui_render
# M5GFX comes from firmware/.arduino/m5-libraries (setup-m5.sh) or $LAUNCHLAB_M5GFX.
set -euo pipefail
firmware=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:-$firmware/build-ui-render}
m5=${LAUNCHLAB_M5GFX:-$firmware/.arduino/m5-libraries/M5GFX}/src
[ -d "$m5" ] || { echo "M5GFX not found at $m5; run firmware/setup-m5.sh first" >&2; exit 1; }
command -v sdl2-config >/dev/null || { echo "sdl2-config not found (brew install sdl2 / apt install libsdl2-dev)" >&2; exit 1; }
cxx=${CXX:-c++};cc=${CC:-cc}
mkdir -p "$build/m5gfx"
# No fused multiply-add: frames must not depend on the host CPU's contraction.
flags=(-std=c++17 -O2 -ffp-contract=off -Wno-deprecated-declarations -Wno-vla-cxx-extension -DLGFX_SDL -I"$m5" $(sdl2-config --cflags))
stamp="$build/m5gfx/.done-$(cd "$m5/.." && (git rev-parse HEAD 2>/dev/null || echo unknown))"
if [ ! -f "$stamp" ]; then
  rm -f "$build"/m5gfx/*.o "$build"/m5gfx/.done-*
  "$cxx" "${flags[@]}" -w -c "$m5/lgfx/v1/lgfx_v1.cpp" -o "$build/m5gfx/lgfx_v1.o"
  for source in "$m5"/lgfx/utility/*.c $(find "$m5/lgfx/Fonts" -name '*.c'); do
    "$cc" -O0 -w -I"$m5" -c "$source" -o "$build/m5gfx/$(basename "$source" .c).o" &
  done
  wait
  touch "$stamp"
fi
render() {
  "$cxx" "${flags[@]}" -DVQF_SINGLE_PRECISION "$@" -I"$firmware/LaunchLabMini" -I"$firmware/LaunchLabRpm" -I"$firmware/tools/ui_render" -I"$firmware/tools/fx" \
    "$firmware/tools/ui_render/ui_render.cpp" "$build"/m5gfx/*.o $(sdl2-config --libs) -lz
}
render -o "$build/ui_render"
