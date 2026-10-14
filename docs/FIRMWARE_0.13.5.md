# Firmware 0.13.5 build context

The shipping runtime is `0.13.5-sticks3-idle-adc-pm`, application SHA-256 `0203763a133de42bf09dd3c5a93a6cbe074c974e815d8d5aff6d2541f6a88edf`. The compiled source snapshot is frozen in the [release directory](../firmware/releases/0.13.5-sticks3-idle-adc-pm/). Use the [USB updater](https://launchlab.beywatch.gg/update) for the shipping image.

## Build from source

Install Arduino CLI, Python 3 and curl, then run `bash scripts/build-firmware.sh` from the repository root. It pins ESP32 core 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30, downloads and verifies the full PM SDK, stages current source, compiles and checks stack budgets. `ARDUINO_CLI` can select the CLI executable; `LAUNCHLAB_ARDUINO_CACHE` can select its cache directory.

The SDK release asset `launchlab-esp32s3-pm-sdk.tar.gz` has SHA-256 `5d322bcc98a71beba048d34e87fe5821b758d05ef204eb1771c0a736254b6865`. To use an already extracted SDK, set `LAUNCHLAB_PM_SDK` to its `esp32-arduino-libs` directory. SDK payload hashes are verified before compilation. The upstream Arduino stock SDK has PM disabled; adding a header define alone is insufficient.

The ESP32-S3 target uses 8 MB flash, OPI PSRAM, hardware USB CDC and default_8MB partitions. Compilation explicitly enables `LAUNCHLAB_MOTION_ADC_GATE=1` and `LAUNCHLAB_CPU_PM=1` together with VQF single precision. Source defaults remain disabled for builds that do not opt into these features. The runtime selects max 80 MHz/min 40 MHz, holds an 80 MHz lock during ADC acquisition and disables automatic light sleep.

## SDK lineage

- ESP32 Arduino lib-builder: `afa5cdd0d41d7e465d2e39867b24aef48b1c7b84`
- ESP-IDF: `b66b5448e0dfac6be135e6f0ac21010aba231180`
- Arduino: `554de56f40e68d591b945bd1a8c931798e8a9167`
- TinyUSB: `8a78804ff91271fb32008288777fad9fe73f5735`

The full qio_opi SDK was rebuilt with `CONFIG_PM_ENABLE=y` and `CONFIG_PM_PROFILING=y`; generated `CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL=1` is the only additional header difference. `CONFIG_PM_DFS_INIT_AUTO` stays disabled. The original dependency lock is preserved; stock bootloader ELFs and speech model bytes are unchanged. The asset includes verification metadata, the dependency lock and upstream license notices.

## Flash layout and checks

Bootloader: `0x0`; partitions: `0x8000`; boot selection: `0xe000`; application: `0x10000`. Ordinary updates and deliberate rollback write only the application and preserve settings/history. First install uses all four images after the updater's explicit confirmation.

Run `bash scripts/test-firmware.sh`, `python3 scripts/verify-artifacts.py`, and `npm test` in `site/` to check firmware behavior, frozen artifact identities and updater guards. `npm run build` in `site/` creates the compatibility redirect and copies the unchanged public artifacts; it builds no flashing interface.
