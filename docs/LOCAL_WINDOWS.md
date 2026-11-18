# LaunchLab local build and flash

Run the commands below in PowerShell from the repository root. The scripts expect these tools under the local `firmware\.arduino` directory:

- Arduino CLI 1.5.1
- ESP32 Arduino core 3.3.0 and its compiler/tool dependencies
- M5Unified 0.2.23 and M5GFX 0.2.30
- LaunchLab's checksum-verified ESP32-S3 power-management SDK
- Isolated Python environment with esptool 5.1.0 and pyserial 3.5

## Build

```powershell
.\scripts\build-local.cmd
```

No administrator access, global PATH change, or PowerShell execution-policy change is needed. The command verifies the PM SDK, compiles the pinned configuration, checks stack budgets and application size, and writes a checksum manifest only after success. It does not access a device.

Build log: `firmware\build\analysis\build.log`
Application after success: `firmware\build\analysis\LaunchLabMini.ino.bin`
Manifest after success: `firmware\build\analysis\local-build.json`

Then inspect the offline plan:

```powershell
.\scripts\device-local.cmd plan
```

If compilation fails, stop and share `build.log` for diagnosis. Do not use the archived stable application as if it contained Analysis Mode. The release-candidate runtime should be `0.20.11-analysis-1.0-rc.5-pm`; see the [Analysis RC notes](ANALYSIS_1.0_RC.md).

## Identify the device

Use an M5StickS3 K150 already running LaunchLab, connected by a USB data cable. Close the browser updater and serial monitors. Confirm the physical model; an ESP32-S3 chip alone does not identify a StickS3.

```powershell
.\scripts\device-local.cmd ports
```

Replace COM7 in the commands below with its actual port. If the device will not enter download mode automatically, use the manufacturer's procedure: hold the power button until the green LED blinks, release it, then recheck the port.

## Preview and flash

After a successful build, preview without opening the serial port:

```powershell
.\scripts\device-local.cmd flash --port COM7
```

Flash the application:

```powershell
.\scripts\device-local.cmd flash --port COM7 --execute
```

The command validates source/build checksums and artifact sizes, then uses one serial
connection to check ESP32-S3, 8 MB flash, the partition layout, app0 boot selection,
and the LaunchLab marker in the first 64 KiB of app0. This marker is a compatibility
check, not authentication or complete validation of the previous application. Unknown
images are refused. It writes only app0 at 0x10000, verifies the complete new application,
and resets after success. An exact device checksum match skips the write. Bootloader,
partitions, NVS and history are not written. There is no full-device backup or comparison
with a previous flash snapshot in this workflow.

Build and flash only when the build succeeds (Windows PowerShell or PowerShell 7):

```powershell
cmd /c ".\scripts\build-local.cmd && .\scripts\device-local.cmd flash --port COM7 --execute"
```

## Back up the device

```powershell
.\scripts\device-local.cmd backup --port COM7
```

This reads the full 8 MB flash and saves a unique timestamped `.bin` plus SHA-256 receipt
under `firmware\.arduino\device-backups`, then restarts the device. It does not require
a current firmware build and does not write flash. Existing backups are never overwritten.
A backup contains private device data; retain it locally for recovery. Backup completion
does not establish compatibility with the app-only updater; flashing runs its own checks.

## Check Analysis Mode

- Main: hold B for one second to enter Analysis.
- Wait for microphone warm-up, then launch.
- Confirm READY before launching, then optical RPM, large circular MM:SS timer, last valid mic RPM timestamp, and the expanding Mic RPM graph.
- Tap B to freeze the timer/graph; launch again to restart both.
- Tap B once stopped to open Analysis Details. B scrolls additional RPM band rows. A returns to the main Analysis screen.
- Stop manually when the physical spin ends to anchor the fitted curve at zero RPM; automatic timeout does not count as an observed zero.
- Confirm fitting remains responsive to A/B, and hold B or launch again during fitting to check cancellation.
- Tap A to show tilt replay and the initial optical RPM graph; tap A again to return. Recording continues in replay.
- Hold B to return to normal Main; check normal audio returns.
- Check history/settings, sleep/wake, and the side power key.

Analysis automatically stops at five minutes and continues through quiet gaps. Inactivity sleep is held while a recording runs. The separate Mic RPM mode retains its two-minute/eight-seconds-without-evidence limits. Analysis remains silent while the microphone is active.

While recording, the Analysis graph spans zero to ten seconds beyond the displayed predicted finish, leaving room after its marker. This applies to raw readings, a fitted curve, and a fit being refreshed. Before a prediction is available, or once elapsed time passes it, the axis ends at elapsed time plus ten seconds. The previous completed curve stays visible until its replacement is ready. Once recording stops, the axis ends at the recorded duration (with a ten-second minimum). Axis labels use the muted theme color. Normal Mic RPM keeps its existing axis behavior.

The labels above the main Analysis graph are:

| Label | When shown |
| --- | --- |
| `Last MM:SS:MS` / `Last --:--:---` | Timestamp of the last accepted mic RPM, or none yet. |
| `Listening` | Before a launch, with the mic ready. |
| `Mic warming up`, `Mic unavailable`, `Not enough memory` | Mic state before a finish estimate is available. |
| `Predicted finish --:--` | Recording has started, but no estimate is available yet. The label uses the warning color. |
| `Predicted finish MM:SS` | Current estimate of total time from launch, shown in the warning color. The source below is `Fixed Riccati`, `Early mic estimate`, or `Population estimate`. |
| `Computing Best Fit` / `RPM Curve. . .` | A manually stopped launch is being fitted; three dots animate over the graph. |
| `Fixed Riccati Model` / `Free Riccati Model` | The completed post-stop fit. |
| `Fixed Riccati Estimate` | Neither mic fit was accepted. The shared shape joins the IR launch to the manual zero endpoint; the entire estimated curve and unused mic readings are gray. |
| `Curve unavailable` | A manual recording has invalid endpoints, such as zero elapsed time. |
| `Time limit reached` / `No stop recorded` | Five-minute timeout. |
| `Recording interrupted` / `No stop recorded` | Exit, power-off, or a replacement launch ended recording. |
| `Save pending` / `Please wait` | The stopped capture still needs a RAM snapshot; B retries it. |

X-axis ticks show `0s`, the midpoint, and the right edge in seconds. Y-axis ticks show zero and three RPM levels abbreviated in thousands. While recording, vertical lines with 2x2 dots mark current time in the theme's text color and predicted finish in its warning color, both at full brightness. The finish marker remains visible if elapsed time passes it. Both markers appear in the raw and fitted views. Live extrapolation is gray; included readings and the fitted portion use the theme accent. The Analysis Details page uses `Fitting curve...`, `Fit unavailable`, `No complete bands`, or `No winning model` if its band table is not available. It does not display the historical four-model page titles.

Post-stop selection prefers the accepted Fixed Riccati fit, then the validated Free Riccati fit. If neither succeeds, a `Fixed Riccati Estimate` scales the shared drag shape between the measured IR launch and manual zero-RPM endpoint. Its revolutions, stamina and band times are inferred from that shape; they are not validated mic measurements. Exports mark this as `final_fit.endpoint_estimate: true`, with zero retained readings and no fit-error metrics. Older records decode this marker as false. If worker allocation fails, the same estimate and details remain available without allocating a trace copy; the saved raw launch remains exportable, without an attached fit snapshot. Neither timeout nor interruption is treated as a measured zero.

| How recording ends | Saved endpoint | Training use |
| --- | --- | --- |
| Short B at the physical finish | `manual`, with an observed finish time | Eligible finish-time target |
| No B before five minutes | `timeout`, no observed finish time | Raw evidence only |
| Side power button, holding B to exit, or another valid launch | `interrupted`, no observed finish time | Raw evidence only |

An interrupted recording is captured before the mic buffers are reused or the unit enters normal sleep. Sleep waits for queued writes; failed writes keep the unit awake and pending. Wake returns to Main and does not resume the old stopwatch. A timed-out recording says "Time limit reached / No stop recorded". Pressing B afterward opens its details and does not turn it into a manual finish. Only a manually observed finish constrains the post-stop curve to zero RPM; nonmanual recordings use the bounded live fitter.

## Export the latest launch for calibration

Build and install the firmware with the `J` export command once. Record an Analysis launch and press B when the spin physically ends. B captures the raw trace immediately. You can leave Analysis or launch again while the final fit runs. Plug in the Stick and run this from the checkout in normal PowerShell:

```powershell
.\scripts\device-local.cmd calibration --port COM7
```

The command reads the latest completed calibration packet without flashing or resetting the Stick. It writes `firmware/build/calibration/latest.json` and a content-addressed JSON copy in the same directory. The batch firmware keeps the latest 32 completed traces in flash. Run the command once after a session to download them oldest first. The summary counts pulls returned by the Stick, new archives saved on the PC, and pulls already present. Repeating the export preserves archive timestamps; later audio metadata or a final fit can enrich the same pull without replacing its labels. Earlier firmware exports one trace. Use `--output DIRECTORY` to choose another folder. Keep the Stick and collector out of a serial monitor while downloading.

New Analysis captures identify recovered acquisition readings and include per-spin tracker decision counts, skipped audio time and queue drops through the last delivered frame. The active stamina predictor uses original mic locks and published Fixed Riccati fit support. A full packet includes the firmware ELF hash, optical launch RPM, raw RPM/time pairs, live-filter rejection and final track exclusion flags, the most recent published preview (if any) with its evaluation time, raw-prefix length and displayed finish, a final fit record with four model slots and any measured fit metrics, and the manual, timeout or interrupted endpoint. Current firmware prefers Fixed Riccati and evaluates Free Riccati only as a fallback. The exporter names the preferred slot `Fixed Riccati` when that fit is selected and retains `Riccati` for free fits and older records. Times for observations and stop labels are milliseconds from optical release. Forecast times are seconds from release. `observed_stop_ms` is null for a timeout or interruption. Use only the first `latest_preview.raw_count` observations to replay that preview, and recompute its filter on the prefix. Final-fit exclusion flags and the recorded manual endpoint contain later information.

Analysis logs the displayed finish at 10, 15, and 30 seconds while a spin is running. Each entry in `forecast_checkpoints` includes the source, the actual observation time in milliseconds, and the displayed estimate in seconds from release before screen rounding. On older firmware with early mic estimates, `corrected_finish_s` records the ridge correction for comparison with the manual stop.

Analysis can also recover relative RPM from weak or ambiguous audio for up to two seconds after a trusted mic lock. At least two identifiable harmonics must move together for three consecutive windows. Common-divisor harmonic families, competing pitch interpretations, silence, clipping and broadband noise cannot supply a recovered reading. Recovery uses measured spectral peaks and does not extend the fitted curve into the observations. Ordinary Mic RPM keeps its existing tracker decisions and cadence.

The Fixed Riccati fit gives these readings a confidence-scaled weight of at most one quarter of an ordinary reading, and caps their combined weight at one quarter of the other observations. They cannot satisfy its minimum count, time span or inlier requirements. The Free Riccati fallback excludes them. Original-lock counts and the existing ML features exclude both recovery types; the original alpha-beta filter sees the same missing window as before. This is a conservative measurement extension, with no established improvement in real-launch finish accuracy yet.

Use the updated collector for these LLCA packets. Each relative observation has `relative_pitch_recovered: true` and `relative_pitch_confidence` from 1 to 255 (an evidence score, not a calibrated probability). `confirmed_acquisition_recovered` identifies the older backfilled acquisition frames. For compatibility, the historical `recovered_acquisition` field remains a generic flag for either recovery type. The collector and replay evaluator still read older exports, including LLC8, LLC9 and the separate LLC6/LLC7 ML shadow builds. Flash capacity remains 32 launches, including interrupted recordings. A recovered point's timestamp is the center of its audio window; it becomes available only after that window and its causal confirmation have completed.

Older Fixed Riccati checkpoints also log `riccati_accepted_inliers` and `riccati_original_inliers` from the published fit. These count original readings within 150 RPM of its curve and exclude recovered acquisition frames. The first count includes only readings accepted by the live filter. When a majority were rejected, `riccati_limited_finish_s` caps the forecast at the count-based finish, with elapsed time as the minimum cap. Otherwise it matches the displayed finish. A null candidate means the shadow estimate was unavailable or another source was displayed. LLCA checkpoints identify the active forecast as Stamina predictor. Earlier packets retain their original checkpoint fields.

Manual stop, timeout and interruption queue a frozen raw packet for the next idle flash checkpoint. Each completed final fit attaches to its original raw packet. An interrupted fit leaves the raw data usable for replay; the JSON then has null final_fit and final_fit_excluded fields. Raw packets are committed before audio metadata and fits, so an older audio failure cannot delay saving newer raw traces. Writes are read back before their commit markers are accepted, and retries after an ambiguous commit do not create duplicate records. A corrupt optional fit is omitted during export while its intact raw trace remains usable; raw corruption is reported explicitly.

The archive uses a dedicated region alongside practice history and effects. After 32 durable packets the next commit replaces the oldest. RAM holds pending packets while flash is busy. Failed writes retry at eligible checkpoints after five seconds. A snapshot allocation failure retains the stopped session, shows "Save pending", and retries before exit or buffer reuse. Normal shutdown waits for the save. A physical power cut before the raw commit can still lose a pending pull. After reboot, the reader returns the last successfully persisted packet. The JSON identifies RAM or flash as its source. When only a legacy `analysis_fit` record exists, the command exports a clearly labeled fit-only summary. Older firmware needs the update to recognize the command; a previously unsaved raw trace cannot be reconstructed from that summary.

## Record full-session audio metadata without USB

Analysis automatically records compact acoustic features from optical release until manual stop, interruption, or the five-minute timeout. The newest **32 sessions** survive reboot, alongside their raw RPM traces. No arming command or USB connection is required during a launch. Export the batch with the usual command:

```powershell
.\scripts\device-local.cmd calibration --port COM7
```

The export summary also reports how many pulls include metadata and how many contain captured windows. Each launch JSON contains `audio_metadata` with a versioned schema, timing, gap indicators and these summaries of the existing 400 ms tracker windows:

- Eight log-energy bands covering 40 to 2,000 Hz, including noisy windows rejected by the RPM tracker.
- Peak signal level, mean spectral flatness, broadband and clipping counts.
- The best-supported candidate frequency in Hz, harmonic support, score, margin, cycle hypothesis and tracker decision. A candidate is not necessarily an accepted RPM reading.
- Delivered frame counts, skipped audio time, queue drops and a conservative feature-availability bound for causal ML replay.

Intervals are 200 ms for the first 30 seconds and one second afterward, through the full session. All delivered windows contribute to their interval; band and level peaks can come from different windows. The features reuse the existing FFT. Quiet and clipped windows skip the FFT as before and have missing spectral values, not zero energy. A missing interval means no processed window arrived, not silence. `collector_available: false` identifies a failed collector allocation or unavailable mic. The raw RPM trace still includes its rejection and relative-pitch flags.

For live-model training, only use a row once `available_by_ms` is within the prediction checkpoint. It bounds the window-end interval plus delivery delay, rounded upward to 20 ms; an overflow has no known bound and should be omitted from causal replay. Times are milliseconds from optical release. Only manual stops provide physical finish labels; timeout and interrupted captures are censored sessions. Features are quantized, use relative digital levels rather than calibrated sound pressure, and cannot reconstruct the waveform.

A five-minute attachment occupies 8,016 bytes. The flash ring reserves 264 KiB for 32 recordings plus a spare slot for safe replacement. Manual stop freezes metadata together with the raw trace; failures retain the snapshot and retry at idle checkpoints. Normal sleep waits for pending writes. As with raw captures, a hard power cut before a successful commit can lose pending data.

**Migration:** the first metadata save reuses the former two-WAV region. Export any WAVs you want to keep with the old firmware before recording on this build. Existing calibration traces, fits, effects and history stay in place. Older exports remain readable and have no audio metadata. New onboard WAV recording is removed; `audio --saved` reports that metadata is now included in calibration exports. The explicitly armed USB diagnostic tape below remains available for occasional waveform comparisons.

### Analysis memory limits

The archive retains 32 sessions of up to five minutes. Each ring has a 33rd physical slot so replacing the oldest record does not erase one of the newest 32 before the replacement commits. Timeout and interrupted sessions count toward retention too.

| Flash region | Physical slots | Reserved | Maximum bytes used per slot | Remaining per slot |
| --- | --- | --- | --- | --- |
| Raw RPM trace and optional fit | 33 × 16 KiB | 528 KiB | 16,111, including header and alignment | 273 bytes |
| Audio metadata | 33 × 8 KiB | 264 KiB | 8,040, including header | 152 bytes |

Together these use 792 KiB of the 824 KiB pull region, leaving 32 KiB unassigned. They do not overlap practice history or effects. A full trace permits 3,000 RPM slots; its raw packet is at most 15,198 bytes and its fit attachment at most 895 bytes. Full-session metadata contains at most 420 summaries and occupies 8,016 bytes. Compile-time assertions enforce the slot boundaries, recording duration and shared retention count. Adding a byte to every sample or metadata row would exceed the current per-slot headroom.

These RAM sizes were checked with the ESP32 compiler on 2026-11-16; they are allocation sizes, not a measurement of the connected Stick's free heap:

| PSRAM allocation | Bytes |
| --- | --- |
| Mic ring, FFT/tracker, trace, metadata recorder, mic stack and spin summaries | 337,987 |
| Live refit with the previous plot retained, workspace and fitting stack | 119,280 |
| One maximum-length stopped packet, including fit room and metadata | 24,149 |

Those buffers total about 470 KiB with one stopped packet. Temporarily overlapping two maximum-size packets during capture raises that subtotal to about 494 KiB. Display buffers, other app allocations and heap overhead are separate. The fitting service itself occupies 1,852 bytes of internal static RAM; each worker also has a 352-byte internal task control block, and the mic queue carries 704 bytes of samples plus its queue control structure.

Saved sessions live in flash, not 32 simultaneous RAM copies. RAM retains the latest packet and any pending writes. Fully committed older packets are released even if an earlier attachment still needs a retry. Shorter recordings allocate only their required metadata length: a 90-second packet uses 3,990 fewer bytes than reserving all five minutes. A persistent write failure can still accumulate pending packets, up to 24,149 bytes each; allocation failure freezes the current capture and blocks buffer reuse rather than discarding it. The optional, explicitly armed USB tape adds 960,000 PSRAM bytes until reboot and is not part of normal metadata recording.

Batch export scans the metadata index once and verifies each payload as it is read. It does not load all 32 recordings into RAM together. Legacy packet readers and the USB diagnostic tape remain supported; the unused onboard WAV writer and sample-copy hooks have been removed.

## Record a longer tethered audio clip

With the Stick connected, run this before the launch and wait for **Tape armed**:

```powershell
.\scripts\device-local.cmd audio --port COM7
```

Start your phone video with sound, enter Analysis Mode, wait for mic warm-up, and launch once. Press B at the physical stop. Leave USB connected and wait for the saved path before starting another pull. The collector downloads the tape automatically, verifies every block and the whole recording, and matches the launch number and optical RPM to the calibration export.

The pair is in `firmware/build/audio/<trace hash>/launch.wav` and `launch.json`. Use `--output DIRECTORY` to choose its parent. The WAV contains mono 16-bit PCM at 4 kHz from release, the waveform the RPM tracker sees. The JSON includes the tape sample count and skipped audio milliseconds. Align the phone audio with the landing sound and verify against another sound where possible. The collector does not record the phone video.

Tape is volatile and remains armed until reboot. It holds 120 seconds total, at most about 90 seconds per spin. If the transfer was interrupted, finish the spin and retry with `audio --port COM7 --latest` before recording another launch or powering down. The collector uses the existing W, V and J firmware commands.

## Replay finish predictions locally

Run the evaluator from the checkout. It reads the existing calibration archives and compiles the current firmware predictor on the PC with a C++17 compiler (Visual Studio C++ Build Tools or `c++` on PATH). Python 3.10 or newer is required.

```powershell
python .\scripts\replay_calibration.py
```

The default directory is `firmware/build/calibration`. Pass a different directory as the first argument to evaluate one Beyblade and stadium setup at a time. Use `--step 5` for five-second checkpoints or `--csv replay.csv` for the individual results. Export the batch before exceeding 32 completed launches.

To compare the historical Coulomb–Viscous, Coulomb–Quadratic, Free Riccati, and Power-Law Damping fits on the PC, run this from the checkout:

```powershell
python .\scripts\replay_calibration.py --compare-models --csv historical-fits.csv
```

This comparison uses each complete raw trace and its manual stop. The CSV identifies the selected model and reports validation and full-trace RPM errors for each model when available. Older JSON exports with raw readings work directly. Fit-only summaries contain no trace to refit.

The evaluator replays raw observations from optical release at ten-second checkpoints until manual stop. It evaluates the active stamina predictor with each available prefix. It models 200 ms delivery for original and relative-pitch readings, 600 ms for acquisition backfills, and a 100 ms worker delay after an eligible refit. Device computation latency can delay actual publication. The stop label and final exclusion flags are withheld from the predictor. Timeouts and legacy summaries are skipped. Identical launches in multiple exports are counted once.

Two historical estimates use only strictly earlier exports still spinning at the same elapsed time. One uses their median finish time. The other uses median remaining time among up to five nearest launches, comparing optical RPM, last retained RPM, recent RPM change per second and time since the last retained reading. An unavailable recent slope leaves that method blank. Archive modification time determines export order, so preserve timestamps when copying a dataset.

Errors are in seconds and use the midpoint for a range. The report also counts how often a range contains the recorded stop. Paired rows compare all three methods at the same checkpoints, grouped by time since the last retained reading. Several checkpoints on one launch are correlated. Collect independent launches before judging accuracy.
