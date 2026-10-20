# Firmware 0.14.0 (beta)

Runtime: `0.14.0-sticks3-themes-pm` · M5StickS3 K150 · ESP32-S3.

**Beta.** It passed the automated release gate (host suite and the on-device hardware-in-the-loop suite on real M5s) and has not yet been hand-tested for stable. The updater installs stable 0.13.9 unless you turn on Beta.

- Custom color themes: make one in the website theme editor, which checks text contrast and previews the device screens, then upload it over USB and choose it under Settings > Theme.
- The ten built-in themes look exactly as before.
- Custom themes are included in USB backup and restore; older backups still restore. A damaged theme falls back to the built-in theme.

Everything in 0.13.9 is retained. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged. Going back to 0.13.9 is a deliberate Repair / rollback in the updater.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.14.0.md).
