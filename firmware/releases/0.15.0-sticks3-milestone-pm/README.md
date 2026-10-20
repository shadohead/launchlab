# Firmware 0.15.0 (beta)

Runtime: `0.15.0-sticks3-milestone-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on a real M5) and has not yet been hand-tested for stable. The updater installs stable 0.13.9 unless you turn on Beta.

- RPM milestone celebrations: under Settings > Celebrate choose New best (the default, as before), Milestones or Both. A pull plays the effect of the highest milestone its RPM reaches, every time.
- The default ladder runs from 9K to 15K, from gentle Sparkles up to Lightning and the new Slash at 15K. Edit the ladder in the website celebrations editor and send it over USB.
- New Slash effect: a blade cuts across the RPM, the number splits apart with sparks, then closes up again.
- Everything in 0.14.0 is retained, including custom color themes. Backups keep working both ways for ordinary settings.

Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.13.9 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.15.0.md).
