# Firmware 0.20.2 (beta)

Runtime: `0.20.2-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.18.0 unless you turn on Beta.

- The default sleep is 1 minute for units that never changed it. If you chose a Sleep after time, it is kept.
- New setting: Shake to wake (Settings, right after Sleep after). On, the default, works as before: a shake wakes the LaunchLab. Off, it sleeps more deeply and only the side button wakes it. A backup taken with Shake to wake Off can't be restored onto 0.20.1 or older (the restore is refused before anything is written); backups taken with it On are unaffected.
- Known issue: a website save while a song plays can make the next song drop out once. Fixed in 0.21.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).
- Includes everything in 0.20.1.

Everything in 0.18.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.18.0 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.2.md).
