# Development

Run these commands from the repository root unless a `cd` step is shown.

Install Arduino CLI, then run `bash scripts/build-firmware.sh`. It installs the pinned ESP32 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30 dependencies and the verified full PM SDK into a project-local cache and compiles the 8 MB flash / OPI PSRAM target. Source builds may differ byte-for-byte from the archived binary across hosts/toolchains. The published binary is the exact saved release, with recorded checksum.

Run `bash scripts/test-firmware.sh` for 39 native C++ suites with address/undefined sanitizers and 15 production harnesses. Website development requires Node 24+:

```sh
cd site
npm ci
npm test
npm run dev
npm run build
```

`python3 scripts/verify-artifacts.py` verifies published checksums and 3MF ZIP integrity. GitHub Actions runs the native tests, updater tests and artifact checks before deploying Pages from `main`.

Frozen M5 firmware versions are in [firmware/releases](../firmware/releases/catalog.json), including the working 0.3.0 QRE baseline and the separate 0.2.0 old TCRT5000 variant.
The web updater recommends the 0.13.5 shared-profile application and preserves earlier selections; withdrawn 0.12.1 remains archived.
Historical source snapshots retain their original layouts and notes; use each release's source rather than mixing headers across versions.
Device backups, personal practice exports and factory flash dumps are excluded.

