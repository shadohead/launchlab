#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../firmware"
CLI="${ARDUINO_CLI:-arduino-cli}"
CACHE="${LAUNCHLAB_ARDUINO_CACHE:-$PWD/.arduino}"
mkdir -p "$CACHE"
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
"$CLI" compile --config-file "$CACHE/launchlab-config.yaml" \
  --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi' \
  --build-property "tools.esp32-arduino-libs.path=$LAUNCHLAB_PM_SDK" \
  --build-property 'compiler.cpp.extra_flags=-DVQF_SINGLE_PRECISION -fstack-usage -DLAUNCHLAB_MOTION_ADC_GATE=1 -DLAUNCHLAB_CPU_PM=1' \
  --build-path "$PWD/build" build-source/LaunchLabMini
python3 tools/check_stack_usage.py "$PWD/build"
