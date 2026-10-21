# Firmware 0.16.0 (beta)

Runtime: `0.16.0-sticks3-effects-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.15.0 unless you turn on Beta.

- Ten new celebration effects: Tidal, Frost, Gale, Eclipse and Radiance, plus Spin, Storm, Quickdraw, Leaves and Bloom, each with its own sound.
- Every celebration now runs about five seconds and keeps moving after its big moment; your RPM stays readable the whole time, and a new pull still cuts it off.
- Milestones can sit at any RPM (up to 12), and each one can play a random pick from several favourite effects.
- Custom themes accept any colors; the theme editor warns when a background is light or text is hard to read.

Everything in 0.15.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.15.0 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.16.0.md).
