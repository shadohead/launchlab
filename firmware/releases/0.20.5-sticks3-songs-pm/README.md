# Firmware 0.20.5 (beta)

Runtime: `0.20.5-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.4 unless you turn on Beta.

- New: the recap graphs your pull's RPM turn by turn, from the gear starting through the peak to the end, with the peak labelled in thousands. It replaces the Start and End level dots.
- New: after a pull the tilt replay plays twice. Press A on Main to open the recap and it loops until you press A again or pull.
- The replay's progress runs down the right edge of the screen; the up arrow is gone.
- Known issue: a website save while a song plays can make the next song drop out once. Fixed in 0.21.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).
- Includes everything in 0.20.4.

Everything in 0.20.4 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.4 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.5.md).
