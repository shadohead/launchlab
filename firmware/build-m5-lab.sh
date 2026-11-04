#!/bin/bash
# Battery lab TEST firmware (LAUNCHLAB_POWER_LAB=1): the production PM build
# plus a flash-backed battery log and switchable power profiles for rundown
# tests. Never ship it. Output: firmware/build-m5-lab/LaunchLabMini.ino.bin.
# See docs/battery/POWER_LAB.md and tools/power_lab.py.
#   --hil  the lab plus the HIL hooks (firmware/build-m5-lab-hil), so the rig's
#          accuracy scenarios can run under a lab profile such as c2
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "${1:-}" = "--hil" ]; then
  shift
  # As build-m5-hil.sh: Preferences' NVS setters are wrapped for fault tests.
  wrap="-Wl,--wrap=nvs_set_blob -Wl,--wrap=nvs_set_u8 -Wl,--wrap=nvs_set_u32"
  LAUNCHLAB_PM_BUILD_DIR="$PWD/firmware/build-m5-lab-hil" LAUNCHLAB_EXTRA_CPP_FLAGS="-DLAUNCHLAB_POWER_LAB=1 -DLAUNCHLAB_HIL=1" \
    bash firmware/build-m5-pm.sh --build-property "compiler.c.elf.extra_flags=$wrap" "$@"
  exit
fi
LAUNCHLAB_PM_BUILD_DIR="$PWD/firmware/build-m5-lab" LAUNCHLAB_EXTRA_CPP_FLAGS="-DLAUNCHLAB_POWER_LAB=1" \
  bash firmware/build-m5-pm.sh "$@"
