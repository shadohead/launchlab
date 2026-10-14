# Contributing

Keep changes focused on M5StickS3 K150, analog QRE/TCRT sensing, print files and firmware validation. Run the native sanitizer suites, core guard tests, redirect build and artifact checks before submitting a pull request. The maintained browser updater lives at https://launchlab.beywatch.gg/update; browser UI changes belong in the Beywatch repository.

Keep firmware and hardware versions separate. Never silently replace a published binary or frozen print package. Publish a new version and checksum. Update `site/public/firmware/catalog.json` alongside the firmware images, record the exact source/toolchain, and distinguish software evidence from physical acceptance.

Do not commit device backups, factory flash dumps, personal practice logs, secrets, build caches, or editor autosaves. Preserve upstream copyright and the hardware share-alike attribution. Inspect file paths before exporting a release archive.
