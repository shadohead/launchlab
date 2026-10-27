# Firmware 0.12.1 source and build context

The historical frozen production source matches the exact `0.12.1-sticks3-flip-colors` application.
Its 49 compiled inputs match frozen source-set SHA-256 `5015e71d8eedd68ed68747539e5624e86267d857c115f79f3aede84bda08f9f1`.
Firmware 0.12.1 is under investigation after owner-reported unusually high readings and intermittent missed pulls.
It remains removed from the updater’s selections; 0.20.2 is now the recommended production release for both saved sensor profiles.
The owner identified a soldering issue in the reported intermittent behavior; the original 0.12.1 source, assets and scoped evidence are preserved.
The exact 0.12.1 source and frozen release files remain preserved.

## Build from the source ZIP

Extract the ZIP into an empty directory and work from its root.
Install Arduino CLI, then run `bash scripts/build-firmware.sh` to fetch pinned ESP32 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30 dependencies.
The M5StickS3 K150 build uses `esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi` and `-DVQF_SINGLE_PRECISION -fstack-usage`.
The stack checker runs automatically after compilation.
Host and toolchain differences can change rebuilt bytes; a rebuild must not silently replace the supplied tested application.
No ESP32 rebuild was performed during public packaging.

## Native verification

Run `bash scripts/test-firmware.sh` with a C++17 compiler and Python 3.
It runs 28 ASan/UBSan native suites and nine production harnesses.
Theme, orientation and main-layout coverage uses synthetic state and legacy schema records.
The main-layout harness extracts the real production draw block and checks conservative font envelopes; it is separate from the archived actual-M5GFX-font pixel proof.
The existing public synthetic motion fixture is unchanged.
The historical public 0.6.2 and 0.11.2 tachometer headers support the paired-estimator test.
Synthetic optical fixtures verify the isolated extra-mark correction, retained terminal/sustained acceleration and unchanged launch boundaries.
The private recorded-data suite also passed all 18 confirmed configurations; ten diagnostic configurations remain diagnostic, and private recordings are not distributed.

## Distribution and acceptance

`LICENSE`, `LICENSE-MIT`, `THIRD_PARTY_NOTICES.md` and vendored Bosch/VQF notices accompany the source.
The first-install ZIP includes the same notices and exact four-image layout.
The six release assets are the application, first-install ZIP, source ZIP, manifest, release notes and checksums.
Private motion traces, practice records, device backups, serial captures, readbacks and ELF debug output are excluded.
See the [release notes](../firmware/releases/0.12.1-sticks3-pulse-guard/README.md) for exact host/build/device acceptance and variant-specific limits.
