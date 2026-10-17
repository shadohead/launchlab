# Firmware 0.13.9

Runtime: `0.13.9-sticks3-rpm-guard-pm` · M5StickS3 K150 · ESP32-S3.

- RPM detection treats the cord rewind after a pull as part of that pull instead of a phantom second launch, confirms sharp pulls, rides through a missed mark or a brief hesitation, and keeps a finished result if sensor samples are lost.
- History saves wait until the launcher is set down (or a minute while handheld), so they no longer pause the sensor right when the next pull usually starts; pulls left unsaved for three minutes are always written.
- USB output is never discarded when the computer pauses, and state-changing USB letters run only when sent on their own, so stray serial data cannot power off the device or change settings. Backup and restore are unchanged.

Everything in 0.13.8 is retained: ±16 g tilt capture, FIFO acquisition with the flash checkpoint barrier, I2C bus recovery, single-click power off, both sensor profiles, history, themes, effects, sound, mounting, Wipe all and USB backup/restore. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.13.9.md).
