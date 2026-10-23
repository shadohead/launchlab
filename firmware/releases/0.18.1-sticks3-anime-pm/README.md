# Firmware 0.18.1 (beta)

Runtime: `0.18.1-sticks3-anime-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.18.0 unless you turn on Beta.

- Saving a milestone table from the celebrations page works again. In 0.18.0 the M5 could miss the end of the request and the page said it stopped answering.
- A request the M5 refuses partway (a damaged backup, theme or milestone line) can no longer start another transfer or run a stray command.
- Celebration sounds now start in step with their animation. Before, a sound could play a little late, most of all in the Best effect editor's preview.
- Fixes a rare reset when the motion sensor fell behind.
- Sunblade runs smoother.
- The speaker now plays its sounds in mono, which takes less work while an animation draws. It is the same speaker and the same loudness.

If you try it, listen to a few celebrations and editor previews with Sound on, and tell us if anything sounds off. Everything in 0.18.0 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.18.0 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.18.1.md).
