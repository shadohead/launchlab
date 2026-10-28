# Firmware 0.20.4 (beta)

Runtime: `0.20.4-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.2 unless you turn on Beta.

- Fixed: String wear check (renamed from Wear check) now measures the retraction after a hold, never the pull out. Pull the string out, hold until LET GO, then let go. Letting go too early asks you to hold longer.
- String wear check reads its tilt in your Mounting setting (Top or Sidecar) and shows it on the page. On a Sidecar install, set Settings, Mounting to Sidecar.
- Known issue: a website save while a song plays can make the next song drop out once. Fixed in 0.21.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).
- Includes everything in 0.20.3.

Everything in 0.20.3 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.2 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.4.md).
