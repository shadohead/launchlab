# Firmware 0.18.0 (beta)

Runtime: `0.18.0-sticks3-anime-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.15.0 unless you turn on Beta.

- 40 new celebration effects in eight sets of five, each with its own sound and about five seconds long:
  - Haymaker, Palms, Elastic, Chains and Waterleaf
  - Transmute, Ignition, Spires, Gate and Fracture
  - Timestop, Zipper, Rush, Menace and Sequel
  - Swordland, Treasury, Sunblade, Aegis and Trace
  - Volley, Starfall, Mimic, Scales and Meadow
  - Rev, Hammer, Foxbite, Detonate and Sharkfin
  - Slingshot, Presence, Threefold, Banner and Vapor
  - Crescent, Release, Petalfall, Mask and Blink
- Boundless is redesigned over the whole screen. Every effect from 0.16.0 and 0.17.0 is still there, 81 in all, and milestones can play any of them.
- Saving never interrupts a pull: pulls, personal bests and the battery log save to a separate area of the M5's flash in a few milliseconds while the sensor keeps measuring.
- The M5 keeps storage room in reserve: a new custom theme, milestone table or effect choice is refused with a plain message on the website before storage gets so full that saving slows down.
- New defaults for units that never changed them: the 1-turn peak RPM method, 30% brightness, and Celebrate set to Both. Settings you saved are kept.

Everything in 0.17.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.17.0, 0.16.0 or 0.15.0 is a deliberate Repair / rollback in the updater; the older firmware keeps your settings and the pulls saved up to the last time the M5 slept or was switched off.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.18.0.md).
