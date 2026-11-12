#!/usr/bin/env bash
# Fetches M5GFX at the commit the firmware builds with (tag 0.2.30, as
# firmware/setup-m5.sh), for check_ui_render on a machine without the
# Arduino setup. Usage: tools/ui_render/fetch_m5gfx.sh [DIR]
# DIR defaults to firmware/.arduino/m5-libraries/M5GFX, where build.sh looks;
# elsewhere, point $LAUNCHLAB_M5GFX at it.
set -euo pipefail
firmware=$(cd "$(dirname "$0")/../.." && pwd)
dir=${1:-$firmware/.arduino/m5-libraries/M5GFX}
commit=b863d50dd3ca57b65c61a468001925fee70b3e05
if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" = "$commit" ]; then
  echo "M5GFX 0.2.30 already at $dir"
  exit 0
fi
[ ! -e "$dir" ] || { echo "$dir exists but is not M5GFX $commit; move it away or set LAUNCHLAB_M5GFX" >&2; exit 1; }
mkdir -p "$dir"
git -C "$dir" init -q
git -C "$dir" remote add origin https://github.com/m5stack/M5GFX.git
for attempt in 1 2 3; do
  git -C "$dir" fetch -q --depth 1 origin "$commit" && break
  [ "$attempt" = 3 ] && exit 1
  sleep 5
done
git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
test "$(git -C "$dir" rev-parse HEAD)" = "$commit"
echo "M5GFX 0.2.30 ($commit) at $dir"
