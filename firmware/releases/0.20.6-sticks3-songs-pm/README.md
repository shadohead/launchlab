# Firmware 0.20.6 (beta)

Runtime: `0.20.6-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.5 unless you turn on Beta.

- Fixed: the device could stay awake, screen on, until its battery was flat. It now goes to sleep even if it keeps moving after you last used it (5 minutes after your last button press or pull, or your Sleep after setting if that is longer), when its storage is full, when light flickers into the sensor, or when a button stays pressed in a pocket.
- Fixed: a device shaken awake in a bag goes back to sleep within 2 minutes.
- Fixed: a device that went to sleep on a charger now switches fully off when you unplug it.
- Fixed: the speaker chip and the green light no longer draw power after a sound or in standby.
- Improved: the screen sends only what changed, which saves power while you hold the device.
- Known issue: a website save while a song plays can make the next song drop out once. Fixed in 0.21.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).
- Includes everything in 0.20.5.

Everything in 0.20.5 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.5 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.6.md).
