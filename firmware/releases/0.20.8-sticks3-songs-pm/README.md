# Firmware 0.20.8 (beta)

Runtime: `0.20.8-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.5 unless you turn on Beta.

- New: Settings → About shows the firmware version, the device ID and how many pulls are saved.
- New: a battery icon in the top right corner of the Menu and Settings shows how much charge is left, like a phone's, without a percentage. It turns orange when low and green while charging. Settings → Battery icon turns it off.
- Fixed: a song could drop out for a moment during its celebration, most often at the start or in a busy song. Its animation now also starts exactly with the music.
- Known issue: the first Mic RPM reading can take a few seconds, and battles, rail hits and a loud room show as drops or gaps.
- Known issue: a website save while a song plays can make the next song drop out once. Fixed in 0.21.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).
- Includes everything in 0.20.7: the Mic RPM page and 0.20.6's battery and sleep fixes.

Everything in 0.20.7 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.7, 0.20.6 or 0.20.5 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.8.md).
