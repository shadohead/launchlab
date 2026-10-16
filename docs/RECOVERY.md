# Recovery and M5Stack software reinstall

For **M5StickS3 / StickS3, SKU K150**, with ESP32-S3 and 8 MB flash. Do not use StickC, StickC Plus/Plus2, CoreS3 or a generic ESP32-S3 image. Chip/USB identity and flash capacity cannot establish the exact board model; check the product label.

**Physical browser recovery, stock reinstall and rollback remain unverified on our device.** Download mode provides a recovery route when available; it does not guarantee recovery from every interruption, electrical fault or chip security configuration. There is no zero-brick promise.

## If a LaunchLab update failed

1. Read the error and save the installation log. Before a write starts, the updater reports that this attempt wrote no flash. A write or MD5-verification failure may leave incomplete software.
2. Connect USB with a known data cable. Hold the side Power/Reset button until the internal green LED flashes. This is [M5Stack's K150 download-mode procedure](https://docs.m5stack.com/en/core/StickS3). Close other serial tools and select the download-mode port again.
3. If an **app-only Update** was interrupted, select the intended version and deliberately confirm **Repair / rollback** after backup. Repair writes app0 at `0x10000` only after checking the device's partition table and boot selection.
4. If either layout check fails, **stop**. Do not select First install to bypass it. Keep the backup and ask for a reviewed recovery choice.
5. If **First install** was interrupted, the bootloader, partition table or boot selection may also be incomplete. A deliberate retry of the same selected bundle can rewrite all four images; it needs the backup/overwrite confirmation again. Do not assume app-only repair will fix it. Persistent failure needs the vendor route below or support.
6. Keep USB connected through verification. After success, restart and check the intended runtime version, history/settings, READY behavior and ordinary pulls. Firmware byte verification does not prove reliable RPM, motion replay or power behavior.

There is no automatic erase-all, mode change or recovery write. Wrong chip, unknown/non-8 MB flash capacity, invalid bundle layout or mismatched device boot layout is a refusal. Changing the target/mode or finishing an attempt clears overwrite consent. Each attempt disconnects before retry.

## Reinstall M5Stack software

Use [official M5Burner](https://docs.m5stack.com/en/uiflow/m5burner/intro) and the [vendor's StickS3 UIFlow2 flashing guide](https://docs.m5stack.com/en/uiflow2/sticks3/program). Select **UIFlow2 firmware specifically for StickS3**. Enter download mode, choose the corresponding USB port and follow the tool's configuration/burn steps. Restart and verify the selected vendor software afterward. Review any erase/overwrite choice before accepting it.

This is a vendor software reinstall, **not a reconstruction of the unit's original factory state**. The exact demo and version shipped with a particular unit have not been verified. If you need that exact software, use your own pre-install backup or ask M5Stack to identify it. LaunchLab does not host third-party factory binaries or guess vendor flash offsets.

Reinstallation may replace or make earlier settings, history, Wi-Fi credentials and calibration inaccessible. Without a usable backup made before replacement, those original records cannot be recovered merely by reinstalling software. A backup also does not undo permanent chip security settings or repair damaged hardware.

After reinstalling vendor software, normal LaunchLab Update/Repair may refuse its different layout. Returning to LaunchLab is an intentional **First install**, with a new backup and overwrite confirmation.

## Before replacing software: keep a private backup

The browser updater does **not** save a backup. Record the model, existing firmware identity and important settings separately. Keep any full flash dump private; it can contain credentials and personal records. Do not attach it to an issue or commit it.

For users who already have Espressif's current esptool available, its [read-flash documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html) supports a full K150 flash read. With the device in download mode, substitute its actual port:

```sh
esptool --chip esp32s3 --port PORT --baud 115200 read-flash 0 0x800000 sticks3-k150-before-install.bin
```

This reads flash; it does not erase or write it. Check successful completion, the expected **8,388,608 bytes**, and keep a checksum plus a second private copy. File length alone does not prove a restorable backup. A dump made after a failed write preserves the current state, not the already-overwritten original. Encrypted/secured devices can restrict reads or make a dump unsuitable for ordinary restoration; stop on security warnings and never use force flags to bypass them.

Restoring a whole-device backup needs a separate model, provenance and security review. This page deliberately offers no generic whole-flash write button. Use the official vendor route for a fresh vendor installation; ask for support before writing a private dump back.

## No port or repeated failure

Use a direct USB connection and another known data cable; close apps holding the port and re-enter download mode. Keep power steady. Check [Espressif's troubleshooting guide](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html) if connection or verification keeps failing. If download mode remains unavailable, stop and contact support rather than repeatedly changing images or erasing flash.

For [LaunchLab support](https://github.com/shadohead/launchlab/issues), include K150 identification, OS/browser, selected version/mode, the exact error, whether writing started, and a scrubbed installation log. Remove personal paths, port identifiers and other private details.

## Release validation limits

Use the [LaunchLab USB updater](https://launchlab.beywatch.gg/update) and check its selected release. The preserved [0.13.7 release](../firmware/releases/0.13.7-sticks3-accel-16g-pm/README.md) and [build context](FIRMWARE_0.13.7.md) describe that image and layout; a newer release does not establish physical browser recovery, stock reinstall or rollback.

### Historical 0.11.1 prototype checks

**0.11.1** is the user-checked prototype revision. It adds saved themes (Classic, Mint, Amber, Violet), a personal-best effect on the recap, and USB backup and restore of history, personal bests, appearance and settings (`K` export, `R` restore). It fixes 0.11.0, which restarted about once a second after boot (a loop-task stack overflow) and was withdrawn. One firmware still serves both sensors through the saved **Sensor profile** setting (QRE / Standard with an 80-count mark floor, TCRT5000 with 60). It was installed app-only on the owner's M5StickS3 K150: no restarts in a 120 s soak, history kept, a backup and restore round trip identical, and the screen, themes and pulls confirmed. The detector, sampling, thresholds and saved history format are unchanged from 0.10.22, which keeps measuring pulls that start right after an automatic save. Battery endurance, long-session reliability, absolute RPM/angle calibration, physical browser transfer and first install/rollback, and stock recovery remain separate checks.

Local guard/instruction fixes do not qualify physical recovery. Preserve exact archives and private backups. Normal pulls/replay and app-only retention were checked on the development device. Still verify battery standby/wake at 80 MHz, USB suspend/detach behavior and interrupted browser update, vendor reinstall and rollback separately. Hidden secured wiring and assembly acceptance remain required before a buyer pilot.
