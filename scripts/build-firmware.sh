#!/usr/bin/env bash
# Builds the M5StickS3 firmware in firmware/LaunchLabMini with the pinned
# toolchain (ESP32 3.3.0, M5Unified 0.2.23, M5GFX 0.2.30, the PM SDK).
#
#   bash scripts/build-firmware.sh          # production image in firmware/build
#   bash scripts/build-firmware.sh --hil    # also the HIL test image in firmware/build-hil
#
# The HIL image (LAUNCHLAB_HIL=1) adds host-driven sensor inputs and fault hooks
# for firmware/tools/hil; never ship it or leave it on a device (CONTRIBUTING.md).
# Each build directory gets build-info.json: the tree fingerprint of the source
# it was built from (scripts/firmware_tests.py), which the HIL proof checks.
set -euo pipefail
hil=0
case "${1:-}" in
  --hil) hil=1 ;;
  '') ;;
  *) echo "usage: bash scripts/build-firmware.sh [--hil]" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/../firmware"
CLI="${ARDUINO_CLI:-arduino-cli}"
CACHE="${LAUNCHLAB_ARDUINO_CACHE:-$PWD/.arduino}"
mkdir -p "$CACHE"
tree=$(python3 ../scripts/firmware_tests.py --fingerprint)
cat > "$CACHE/launchlab-config.yaml" <<YAML
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: $CACHE/data
  downloads: $CACHE/downloads
  user: $CACHE/user
YAML
"$CLI" core update-index --config-file "$CACHE/launchlab-config.yaml"
"$CLI" core install esp32:esp32@3.3.0 --config-file "$CACHE/launchlab-config.yaml"
"$CLI" lib install 'M5Unified@0.2.23' 'M5GFX@0.2.30' --config-file "$CACHE/launchlab-config.yaml"
# Arduino's sketch preprocessor runs its ctags helper, an Intel binary on macOS.
ctags="$CACHE/data/packages/builtin/tools/ctags/5.8-arduino11/ctags"
if [[ -x $ctags ]] && ! "$ctags" --version >/dev/null 2>&1; then
  echo "Arduino's ctags helper ($ctags) cannot run here." >&2
  echo "On a Mac with Apple silicon, install Rosetta 2: softwareupdate --install-rosetta --agree-to-license" >&2
  exit 1
fi
if [[ -z "${LAUNCHLAB_PM_SDK:-}" ]]; then
  sdk_archive="$CACHE/launchlab-esp32s3-pm-sdk.tar.gz"
  if [[ ! -f "$sdk_archive" ]]; then
    curl --fail --location --output "$sdk_archive" 'https://github.com/shadohead/launchlab/releases/download/firmware-v0.13.5/launchlab-esp32s3-pm-sdk.tar.gz'
  fi
  python3 - "$sdk_archive" <<'CHECK'
import sys,hashlib
from pathlib import Path
assert hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest() == '5d322bcc98a71beba048d34e87fe5821b758d05ef204eb1771c0a736254b6865', 'PM SDK archive checksum mismatch'
CHECK
  mkdir -p "$CACHE/pm-sdk"
  tar -xzf "$sdk_archive" -C "$CACHE/pm-sdk"
  LAUNCHLAB_PM_SDK="$CACHE/pm-sdk/esp32-arduino-libs"
fi
# Verify the complete SDK, not just a header define on stock libraries.
python3 - "$LAUNCHLAB_PM_SDK" <<'CHECK'
import sys,json,hashlib
from pathlib import Path
sdk=Path(sys.argv[1])
manifest=json.loads((sdk/'sha256-manifest.json').read_text())
for name,digest in manifest.items():
    assert hashlib.sha256((sdk/name).read_bytes()).hexdigest()==digest,name
header=(sdk/'esp32s3/qio_opi/include/sdkconfig.h').read_text()
assert '#define CONFIG_PM_ENABLE 1' in header
assert '#define CONFIG_PM_PROFILING 1' in header
assert '#define CONFIG_PM_DFS_INIT_AUTO 1' not in header
CHECK
# Fresh source staging prevents stale files from previous revisions.
rm -rf build-source/LaunchLabMini
mkdir -p build-source/LaunchLabMini
cp LaunchLabMini/*.ino LaunchLabMini/*.h build-source/LaunchLabMini/
cp -R LaunchLabMini/src build-source/LaunchLabMini/

# build <dir> <extra C++ flags> [arduino-cli arguments]
build() {
  local dir=$1 extra=$2
  shift 2
  rm -f "$dir/build-info.json"
  # -mtext-section-literals keeps each function's literal pool beside its code:
  # the sketch is one object with over 256 KB of code since 0.17's effects, and
  # pooled literals end up beyond an l32r's reach of the last functions.
  "$CLI" compile --config-file "$CACHE/launchlab-config.yaml" \
    --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi' \
    --build-property "tools.esp32-arduino-libs.path=$LAUNCHLAB_PM_SDK" \
    --build-property "compiler.cpp.extra_flags=-DVQF_SINGLE_PRECISION -fstack-usage -mtext-section-literals -DLAUNCHLAB_MOTION_ADC_GATE=1 -DLAUNCHLAB_CPU_PM=1${extra:+ $extra}" \
    --build-path "$PWD/$dir" "$@" build-source/LaunchLabMini
  # loop() runs on an 8 KB task; 0.11.0 overflowed it. -fstack-usage does not change code.
  python3 tools/check_stack_usage.py "$PWD/$dir"
  # Static internal RAM taken before boot: too much leaves the ADC's DMA without memory.
  python3 tools/check_static_ram.py "$PWD/$dir"
  [[ $(python3 ../scripts/firmware_tests.py --fingerprint) == "$tree" ]] || {
    echo "The source changed during the build; build again." >&2
    exit 1
  }
  python3 ../scripts/firmware_tests.py --build-info "$dir" not-gated --expect-fingerprint "$tree"
}

build build ''
if [[ $hil == 1 ]]; then
  # Preferences' NVS setters are wrapped so tests can force failed or slow writes.
  build build-hil '-DLAUNCHLAB_HIL=1' \
    --build-property 'compiler.c.elf.extra_flags=-Wl,--wrap=nvs_set_blob -Wl,--wrap=nvs_set_u8 -Wl,--wrap=nvs_set_u32'
  # The internal RAM the HIL image takes beyond production's, for the songs scenario's heap floor.
  python3 tools/hil_overhead.py build-hil build
fi
