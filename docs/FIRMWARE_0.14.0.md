# Firmware 0.14.0 build context (beta)

A newer beta, [0.15.0](FIRMWARE_0.15.0.md), adds RPM milestone celebrations.

The beta runtime is `0.14.0-sticks3-themes-pm`, application SHA-256 `e3014ec527da2fa13dc29ed3d1f4c351d9d45d55e893eb07f047235b7b0e4659`. The compiled source snapshot is frozen in the [release directory](../firmware/releases/0.14.0-sticks3-themes-pm/). It is a beta: the recommended release is now [0.18.0](FIRMWARE_0.18.0.md). It uses the same PM SDK, toolchain, bootloader and partition table as [0.13.5](FIRMWARE_0.13.5.md). Use the [USB updater](https://launchlab.beywatch.gg/update) for the shipping image.

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

## IMU acquisition

The BMI270 accelerometer is programmed to ±16 g and verified after every IMU initialization; the gyro stays at its ±2000 °/s maximum. Samples come from the header-mode FIFO (gyro 200 Hz, accelerometer 100 Hz), read in single I2C transactions and timestamped by a phase-locked sensor timeline. FIFO reads stop while flash is written: a save waits up to 250 ms for a read in progress and is otherwise deferred. Frames are timestamped at the FIFO length read. A full or stalled read is discarded and reported as a gap. Before the first bus use, startup clocks the internal I2C bus free if a reset interrupted a transfer (`I2C_RECOVERY`). Startup compares a resting FIFO frame with M5Unified's reading to prove the axis mapping and scale. Serial `p` reports `IMU_FIFO_STATUS`, `MOTION_CLIP` and recent `MOTION_GAP` records.

## RPM detection

The optical detector cannot see direction, so the cord rewind after a pull looks like a new pull. While the rearm window expects that rewind, irregular strong turns extend it; once the rewind is recognized (three turns gaining 15% in speed), only matching turns do. A second pull less than about 1.1 s after the first one's marks stop, with no rewind between, is therefore absorbed; from about 1.1 s it is measured. Sharp pulls confirm on monotonic windows, a missed mark near the peak waits for the next marks instead of ending the burst, and lost ADC samples keep a finished result. All recorded replays keep identical launch counts and RPM.

## USB serial

Output is drained with a bounded wait and never discarded; the Arduino core's `Serial.flush()` empties the whole transmit buffer after a 10 ms host pause, so it is not used. Backup `K`, restore `R` and the status letters run at once. Every other single-letter command (power off, sleep, settings, session, buttons, storage checks) runs only when it stands alone: after a newline or 20 ms with the receive queue empty, and followed by a newline or the same quiet. `PERF` reports `usb_drain_timeouts` and `usb_rejected_commands`.

## Custom themes

Every UI color comes from a palette with eight roles: background, text, muted, accent, number, warning, ready and alert. The other colors are derived from these. The ten built-in themes map to palettes that render pixel-identical to 0.13.9. A custom theme is a name of up to 14 characters plus the eight roles. Validation requires a dark background and at least 4.5:1 contrast for text roles, and a lower bound for muted text. Custom themes are stored in their own NVS namespace, travel in backup format 2 (format 1 backups still restore), and a damaged record falls back to the default theme. The website theme editor previews the screens with the firmware's own drawing code and uploads over USB.

## Flash layout and checks

Bootloader: `0x0`; partitions: `0x8000`; boot selection: `0xe000`; application: `0x10000`. Ordinary updates and deliberate rollback write only the application and preserve settings/history. First install uses all four images after the updater's explicit confirmation.

Run `bash scripts/test-firmware.sh`, `python3 scripts/verify-artifacts.py`, and `npm test` in `site/` to check firmware behavior, frozen artifact identities and updater guards. `npm run build` in `site/` creates the compatibility redirect and copies the unchanged public artifacts; it builds no flashing interface.
