# Firmware 0.20.3 (beta)

Runtime: `0.20.3-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.2 unless you turn on Beta.

- New: Wear check (Menu, Wear check). Measure how fast your launcher's string rewinds: three pulls level, three pulled down and three pulled up, each guided on screen. Extend the string slowly, wait for READY, and let go. Wear check pulls are not added to your history or bests.
- Known issue: a website save while a song plays can make the next song drop out once. Fixed in 0.21.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).
- Includes everything in 0.20.2.

Everything in 0.20.2 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.2 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.3.md).
