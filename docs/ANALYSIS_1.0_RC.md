# Analysis 1.0-rc.1

Runtime: `0.20.11-analysis-1.0-rc.1-pm` · M5StickS3 K150 · based on firmware 0.20.11.

This candidate freezes the current predictor and recording format for hardware acceptance. It has not been published or promoted to stable. The analysis feature version is separate from the base firmware version.

## Included

- Hold B on Main to enter Analysis; launch to start recording. Tap B when the physical spin finishes. A toggles replay while recording, and B after stopping opens Analysis Details. Hold B exits.
- Fixed Riccati live prediction, with the existing early mic-count fallback from ten seconds and a population estimate when the microphone is unavailable. Relative-pitch recovery remains enabled in Analysis. No new prediction coefficients or acceptance thresholds are introduced in this RC.
- The graph and finish label use the same estimate. The axis extends ten seconds beyond the prediction, then follows elapsed time plus ten seconds if the spin continues. Current time and expected finish have dotted markers; the expected finish uses the warning color.
- Post-stop Fixed Riccati fitting, Free Riccati fallback, and a gray endpoint-only estimate when neither acoustic fit succeeds. The manual endpoint and optical launch anchor are preserved; the fitted tail uses monotone cubic Hermite interpolation.
- Immediate raw capture at stop, independent fit/metadata attachments, retryable flash saves, and the newest 32 recordings retained across restarts. Full-session audio metadata replaces on-device WAV storage. Existing export formats remain readable.
- Device page with battery and flash information, plus a battery percentage on the main menu.

The experimental audio ML candidate stays offline and is excluded from the default firmware and this release bundle. Recording metadata does not enable ML inference. The other physics models remain available in the offline comparison tools.

## Interpretation and limits

Manual spin time is measured. Predicted finish, the RPM curve, revolutions, RPM-band times and battle stamina are estimates. The optical reading measures launcher-shaft speed. The gray endpoint-only curve is an assumed shape, not a validated acoustic fit.

Predictions remain available after ten seconds even with sparse readings. They can be inaccurate for aggressive bits, rail impacts, background noise or late wobble, and can be earlier than the actual stop. Press B at the real finish regardless of the prediction. The experimental audio model is not yet consistently better across the available combinations.

Recording ends at five minutes. Timeout, leaving Analysis, the side power key, or a replacement launch records an unknown physical endpoint unless B already recorded the finish. These sessions must not be treated as manual-stop training labels. Starting the next pull does not require waiting for the final fit; raw data and metadata are captured first, but an unfinished optional fit may be cancelled.

A save failure retains the RAM packet and retries at idle checkpoints. Ordinary sleep waits for pending saves; a forced reset or loss of power before a successful flash commit can still lose unsaved data. Export regularly: the 33rd completed recording displaces the oldest of the retained 32.

## Installation and rollback

Use the [local build and flash workflow](LOCAL_WINDOWS.md). Confirm the runtime in Settings → About after flashing. The app-only updater preserves the partition layout, settings and archive. Export recordings before upgrading or rolling back. A full-device backup is a separate optional command; the normal settings/history backup does not replace the calibration export.

```powershell
cmd /c ".\scripts\build-local.cmd && .\scripts\device-local.cmd flash --port COM7 --execute"
```

Substitute the actual Stick port. Do not flash an arbitrary COM port. For a previous firmware, use a deliberately selected archived release through its documented repair/rollback workflow; older firmware may not expose Analysis records or understand the metadata region. Export the records before any wipe or full-image restore.

## Acceptance before stable

Local validation on November 16, 2026:

- After integrating upstream 0.20.11, 108 native suites and 52 Python harness/tool suites passed on Windows. This includes full-session metadata, 32-record retention, interrupted writes, retries, legacy exports, allocation failures, lifecycle handling, Bluetooth restart/pairing interactions and graph layout.
- The Windows host compiler ran with assertions enabled but without ASan/UBSan runtimes. The complete Linux host suite then passed all 163 suites with ASan/UBSan enabled, including pinned screen renders. The [candidate validation run](https://github.com/trumantse1/launchlab/actions/runs/44004076305) retains the proof for the tested firmware tree.
- Published-artifact verification, all 145 site tests, and the updater redirect build passed on Linux. Device/menu render changes were reviewed; ordinary Mic RPM and all other pinned screens are unchanged.
- The pinned ESP32-S3 PM build passed. Application image: 2,273,152 bytes of a 3,342,336-byte slot. Static globals: 91,148 bytes against the upstream 155,000-byte budget. The loop stack frame is 912 bytes against its 1,536-byte budget; other sketch frames satisfy the 2,560-byte limit. Source/artifact checksums and the unchanged partition table passed the offline flash plan.
- ESP32 type-size measurement gives approximately 470 KiB of Analysis PSRAM for microphone processing, a live refit with the previous plot retained, and one maximum-size pending packet. Each additional failed-save packet needs at most 24,149 bytes. This excludes the rest of the application and is not an on-device free-heap measurement. Raw and metadata archive slots still fit, with 273 and 152 bytes of headroom respectively.

Published binaries, catalogs, checksums and frozen source snapshots are unchanged from upstream. `verify-artifacts.py` checks these independently of the development sketch; `--check-working-source` additionally requires the sketch to match the published snapshot when auditing a release sync. The ordinary firmware host suite validates the development sources. Historical Windows CRLF conversion can affect the artifact check, so Linux CI is the reference for those byte-for-byte checks.

The following checks require a physical Stick and remain pending for this candidate:

| Check | Pass condition |
| --- | --- |
| Battery-powered launches | READY, timer, replay, finish label and Details behave correctly; buttons remain responsive during fits. |
| Early estimates | Finish appears around ten seconds, updates with new evidence, and does not disappear during refits; label and marker agree. |
| Immediate next launch | Stop and launch again without waiting for the fit; export contains both distinct raw traces and metadata. |
| No manual stop | Leave running to five minutes; export says timeout and has no observed zero endpoint. |
| Exit / side power / wake | Interrupt a running session; export says interrupted after restart, and wake returns to Main. |
| Retention | Export 32 pulls, record one more, restart and export; newest 32 remain and counts/deduplication agree. |
| Save recovery | Check a controlled write failure and retry on a test device; no duplicate pull and no missing raw trace after recovery. |
| Ordinary use | Normal Main, Mic RPM, history, settings, sound, sleep/wake and both display rotations still work. |
| Independent beta | Freeze firmware for 30–50 fresh manual-stop launches across stamina and aggressive bits, launch strengths and noise conditions. Report errors by combination and at 10/15/30 seconds. |

Do not claim stable readiness from host tests alone. Accept the core recording behavior first; score forecast quality separately without tuning on the acceptance batch.
