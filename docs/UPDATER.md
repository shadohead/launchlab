# Update, repair and rollback

Use the [LaunchLab USB updater](https://launchlab.beywatch.gg/update). It is the only maintained browser updater; the former GitHub Pages address redirects there. Check the selected target version and release notes before installing. This repository preserves the [0.20.4 release notes](../firmware/releases/0.20.4-sticks3-songs-pm/README.md), [build context](FIRMWARE_0.20.4.md) and [firmware catalog](../site/public/firmware/versions.json) as source and artifact references.

Ordinary Update blocks downgrades and unknown or incomplete applications. Repair / rollback and First install require their own explicit confirmations. Current byte verification does not establish physical rollback, stock reinstall or reliable pull measurement. Follow the procedures below and keep private backups.

## Stable and beta firmware

Every release is published in one of two channels.
A **beta** has passed the automated gates: the host test suite and the hardware-in-the-loop suite on real M5StickS3 devices.
A **stable** release has also been hand-tested by the maintainer against a fixed checklist (real pulls, tilt replay, browser install, backup and restore, settings across a reboot, battery sleep, wake and power off, and every new feature).
The updater installs stable by default.
Its **Beta** checkbox, remembered in that browser, offers the newest beta while one is newer than stable.
Moving from a beta to a newer stable is an ordinary Update; going back from a beta to an older stable is a deliberate Repair / rollback, which writes only the application and keeps your data.
Beta GitHub releases are marked as pre-releases.

[`site/public/firmware/versions.json`](../site/public/firmware/versions.json) records the channels: `latest` and `releases` list stable releases only, `betaReleases` lists betas, and `beta` names the newest beta while it is newer than `latest`.
A release's frozen manifest and catalog record the channel it was first published in as `published_channel`; a beta that is later promoted moves to `releases` without changing its frozen files.

## Historical 0.12.1 investigation

Firmware **0.12.1 was placed under investigation** after owner-reported unusually high readings and intermittent missed pulls.
It was removed from the updater’s selections, with its source, tag and release files preserved.
At that time the updater defaulted to the previous **0.11.2** release, runtime `0.11.2-sticks3-score-preview`, without claiming it fixes these symptoms.
Ordinary Update blocks downgrades; deliberate Repair / rollback still requires explicit confirmation.
At that time, earlier **0.11.1**, **0.10.22**, **0.10.21**, **0.10.14**, **0.10.11**, **0.10.7** and **0.10.1** remained selectable; the withdrawn 0.11.0 is recognised as an installed application so it can be updated, but is not offered.
These files target M5StickS3 K150 with an analog QRE1113 or TCRT5000 sensor: after installing, choose Menu → Settings → Sensor profile.
The old TCRT 0.2.0 build remains separate.
**0.11.1** is the user-checked prototype revision.
It adds saved themes (Classic, Mint, Amber, Violet), a personal-best effect on the recap, and USB backup and restore of history, personal bests, appearance and settings (`K` export, `R` restore).
It fixes 0.11.0, which restarted about once a second after boot (a loop-task stack overflow) and was withdrawn.
One firmware still serves both sensors through the saved **Sensor profile** setting (QRE / Standard with an 80-count mark floor, TCRT5000 with 60).
It was installed app-only on the owner's M5StickS3 K150: no restarts in a 120 s soak, history kept, a backup and restore round trip identical, and the screen, themes and pulls confirmed.
The detector, sampling, thresholds and saved history format are unchanged from 0.10.22, which keeps measuring pulls that start right after an automatic save.
Battery endurance, long-session reliability, absolute RPM/angle calibration, physical browser transfer and first install/rollback, and stock recovery remain separate checks.
See [recovery guidance](RECOVERY.md).

0.12.1 discounts an isolated extra optical mark and offers ten accent themes, a mounted Flip mapping, a clearer RPM/level screen, five personal-best effects and optional sound.
Both saved sensor profiles use one application.
Older released identities and the previously installed 0.12.0 identity remain recognized.
Complete post-write MD5 verification and deliberate repair for a different build sharing the same version are retained.

## Update an existing LaunchLab

1. Save important history/settings and a private full-device backup before flashing. There is no automatic browser backup. Do not upload a full flash dump to an issue.
2. Open the USB updater in desktop Chrome or Edge with a data cable. Read the selected target version and confirm K150.
3. Use the default **Update** mode for a normal application update. Connect USB with the M5 powered on and close other serial apps. The updater attempts to enter download mode automatically. If connection fails, hold the side Power/Reset button with USB connected until the green LED flashes, release it, then retry and select the download-mode port.
4. Connect. The updater checks every downloaded file's SHA-256, the bundle's fixed partition map, the detected chip and 8 MB flash capacity, the actual device partition and boot-selection bytes, and the installed application identity plus complete device MD5. The device label remains necessary for K150 confirmation.
5. An older target is blocked. An unknown or incomplete application is blocked. If the selected exact application is already installed, no flash is written.
6. After a verified write/no-op, restart automatically or use the side button when prompted. Check the runtime version, history/settings, references, READY state and an ordinary pull afterward.

## Repair or intentional rollback

Use **Repair / rollback** only after backup and a deliberate version choice. The checkbox names the selected target; changing the version or mode, or completing an attempt, clears consent. Repair can rewrite an interrupted application or intentionally install the previous 0.10.1. It writes **only the application at 0x10000**, retaining the data and boot areas; it cannot guarantee that older firmware understands newer data.

Both Update and Repair stop for a different partition layout or boot selection. This updater only supports its original app0 boot layout. It does not guess which slot an external OTA tool selected. Do not use First install merely to bypass these checks.

If USB disconnects during an app-only write, success is not reported. Re-enter download mode and reconnect. Normal Update will stop if the application is incomplete; deliberately confirm Repair for the intended target. Each attempt releases its connection before retry. Keep USB connected through device verification. A device MD5 failure is an error, not success. For an interrupted First install or persistent layout/connection failure, follow the [recovery guide](RECOVERY.md).

## First install

First install requires a separate explicit backup/factory-overwrite confirmation. It writes bootloader **0x0**, partitions **0x8000**, boot support **0xe000**, and application **0x10000**. No mode uses erase-all. It changes the layout and can make prior factory applications/data inaccessible even though these write ranges do not directly address the new NVS region.

The partition-table image is limited to its 4 KB slot ending at `0x9000`; the gap before boot support contains NVS and is not a writable part of that slot. The downloaded map and its embedded MD5 must match the reviewed 8 MB layout. Detected capacity must be exactly 8 MB in every mode. Checksums and capacity checks cannot prove the board model or guarantee interruption recovery.

## Return to M5Stack software

Follow [Recovery and M5Stack software reinstall](RECOVERY.md) for the official M5Burner/StickS3 route, backup limits and failed-update steps. Vendor software reinstall does not restore original data/settings/calibration or identify the exact as-shipped demo. No vendor binary is hosted here; physical stock reinstall remains unverified.

## Historical 0.12.1 verification scope

The exact 0.12.1 application is 808064 bytes, SHA-256 `a13d590f11c3c8e8f8ab84a0b8ebd8d69761cf1046e2a0f9fd1d9c162d01ad41`.
Its native app-only install, complete readback and complete backup retention passed.
Initial owner feedback while pulling during a preceding reading was positive.
The owner later reported unusually high readings and intermittent missed pulls; the earlier scoped checks do not establish resolution of these symptoms.
The preceding variants passed palette/Flip/reset and small-status main-screen review with identical UI source.
Twenty-eight native suites, nine production harnesses and updater software tests cover the production source and update guards.
Physical browser transfer, four-image first install, rollback, reference-bearing restore, physical PB/Random sequencing and endurance remain outside acceptance.
See [release notes](../firmware/releases/0.12.1-sticks3-pulse-guard/README.md).

The browser connection now uses esptool-js's automatic reset selection, including native USB-Serial/JTAG reset, instead of requiring an already-running ROM bootloader. Native esptool and the deployed browser both successfully connected to the affected device, started the stub and detected ESP32-S3 with 8 MB flash. The browser's subsequent partition read timed out without writing flash. Preflight reads now request 256-byte packets with one unacknowledged packet at a time and consume/verify the trailing MD5 described in [Espressif's protocol](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/serial-protocol.html#reading-flash). Tests cover truncated/oversized packets, missing or corrupt digests, timeouts and consecutive reads. Physical acceptance of this read adjustment is pending. Software tests also require automatic reset before inspection/writes and release the port after failure.

The current storage schema identifiers remain unchanged, while save/validation behavior has changed since 0.10.7. This is static compatibility evidence, not a physical rollback guarantee. Older releases lack some current startup/timing repairs or the CPU policy. Preserve your backup until the intended release passes a real check.
