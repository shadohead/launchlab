# Firmware 0.20.10 (beta)

Runtime: `0.20.10-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.8 unless you turn on Beta.

- Battery: a stick carried in a bag no longer keeps waking itself. After three shake wakes in a row that nobody uses, it sleeps until you press the side key; any use turns Shake to wake back on.
- Battery: a shake wake that nobody uses ends sooner, 15 seconds after the stick stops moving and at most a minute after it woke.
- Battery: the screen dims 30 seconds after the last button press or pull, also while the stick is in your hand.
- Changed: the default brightness is 20%. A stick with a saved brightness keeps it.
- Fixed: the battery percentage read 20 to 26 points too high while charging, so a part-charged stick looked full. A full charge from flat takes about two and a half hours.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s). Turning Bluetooth on or off restarts the device.
- Includes everything in 0.20.9.

Everything in 0.20.9 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.9 or 0.20.8 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.10.md).
