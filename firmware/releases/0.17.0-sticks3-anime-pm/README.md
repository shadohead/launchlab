# Firmware 0.17.0 (beta)

Runtime: `0.17.0-sticks3-anime-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.15.0 unless you turn on Beta.

- 25 new celebration effects in five sets of five, each with its own sound and about five seconds long:
  - Crimson, Bulwark, Crux, Klaxon and Prism
  - Sunwheel, Torrent, Blitz, Tigerclaw and Butterfly
  - Birdsong, Spiral, Pinwheel, Overdrive and Summon
  - Breach, Bladework, Moonpetal, Barrage and Glitch
  - Rupture, Boundless, Precision, Umbra and Ravage
- Every effect stays on the M5, and the [celebrations page](https://launchlab.beywatch.gg/celebrations) picks which ones Best effect and Random use. An updated M5 offers the same 16 effects as 0.16.0 until you change it. Milestones can play any effect.
- Backups carry your effect choice when you change it; other backups keep their format, so 0.16.0 still reads them.

Everything in 0.16.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.16.0 or 0.15.0 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.17.0.md).
