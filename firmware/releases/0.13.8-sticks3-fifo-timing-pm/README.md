# Firmware 0.13.8 development candidate

Runtime: `0.13.8-sticks3-fifo-timing-pm` · M5StickS3 K150 · ESP32-S3.

This is an unlisted development candidate in the public repository. No GitHub
Release/tag is published for it, and the recommended firmware remains 0.13.7.
If the existing main workflow deploys, direct candidate artifact URLs become
public on Pages; absence from the picker is not an access restriction.

The checkpoint preflight now waits once for at most 250 ms for the IMU gate,
rejecting the checkpoint if unavailable. FIFO timestamps use the time before
the FIFO length request, excluding the burst transfer from the anchor.

Pinned ESP32-S3 PM compilation and stack checks passed. Native regressions,
updater tests and artifact checks are required for the PR. The exact source,
build/test scripts, toolchain and checksums are frozen here. Bootloader,
partition table and boot_app0 are unchanged from 0.13.7. Ordinary installation
uses the application at 0x10000; no device installation is performed by this PR.

Physical evidence belongs to the preceding development application, SHA-256
`3ccbd4cb84cc07c60eb21ad71053bad71769b90476c1d2d0af720628513f2c2a`,
whose acquisition logic is unchanged; only runtime identity/documentation changed
for this candidate. The exact 0.13.8 binary has not been flashed/read back.
Three real FIFO bursts reproduced the old early return and verified the fixed
barrier; a held-gate test verified bounded rejection and continued ADC sampling.
Stationary IMU capture/playback used synthetic timing/RPM, not an optical launch.

Natural incidence, absolute timing against externally timed optical launches,
both profiles' end-to-end capture, actual flash-boundary stress, and prolonged
or near-capacity hardware qualification remain unproven. No unsafe flash/read
collision or corruption was induced. See the [full timing evidence and limits](../../../docs/FIRMWARE_CAPTURE_TIMING_FIX_2026-10-16.md).
