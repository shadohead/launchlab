# LaunchLab Mini for M5StickS3

The second M5 now runs **[0.13.6-sticks3-power-key-pm](../evidence/sticks3-0.13.6-power-key-20261015/README.md)**, privately installed October 15.
A single click of the side power key now saves and powers off; it previously triggered PM1's hardware reset, so the screen came back on about a second later.
The first M5 remains on 0.13.5.

Both M5 units now have **0.13.5-sticks3-idle-adc-pm** privately installed. The first unit was updated from the owner-tested [0.13.4 stillness build](../evidence/sticks3-0.13.4-stillness-20261011/README.md) on October 12 with exact application readback, preservation of the rest of flash, and a validated byte-identical user-data section: 44 pulls, 6 sessions and settings. It reports both experiments enabled and about 50 kS/s without ADC errors/overflow. Filtered sustained movement renews the inactivity timer; an unused shake wake sleeps about 30 seconds after qualifying movement stops. Buttons/pulls restore the selected normal timeout. The owner confirmed that sleep behavior in 0.13.4; battery endurance remains unmeasured. Intermittent IMU gaps and level/tilt dropouts remain under investigation in [the power/recovery notes](../../docs/FIRMWARE_POWER_RECOVERY.md).

The working source also contains a [staged 0.13.5 sampling-pause experiment](../evidence/sticks3-0.13.5-idle-adc-20261011/README.md), disabled by default and built separately with an explicit experimental flag. It pauses ADC/recorder work during stillness and keeps the IMU/display running. Its emitter remains powered because 3V3_L2 is shared with the processor. It is not installed; steady-hold/first-pull capture and measured savings need physical testing.

The second M5 has **[0.13.5-sticks3-idle-adc-pm](../evidence/sticks3-0.13.5-idle-adc-pm-20261012/README.md)**, privately installed October 12 with exact application/full-flash protection checks and byte-identical user-data backup sections (128 pulls, 24 sessions, settings and last motion). Both units use a complete rebuilt, pinned Arduino SDK for checked 40–80 MHz CPU scaling with automatic light sleep disabled. Separate UI/IMU locks end before their normal waits; the ADC driver's lock holds 80 MHz while sampling. Serial `p` reports `CPU_PM`, and read-only `C` exports cumulative frequency-mode times. `python3 firmware/tools/read_power_status.py --cpu` retrieves profiling without resetting the device; omit `--cpu` for older firmware, which treats unknown commands as interaction. Build with `LAUNCHLAB_PM_SDK=/absolute/sdk/root bash firmware/build-m5-pm.sh`; both experimental application flags remain off by default. All 39 native suites/14 production harnesses and target/stack checks pass; live checks report both features enabled and READY near 50 kS/s with zero ADC errors/overflows. Battery-only RESTING/first-pull acceptance and measured savings remain pending.

Current published release: **0.12.2-sticks3-recovery**, runtime `0.12.2-sticks3-flip-colors`.
The 808,144-byte application SHA-256 is `9548c322ae1f7383641e62ec5b81f7a2f54777c05034f8c5aac47a40b4f25b49`.
It retains the requested effects, sounds, ten accent colors, inverted default display orientation and balanced unbranded device UI.

The working **0.13.1-sticks3-sidecar-wipe** candidate adds Menu → Settings → Mounting → Top/Sidecar and **Wipe all**. Sidecar maps both acceleration and gyro into the existing Top launcher frame so the live level and saved tilt replay agree across mounts. Top is the migration default; Flip remains a separate display choice. Wipe requires two separate A confirmations followed by a fresh three-second A hold; B cancels before erasing. It clears launches, personal bests, motion recordings, settings and diagnostic logs, then restarts with defaults. See [Sidecar notes](../../docs/FIRMWARE_SIDECAR_MODE.md) and [wipe safety and recovery](../../docs/FIRMWARE_WIPE_ALL.md). The candidate is not installed or published.

The working source now builds **0.13.2-sticks3-power**, extending those features with weak-signal startup/rearm recovery, bounded weak launch tails, idle dimming, shorter unused shake wakes, reduced UI polling, checked PM1 idle wake handling and battery-voltage filtering. See [power recovery fixes and device acceptance](../../docs/FIRMWARE_POWER_RECOVERY.md). Battery current/runtime and low-charge optical behavior remain unmeasured on this candidate; it is not published.

Private installation on October 11 is now verified: [exact application readback and data retention](../evidence/sticks3-0.13.2-power-install-20261011/README.md). Runtime 0.13.2 returns READY at approximately 50 kS/s, with 128 records/24 sessions retained and PM1 timeout readback confirmed. Physical pull, battery-life and mounting acceptance remain pending; public firmware remains 0.12.2.

The owner subsequently reported the checklist stable except the unused shake-wake timeout. The installed local **0.13.3-sticks3-wake** [follow-up](../evidence/sticks3-0.13.3-shake-20261011/README.md) fixes a reproduced USB boot edge: remembered optimistic SOF without checked VIN no longer promotes a shake wake to the full timeout. Exact application readback, current backup/data retention, initial READY and approximately50 kS/s operation pass. The unplugged 30-second physical retest remains pending; no public release was made.

This release adds two independently reproduced detector corrections: reset the swing-ring write position together with its count after acquisition resumes, and preserve the fastest completed startup period while the latest Single Turn arrival is still being checked. The existing narrow abnormal split-pulse guard is retained.
The production source passes 31 sanitizer-backed native suites, eight production checks and the pinned M5StickS3 target/stack gate.
The retained private recordings verify software behavior against existing labels; they do not provide calibrated shaft RPM.

On October 11 the owner identified a soldering issue as the physical cause of the reported intermittent high RPM and stopped registration. The specific joint and repair details were not supplied. The recurring investigation is paused.
The installed diagnostic application `80c9bb99f6c5ce22313f38f75b3ed64bd37d95abe1a7ec39a9e679cf1448f3b6` contains the same detector corrections and was app-only flashed with exact readback and byte-identical settings/history retention. The new production 0.12.2 application has not itself been physically flashed or qualified by that related receipt.

Published on October 11: GitHub latest is `firmware-v0.12.2`; standalone Pages workflow `38160325174` passed at public source `651038b8688261288beada6464b7de7dac24c0c8`. All six downloaded GitHub assets and the four live Pages images match their expected hashes. Beywatch PR [#112](https://github.com/shadohead/beywatch/pull/112) merged as `60aacb595e4ef8dbbb3a88bb4db91a26c4ecb2fc`; production deployment `6999303368` succeeded. Final live Beywatch byte verification is recorded in the investigation handoff.

Release links: [GitHub firmware-v0.12.2](https://github.com/shadohead/launchlab-mini/releases/tag/firmware-v0.12.2), [standalone updater](https://shadohead.github.io/launchlab-mini/#updater), [Beywatch updater](https://launchlab.beywatch.gg/update).
The previous 0.12.1 assets remain preserved as a historical withdrawn prerelease; they are not overwritten with the 0.12.2 bytes.
See the [investigation chronology](../evidence/sticks3-0.12.1-waiting-spike-20261011/README.md) for earlier captures, generated defects, installation receipts and owner diagnosis. Earlier publication holds in that chronology are historical; the owner explicitly authorized this new release.

## Earlier device and development history

Preserved development revision: `0.12.1-sticks3-flip-colors`.
Flip display Off uses the former On orientation, and On uses the former Off orientation.
The saved switch keeps its existing value, so both existing choices rotate 180 degrees after upgrading.
The live level and saved launch replay follow the same display axes.
Theme offers ten Beyblade-inspired accents: Pegasus Cyan, Wolf Mint, Wizard Gold, Meteor Purple, Golem Green, Phoenix Red, Cobalt Blue, Shark Pink, Valkyrie White and Tyranno Orange.
The first four preserve the stored IDs and colors of Classic, Mint, Amber and Violet respectively; the six additional choices append new IDs without changing the appearance or backup format.
This revision passed native, production-path, actual-font rendering, ESP32 compilation and stack checks; [candidate evidence](../evidence/sticks3-0.12.1-orientation-colors-20261010/README.md) includes its exact binary and previews.
The subsequently requested removal of the displayed product name and USB boot branding was built as [a new exact candidate](../evidence/sticks3-0.12.1-unbranded-20261010/README.md), SHA-256 `772c1cdf3042d42fb68ee003a6b6dc5bf78d31160bf1e4516ef8deced5601ed7`.
It was installed privately on the owner's device; full application readback, retention of 128 records and 24 sessions, all ten theme previews, Flip preview/cancel and theme/Flip restart persistence passed.
The settings at that earlier acceptance check were Flip Off, Wolf Mint, Shockwave and Sound High.
The owner confirmed correct physical orientation, the absent name and one normal pull/replay; telemetry confirmed exactly one valid result, valid motion and a completed save in the [device receipt](../evidence/sticks3-0.12.1-device-20261010/README.md).
The preceding [small-status layout build](../evidence/sticks3-0.12.1-small-status-20261010/README.md), previously installed for the owner's requested comparison, has SHA-256 `25eb22f75833dc4ce353a3c69aca1bfd3330ed0209396b4c9020d29cf831e7e5`.
Ready retains its original small Font2 at the top, RPM is raised and the live level is enlarged; long status labels remain within the display and errors take priority over the session notice.
The owner rejected the larger Ready label in the preserved [preceding layout build](../evidence/sticks3-0.12.1-main-layout-20261010/README.md).
Full readback, all backup sections, 153 history export lines and the actual device screen passed the [final layout installation check](../evidence/sticks3-0.12.1-small-status-device-20261010/README.md), including retention of the latest saved pull.
The physical pull was not repeated for this layout-only refinement; acquisition, controls and recap behavior are unchanged.
That unguarded application was not published and is now replaced by the exact guarded build above.

Preserved private animation freeze: [0.12.0 effects and sound](../releases/0.12.0-sticks3-effects-sound/README.md), runtime `0.12.0-sticks3-effects-sound`.
It refreshes Orbit, Flames, Sparkles, Shockwave and Lightning celebrations, with Random and Off choices and optional matching Sound Off/Low/High.
The exact application passed app-only installation and readback, data retention, device effect/editor checks, normal pulls and one owner-confirmed pull during active High Flames sound.
Actual physical personal-best/Random sequencing remains unobserved, with those production paths covered by host tests; see the archive's scoped acceptance and limits.
The archive is frozen and unpublished; isolated public catalog/browser software checks passed, with physical browser-write acceptance unclaimed.

The live standalone web updater catalog recommends 0.11.2 and excludes 0.12.1; withdrawal and retained versioned payload integrity are verified.
GitHub latest stable is 0.11.2, with 0.12.1 retained as a prerelease.
The last verified Beywatch recommendation remains [0.11.2 score preview](../releases/0.11.2-sticks3-qre1113-scorepreview/README.md), runtime `0.11.2-sticks3-score-preview`; its 0.12.1 rollout is paused after the renewed fault report.

Previous release: [0.11.1 themes and backup](../releases/0.11.1-sticks3-themes-backup/README.md), runtime `0.11.1-sticks3-themes-backup`.
It fixes the 0.11.0 crash loop: the loop task overflowed its 8 KB stack on the first status print after boot and restarted about once a second.
0.11.0's loop() frame was 6,864 bytes, mostly the backup receiver's reset building a 5 KB temporary plus the encoder's record images, inlined into loop().
0.11.1 keeps those buffers off the stack, makes loop()'s helpers real calls (loop() is now 800 bytes) and the build fails if a frame exceeds its budget (`tools/check_stack_usage.py`).
`p` status now reports `STACK loop_min_free_bytes`.

Withdrawn: 0.11.0 (`0.11.0-sticks3-themes-backup`) crash-loops on hardware; do not install it.
Settings gain Theme (Classic, Mint, Amber, Violet) and Best effect (Orbit, Flames, Sparkles, Shockwave, Crown, Off); a new personal best for the active sensor profile and RPM method plays the chosen effect on the recap.
See [appearance-implementation.md](../../docs/appearance-implementation.md) for storage and rendering bounds.
USB backup and restore: `K` exports history, sessions, personal bests, appearance, the saved recap and user settings as one checksummed container; `R` restores one, validated in full before any write, then restarts.
The detector, estimators, sampling, thresholds and saved history format are unchanged from 0.10.22.
Installed on the owner's K150 on 2026-10-09: no restarts, history kept, backup and restore round trip identical, screen, themes and pulls confirmed.

Previous release: [0.10.22 save rearm](../releases/0.10.22-sticks3-saverearm/README.md), runtime `0.10.22-sticks3-saverearm`.
Automatic idle saves (history about ten seconds after a pull, the five-minute power log, and Settings Apply) now resume the detector in Ready instead of Starting.
In 0.10.21 a pull that began in the roughly 1.2 seconds after such a save was silently missed, so pulling about every ten seconds lost pulls.
The idle checkpoint used by the power log, Settings Apply and auto-off now also refuses to pause while the first strong marks of an unconfirmed pull are arriving.
Detection thresholds, the UI and saved data are unchanged.

Previous published release: [0.10.21 quiet setting hint](../releases/0.10.21-sticks3-quiethint/README.md), runtime `0.10.21-sticks3-quiethint`. It was the web updater's default when published (public release `0.10.21-sticks3-qre1113-quiethint`, published 2026-10-08), and the user reported it working. One firmware serves QRE1113 and TCRT5000 through the saved Sensor profile setting. Values in setting editors are centered at full width, with faint `B next option ->` text beneath the value card. Button actions remain unchanged.

Preserved side-hint revision: [0.10.20 setting cycle hint](../releases/0.10.20-sticks3-cyclehint/README.md), runtime `0.10.20-sticks3-cyclehint`. Each open setting shows its value in a selector box with a visible **B →** affordance on the right. B cycles the choice; A applies and returns. The footer says `A Apply  B Next`, and hold B still cancels the edit. Long labels use a smaller font to fit beside the cycle indicator. Navigation and button actions retain the 0.10.19 behavior.

Preserved tilt correction: [0.10.19 tilt display orientation](../releases/0.10.19-sticks3-tiltflip/README.md), runtime `0.10.19-sticks3-tiltflip`. The 3D recap uses the same saved or previewed 180-degree display axes as the live level and frozen Start/End dots. Conversion occurs after cached pose lookup, so recalling an existing launch also follows the current screen orientation. The white marker remains on the physical USB end. Rotation capture, replay timing, sensor profiles and saved history retain the 0.10.18 behavior. Software checks and device installation are recorded separately in the release manifest.

Preserved profile revision: [0.10.18 saved sensor profiles](../releases/0.10.18-sticks3-sensorprofiles/README.md), runtime `0.10.18-sticks3-sensorprofiles`. One firmware contains **QRE / Standard** (80-count minimum) and **TCRT5000** (60-count minimum), selected in Menu → Settings → Sensor profile. A edits/applies, B changes the draft, and hold B cancels. Changes wait for idle and start the next pull in a new practice session; history remains intact. The saved choice loads on restart and shake wake. A fresh device defaults to QRE / Standard. Build one image with `bash firmware/build-m5.sh`; no compile-time sensor selection is needed. The initial TCRT installation preserved that device's working profile by applying it through the setting after the update.

The following 0.10.17 build-specific profile is frozen as a tested recovery revision; its historical compile-time selection is superseded by the saved runtime setting.

Current local TCRT trial: [0.10.17 sensitivity](../releases/0.10.17-sticks3-tcrt-sensitivity/README.md), runtime `0.10.17-sticks3-tcrt-sensitivity`. Two reported normal pulls produced clean 62.72–71.40 count optical marks, rejected by the previous 80-count minimum. The opt-in TCRT profile uses 60 counts; the default standard/QRE build keeps 80. Both retain 0.70 hysteresis, 50 kS/s acquisition, three-turn confirmation and the current controls. The archived 0.10.17 source used a compile-time TCRT selector; current builds use the saved Sensor profile setting instead. Replay recognition and fresh-device acceptance remain separate checks.

After installation, the user confirmed six physical pulls in total. The device recorded six results, increased history from 44 to 50 records, and produced six valid fused motion traces. The user reported every requested pull detected and ordinary handling quiet; the monitored count stayed at six afterward. This establishes an initial live detection/handling check for this mounted TCRT setup, not absolute RPM or long-session qualification.

The preserved [0.10.16 controls](../releases/0.10.16-sticks3-controls/README.md) revision received positive user feedback, but the connected TCRT showed no RPM on two normal pulls. A opens or applies the visible action, B advances choices, and hold B returns one level or cancels an edit. Main keeps A recap and hold A tournament; tournament B cycles Recording Only/RPM. Menus remain visible while launches continue recording. Brightness/display flip preview before applying and revert on cancel. Sleep defaults to three minutes, with 1–10 minute choices. Software verification and installation receipts are recorded in the release manifest; physical trial results remain separate.

The prior unpublished [0.10.15 display flip](../releases/0.10.15-sticks3-displayflip/README.md) candidate is retained. Its saved 180-degree orientation is included in 0.10.16, with revised controls and reversible preview.

Previous local revision: [0.10.14 focused launch UI](../releases/0.10.14-sticks3-analog-cleanui/README.md), runtime `0.10.14-sticks3-cleanui`. Main, recap, history and Settings omit save/pending captions. Recap always says "Last launch"; history controls stay visible. The normal product UI focuses on RPM, tilt, practice trends and user controls. Implementation details about saving, queues and storage belong in USB diagnostics, not ordinary device screens or user guidance. Settings only show "Try again" when a requested change did not take effect.

Installed and checked on the SparkFun QRE1113 analog development setup on 2026-10-07. The user corrected the earlier TCRT5000 identification after this check. After physical restart, live paired IMU acquisition and fused orientation were working. Three normal consecutive pulls each produced valid tilt; recap presentation took 202–216 ms after the detected end. The user confirmed all three appeared promptly, without a stationary-start requirement. The candidate remains unpublished; browser installation is a separate check.

Retains [0.10.13 IMU startup recovery](../releases/0.10.13-sticks3-analog-imurecover/README.md). The development device's 0.10.12 boot reported no IMU driver, no recording task and zero paired samples; the user reported no tilt playback. Explicit IMU reinitialization restored live level data but the missing owner task did not restart. The recovery checks fresh finite paired accel/gyro data, retries startup up to three times, and retries a missing owner at five-second intervals when idle. Owner allocation is idempotent and retains reusable mutexes after failure. Actual physical startup and consecutive-pull checks are recorded separately in the manifest.

The 0.10.12 timing policy is retained: history and periodic power-log saves wait for ten seconds since the last accepted pull, completed capture and no visible recap. Long bouts no longer force a save by age. A valid pull may use a shorter continuous, settled pre-roll, and post-roll is 150 ms. Actual pull gaps and unsettled poses still reject tilt; VQF's 1.5-second recovery and optical rearm are unchanged. The attached development sensor is SparkFun QRE1113 analog; the shared 80-count / 0.70 profile is unchanged, and the old sensor archive remains separate.

Historical published update: [0.10.11 lower CPU clock](../releases/0.10.11-sticks3-qre1113-lowcpu/README.md). Runs at 80 MHz with the 80 MHz peripheral clock and unchanged 50 kS/s optical rate. Bench/runtime checks and the user normal-pull/replay check passed; latest exported tilt is valid/fused. Startup clock/readback failure falls back to 240 MHz. Actual battery-current/endurance improvement and a battery shake-wake cycle at this clock remain unmeasured. That version's post-pull flash stalls still create rejected IMU gaps. Published to [GitHub](https://github.com/shadohead/launchlab-mini/releases/tag/firmware-v0.10.11) and the [web updater](https://shadohead.github.io/launchlab-mini/); live download hashes and Pages checks passed.

Previous published update: [0.10.7 clearer navigation](../releases/0.10.7-sticks3-qre1113-navigation/README.md). History, battery, motion and Settings pages in that release say `A next page  B main`: tap A cycles the pages; tap B returns to the main launch screen. The battery page shows percentage, icon, voltage and charging status; no clock, countdown or approximate-charge caption. Settings brightness is saved in 10–100% steps; hold B selects Sleep/RPM/Brightness, hold A changes it. Shake-only standby, green LED off while awake and saved power diagnostics remain from 0.10.2.

Unpublished **0.10.10-sticks3-qre1113-usbawake**, runtime `0.10.10-sticks3-usbawake`, rebuilt locally and installed on the connected development device 2026-10-06. App hash, NVS preservation, runtime and USB detection verified; fresh-pull replay acceptance remains pending. Frozen unpublished candidate **0.10.10-sticks3-qre1113-usbawake**, runtime `0.10.10-sticks3-usbawake`: Recreate and the separate Motion page are removed. The latest-launch recap remains automatic and can be recalled with A. History, tilt capture and previously saved motion bytes are retained. Controls below describe the frozen 0.10.10 candidate; published 0.10.7 retains its earlier pages. This candidate also keeps the device awake while a USB computer is detected, including an idle computer with no serial reader. Frozen 0.10.9 retains the preceding recap repair and remains unchanged. The recap repair adds a replay clock anchored after the first complete displayed frame and plays the full measured pre/pull/post context at 1×. History saves coalesce for 2 seconds after the latest pull, with a 10-second eligible attempt target; capture and replay can defer it. The screen marks pending records as “Unsaved launch”. Automatic standby flushes pending history; a failed flush keeps the device awake. Abrupt power loss can lose an unsaved bout. Real timing gaps still reject tilt until fusion and pre-roll recover. Frozen 0.10.8 remains unpublished and physically blocked after reported missing playback and unavailable tilt; its binary is retained unchanged. Software verification, device qualification and public-release status are recorded in the release manifest.

**Working combination: M5StickS3 K150 + SparkFun QRE1113 analog.** The user
confirmed on 2026-10-01 that the installed firmware "works pretty well".
The confirmed baseline binary and source are saved as
[0.3.0-sticks3-qre1113](../releases/0.3.0-sticks3-qre1113/README.md); its runtime
version is `0.3.0-sticks3-practice`. The existing 80-count / 0.70 optical
profile is unchanged. The original old-sensor three-pull release is preserved as
[0.2.0-sticks3-tcrt5000](../releases/0.2.0-sticks3-tcrt5000/README.md).
The QRE confirmation is a qualitative live-use result; an annotated exact
pull-count trial, absolute RPM calibration and battery power-cycle retention
are separate checks.

The refined single-sensor orientation recap is built offline and saved as
[0.10.1-sticks3-qre1113-tiltreplay](../releases/0.10.1-sticks3-qre1113-tiltreplay/README.md).
The former [estimated-travel recap](../releases/0.5.1-sticks3-qre1113-front/README.md) is preserved.
The prior [cube recap](../releases/0.5.0-sticks3-qre1113-cube/README.md) is preserved.
The prior [frozen level recap](../releases/0.4.3-sticks3-qre1113-recap/README.md) is preserved.
The prior [motion/recreate update](../releases/0.4.2-sticks3-qre1113-feedback/README.md) is preserved.
The prior [power update](../releases/0.3.1-sticks3-qre1113-power/README.md) is preserved.
Current development runtime `0.13.1-sticks3-sidecar-wipe`, for StickS3 K150 (ESP32-S3,
8 MB flash, 8 MB OPI PSRAM). Uses the installed Arduino ESP32 3.3.0,
M5Unified 0.2.23 and M5GFX 0.2.30 dependencies. This is a separate board
application; the Waveshare binary cannot be installed on the M5.

## Acquisition and performance

`acquisition.h` owns all ADC calls, detector state and recorder writes in one
priority-3 task on core 0. M5Unified, screen drawing, buttons, USB and
Preferences remain on Arduino core 1. The IMU also runs on core 0, at
lower priority than the ADC owner; it never owns the ADC. The UI exchanges commands and immutable
snapshots through queues and short critical sections. It never reads the live
detector or changes the ADC handle directly. Results/state changes use a
bounded event queue, with loss explicitly counted in diagnostics.

Sampling uses ADC1 continuous DMA, 50,000 samples/s, 12-bit, 12 dB attenuation,
TYPE2 output, 1,024-byte frames and a 32,768-byte driver pool. GPIO mapping is
queried from the installed driver. AO has no pull-up or pull-down. Each sample
advances detector time by 20 us regardless of UI or USB delays. All samples in
a frame must match the selected unit/channel; malformed frames and DMA
loss invalidate the detector sequence and recorder continuity. ADC read errors
and overflow trigger recovery in the owning task. Setup failures are retried
once per second.

The recorder retains 60 seconds (6 MB) in PSRAM. A wrapping cursor replaces
64-bit modulo in the per-sample write path. Absence of PSRAM recording does
not stop RPM acquisition and is reported as `recorder_s=0`.

`PERF` reports sample-processing wall time as a fraction of the acquisition
window, worst batch duration, worst read interval, task stack headroom,
invalid frames, lost events, display duration and available memory. Processing
percentage excludes blocking ADC reads and snapshot/driver overhead; it is
not a measurement of total CPU utilization. One 256-sample batch has a
5,120-us time budget. Keep batch duration comfortably below this budget and
verify zero ADC loss under UI/USB load.

## M5 sensor profile

The shared detector retains its original Waveshare defaults: 300-count mark
floor and 30% reversal hysteresis. `sensor_profile.h` applies **M5-only**
settings: **80-count minimum mark**, **70% of repeated mark swing** for reversal
hysteresis. This change is based on the confirmed 2026-09-30 TCRT5000 pull:
89–130 count marks contain subsidiary peaks that the original detector counted
as extra cycles. Lowering the mark floor alone does not decode that capture.

One cycle/revolution, the 1,000-RPM start floor, three consistent revolutions
to confirm a pull, first-burst selection and quiet/rewind rearm remain.
Settings offers two results.
**3-turn average** is the maximum valid three-consecutive-turn elapsed-time
average: `180000000 / (period1_us + period2_us + period3_us)`.
This restores the frozen 0.6.2 algorithm, not an arithmetic mean of RPMs or three
nonadjacent fastest turns. **1-turn peak** is the default from 0.18.0 (before that, 3-turn average): after confirmation,
one complete valid revolution can set its peak, without requiring that faster speed to repeat.
Its timing, duty, amplitude and neighboring mark must match established optical shape.
The arrival mark is checked after the 0.5-ms optical mean settles.
The published split-pulse guard also checks following intervals before accepting a new single-turn maximum.
A peak already confirmed by a strong, consistent three-turn window retains the original arrival check and does not depend on a later mark.
If two short intervals sum to the preceding full interval within 200 us and the next interval returns to that preceding cadence, the still-unconfirmed single-turn candidate is rejected as a possible subdivided mark.
The tolerance accounts for the detector's 100-us edge timestamp grid; it is not an RPM ceiling or general acceleration limit.
A clean final surge can be accepted when the burst ends, so sustained acceleration and terminal one- or two-turn peaks remain supported.
An actual transient with exactly the same split-and-return waveform is ambiguous and is conservatively rejected; a terminal split without a return interval cannot be distinguished by this guard.
Moderate changes already accepted by a strong three-turn window remain accepted, even if later intervals could also be interpreted as a split.
Both metrics are calculated from the same stream; the three-turn control path governs confirmation, slowdown and quiet/rearm timing for both selections.
Tracking the alternative cannot extend quiet/rearm timing.
Other boards retain their original detector defaults.

The existing timing ceiling is 30,000 RPM (2-ms periods), a measurement bound,
not a scientifically established human limit. Detector edge timestamps have
100-us resolution; a single-turn peak is more sensitive to quantization and
optical jitter than the former average. It remains an average over one turn,
not instantaneous speed within that turn or directly measured release RPM.
`RPM_ESTIMATORS` reports both metrics and the selected method; `LAUNCH_METRICS`
logs both and the selected result for each accepted shot in RPM units. No magnetometer/IMU speed estimate
is substituted for optical speed.

Saved traces reproduce ~4,945 RPM three-turn and ~5,128 RPM single-turn estimates
for the confirmed old-sensor pull. The 14-trace comparison matches frozen 0.6.2
three-turn results and prior single-turn peak values. Several traces are from
another board or lack individual pull annotations; these are software evidence,
not absolute RPM calibration or proof of mounted repeatability. The 100-us edge
grid and optical shape can affect results. No 60-Hz notch is applied, since it
could remove a legitimate 3,600 RPM signal.

The new versioned `launchlab-mini/rpm_method` key persists the selector; missing
means the default (`RpmEstimator::DEFAULT_MODE`, 1-turn peak from 0.18.0) and corrupt bytes are retained with a default fallback.
A unit that never changed the method has no record, so updating it from 0.17 or earlier moves it from 3-turn average to 1-turn peak; a saved choice is kept.
Valid sleep/GPIO settings and existing history/reference images are preserved.
Changing method starts a new session on the next accepted pull and clears the
live number until then. Boot also starts a fresh session. Existing history stores
only its original scalar RPM: older records have no encoded estimator identity,
and neither historical values nor trend labels are rewritten. Paired metrics
are in RAM and USB logs, not added to the persistent history schema. Therefore
cross-session trends can still span different estimators; compare sessions with
the same selected method. The single-turn figure is not instantaneous release
speed, and this measurement is not official Shoot Power.

The normal screen stays still during an accepted burst. Screen start/end
boundaries are retained in recorder bits 15/14, on the first consumed ADC batch
after each UI boundary (about 5 ms resolution). They are evidence markers;
**the Waveshare's measured 2.5–14 ms blanking interval does not apply here**.
M5 exports report `display_blanking=false`. Use `scripts/test-real-captures.sh`
for regression replay through the production detector and paired estimators;
it treats display markers as observations without applying electrical blanking.
The older `replay_sticks3.cpp` is a historical mode-comparison tool and changes
detector configuration, so its results are not the production regression gate.

## Wiring and controls

AO defaults to Hat G1, ADC1 channel 0. Sensor VCC uses 3V3_L2/3V3, and ground
uses GND. GPIO0/BOOT is not an ADC input. Use analog AO/OUT. Grove red is 5 V;
do not treat it as a 3.3 V sensor supply. The current user's sensor-end supply
was measured at 3.3 V. The firmware does not automatically alter sensor power,
ADC attenuation, or GPIO selection.

- Portrait screen with USB-C / Grove down (rotation 0, 135×240 pixels).
  Main screen retains peak RPM and detector state above the bubble, without
  a divider or "LEVEL" label. Large
  RPM numbers automatically use a smaller font if needed to fit the width.
- A circular level and tilt angle appear below RPM on the main screen. The
  built-in BMI270 is initialized through M5Unified; fresh acceleration is
  sampled by a dedicated priority-2 task on core 0, polling every 5 ms,
  with 200-ms level smoothing. Display changes are
  limited to 10 Hz and meaningful dot/angle changes. The dot is cyan within
  2 degrees and orange beyond; 30 degrees reaches the rim. The screen plane is
  the reference, not a calibrated launcher mounting plane or launch trajectory.
  Both bubble directions are reversed from 0.3.1 following the user
  report that left/right and up/down were mirrored on the mounted device.
- Accelerations outside 0.75–1.25 g hide the bubble and show "--". Missing
  or stale (>250 ms) samples show "--". Acceleration within that interval can
  still affect tilt, so use level while holding still. Normal display updates
  remain frozen during accepted RPM bursts. IMU calls cannot block the
  independent ADC owner.
- Measurement stays enabled on every screen. Main A shows the latest recap; recap A returns to Main. Main/recap B opens a labelled menu. A opens the selected item; B advances the highlighted choice. Hold B returns one level. History offers Launches, Pull trend and Session trend; A returns to the history choices and B browses older windows. Every list includes a Back choice.
- The menu contains History, New session, Battery, Settings, Tournament and Back. New session offers Cancel/Start, initially Cancel, and retains prior launches. The next accepted pull begins the new session. A on Battery returns to the menu.
- Settings contains Sleep after, RPM method, Brightness, Flip display, Sensor profile, Theme, Best effect, Celebrate, Sound, Mounting, Wipe all and Back, five rows to a page. Celebrate chooses what plays an effect: New best, Milestones (the RPM milestone table, edited on the web) or Both (the default from 0.18.0, New best before); see [RPM milestones](../../docs/FIRMWARE_RPM_MILESTONES.md). Theme cycles through the ten built-in themes, then any custom themes sent from the beywatch theme editor (USB `L`/`T`); a custom theme that is missing, invalid or unreadable shows Pegasus Cyan. Every screen color comes from the theme's palette (`palette.h`); the built-in themes draw exactly as before custom themes, pinned by `tests/check_ui_render.py`. A edits the highlighted setting; B selects the next row. In an editor, B cycles values, A applies, and hold B cancels. Wipe all instead opens two warning confirmations and requires a fresh three-second A hold; B cancels before erasing. Sleep is 1–10 minutes (three-minute default); RPM is 1-turn peak (default from 0.18.0) or 3-turn average; brightness is 10–100% in 10% steps (30% default from 0.18.0, 40% before; `brightness.h`). A saved choice always wins; a unit with none, including after Wipe all, uses the defaults. Brightness and display flip preview immediately when not in a measured burst, and cancellation restores their committed values. A applies the choice at once and returns to the list, even mid-pull; its record is written in the next idle checkpoint (see Practice history). RPM method, sensor profile and mounting switch inside that checkpoint, never during a pull or its capture, and an RPM or sensor change begins a new session on the next accepted pull. An unfinished edit is cancelled before sleep.
- Hold A for one second on Main enters tournament, initially Recording Only. B cycles Recording Only/RPM; A taps and B holds are ignored. Hold A exits. The selected mode/view resumes after shake wake. A short hint appears after entry/view changes, then disappears. A genuine acquisition fault replaces the recording caption with Check sensor. Sensor modulation/pull reliability remains a separate physical check.
- Accepted launches automatically show a five-second recap only from Main/recap. Menu/history/settings focus and draft values remain intact during pulls. Stored Start/End dots remain fixed; the compact circles leave room for one-line recap controls. Physical A/B identities remain unchanged after display flip. Holds show progress, fire once and suppress a release click. No double taps or button chords are required for app navigation.
- USB diagnostics remain explicit commands. Internal storage/checkpoint details are hidden from normal pages. The source profile is shared by analog AO sensors; TCRT/QRE release provenance and physical qualification remain separate. Current replay accepts the preserved confirmed TCRT traces, but that does not validate a newly connected TCRT module.
- Side Power key: while running, a single click saves pending pulls and powers off; a single click turns the unit back on.
  From 0.13.6 the firmware sets M5PM1 `SINGLE_RST_DIS` (BTN_CFG_1 0x49 bit 0), which otherwise makes a running single click a hardware reset: the screen went dark and the app restarted about a second later.
  PM1 routes only the click event to its IRQ line (PM1 GPIO1 to ESP32 G13, schematic `PYG1_IRQ`); the loop reads and clears it, so an idle key adds no PM1 I2C traffic.
  A click during a measured burst or motion capture powers off once it finishes; a failed save keeps the unit on.
  The click is ignored during an erase and cancels an unconfirmed wipe prompt.
  If routing cannot be configured, the hardware reset is left enabled.
  Double-click off and long-press download mode remain built-in PM1 controls, so an unresponsive app can still be switched off.
  On USB power the ESP32 can stay powered after PM1 power-off, so it deep-sleeps until the next click asserts G13.
  Older firmware does not clear `SINGLE_RST_DIS`; if PM1 retains it, a single click on rolled-back firmware does nothing, while double-click off still works.
  USB command `O` runs the same save-and-power-off path; `p` reports `POWER_KEY`, and boot logs `POWER_KEY_BOOT` with the button configuration PM1 retained.
- USB letters are guarded against stray byte streams (`usb_command_guard.h`).
  The status letters `p`, `P`, `C` and the updater's backup `K` and restore `R` run at once.
  Every other command must stand alone: it starts after 20 ms of input quiet or on the line right after an accepted command, and ends with a newline or 20 ms of quiet.
  Quiet means the receive queue was seen empty for 20 ms, so a slow loop pass or a long export never makes a queued burst look quiet.
  `g<pin>` needs its newline; `g<pin>` and `E<id>` may be typed key by key, except that a two-digit effect id must arrive in one write (its second digit within 20 ms).
  Send several commands as separate lines (`h\nh\n`); glued letters such as `hh` are rejected and counted in PERF `usb_rejected_commands`.
- Automatic IMU-only sleep is enabled after a configurable timeout (default 3 minutes) without a measured pull,
  button use or an interactive USB command. Background level/battery updates,
  raw optical noise and status polling do not reset the timer. A USB computer inhibits
  inactivity sleep even with no serial reader; ordinary battery auto-off resumes
  with a full selected timeout after disconnect. A charger alone does not
  establish a host. Host detection uses USB SOF, polled at 250 ms. Once observed,
  a host remains remembered through USB suspend while a checked PM1 5VIN
  source read reports external input (at most once per second outside a pull).
  A failed power read is not treated as unplugging; stale/unknown power has a
  bounded 2-second grace before the battery timer resumes, and later checked
  input can recover the remembered host. Boot-time USB suspend before any
  host SOF cannot be distinguished from a charger; an uninterrupted external
  supply during host-to-charger swapping may retain host identity until a
  sampled input loss or restart. These are detection limits, not charging controls.
  Charging/boost settings and manual side-button controls are unchanged.
  Active measured
  bursts are protected by the ADC owner. Pending history data is saved before
  sleep; a failed save defers sleep and retries after a minute.
  A shake wakes the device through BMI270 any-motion and M5PM1 GPIO4.
  Only the accelerometer/IMU rail is held on; the sensor's 3V3_L2 rail turns off.
  Initial shake threshold is approximately 983 mg for 120 ms on any axis.
  Ordinary IMU movement while awake does not extend the selected timeout.
  Wake restarts the firmware and restores the previous mode/page; older Motion/Recreate wake records open Recent history. Settings keeps its original numeric wake value. Wait for the screen before pulling. The normal
  VQF startup settling period still applies to motion feedback, not optical RPM.
  A manual side-button power-off remains manual; it does not arm shake wake.
  USB command `i` arms, reads back and restores the wake configuration without sleeping.
  Current draw, wake latency and shake sensitivity require physical validation.
- The battery page is Menu → Battery (tap B from Main, B to Battery, A to open). It shows approximate charge percentage, battery voltage,
  a battery icon and charging status; it has no countdown or clock. PM1 I2C battery/status reads
  run at most once every five seconds on the UI core. There are no battery ADC
  calls and no claimed hours remaining. Missing/stale reads show "--".
- USB `p`: status, detector profile and performance.
- `e`: export the latest real motion points and any retained older reference.
  The removed `m`, `r`, `k`, `l` and `f` commands no longer select pages or write references.
  `q`: labeled five-second recap preview, also RAM-only, with no real-record writes.
  `z`: guarded reference storage bench: 16 saves/reopens in `ll-mot-qa`,
  cleaning only its own scratch keys. Like `y`, it first checks NVS space and
  prints `MOTION_CHECK_SKIPPED reason=nvs_space` instead of writing when the
  two scratch slots plus a rewrite (117 entries) do not fit.
- `MOTION_STATUS` reports fresh paired samples, task timing/stack, capture quality,
  retained-reference load state and dropped pending captures.
- `LEVEL_STATUS` reports IMU readiness/type, fresh sample count, rejected
  samples, angle, acceleration and worst poll duration.
- `a`: recent/live toggle; `d`: signal display.
- `a`/`c`: tap A; `h`: tap B; `o`: hold A; `b`: hold B (back); `n`: new session;
  `u`: export retained launches and full session summaries to USB.
- `K`: export a device backup: `BACKUP_BEGIN`, base64 `BACKUP_DATA` lines and
  `BACKUP_END`. The container (`LLBK`) holds the practice history
  image, appearance, personal bests, the saved recap reference when present and
  user settings (sleep, brightness, RPM method, sensor profile, display flip),
  sealed with an FNV-1a checksum. It is read from RAM, so pulls not yet flushed
  are included. Sensor GPIO, 5 V output and the power log are device-specific
  and stay out.
  Format 2 adds the custom themes record; a backup with no custom themes whose look selects a built-in is still written as format 1, byte for byte as before, so older firmware and updaters keep reading it.
  `R` accepts both formats; only a format 2 backup replaces the device's custom themes.
  Format 3 adds the milestones record (Celebrate mode and milestone table), written only when they differ from the defaults; its custom themes section is optional.
  Format 4 (0.16) carries a table 0.15's record cannot hold (effect pools, more than eight rows or an effect 0.15 lacks) as the pooled record in section 8 instead of section 7; any other table is still format 3, byte for byte.
  Format 5 (0.17) adds the effect set record (section 9), written only when the set differs from the default; its custom themes and milestones sections are optional, and its milestones are always the pooled record (section 8). A backup with the default set stays format 1-4 byte for byte.
  Format 6 (0.18) carries a table whose pools hold an effect past value 63 as the 128-bit pooled record (v3) in section 8; its effect set section is optional. Any other backup stays format 1-5 byte for byte.
  Older firmware refuses a newer format whole (`RESTORE_ERROR reason=invalid_backup written=0`): 0.15 reads formats 1-3, 0.16 formats 1-4, 0.17 formats 1-5.
  `R` accepts formats 1-6; only a backup that carries milestones replaces the device's milestones, and only one that carries the effect set replaces its set.
- `R`: restore a backup. The device answers `RESTORE_READY`, acknowledges each
  base64 line with `RESTORE_CHUNK` and treats a `.` line as the end. The whole
  container is decoded and every record validated before anything is written;
  then, inside one ADC-owned idle checkpoint, each store is written with
  readback at a generation newer than any saved slot, and the device prints
  `RESTORE_OK` and restarts. `RESTORE_ERROR reason=busy` (a pull in progress)
  writes nothing; `write_failed` may leave some records restored, and repeating
  the restore is safe. A transfer idle for 15 s is abandoned.
- `L`: list the custom theme slots: `THEME_LIST` (selected and shown theme IDs, record state, free and needed NVS entries, and since 0.16 `colors=any`, which tells the web editor that light and low-contrast themes are accepted), one `THEME_SLOT` per slot (state, the 32-byte slot record in base64, then `name=` to the end of the line) and `THEME_LIST_END`.
- `T`: one custom theme request, framed like a restore: `THEME_READY`, one base64 line (`THEME_CHUNK`), then a `.` line.
  The 44-byte request (`theme_library.h`) is magic `LLQ\1`, an op (put, delete, select, clear), a slot or theme ID, a slot record for put and an FNV-1a checksum.
  Put refuses only a malformed name (1-14 printable ASCII characters, no space at either end); any colors are accepted (since 0.16; 0.14 and 0.15 refused a light background and text under 4.5:1).
  It checks NVS room for a history rewrite beside the record; put, delete and clear write inside an ADC-owned idle checkpoint with readback.
  Replies are `THEME_OK op=... slot=... revision=... state=... warnings=...` or `THEME_ERROR ... reason=busy|name|bad_slot|invalid_request|nvs_space|store_unreadable|storage_unavailable|unavailable|write_failed`.
  `warnings` is `none` or a comma list of `light_background` (celebrations add light, so they fade on it), `low_contrast:<role>` (under 4.5:1 on the background, 3:1 for muted) and `invisible:<role>` (under 1.5:1), the same advice the web editor shows.
  Deleting the selected theme returns to Pegasus Cyan; select saves like Settings Apply.
  The beywatch theme editor (`/launchlab/themes`) speaks this protocol.
- `M`: list the Celebrate mode and milestone table: `MILESTONE_LIST` (`version=3`, mode, rows, limits, the number of effect IDs, revision, record state `ok`/`damaged`/`unavailable`, whether it is the default, free and needed NVS entries, the 232-byte v3 record in base64), one `MILESTONE_ROW` per row (RPM, the pool's first effect name and ID, `pool=` as 32 hex digits, `effects=` the pool's labels joined by `+`, `next=` its next pick) and `MILESTONE_LIST_END`.
- `U`: one milestone request, framed like a theme request: `MILESTONE_READY`, one base64 line (`MILESTONE_CHUNK`), then a `.` line.
  The request (`milestones.h`) is magic `LLU\3` (232 bytes, 0.18), `LLU\2` (136 bytes, 0.16 and 0.17) or `LLU\1` (40 bytes, 0.15), an op (set, or reset to the defaults), the mode and up to twelve rows (eight in v1) of RPM and a 128-bit pool (v3), a 64-bit pool (v2) or one effect (v1), and an FNV-1a checksum.
  Set checks the whole table first (1-12 rows, 1,000-30,000 RPM, strictly rising, each a drawn effect) and NVS room for a history rewrite beside it, then writes inside an ADC-owned idle checkpoint with readback.
  Replies are `MILESTONE_OK op=... revision=... mode=... rows=...` (or `unchanged=1`) or `MILESTONE_ERROR ... reason=busy|invalid_request|invalid_table problem=mode|count|range|order|effect|unused row=N|nvs_space|storage_unavailable|write_failed`.
  The beywatch milestone editor (`/launchlab/celebrations`) speaks this protocol.
  The effect set (`effect_set.h`) rides on the same commands: `MILESTONE_LIST` ends with `effect_set=<32 hex digits> effect_set_store=ok|damaged|unavailable effect_set_default=0|1`, and op 3 (`set_effects`) is the same 40-byte frame with the 128-bit mask of offered effect IDs (bit n = stored effect value n, little-endian, low 64 bits first) in body bytes 0-15 and the other 12 body bytes zero.
  It replies `MILESTONE_OK op=set_effects revision=... effect_set=... effects=<count>` (or `unchanged=1`) or `MILESTONE_ERROR op=set_effects reason=busy|invalid_effects problem=empty|effect effect=N|nvs_space|storage_unavailable|write_failed`; a corrupt one is `MILESTONE_ERROR reason=invalid_request written=0`, which is also how 0.15 and 0.16 refuse op 3.
  Every accepted pull prints `CELEBRATION` (its RPM as shown, mode, best, milestone and effect), and `CELEBRATION_START` when its effect begins; `p` includes `MILESTONE_STATUS`.
- `t`: clearly labeled RAM-only demo preview. It cannot add synthetic data to
  the actual practice history or storage. Send `t` again to turn it off.
- `y`: USB-only storage diagnostic, requiring no active burst or pending capture.
  The ADC owner rechecks the launch counter, pauses only for the scratch writes
  and resumes automatically.
  It writes/reopens 36 snapshots in the dedicated `ll-prac-qa` namespace and
  removes only those scratch keys. Actual practice records are untouched.
  Two scratch slots plus a rewrite need 261 NVS entries, more than the 20 KB
  NVS has free beside real history, so on a device with history it prints
  `HISTORY_CHECK_SKIPPED reason=nvs_space available_entries=N needed_entries=261`
  and writes nothing. `HISTORY_CHECK` reports both counts.
- `NVS_STATUS` (in `p`) reports used and available NVS entries and
  `history_headroom`: entries left after one history save (87 entries). A
  negative headroom means the next history save would fail for lack of space.
  At boot the firmware removes an orphaned 1904 B RF calibration blob
  (`ll-practice/cal_data`, 62 entries) if present and logs `NVS_RECLAIM`.
- Automatic saves (0.18): history, personal bests and the five-minute power log go to the history log in the `spiffs` partition ([FIRMWARE_FLASH_LAYOUT.md](../../docs/FIRMWARE_FLASH_LAYOUT.md)), program-only writes with the ADC sampling throughout, so a pull that starts during a save is measured however long the save takes; `HISTORY_SAVE ... store=log`.
  NVS keeps 0.15-0.17's records as a mirror written at sleep, power-off and restore, so a rolled-back unit keeps its data; at boot the newer copy wins (`HISTORY_LOG_BOOT`, `HISTORY_LOG_LOAD`).
  ADC frames are 4092 bytes (a 102 ms DMA ring that rides one flash operation), and a frame-silence watchdog restarts a stopped ADC (`PERF ... adc_stalls`).
- The NVS reserve (0.18, `NvsBudget::RESERVE_ENTRIES`): a custom theme, milestone table or effect set save that would take more room (the first theme, a slot filled for the first time) is refused with `reason=nvs_space` unless 87 + 10 entries stay free after it; a rewrite in place needs only its own room beside a history rewrite, and history and settings saves never check the reserve.
  With a full history, a history save takes about 130 ms, 295 ms with a page erase, or 460-480 ms with two (past the 400 ms checkpoint budget); two-erase saves were 49% of saves at 85-94 free entries and 13% at 100-119 on 83201, so the reserve keeps 97 free, out of the worst band, and the fullest unit seen (110 free) can still add a first custom theme.
- `E<id>`: replay the last recap with the effect of that stored id: `E0`-`E4` Orbit, Flames, Sparkles, Shockwave, Lightning, `E7` Slash, `E8`-`E12` Tidal, Frost, Gale, Eclipse, Radiance and `E13`-`E17` Spin, Storm, Quickdraw, Leaves, Bloom and `E18`-`E42` the 0.17 effects Crimson to Ravage in stored order (send a two-digit id such as `E12` as one write); `E` then Enter uses the saved one.
  USB-only preview for checking effects on the device: bests, history and settings are unchanged.
  It restarts `max_effect_us` and `max_draw_us` in the `PERF` line: the slowest effect pass and whole frame since the preview began.
  With Settings → Sound on, the effect's sound plays too; `SOUND_STATUS` in `p` reports the speaker.
- `v`: export the last displayed 135×240 RGB565 frame. A 16-bit canvas permits
  one display transfer per frame and diagnostic readback. Allocation failure
  falls back to direct display drawing without stopping RPM or level sensing.
- `g1\n` through `g10\n`: select an exposed ADC1 GPIO; it is persisted in the next idle checkpoint. Invalid input is rejected.
- `x`: explicitly toggle external 5 V output (default off), persisted in the next idle checkpoint. Use output mode only when powering through USB or Hat 5VIN; feeding Grove/EXT_5V while they are outputs can damage the device. See [M5Stack power-interface restrictions](https://docs.m5stack.com/en/core/StickS3#note).
- `s`: toggle a 20 Hz diagnostic display stress mode, including during bursts.
  Disable it after testing. `j`: deliberately pause the UI for 200 ms without
  blocking acquisition. Both are diagnostic commands, not normal controls.
- `w`: export the newest contiguous raw recorder segment. The owning task
  stops acquisition and acknowledges before the UI accesses recorder memory.
  After ACK/FNV-checked transfer, the owner flushes/restarts acquisition and
  resets detector history. Sampling is intentionally paused during export.
  Use `firmware/tools/flight_recorder.py`, not the plain console for `w`.

## Latest launch tilt recap

Every accepted pull opens five seconds of feedback: RPM, a fixed-center
launcher rotating in an isometric cube, and frozen Start/End tilt circles.
The launcher stays in one location. There is no travel trail, height estimate,
velocity, displacement, or compass heading. Cube and body size are constant
across captures and have no physical distance units. Replay follows the full recorded pre/pull/post interval at 1×, starting its clock
after the first complete displayed frame, then holds the end pose. A progress
marker identifies measured playback time. Start/End circles remain at optical
onset/end; the played pre/post context is not additional optical pull time. The replay preserves measured gravity-relative tilt. Only arbitrary heading is
reset at optical onset; it does not flatten the initial pose.
New captures retain up to 500 ms of continuous, settled pre-roll and 150 ms of
post-roll. Missing, gapped or unsettled context before onset is trimmed; the
entire optical pull and post-roll must still pass the original quality checks.
This removes the extra 500 ms readiness requirement after VQF's unchanged
1.5-second startup/recovery. Older stored captures keep their recorded span.
The circles record gravity-relative tilt at optical onset/end; they never follow
the current sensor. Recap omits numeric angles and is labeled "Tilt replay". The portrait body
has a blue screen, violet back, dark sides, screen inset and white USB-C cap.
A quieter, tighter cage and fixed middle-height reference make tilt easier to
interpret. The recap shows one body and is labeled "Last launch"; it replaces
the duplicate Motion page and its gray reference outline. Missing/pending tilt has a plain status label.
Its neutral heading faces the viewer midway between the two front cube walls,
aligned with the nearest vertical edge, instead of following the left wall.
This fixed 45-degree display-heading offset does not change measured tilt,
relative rotation, or Start/End dots. The rectangle remains centered in the cube.
An unknown board-to-launcher mounting rotation is not silently assumed calibrated.
A recalls the recap; B opens the menu. RPM acquisition is always enabled.

`orientation_estimator.h` continuously runs the upstream VQF 6D filter, using
fresh paired acceleration/gyro readings. Timestamp-based trapezoidal gyro
resampling preserves measured elapsed time on a 100 Hz filter grid. Acceleration
is interpolated onto the same grid. Gyro units are converted to rad/s and
acceleration to m/s² before VQF. The algorithm estimates gyro bias during natural
rest and motion, and filters acceleration in an approximately inertial frame
for inclination correction. No magnetometer or external sensor is used.
The pinned MIT-licensed C++ source and provenance are in `src/vqf/`.
`build-m5.sh` compiles it in single precision, while the upstream Butterworth
filters retain double precision. Filter parameters are upstream defaults.

Fusion runs before capture and chart reduction. Start/End poses are interpolated
from full-rate fused samples at their exact optical timestamps. The 48-point
trace dedicates up to 24 points to the optical interval, including onset/end,
and retains pre/post-roll. It does not infer additional motion from additional
chart points. The live level uses the continuously fused gravity direction.
M5Unified's mounted-device axis mapping and the user-confirmed dot directions
are preserved. Sensor-to-launcher mounting calibration is not inferred.

The optical DMA and IMU polling clocks are approximately aligned, not hardware
synchronized; very fast pulls have limited IMU samples. The visualized capture
is the latest accepted launch, not an older history row. RPM history stores scalar
results, not a tilt recording for every launch.

The removal does not erase or rewrite the alternating checksummed `ll-motion`
NVS slots. They remain readable and exportable with `e`. Schema 4 marks fused
captures; older schemas remain readable without treating them as fused by the
new estimator. The image remains 1124 bytes. No motion is invented for older
RPM records; latest motion remains in RAM. On reboot, an existing fused saved
reference is reused for the recap only if its launch number matches the latest
RPM record. There are no new reference-selection controls or writes.

The single IMU owner runs at priority 2 on core 0, below the priority-3 ADC
owner. It shares an I2C mutex with M5 button/power operations and never owns the
ADC. `ORIENTATION_STATUS` reports readiness, bias/uncertainty, rest detection,
resets, update count, and worst fusion duration. Every gap over 35 ms resets
orientation while retaining learned gyro bias; captures overlapping gaps are
rejected. The filter settles for 1.5 seconds after startup/reset. There is no
per-launch hold-still requirement. Naturally occurring rest improves bias.
Clipping near 8 g / 2000 deg/s, missing coverage and optical bursts over 2.5
seconds also make motion unavailable. RPM/history still record independently.
Capture waits for fused-sample post-roll coverage, not the newer raw-poll
timestamp. Saving history waits for the motion post-roll; guarded flash writes can cause
IMU gaps outside the finished capture. Failed saves retain prior durable data.

This provides a bounded orientation estimate, not a calibrated motion-capture
system. Sustained linear acceleration can bias inclination; unreferenced yaw
can drift. Relative heading is reset for each short capture, not established
against north. Synthetic known-angle tests check the algorithm and timing,
and device readbacks check installation/performance. Real launch angle accuracy
and repeatability still require a physical reference-angle trial; no such
accuracy is claimed from software tests. No camera is required by this firmware.

## Practice history

Accepted peak RPM is the practice metric. Failed, cancelled and duplicate result
events are excluded. Retains the latest 128 individual launches and 24 session
summaries. Every session's mean and best include all of its accepted launches,
even after its oldest individual readings leave the ring. A session starts on
the first pull after boot, after 10 minutes without an accepted pull, or after
the manual new-session gesture. In a history view, B browses older windows and hold B returns to the history choices.

Pull trend shows up to 30 readings as gray points, with a cyan trailing five-pull
mean. Session trend shows up to 12 session means in each window, with that
window's most recent session count and best. Change compares the first and last
five pulls (at least 10 readings), or first and last two session means (at least
four sessions); these groups do not overlap. Axes use pull/session order, not
calendar dates or equal elapsed intervals. Keep the same launcher, sensor setup
and practice conditions when comparing RPM. It is an RPM trend, not calibrated
force or a causal measure of technique improvement.

Two versioned, checksummed NVS snapshots in the separate `ll-practice` namespace
alternate on successful coalesced saves of accepted launches. Saves read back the exact bytes before marking
the data saved. Boot chooses the newest valid copy and retains the previous copy
after a failed write. Unsupported/corrupt storage is reported and never erased
automatically. A save failure is reported in USB status; the device screens do not show save state. History
saves wait for ten seconds after the latest accepted pull, completed capture and
no visible five-second recap. There is no maximum pending-age override during
a long practice bout. The ADC owner's idle guard also checks for an optical
candidate before permitting a write. Periodic power-log writes use the same
quiet and recap guard. Failed history writes retry at least five seconds after
acquisition resumes and still require practice quiet. Pending status remains
visible until exact readback succeeds; normal automatic standby forces a save
and stays awake if it fails. Unavailable optional power-log storage does not
checkpoint acquisition.

The installed ADC driver is not configured for cache-safe interrupts during
flash operations. Therefore the ADC owner acknowledges a guarded checkpoint
while Ready or Paused, after the ADC owner rechecks the launch counter and optical idle state. Flash writes briefly pause sampling;
the completed RPM remains visible and fresh quiet is required before rearming.
No checkpoint can interrupt an active measured burst. The raw recorder begins
a new contiguous segment after each checkpoint, just as after other acquisition
gaps; `w` exports that newest segment rather than inventing continuous time
across the save. Do not interpret delivered sample rate as uninterrupted uptime.
Abrupt power-off can lose the entire pending unsaved bout; prior saved data
remains available. No older on-device readings are invented when upgrading.

Settings, the sensor GPIO and the 5 V output choice are written under the same rule, but Apply never waits for one.
Apply takes the choice at once and queues its record, which is written in the first checkpoint the ADC owner grants: normally immediately, otherwise as soon as the optical signal is quiet again.
Previously Apply itself needed that checkpoint, and pressing A could jiggle the launcher across a mark, so Apply after Apply was refused with "Try again when idle".
A refused checkpoint is retried after one second and a failed write after five, so a busy signal or IMU transfer costs at most one quiet-window attempt per second.
The shutdown checkpoint writes whatever is still queued; a settings write failure is logged and never holds up power-off.
While a record is queued the sampling pause (`RESTING`) waits, as it does for unsaved history.
`p` reports `SETTINGS_SAVE_STATUS pending=<items> failed=<0|1> saves=<n> errors=<n> refusals=<n>`, and each write logs `SETTINGS_SAVE items=<mask> saved=<mask>`.

A sensor fault (signal held on the low rail, shown as Check sensor) keeps the detector in Hold until the signal returns, and nothing can be measured meanwhile.
Pending history then flushes through an ordinary idle checkpoint after the usual ten quiet seconds instead of waiting for standby.

Every checkpoint begins and ends within one call; only the wipe flow keeps one.
If an owner reply is lost, for example a Begin the owner executed but the UI saw as refused, or an End that never arrived, the ADC would otherwise stay stopped and no later pull would be measured.
The loop releases a stopped, non-resting owner after one second and logs `ADC_NOTICE orphaned_checkpoint_released resumed=<0|1>`.

## Build and validation

Run from the private project root:

```sh
bash firmware/build-m5.sh
bash scripts/test-firmware.sh
bash scripts/test-real-captures.sh
```

The build script uses the configured project-local Arduino ESP32 3.3.0,
M5Unified 0.2.23 and M5GFX 0.2.30 dependencies, 8 MB flash, OPI PSRAM and
`VQF_SINGLE_PRECISION`. It copies the canonical
`LaunchLabRpm/analog_tachometer.h` into its build sketch. The source target is
the local 0.13.1 Sidecar and wipe candidate; building it does not install or republish it.
The native test script runs C++ suites and production-function/rendering
harnesses with address/undefined sanitizers, including both RPM estimators,
storage failures, real fusion/history scheduling, replay, USB standby and IMU
detection/data/allocation failure recovery.
The separate [real capture suite](../../docs/REAL_CAPTURE_TESTS.md) replays the owner's hash-pinned raw ADC recordings through the actual production detector and estimator selection.
Run it before asking for further physical pulls when changing detection or checkpoint behavior.
Its confirmed regressions and unlabeled diagnostic challenges are reported separately; passing historical recordings does not resolve the current field failure.
The [hardware-in-the-loop rig](../tools/hil/README.md) (`build-m5-hil.sh`, a test-only `LAUNCHLAB_HIL=1` build) plays scripted pulls, recorded waveforms, IMU motion and I2C/NVS faults through the real device and asserts on its serial output; production builds compile none of its hooks.
Archived source, application/support identities and frozen build evidence are
recorded in the separate release manifest; source builds can differ byte for byte
across toolchains. Earlier release archives remain unchanged.

The recorded waveform comparisons below used private development captures and
replay tools. They are historical software evidence, not a mounted accuracy
trial. Native synthetic tests do not establish absolute RPM or physical fit.

Before updating a device, preserve important data and use the public
[update and recovery guide](../../output/launchlab-mini/docs/UPDATER.md). Normal Update and deliberate
Repair write only application offset `0x10000` after layout/boot checks; First
install replaces the factory layout. Do not substitute a development flash script
for those recovery checks.

The initial installed three-pull check passed: the user confirmed three working
pulls, the device counter advanced to three, and checksum-verified raw replay
accepted all three. Latest live result: 6,406 RPM (not independently calibrated).

Evidence for the acquisition baseline is in `firmware/evidence/sticks3-0.2.0/`;
the original level update is retained in `firmware/evidence/sticks3-0.2.1-level/`,
and the portrait update in `firmware/evidence/sticks3-0.2.2-portrait/`. Practice
history evidence is in `firmware/evidence/sticks3-0.3.0-practice/`; QRE release
metadata records the subsequent user confirmation. Battery/runtime evidence
for the power update is in
`firmware/evidence/sticks3-0.3.1-power/`. Motion/recreate evidence is in
`firmware/evidence/sticks3-0.4.2-feedback/`; the frozen start/end recap update is
in `firmware/evidence/sticks3-0.4.3-recap/`. Flash hash
verification proves installed bytes; USB status/stress checks prove acquisition
behavior. Repeated annotated live pulls establish detection reliability.
Independent known-speed shaft/tachometer testing is still needed for absolute
RPM accuracy. Battery-only sampling/power endurance also needs separate testing.

Official references:
- https://arxiv.org/abs/2203.17024
- https://vqf.readthedocs.io/en/stable/
- https://docs.m5stack.com/en/core/StickS3
- https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/adc_continuous.html
