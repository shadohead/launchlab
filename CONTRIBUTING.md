# Contributing

Keep changes focused on M5StickS3 K150, analog QRE/TCRT sensing, print files and firmware validation.
The maintained browser updater lives at https://launchlab.beywatch.gg/update; browser UI changes belong in the Beywatch repository.

## Where the firmware here comes from

`firmware/LaunchLabMini` is the source of the latest firmware release.
The maintainer develops the firmware in a private repository and, at each release, syncs the source, its host test suite and the hardware-in-the-loop (HIL) tools here; `firmware/SYNC.json` lists the synced files.
Pull requests against these files are welcome.
When one is merged, the maintainer brings it into the private repository, so the next release carries it.

A few suites stay private because they replay the maintainer's own sensor recordings or test release tooling (`PRIVATE_ONLY` in `scripts/firmware_tests.py` says which and why).
The maintainer runs those when bringing your change in.
Do not edit `firmware/SYNC.json` or anything under `firmware/releases/`: those are generated or frozen.

## Setup

You need macOS or Linux (on Windows, use WSL2), git, Python 3.12 (what CI uses) and a C++17 compiler with AddressSanitizer and UndefinedBehaviorSanitizer:

- macOS: the Xcode command line tools (`xcode-select --install`) and SDL2 (`brew install sdl2`).
- Debian or Ubuntu: `sudo apt install clang zlib1g-dev libsdl2-dev python3`, then use clang: `export CXX=clang++` (CI does the same).

The screen render check draws every screen with the firmware's own code on a real M5GFX canvas, so it needs SDL2's headers and M5GFX 0.2.30.
The proof command below fetches M5GFX into `firmware/.arduino/m5-libraries/M5GFX` the first time (or run `bash firmware/tools/ui_render/fetch_m5gfx.sh`); to use a checkout of your own, set `LAUNCHLAB_M5GFX`.

## Run the suite and write the proof

Commit your change first: the proof is for a committed tree.
Then run, from the repository root:

```bash
python3 scripts/firmware_proof.py host
```

It runs every public host suite in parallel (about 130 suites: sanitizer builds of the firmware's own code, Python harnesses that compile production sketch code with fakes, and the screen render check) and takes a few minutes the first time, less once its compile cache is warm.
It refuses to start while files the suite reads differ from your commit, and it writes `launchlab-proof.json` (ignored by git) and prints the same proof as a fenced JSON block.
The proof records:

- the commit, the git object of every path the suite reads and the suite's fingerprint of them;
- how many suites ran, a hash of their names, which failed, and whether anything changed during the run;
- your compiler, Python, git, OS, SDL2 and M5GFX versions;
- the HIL section, if you add a device run (below).

When something fails, `bash scripts/test-firmware.sh -k NAME -v` runs the suites whose name contains NAME and prints their output, `--serial` runs one suite at a time with live output, and `--list` lists them all.
The suite refuses to run when a `test_*` or `check_*` file in `firmware/tests` is not part of it: add a new test to `scripts/firmware_tests.py`.
macOS's AddressSanitizer does not report leaks and Linux's does, so a test that passes on a Mac can still fail in CI: free what your test allocates.

## Put the proof in your pull request

Paste the block into the "Test proof" section of the pull request description (the template has it).
After every push, run the command again and replace the block: the proof is tied to one tree.
Changes outside what the suite reads (`site/`, `docs/`, `hardware/`) need no proof.

## What CI checks

- **Firmware test proof** (on every push and every edit of the description): the pull request changes what the suite reads, so its description holds exactly one proof, for the pushed head tree (same git objects and fingerprint), showing this tree's whole public suite passed (the same count and names) with nothing failed or changed during the run.
  The check runs the base branch's copy of `scripts/firmware_proof.py`, so a pull request cannot change how it is judged, and it flags changes to the test runner or the workflows for the maintainer to review.
- **Firmware host suite**: CI runs the whole public suite again on Linux with clang.
  This run is the one that counts: your proof shows you ran it, CI shows it passes.
- **build**: the site's tests, the published artifacts' checksums and the redirect build.
- A HIL result is shown as contributor-reported: CI has no device, so it cannot check it.
  The check fails if your HIL report says the run did not pass, and warns when it is for another tree.

## Optional: the HIL scenarios on your own M5StickS3

The HIL test image replaces the optical sensor's samples and the IMU's FIFO bytes with inputs from your computer, while every production path above them runs unchanged, and `firmware/tools/hil/run_hil.py` plays scripted pulls and checks the device's own output.
See `firmware/tools/hil/README.md` for what each scenario covers.
The run adds synthetic pulls to the device's history and changes its settings, so the command puts everything back afterwards.

**Before you start, and every time:**

1. **Back up the device with the updater**: open https://launchlab.beywatch.gg/update in desktop Chrome or Edge, connect, and under "Back up your launches" choose **Back up to a file**.
   Keep that file; the command will not start without it.
2. The command writes **only the application at `0x10000`**.
   It never erases NVS (your launches and settings) and never writes the bootloader, partition table or OTA data; do not do any of those yourself either.
3. Afterwards it **puts the stable firmware back** (the newest stable release in `site/public/firmware/versions.json`, checked against its SHA-256 and read back from flash) and restores your data.
   Never leave the HIL image on a device: it accepts test commands and never sleeps.
   If the command cannot put the device back (it says so), open the updater, install the stable release, then restore your backup with **Restore from a file**.
4. If an earlier run was interrupted and left the HIL image on the device, the command refuses to start: a backup taken now would hold that run's test data.
   Put the device back first with the updater (install the stable release, then **Restore from a file** with your backup from before that run).

You also need Arduino CLI 1.x (on a Mac with Apple silicon, also Rosetta 2: `softwareupdate --install-rosetta --agree-to-license`), and esptool and pyserial: `python3 -m pip install "esptool>=5" pyserial`.
HIL runs on macOS or Linux: the tools check with `lsof` that nothing else holds the port, so on Linux install it (`sudo apt install lsof`) and give yourself access to the port (the `dialout` group).

```bash
python3 scripts/firmware_proof.py host                 # the host suite passes first, for this commit
bash scripts/build-firmware.sh --hil                   # production and HIL images of this commit
python3 scripts/firmware_proof.py hil --port /dev/cu.usbmodem1101 --backup ~/Downloads/launchlab-backup-2026-11-09-1200.json
```

Use your device's port (`ls /dev/cu.usbmodem*` on a Mac, `/dev/ttyACM*` on Linux).
With no scenario names it runs `all`, about an hour; name scenarios to run fewer (for example `normal hard weak`), and add `--sensor fitted` or `--sensor absent` to say whether an optical sensor is attached.
Reading each image back from flash takes about three minutes, so even a single scenario takes about ten minutes in all.
In order, it checks your updater backup, takes a fresh backup over USB, writes and reads back the HIL application, runs the scenarios, then writes back and reads back the stable application and restores the fresh backup.
Logs and backups go to `~/launchlab-hil/<time>`, outside the repository: they hold your device's history, so do not attach them to a pull request.
The proof gets the application's SHA-256 as read back from flash, the scenarios' results, a hash of the serial log and what the device was put back to.

## Hardware, the site and releases

Keep firmware and hardware versions separate.
Never silently replace a published binary or frozen print package: publish a new version and checksum.
Update `site/public/firmware/catalog.json` alongside the firmware images, record the exact source and toolchain, and distinguish software evidence from physical acceptance.

Do not commit device backups, factory flash dumps, HIL run folders, personal practice logs, secrets, build caches or editor autosaves.
Preserve upstream copyright and the hardware share-alike attribution.
Inspect file paths before exporting a release archive.
