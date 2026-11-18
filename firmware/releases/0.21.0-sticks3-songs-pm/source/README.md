# LaunchLab for M5StickS3

Current checkout: **Analysis 1.0-rc.5**, based on firmware 0.20.11, for **M5StickS3 K150 (ESP32-S3, 8 MB flash / 8 MB OPI PSRAM)**. Runtime: `0.20.11-analysis-1.0-rc.5-pm`. This is a release candidate awaiting hardware acceptance, not a published stable release. See the [RC notes and checklist](../../docs/ANALYSIS_1.0_RC.md).

Upstream release baseline: **<!-- current-release:start runtime -->0.20.10-sticks3-songs-pm<!-- current-release:end -->**, the recommended stable release, for **M5StickS3 K150 (ESP32-S3, 8 MB flash / 8 MB OPI PSRAM)**. <!-- current-release:start source -->This folder holds the source of the newer **0.21.0-sticks3-songs-pm** beta release: see its [0.21.0 release notes](../releases/0.21.0-sticks3-songs-pm/README.md) and [build context](../../docs/FIRMWARE_0.21.0.md).<!-- current-release:end -->
One application supports analog QRE1113 and TCRT5000 through the saved Sensor profile setting.
See <!-- current-release:start build-context -->[build context](../../docs/FIRMWARE_0.20.10.md)<!-- current-release:end --> and <!-- current-release:start release-notes -->[0.20.10 release notes](../releases/0.20.10-sticks3-songs-pm/README.md)<!-- current-release:end -->.

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

## Analysis Mode

Hold **B** for one second on Main to enter Analysis. Give the microphone about a second to warm up. A valid optical launch starts the timer and the acoustic RPM graph. Press **B** when the spin finishes. The five-minute timeout ends recording but leaves the physical stop time unknown.

**A** toggles tilt replay and the initial optical RPM graph. Recording continues during replay. Hold **B** to leave Analysis. The microphone stays warm between launches and the speaker is muted while Analysis is open.

During recording, a gated alpha-beta filter smooths the microphone readings. Gray points show readings withheld by the live filter. The Now row shows the latest heard RPM and becomes Last after stopping. The fitter retains the original readings so that smoothing does not distort its coefficients.

### Fitted curve

From ten seconds after release, Analysis can fit a live preview to ten or more raw readings covering at least two seconds. As new readings arrive it starts at most one refit every two seconds. Fixed Riccati fits a straight line to transformed acoustic RPM with a free intercept and eight robust, RPM-weighted passes. Its shared drag shape in kRPM is 0.00360 + 0.00380x + 0.00589x^2. The per-spin time scale must be between 0.3 and 3, with at least 70% of readings within 150 RPM of the curve. The Fixed Riccati line projects to 578 RPM. The live graph keeps the measured interval at its original timestamps and scales the dotted tail to the active finish estimate.

The Stamina Analysis screen gives a finish estimate every second from ten seconds after launch. It blends a robust curve through accepted original mic readings with the published Fixed Riccati finish, giving the latter more weight as successive fits agree and its acoustic support improves. Either curve can supply the forecast by itself. If both are unavailable, a regression uses the launch RPM, mic counts and observed decline. A bounded signed correction and the elapsed spin time produce the displayed finish. The two regressions were trained on manually stopped FB and stamina launches. Full-session audio metadata is archived for offline research.

Analysis buffers up to two acquiring windows and recovers their measured frequencies when the next lock confirms the same cycle count and frequency sequence. The calibration export marks recovered points individually and records counts of delivered frames by tracker decision. These counts include recovered windows under their original acquiring decision. The archive also records skipped audio milliseconds and queue drops as of the last delivered frame. Normal Mic RPM tracking uses its original path.

Stopping the timer starts a final Fixed Riccati fit on the complete mic trace. For a manual stop, the graph connects the optical launch RPM to the fitted acoustic interval. A monotone cubic Hermite tail joins the fitted RPM and slope at the last inlier to zero RPM at the manual stop. A timeout retains the distinction between recorded duration and spin finish.

If Fixed Riccati declines the trace, the Stick fits Free Riccati (`a + b*x + c*x^2`, where `x` is in thousands of RPM). The fallback classifies sustained downward tracks, trains on the first 80% of retained readings and validates on the final 20%. If neither fit succeeds after a manual stop, a `Fixed Riccati Estimate` scales the shared shape between the IR launch and manual zero endpoint. This estimate and its unused readings are gray, and exports distinguish it from a mic fit. Accepted fits keep their existing validation checks. While recording, full-brightness 2x2 dotted lines show current time in the theme's text color and predicted finish in its warning color. The predicted-finish label also uses the warning color. The live axis extends ten seconds beyond that prediction, then follows elapsed time plus ten seconds if the spin outlasts it.

### Analysis Details

After stopping, press **B** to open Details. The page shows duration, average RPM lost per second, total revolutions, estimated battle stamina and the time spent crossing each complete 1,000 RPM band. Revolutions come from the area under the displayed curve. Battle stamina is the time from launch to its first downward crossing of 1,000 RPM. A timeout limits these metrics to the recorded interval. Press **B** for additional bands or **A** to return.

Opening Details queues the latest completed fit for storage at an idle flash checkpoint. The 512-byte `analysis_fit` record contains the coefficients, scores, curve anchors and winning model when one was selected. Saving reserves room for practice history. Manual stop and timeout immediately queue the frozen raw observations, last live preview and endpoint for calibration. The final fit adds metrics and exclusion flags when available. A failed raw or fit write remains queued for retry at an idle checkpoint. The newest 32 completed packets are retained across restarts at idle flash checkpoints. Each includes full-session acoustic metadata: 200 ms summaries for the first 30 seconds and one-second summaries thereafter, up to five minutes. Eight FFT energy bands, level, flatness, candidate pitch evidence, noise/clipping counts and delivery timing are recorded even when no RPM is accepted. A separate 32-record ring replaces the former two-WAV storage; metadata writes retry independently of raw traces and fits. The collector uses guarded command `Z` to download the batch in chronological order. Command `J` retrieves the newest RAM packet, or the latest saved packet after a restart. See [Windows calibration export](../../docs/LOCAL_WINDOWS.md#export-the-latest-launch-for-calibration). A new launch or leaving Analysis releases the previous result.

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
- **Menu items:** History, New session, Device, Settings, Tournament, String wear check, Mic RPM and Back. History offers Launches, Pull trend and Session trend. New session starts on the next accepted pull and keeps earlier launches.
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

Run `bash scripts/test-firmware.sh` for the native suites, production-code harnesses, export compatibility and local updater checks. The native gate requests AddressSanitizer and UndefinedBehaviorSanitizer; use a compiler with those runtimes. The local updater tests cover application-only upgrades, no-op detection, chip/capacity checks and artifact verification without opening a device.

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
