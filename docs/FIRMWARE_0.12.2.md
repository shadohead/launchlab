# Firmware 0.12.2 source and build context

The frozen 0.12.2 source contains the exact 49 compiled inputs for `0.12.2-sticks3-flip-colors`.
Source-set SHA-256: `4cba14781316dbff76c3314ff3b26420a99a3c7c39be6e70c99f4a3266be61e6`.
The current updater recommends 0.20.2; this document describes the preserved 0.12.2 release.
Historical 0.12.1 payloads remain unchanged and withdrawn from the version picker.

## Build from the source ZIP

Extract into an empty directory and work from its root.
Install Arduino CLI, then run `bash scripts/build-firmware.sh` to fetch pinned ESP32 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30 dependencies.
The build uses `esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi` and `-DVQF_SINGLE_PRECISION -fstack-usage`.
The stack checker runs after compilation.
The distributed application comes from the exact pinned production build; packaging does not rebuild it.
Host and toolchain differences can change rebuilt bytes, so a rebuild must not silently replace the supplied application.

## Native verification

Run `bash scripts/test-firmware.sh` with a C++17 compiler and Python 3.
It runs 31 ASan/UBSan native suites and nine production harnesses.
The three new generated suites exercise swing-ring reset, four-reference short-burst eligibility, and completed startup Single Turn peak preservation across all period-ring positions.
The existing synthetic motion fixture is unchanged.
Historical 0.6.2 and 0.11.2 tachometer headers provide paired-estimator controls.
Theme, orientation and main-layout tests use synthetic state; the main-layout harness checks the real production draw block and conservative font envelopes.
Same-source private recorded-data checks retain 18 confirmed configurations and 12 diagnostic configurations; private recordings are not distributed or treated as calibrated speed ground truth.

## Distribution and acceptance

The source and first-install ZIPs include `LICENSE`, `LICENSE-MIT`, `THIRD_PARTY_NOTICES.md` and the vendored Bosch/VQF notices.
The six release assets are the application, first-install ZIP, source ZIP, manifest, release notes and checksums.
Private captures, practice records, backups, readbacks and ELF debug output are excluded.
The exact 0.12.2 binary has not been installed or read back.
The release notes distinguish related diagnostic-build device evidence from exact production-build software validation.
See [release notes](../firmware/releases/0.12.2-sticks3-recovery/README.md) for acceptance and limits.
