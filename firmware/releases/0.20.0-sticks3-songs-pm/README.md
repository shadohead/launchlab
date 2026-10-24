# Firmware 0.20.0 (beta)

Runtime: `0.20.0-sticks3-songs-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.18.0 unless you turn on Beta.

- Songs: an added celebration effect can carry a song of up to 120 s on up to 8 channels, which plays on after its animation.
- New voices for songs and sounds: pulse, wavetable, FM, stepped noise and a built-in bank of eight drums.
- The website's effects library makes a song from a MIDI file and previews it on the firmware's own renderer; three original example songs come with it.
- A song plays from its own task below RPM capture and fades out within 20 ms when a pull starts; RPM capture and the effect frame budget are unchanged.
- A song must fit a render budget, as an effect's frames fit the frame budget; the website checks it before sending.
- Effects without a song are the same files as in 0.19.0. 0.19.0 keeps a song effect stored, lists it as needing an update and plays its fallback.
- Includes everything in 0.19.0.

If you try it, add a song from the library or import a MIDI file, let it celebrate a pull, and tell us if anything sounds off. Everything in 0.18.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.18.0 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.20.0.md).
