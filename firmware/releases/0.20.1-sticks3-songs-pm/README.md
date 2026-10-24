# Firmware 0.20.1 (beta)

Runtime: `0.20.1-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.18.0 unless you turn on Beta.

- RPM readings are accurate at high speed: a pull above about 25,000 rpm no longer ends early and reads low, and every reading is timed between the detector's 100 µs steps rather than on them. On the test rig every speed from 8,000 to 29,000 rpm reads within 0.3%.
- Readings at any speed may differ from 0.20.0's by a percent or two (usually a fraction of one); your saved bests are kept as they are.
- Songs now match the celebration length: at most 6 s. Songs you already installed keep playing; a longer one plays its first 6 s and fades out. The example songs are shorter phrases, and the website's MIDI import takes a 6 s excerpt.
- While a sound or song plays, a pull must start with marks at least half as strong as your own recent pulls, so speaker noise cannot start a phantom pull. It learns from your last pulls and starts over when you change the sensor.
- Includes everything in 0.20.0.

If you try it, spin a few fast pulls and tell us how the readings compare. Everything in 0.18.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.18.0 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.1.md).
