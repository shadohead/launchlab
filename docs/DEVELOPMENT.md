# Development

Run these commands from the repository root unless a `cd` step is shown.

Install Arduino CLI, then run `bash scripts/build-firmware.sh`.
It installs the pinned ESP32 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30 dependencies and the verified full PM SDK into a project-local cache and compiles the 8 MB flash / OPI PSRAM target into `firmware/build`; `--hil` also builds the hardware-in-the-loop test image into `firmware/build-hil`.
On a Mac with Apple silicon, Arduino's ctags helper needs Rosetta 2.
Source builds may differ byte-for-byte from the archived binary across hosts/toolchains.
The published binary is the exact saved release, with recorded checksum.

Run `python3 scripts/firmware_proof.py host` for the firmware host suite (`scripts/firmware_tests.py`: native C++ suites with address/undefined sanitizers, production harnesses and the screen render check) and a proof of the run for your pull request; `bash scripts/test-firmware.sh` runs the same suite without the proof.
[CONTRIBUTING.md](../CONTRIBUTING.md) has the setup, the proof, what CI checks and the optional HIL run on your own device.
With Node 22 or later, run the dependency-free transfer guard and compatibility redirect checks:

```sh
cd site
npm test
npm run build
```

`site/src/` retains unhosted transfer guard modules for synthetic tests and reviewed source imports by the Beywatch site. It has no browser entry point or runnable updater UI. The maintained [USB updater](https://launchlab.beywatch.gg/update) lives in [Beywatch](https://github.com/shadohead/beywatch); its interface and deployment are maintained there.

`npm run build` copies the redirect-only `site/index.html` and unchanged `site/public/` artifacts into `site/dist/`. No JavaScript or WebSerial interface is published. The static redirect and its manual link take former GitHub Pages visitors to the exact canonical updater URL. Existing firmware and download URLs remain available.

`python3 scripts/verify-artifacts.py` verifies published checksums, exact firmware source identities and 3MF ZIP integrity. It also fails when a document shows a current release other than the one in [`site/public/firmware/versions.json`](../site/public/firmware/versions.json); `python3 scripts/render-current-release.py` rewrites those generated `current-release` blocks, so publishing or promoting a release edits no document or test by hand. GitHub Actions runs native tests, transfer guard tests, redirect checks and artifact verification before publishing this compatibility output from `main`. There are no npm dependencies to install.

Frozen M5 firmware versions are in [firmware/releases](../firmware/releases/catalog.json), including the working 0.3.0 QRE baseline and the separate 0.2.0 old TCRT5000 variant. The preserved public catalog contains the <!-- current-release:start version -->0.20.10<!-- current-release:end --> shared-profile application; withdrawn 0.12.1 remains archived. Check the canonical updater's offered version and release notes before installing.

Historical source snapshots retain their original layouts and notes; use each release's source rather than mixing headers across versions. Device backups, personal practice exports and factory flash dumps are excluded.
