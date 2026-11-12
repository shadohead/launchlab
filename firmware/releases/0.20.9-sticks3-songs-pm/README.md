# Firmware 0.20.9 (beta)

Runtime: `0.20.9-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.8 unless you turn on Beta.

- New (experimental): Bluetooth, under Menu → Bluetooth. Apps that read the BEYBATTLE PASS can read your launches from the stick, which appears as BEYBLADE_TOOL01. It is off until you turn it on, and it uses more battery while it is on. Built on a prototype contributed by MaximilianRTS.
- New: while the stick charges, the main page shows a battery icon with a bolt in its top right corner.
- Changed: the Menu is in a new order, in groups: History, New session and Tournament; String wear check and Mic RPM; then Battery, Bluetooth and Settings.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s). Turning Bluetooth on or off restarts the device.
- Includes everything in 0.20.8.

Everything in 0.20.8 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.8 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.9.md).
