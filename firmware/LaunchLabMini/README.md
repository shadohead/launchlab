# LaunchLab for M5StickS3

Current revision: **<!-- current-release:start runtime -->0.20.8-sticks3-songs-pm<!-- current-release:end -->**, the recommended stable release, for **M5StickS3 K150 (ESP32-S3, 8 MB flash / 8 MB OPI PSRAM)**. <!-- current-release:start source --><!-- current-release:end -->
One application supports analog QRE1113 and TCRT5000 through the saved Sensor profile setting.
See <!-- current-release:start build-context -->[build context](../../docs/FIRMWARE_0.20.8.md)<!-- current-release:end --> and <!-- current-release:start release-notes -->[0.20.8 release notes](../releases/0.20.8-sticks3-songs-pm/README.md)<!-- current-release:end -->.

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

During recording, a gated alpha-beta filter smooths the microphone readings. Gray points show readings withheld by the live filter. The timestamp above the graph shows the last accepted reading. The fitter retains the original readings so that smoothing does not distort its coefficients.

### Fitted curve

After five seconds without a valid raw microphone reading, Analysis can fit a provisional curve in the background. It needs at least eleven raw observations to attempt chronological validation. The live status shows **Predicted finish** in MM:SS from launch when a stable winning fit has a corroborated stop estimate. The estimate must still be in the future and its remaining span from the last observation cannot exceed twice the observed acoustic duration. At least one comparably accurate stable model must agree on the remaining time (within five seconds or 25%, whichever is larger). Fits at the minimum constant-drag bound do not qualify. These display limits are heuristics. Until then it shows **--:--**. The projection is gray. The live axis extends ten seconds beyond the timer; its rightmost label is the edge of the viewing window. A new raw reading clears it. A preview uses the optical launch and acoustic evidence alone. Only the final fit can populate Analysis Details or be saved.

Stopping the timer starts the final background fit. The worker consolidates repeated timestamps and checks upward jumps against the subsequent readings. After a gap longer than two seconds, a higher reading needs corroboration. A sustained higher track with at least four readings and two distinct declines can replace an earlier track.

All four models start at the optical launch RPM. With speed `x` in thousands of RPM, the models use these deceleration laws:

| Model | Deceleration |
| --- | --- |
| Coulomb-Viscous | `a + b*x` |
| Coulomb-Quadratic | `a + c*x^2` |
| Riccati | `a + b*x + c*x^2` |
| Power-Law Damping | `a + k*x^n`, with `0.5 <= n <= 3` |

A manual stop constrains the fitted duration. Time-weighted Huber loss reduces the influence of large acoustic errors. The first 80% of retained observations train each model, and the last 20% provide its validation score. The fitter favors the simpler model within a 15 RPM validation tie and refits the coefficients on the full track. USB serial reports the individual coefficients and errors in `ANALYSIS_MODEL` and `ANALYSIS_METRIC` records.

The winning model appears above the graph. The solid line uses the theme accent over the retained acoustic interval and gray outside it. After a manual stop, a monotone cubic Hermite segment joins the last retained reading to zero RPM at the stop time. If validation is inconclusive, the graph shows the classified observations.

### Analysis Details

After stopping, press **B** to open Details. The page shows duration, average RPM lost per second, total revolutions, estimated battle stamina and the time spent crossing each complete 1,000 RPM band. Revolutions come from the area under the displayed curve. Battle stamina is the time from launch to its first downward crossing of 1,000 RPM. A timeout limits these metrics to the recorded interval. Press **B** for additional bands or **A** to return.

Opening Details queues the latest completed fit for storage at an idle flash checkpoint. The 512-byte `analysis_fit` record contains the coefficients, scores, curve anchors and winning model when one was selected. Saving reserves room for practice history. Raw readings live in RAM for the current session. A new launch or leaving Analysis releases the previous result.

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
