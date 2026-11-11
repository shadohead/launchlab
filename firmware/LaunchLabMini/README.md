# LaunchLab for M5StickS3

Current revision: **0.20.8-sticks3-songs-pm**, the recommended stable release, for **M5StickS3 K150 (ESP32-S3, 8 MB flash / 8 MB OPI PSRAM)**.
One application supports analog QRE1113 and TCRT5000 through the saved Sensor profile setting.
See [build context](../../docs/FIRMWARE_0.20.8.md) and [release notes](../releases/0.20.8-sticks3-songs-pm/README.md).

Tilt replay is reliable on hard pulls: the accelerometer uses its ±16 g range, and every IMU sample is read from the BMI270 FIFO, so the live level and recordings no longer drop out at rest or during saves. A pull that reaches the gyro's 2000 °/s limit keeps its replay, marked **Tilt approx.**
A single click of the side power key saves pending pulls and powers off.
RPM detection treats the cord rewind after a pull as part of that pull, confirms sharp pulls and rides through a missed mark. State-changing USB letters run only when sent on their own, so stray serial data cannot power off the device or change settings; backup and restore work as before.

Filtered movement renews the 30-second unused shake-wake timer. Stillness also enables RESTING, which pauses ADC sampling until movement or a button resumes it. The shared sensor supply remains powered. USB keeps the device awake.
0.20.4 adds String wear check under Menu: pull out, hold until LET GO, then let go, and it measures the retraction, never the pull out, reading the launcher's tilt in the selected Mounting (Top or Sidecar). 0.20.3 introduced it as Wear check.
0.20.5's recap graphs each pull's RPM turn by turn, from the gear starting through the peak to the end, in place of the Start/End level dots; after a pull the tilt replay plays twice, and A on Main opens the recap looping until A again or a pull.
0.20.6 fixes four ways a unit could stay awake until its battery ran flat (movement after use, a refused save at power-off, steady optical marks and a held button): a used unit now sleeps at most 5 minutes after its last use even while it moves, the standby leaves the green LED off, and the speaker is powered down after every sound.
0.20.7 adds Mic RPM on the Menu: while the page is open, the built-in mic plots the RPM heard from the spin after each pull beside the optical peak, and the speaker stays muted.
0.20.8 adds Settings → About (the firmware, the unit's ID and its saved pulls) and a battery icon in the top right corner of the Menu and Settings, with no percentage (Settings → Battery icon turns it off), and plays songs without gaps.
0.20.2 sleeps after 1 minute by default on a unit that never chose (a chosen Sleep after time is kept) and adds a Shake to wake setting: On, the default, wakes on a shake as before; Off sleeps more deeply and only the side key wakes it.
0.20.1 reads RPM accurately at high speed, plays songs for at most 6 s like a celebration, and while a sound plays a pull must start with marks at least half as strong as your recent pulls, so speaker noise cannot start one.
0.20.0 lets an added celebration effect carry a song made from a MIDI file in the [website effects library](https://launchlab.beywatch.gg/effects), with new voices and a drum bank; a song fades out when a pull starts.
0.19.0 plays celebration effects added without a firmware update: shared as codes in the [website effects library](https://launchlab.beywatch.gg/effects), checked against the frame budget and installed over USB, with their own sounds. The compiler and example effects are in [`firmware/tools/fx`](../tools/fx).
0.18.1 starts celebration sounds in step with their animation, receives milestone saves from the website whole (an 8 KB USB queue in every build), fixes a rare IMU task watchdog reset and plays the speaker in mono.
0.18.0 adds 40 more celebration effects in eight sets of five (81 in all, any of which a milestone can play) and redesigns Boundless over the whole screen.
Automatic saves go to a separate log in flash in a few milliseconds while the ADC keeps sampling, so saving never interrupts a pull; a storage reserve refuses a new custom theme, milestone table or effect set before saves would slow down.
A unit that never saved these choices now uses the 1-turn peak RPM method, 30% brightness and Celebrate Both.
25 more celebration effects in five sets arrived in 0.17.0, and the [website celebrations editor](https://launchlab.beywatch.gg/celebrations) picks which effects Best effect and Random use.
Ten more celebration effects (Tidal, Frost, Gale, Eclipse, Radiance, Spin, Storm, Quickdraw, Leaves and Bloom) arrived in 0.16.0, where every celebration runs about five seconds, each milestone can play a random pick from several effects, milestones can sit at any RPM, and custom themes accept any colors with warnings.
RPM milestone celebrations play an escalating effect at each milestone a pull reaches, 9K to 15K by default ending with the new Slash effect; choose Settings > Celebrate and edit the ladder in the [website celebrations editor](https://launchlab.beywatch.gg/celebrations).
Custom color themes made in the [website theme editor](https://launchlab.beywatch.gg/themes) can be uploaded over USB and chosen under Settings > Theme; the ten built-in themes are unchanged.
Settings adds saved Top/Sidecar mounting and a guarded Wipe all flow. Themes, personal-best effects, optional sound, history and USB backup/restore remain available.
Battery display uses filtered voltage readings, idle screen updates are reduced, and PM1 access uses checked wake/retry handling.

## Local Analysis Mode development

This checkout adds Analysis Mode to the 0.20.8 base; it is not a published release.
Hold **B for one second** on Main (including its recap) to enter, and hold B again to exit.
Wait for the microphone warm-up indication to clear (about one second) before the first pull.
Each valid launch shows its optical peak, starts a large circular **MM:SS** elapsed timer from the recorded
end of the pull, and replaces the bottom optical curve with the existing acoustic RPM graph.
Before a launch the screen shows the detector's READY state. A second timestamp below the
elapsed timer shows when the last valid microphone RPM was recorded. The graph occupies the bottom third of the screen and expands its
time axis throughout recording, including quiet gaps. Tap **B** to stop and freeze both;
the next valid launch resets them. Tap **A** to show the tilt replay and initial optical
RPM curve, then A again to return. Recording continues while replay is visible.

The microphone stays warm between launches; sounds and celebrations are suppressed while
Analysis is open. Analysis records for up to **5 minutes**, even through quiet gaps; the
separate Mic RPM page keeps its existing two-minute/eight-second silence limits.
Active timing prevents automatic sleep. A manual stop or five-minute timeout starts the
ordinary inactivity timeout again. The side power key still saves and powers off.
Sleep/wake returns to normal Main, not an active microphone.
Analysis traces remain in RAM; the latest fitted results can be saved as described below. The existing optical launch history is still saved normally.
Millisecond formatting is not a claim of millisecond release-detection accuracy.

Analysis performance: its main view requests frames at 100 ms intervals, with immediate
state/button updates and 50 ms button-hold feedback. Replay animates at up to 20 FPS and
holds its endpoint without timer- or microphone-driven redraws. Static header, RPM and
stopwatch pixels are retained in the existing screen sprite; the timer is redrawn only
when its displayed second changes. Palette, launch, sensor-status and screen transitions
invalidate this cache, and direct-display fallback always redraws fully. The graph remains
clock-driven during silence. Analysis microphone followers wait for capture notifications
with a 100 ms fallback; stop, exit and new-pull notifications wake them promptly.
The standalone Mic RPM page retains its original polling and display behavior.
Host tests cover these paths; on-device timing and battery gains remain unmeasured.

Analysis alone applies a gated alpha-beta filter to accepted acoustic readings (alpha 0.6,
beta 0.08), using each reading's sample timestamp. Deviations over max(200 RPM, 5% of
predicted RPM) are withheld before updating the estimate. Three consecutive candidates
within max(150 RPM, 3%) of one another can establish a changed speed; a gap over one
second also requires three candidates to reacquire. Missing/rejected readings emit no
predicted RPM and do not advance the last-reading timestamp. Every launch resets the
filter. Optical RPM, the raw tracker, diagnostic spin summaries and the ordinary Mic RPM
page are unchanged. These initial parameters passed synthetic host checks; they still
need validation against real recordings, particularly impacts and competing tones.



Analysis graphs show readings withheld by the alpha-beta filter as small gray dots at their original RPM and sample time. These diagnostic dots do not update the last accepted RPM timestamp or the filtered curve, and are hidden in ordinary Mic RPM. The vertical axis includes them so outliers remain visible. Their optional Analysis-only storage adds 6 KB of PSRAM for a five-minute trace.

## Analysis Result (local development)

Once validation selects a winner, the main Analysis graph shows its curve as a solid line. The measured acoustic interval uses the theme accent, and extrapolated sections and excluded readings use gray. Three animated dots mark fitting in progress. The graph title identifies the winning model. After stopping, tap **B** to open Analysis Details. B scrolls complete 1k RPM bands six rows at a time. **A** returns to the main Analysis screen; a subsequent A opens replay as before. Holding B exits Analysis. A new valid launch resets the results view. Stopping the timer starts calculation; a busy screen remains responsive while fitting.

The laws are `dx/dt = -(a+b*x)`, `-(a+c*x*x)`, `-(a+b*x+c*x*x)`, and `-(a+k*x^n)`, with `x` in kRPM, seconds from the optical launch, nonnegative coefficients, a positive `a` floor and `0.5 <= n <= 3`. The optical launch is a circle marker, the manual zero-RPM stop a cross. The line starts at the measured optical RPM and reaches zero at the manual stop. Press stop when the physical spin ends. A five-minute timeout is not an observed physical stop.

Original acoustic readings (before the Analysis alpha-beta filter) are retained in the existing optional 6 KB buffer. The live filtered display is unchanged. Results show retained observations in the theme accent and excluded readings in gray. The fitted line is solid through the last retained reading. The Hermite tail to a manual stop and the extrapolation after a timeout use dots in the same theme accent. The curve starts at launch even when the microphone takes time to acquire the spin. The graph extends to the observed/manual stop or predicted stop (prediction display capped at ten minutes).

Fitting accepts every valid positive microphone RPM, consolidates duplicate timestamps, rejects unconfirmed upward jumps, replaces the earlier track when a sustained higher run decays consistently and uses time-weighted Huber loss (150 RPM transition). Each model uses the optical initial RPM. With a manual stop, the optimizer adjusts the curve shape against acoustic readings and scales the drag coefficients to match the recorded duration. With a manual stop, the displayed curve passes through the last retained acoustic reading and uses monotone cubic Hermite interpolation from there to zero. Its initial slope is estimated from the last five retained readings and limited to prevent overshoot. It flattens at the stop. Fit and validation errors describe the underlying decay model. With sparse data the earlier section uses the endpoint-only line as its baseline. For 48 or more observations each optimizer trial integrates elapsed time on a 128-bin RPM grid and interpolates at the observed timestamps. Shorter trials, reported errors and displayed model curves use RK4 with steps no larger than 0.1 seconds. Bounded multi-start Nelder�Mead has a 480-evaluation budget per fit; the power model starts at exponents 1, 1.5 and 2. The first 80% train and final 20% validate chronologically; displayed curves are refitted on all usable data. Rankings require convergence and adequate coverage; a two-coefficient model can win a near tie within 15 RPM. Sparse/flat recordings, coefficient bounds, incomplete convergence and disagreement between starting points are flagged.

The details screen shows recorded duration, average RPM lost per second, estimated revolutions, estimated battle stamina (seconds from launch to the first downward crossing of 1,000 RPM on the winning curve) and time per complete RPM band. Revolutions are the area under the winning curve in RPM-seconds divided by 60. Timeout metrics cover the recording interval; USB serial `ANALYSIS_MODEL` and `ANALYSIS_METRIC` lines report coefficients, convergence, training/validation/all-data RMSE, MAE and maximum error. Without a manual stop, stop integration checks numerical convergence and declines estimates beyond one hour.

A priority-zero core-1 task yields regularly and uses approximately 72 KiB of PSRAM plus an 8 KiB PSRAM stack, allocated after the timer stops. The fitting workspace and stack are freed when the result is ready. Leaving Analysis or launching again cancels it without blocking the loop. Allocation failure shows an error. The normal Mic RPM page, acquisition and CPU clock policy are unchanged. Fitting adds temporary CPU load; runtime and energy cost require device measurement.

The latest four fitted results and selected model are stored in a 512-byte versioned, CRC-protected binary `analysis_fit` record in the existing `launchlab-mini` namespace, through the existing ADC-safe settings checkpoint. Writes reserve space for practice history and verify a read-back; optional storage failure is reported over serial and keeps results in RAM. A device wipe clears the record. Raw samples are not persisted, and this version does not add a saved-results browser or reopen a prior result after reboot. The record contains time origins, normalization/version, coefficients and metrics sufficient to regenerate curves later.

Host tests cover synthetic curves, analytic integrals, noise/spikes/dropouts, optical and manual endpoint constraints, failure cases, navigation, rendering bounds, raw capture, background cancellation and persistence faults. Hardware validation is still needed for fitting duration, real-world model selection and stopping accuracy.

## Wiring

Sensor VCC → Hat 3.3 V; GND → GND; analog OUT → Hat G1 / GPIO1. The Grove red wire supplies 5 V and must not be used as the sensor supply. Use the analog output; leave any digital output unused. See [assembly guide](../../docs/BUILD.md).

## Sensor profile

One firmware supports both sensors. Choose **Menu → Settings → Sensor profile**:

- **QRE / Standard** (default): 80-count minimum optical mark.
- **TCRT5000**: 60-count minimum, for the weaker marks a TCRT5000 module produces.

Both keep 0.70 hysteresis, 50 kS/s acquisition and three consistent turns to confirm a launch. A profile change waits until the device is idle and starts a new practice session on the next accepted pull; history is kept. The choice survives restart and shake wake.

## Controls

The mounted Flip Off default uses display rotation 2; Flip On uses native portrait rotation 0.
Measurement runs on every screen while active. After 30 seconds of stillness, RESTING pauses ADC sampling; movement or a button resumes acquisition.

- **Main:** A shows the latest launch recap; B opens the menu. Hold A for one second to enter tournament mode.
- **Menus:** A opens or applies the highlighted item; B moves to the next choice; hold B goes back one level or cancels an edit. Every list has a Back choice.
- **Menu items:** History, New session, Battery, Settings, Tournament, String wear check, Mic RPM and Back. History offers Launches, Pull trend and Session trend. New session starts on the next accepted pull and keeps earlier launches.
- **Settings:** Sleep after, Shake to wake, RPM method, Brightness, Sensor profile, Flip display, Theme, Best effect, Celebrate, Sound, Mounting, Battery icon, Wipe all and About, five rows to a page. In an editor the value is centred, with a faint `B next option ->` hint beneath it: B cycles the value, A applies, hold B cancels.
  - Sleep after: 1–10 minutes, 1-minute default (3 minutes before 0.20.2; a chosen time is kept).
  - Shake to wake: On (default) wakes on a shake; Off sleeps more deeply and only the side key wakes it.
  - RPM method: 1-turn peak (default from 0.18.0) or 3-turn average. The launch floor is 1,000 RPM.
  - Brightness: 10–100% in 10% steps, 30% default.
  - Flip display: rotates the screen 180°. Brightness and flip preview immediately and revert if you cancel. Physical A/B buttons keep their roles when flipped.
- **Tournament:** starts in Recording Only; B cycles Recording Only / RPM; hold A exits. The selected view resumes after shake wake.

Accepted pulls show a recap from Main: its tilt replay plays twice and it closes after five seconds (longer for a long recording). A on Main opens the latest recap and loops its replay until A again or the next pull. Menus, history and setting drafts stay open while launches keep recording. The battery page shows percentage, icon, voltage and charging status; the voltage-based percentage is not a calibrated capacity measurement.

## Acquisition and replay

A dedicated core-0 task owns continuous ADC1 DMA acquisition at **50,000 samples/s**. Detector time advances by sample count (20 µs per sample), independent of screen/USB delays. ADC losses invalidate sequence continuity and are reported in diagnostics. A separate lower-priority IMU task reads the BMI270 FIFO (gyro 200 Hz, accelerometer 100 Hz at ±16 g / ±2000 °/s) and performs six-axis VQF fusion on a sensor-clocked timeline; screen/buttons/USB run on core 1. IMU reads pause while flash is written (a save waits for any read in progress, or is retried later) and resume from the FIFO without losing samples. Startup frees the internal I2C bus if a reset interrupted a transfer.

The release enables CPU power management at **40–80 MHz**, holding 80 MHz during acquisition. Automatic light sleep is disabled. Startup verifies fresh paired accelerometer/gyro data and the FIFO axis mapping, makes up to three attempts, and retries a missing IMU owner every five seconds while idle.

After a pull, the recap shows RPM, a rotation replay with its progress down the right edge, and a graph of the pull's RPM turn by turn (one dot per turn, the peak on the top line in thousands; from 0.20.5, in place of the Start/End level dots). The graph is kept until the next pull; after a restart the recap shows no curve. With the display flipped, the live level and the 3D recap follow the flipped screen; recorded data is unchanged. The IMU measures gravity-relative tilt and relative rotation, not travel distance or compass heading. RPM describes launcher-shaft speed, not stadium/release RPM.

History retains up to 128 launches and 24 sessions. Automatic checkpoints wait for ten quiet seconds after the last accepted pull once the launcher is set down, or a minute while it is handheld; pulls left unsaved for three minutes are written at the next idle moment. Standby flushes pending history; abrupt power loss can lose the pending bout.

## Power

Measured launches, button use and interactive USB commands refresh the inactivity timer. A detected USB computer keeps the device awake even without a serial reader. Battery sleep uses IMU shake wake when Shake to wake is On; with it Off, the side key wakes the device. An unfinished setting edit is cancelled before sleep. Wait for the screen before pulling after wake.

## Build and verification

Run `bash scripts/build-firmware.sh` with Arduino CLI installed. The script pins ESP32 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30, verifies the complete PM SDK and explicitly enables both release flags. Use the frozen application for exact binary restoration; rebuilt bytes can vary with build environment.

Run `bash scripts/test-firmware.sh` for 80 sanitizer-enabled native suites and 20 production-code harnesses with synthetic fixtures. The updater tests cover application-only upgrades, no-op detection, intentional rollback, first-install confirmation, chip/capacity checks and artifact verification.

[USB update and backup](../../docs/UPDATER.md) · [Recovery](../../docs/RECOVERY.md).

## Device page

The main menu's Device entry replaces Battery, retaining the same navigation and saved
page identifier. It shows charge percentage, voltage and charging status, plus flash
chip capacity, verified running-firmware image size / running-slot capacity, and unused
space within that firmware slot (MiB). App free is firmware growth room, not storage
available for history or effects; other flash partitions are reserved separately.
Unavailable flash metadata is shown explicitly. It is read once before acquisition starts.
The main menu also shows the battery percentage to the left of its top-right icon; both follow
the existing Battery icon setting and battery-reading validity.
