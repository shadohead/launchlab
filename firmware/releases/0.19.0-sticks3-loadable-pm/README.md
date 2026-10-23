# Firmware 0.19.0 (beta)

Runtime: `0.19.0-sticks3-loadable-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.18.0 unless you turn on Beta.

- Add celebration effects without a firmware update. Paste a share code (or open a `.llfx` file) in the website's effects library, preview it, and send it to your LaunchLab over USB.
- Added effects play like the built-in ones: as Best effect, in Random and in milestone pools. They can have their own sound.
- Every added effect is checked before it plays. One that would draw too slowly is refused, and if one ever runs slow on the device it gives way to the plain recap.
- Each added effect names a built-in stand-in, which plays wherever the effect is removed or cannot play. Going back to older firmware shows the stand-ins, and coming back to 0.19 finds your effects where they were.
- Backups of a unit with added effects carry them (a new backup format that older firmware refuses without changing anything).
- Includes everything in 0.18.1.

If you try it, add an effect from the library, watch it celebrate and listen to its sound, and tell us if anything looks or sounds off. Everything in 0.18.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.18.0 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.19.0.md).
