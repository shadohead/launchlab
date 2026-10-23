# LaunchLab for M5StickS3

This source is **0.18.1-sticks3-anime-pm** (beta). The recommended stable release is **0.18.0-sticks3-anime-pm**. Both are for **M5StickS3 K150 (ESP32-S3, 8 MB flash / 8 MB OPI PSRAM)**.
One application supports analog QRE1113 and TCRT5000 through the saved Sensor profile setting.
See the beta [build context](../../docs/FIRMWARE_0.18.1.md) and [release notes](../releases/0.18.1-sticks3-anime-pm/README.md), or the stable [build context](../../docs/FIRMWARE_0.18.0.md).

Tilt replay is reliable on hard pulls: the accelerometer uses its ±16 g range, and every IMU sample is read from the BMI270 FIFO, so the live level and recordings no longer drop out at rest or during saves. A pull that reaches the gyro's 2000 °/s limit keeps its replay, marked **Tilt approx.**
A single click of the side power key saves pending pulls and powers off.
RPM detection treats the cord rewind after a pull as part of that pull, confirms sharp pulls and rides through a missed mark. State-changing USB letters run only when sent on their own, so stray serial data cannot power off the device or change settings; backup and restore work as before.

Filtered movement renews the 30-second unused shake-wake timer. Stillness also enables RESTING, which pauses ADC sampling until movement or a button resumes it. The shared sensor supply remains powered. USB keeps the device awake.
0.18.1 beta starts celebration sounds in step with their animation, receives milestone saves from the website whole (an 8 KB USB queue in every build), fixes a rare IMU task watchdog reset and plays the speaker in mono.
0.18.0 adds 40 more celebration effects in eight sets of five (81 in all, any of which a milestone can play) and redesigns Boundless over the whole screen.
Automatic saves go to a separate log in flash in a few milliseconds while the ADC keeps sampling, so saving never interrupts a pull; a storage reserve refuses a new custom theme, milestone table or effect set before saves would slow down.
A unit that never saved these choices now uses the 1-turn peak RPM method, 30% brightness and Celebrate Both.
25 more celebration effects in five sets arrived in 0.17.0, and the [website celebrations editor](https://launchlab.beywatch.gg/celebrations) picks which effects Best effect and Random use.
Ten more celebration effects (Tidal, Frost, Gale, Eclipse, Radiance, Spin, Storm, Quickdraw, Leaves and Bloom) arrived in 0.16.0, where every celebration runs about five seconds, each milestone can play a random pick from several effects, milestones can sit at any RPM, and custom themes accept any colors with warnings.
RPM milestone celebrations play an escalating effect at each milestone a pull reaches, 9K to 15K by default ending with the new Slash effect; choose Settings > Celebrate and edit the ladder in the [website celebrations editor](https://launchlab.beywatch.gg/celebrations).
Custom color themes made in the [website theme editor](https://launchlab.beywatch.gg/themes) can be uploaded over USB and chosen under Settings > Theme; the ten built-in themes are unchanged.
Settings adds saved Top/Sidecar mounting and a guarded Wipe all flow. Themes, personal-best effects, optional sound, history and USB backup/restore remain available.
Battery display uses filtered voltage readings, idle screen updates are reduced, and PM1 access uses checked wake/retry handling.

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
- **Menu items:** History, New session, Battery, Settings, Tournament and Back. History offers Launches, Pull trend and Session trend. New session starts on the next accepted pull and keeps earlier launches.
- **Settings:** Sleep after, RPM method, Brightness, Flip display, Sensor profile, Theme, Best effect, Sound, Mounting and Wipe all. In an editor the value is centred, with a faint `B next option ->` hint beneath it: B cycles the value, A applies, hold B cancels.
  - Sleep after: 1–10 minutes, 3-minute default.
  - RPM method: 3-turn average (default) or 1-turn peak. The launch floor is 1,000 RPM.
  - Brightness: 10–100% in 10% steps.
  - Flip display: rotates the screen 180°. Brightness and flip preview immediately and revert if you cancel. Physical A/B buttons keep their roles when flipped.
- **Tournament:** starts in Recording Only; B cycles Recording Only / RPM; hold A exits. The selected view resumes after shake wake.

Accepted pulls show a five-second recap from Main. Menus, history and setting drafts stay open while launches keep recording. The battery page shows percentage, icon, voltage and charging status; the voltage-based percentage is not a calibrated capacity measurement.

## Acquisition and replay

A dedicated core-0 task owns continuous ADC1 DMA acquisition at **50,000 samples/s**. Detector time advances by sample count (20 µs per sample), independent of screen/USB delays. ADC losses invalidate sequence continuity and are reported in diagnostics. A separate lower-priority IMU task reads the BMI270 FIFO (gyro 200 Hz, accelerometer 100 Hz at ±16 g / ±2000 °/s) and performs six-axis VQF fusion on a sensor-clocked timeline; screen/buttons/USB run on core 1. IMU reads pause while flash is written (a save waits for any read in progress, or is retried later) and resume from the FIFO without losing samples. Startup frees the internal I2C bus if a reset interrupted a transfer.

The release enables CPU power management at **40–80 MHz**, holding 80 MHz during acquisition. Automatic light sleep is disabled. Startup verifies fresh paired accelerometer/gyro data and the FIFO axis mapping, makes up to three attempts, and retries a missing IMU owner every five seconds while idle.

After a pull, the recap shows RPM, frozen Start/End level dots and a rotation replay. With the display flipped, the live level, the Start/End dots and the 3D recap all follow the flipped screen; recorded data is unchanged. The IMU measures gravity-relative tilt and relative rotation, not travel distance or compass heading. RPM describes launcher-shaft speed, not stadium/release RPM.

History retains up to 128 launches and 24 sessions. Automatic checkpoints wait for ten quiet seconds after the last accepted pull once the launcher is set down, or a minute while it is handheld; pulls left unsaved for three minutes are written at the next idle moment. Standby flushes pending history; abrupt power loss can lose the pending bout.

## Power

Measured launches, button use and interactive USB commands refresh the inactivity timer. A detected USB computer keeps the device awake even without a serial reader. Battery sleep uses IMU shake wake. An unfinished setting edit is cancelled before sleep. Wait for the screen before pulling after wake.

## Build and verification

Run `bash scripts/build-firmware.sh` with Arduino CLI installed. The script pins ESP32 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30, verifies the complete PM SDK and explicitly enables both release flags. Use the frozen application for exact binary restoration; rebuilt bytes can vary with build environment.

Run `bash scripts/test-firmware.sh` for 67 sanitizer-enabled native suites and 19 production-code harnesses with synthetic fixtures. The updater tests cover application-only upgrades, no-op detection, intentional rollback, first-install confirmation, chip/capacity checks and artifact verification.

[USB update and backup](../../docs/UPDATER.md) · [Recovery](../../docs/RECOVERY.md).
