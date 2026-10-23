#!/usr/bin/env bash
# Contact sheets of loaded effects beside the built-in effects they port:
# per effect, a row of the native frames over a row of the loaded file's,
# every STEP_MS (default 250) over the effect, on the real recap.
# Usage: firmware/tools/fx/render_fx.sh OUT_DIR [FILE.fx...] (default: the six ports)
# Needs M5GFX (firmware/setup-m5.sh), SDL2 headers and ffmpeg. Frames are
# rendered in a scratch directory next to OUT_DIR and deleted afterwards.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
firmware=$(cd "$here/../.." && pwd)
out=${1:?usage: render_fx.sh OUT_DIR [FILE.fx...]};shift
files=("$@");[ ${#files[@]} -gt 0 ] || files=("$here"/effects/{orbit,flames,sparkles,shockwave,slash,rupture}.fx)
build=$firmware/build-ui-render
bash "$firmware/tools/ui_render/build.sh" "$build" >/dev/null
m5=${LAUNCHLAB_M5GFX:-$firmware/.arduino/m5-libraries/M5GFX}/src
c++ -std=c++17 -O2 -ffp-contract=off -DLLFX_COUNTS -Wno-deprecated-declarations -Wno-vla-cxx-extension -DLGFX_SDL -I"$m5" $(sdl2-config --cflags) \
  -DVQF_SINGLE_PRECISION -I"$firmware/LaunchLabMini" -I"$firmware/LaunchLabRpm" -I"$firmware/tools/ui_render" -I"$here" \
  "$here/render_fx.cpp" "$build"/m5gfx/*.o $(sdl2-config --libs) -o "$build/render_fx"
mkdir -p "$out"
frames=$(mktemp -d "$out/.frames.XXXXXX")
trap 'rm -rf "$frames"' EXIT
"$build/render_fx" "$frames" "${files[@]}"
step=$(( ${STEP_MS:-250} / 50 ))
for file in "${files[@]}"; do
  name=$(sed -n 's/^effect "\(.*\)"/\1/p' "$file" | head -1)
  rows=()
  for kind in native loaded; do
    ls "$frames/${name}_${kind}_"*.ppm >/dev/null 2>&1 || continue
    count=$(ls "$frames/${name}_${kind}_"*.ppm | wc -l | tr -d ' ')
    tiles=$(( (count + step - 1) / step ))
    ffmpeg -loglevel error -y -framerate 20 -i "$frames/${name}_${kind}_%03d.ppm" \
      -vf "select='not(mod(n\\,$step))',tile=${tiles}x1:padding=2:color=0x303030" -frames:v 1 "$frames/strip_${kind}.png"
    rows+=(-i "$frames/strip_${kind}.png")
  done
  if [ ${#rows[@]} -eq 4 ]; then
    ffmpeg -loglevel error -y "${rows[@]}" -filter_complex "[0:v]pad=iw:ih+4:0:0:color=0x303030[a];[a][1:v]vstack=inputs=2" "$out/${name}-sheet.png"
  else
    cp "$frames/strip_loaded.png" "$out/${name}-sheet.png"
  fi
  rm -f "$frames"/strip_*.png
done
cp "$frames/frames.csv" "$frames/prims.csv" "$out/"
echo "Wrote sheets to $out"
