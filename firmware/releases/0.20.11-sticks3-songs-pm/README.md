# Firmware 0.20.11 (beta)

Runtime: `0.20.11-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.10 unless you turn on Beta.

- New: the Beyblade X app can register the stick as a Battle Pass and sync its launches. Turn Bluetooth on, open Menu, Bluetooth, Beyblade X, and hold A when the app asks you to press the pass's button.
- New: after a sync the stick counts from 0 again, as a real Battle Pass does, so the next sync only brings the launches since the last one.
- Changed: the curve sent to the app covers the whole pull, not only its first tenth of a second.
- Known issue: a phone that was paired with the stick under an earlier firmware can keep its old list of services and never finish connecting; forget the stick in the phone's Bluetooth settings and connect again.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s). Turning Bluetooth on or off restarts the device.
- Thanks to MaximilianRTS for this contribution.
- Includes everything in 0.20.10.

Everything in 0.20.10 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.10 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.11.md).
