# Firmware 0.21.0 (beta)

Runtime: `0.21.0-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.20.10 unless you turn on Beta.

- New (experimental): Analysis. Hold B on Main, or open Menu, Analysis, then launch: it times the spin by ear and forecasts when it will end. Tap B when the Bey stops for the fitted curve and Details. The forecast is still being improved.
- New: Battles. Two LaunchLabs with Bluetooth on, launched together, show both launches and keep score between the two players. Menu, Battles has the switch, the match length and each rival's record.
- New: Min RPM in Settings, 4,000 rpm by default: slower launches, such as feeding the string back in, are never recorded.
- Changed: Mic RPM is now part of Analysis, which has its Menu row. The Battery page is now Device and also shows the flash use.
- Fixed: a restore that fails changes nothing; Bluetooth's effect list no longer freezes the stick; Wipe all stays wiped; sleep is no longer held off for ever by a refused save.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s). Turning Bluetooth on or off restarts the device.
- Thanks to trumantse1 for Analysis.
- Includes everything in 0.20.11.

Everything in 0.20.11 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.20.11 or 0.20.10 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.21.0.md).
