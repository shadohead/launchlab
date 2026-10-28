# Firmware 0.20.3 build context (beta)

The beta runtime is `0.20.3-sticks3-songs-pm`, application SHA-256 `d21cec9223582fa1d58d130025ad947aabf364d3f9d268a355233fa4897f8672`. The compiled source snapshot is frozen in the [release directory](../firmware/releases/0.20.3-sticks3-songs-pm/). It is a beta: the recommended release is now [0.20.4](FIRMWARE_0.20.4.md). It uses the same PM SDK, toolchain, bootloader and partition table as [0.13.5](FIRMWARE_0.13.5.md). Use the [USB updater](https://launchlab.beywatch.gg/update) for the shipping image.

## Build from source

Install Arduino CLI, Python 3 and curl, then run `bash scripts/build-firmware.sh` from the repository root. It pins ESP32 core 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30, downloads and verifies the full PM SDK, stages current source, compiles and checks stack budgets. `ARDUINO_CLI` can select the CLI executable; `LAUNCHLAB_ARDUINO_CACHE` can select its cache directory.

The SDK release asset `launchlab-esp32s3-pm-sdk.tar.gz` has SHA-256 `5d322bcc98a71beba048d34e87fe5821b758d05ef204eb1771c0a736254b6865`. To use an already extracted SDK, set `LAUNCHLAB_PM_SDK` to its `esp32-arduino-libs` directory. SDK payload hashes are verified before compilation. The upstream Arduino stock SDK has PM disabled; adding a header define alone is insufficient.

The ESP32-S3 target uses 8 MB flash, OPI PSRAM, hardware USB CDC and default_8MB partitions. Compilation explicitly enables `LAUNCHLAB_MOTION_ADC_GATE=1` and `LAUNCHLAB_CPU_PM=1` together with VQF single precision, and passes `-mtext-section-literals` so each function's literal pool stays beside its code (the sketch now holds more code than an `l32r` can reach across). Source defaults remain disabled for builds that do not opt into these features. The runtime selects max 80 MHz/min 40 MHz, holds an 80 MHz lock during ADC acquisition and disables automatic light sleep.

## SDK lineage

- ESP32 Arduino lib-builder: `afa5cdd0d41d7e465d2e39867b24aef48b1c7b84`
- ESP-IDF: `b66b5448e0dfac6be135e6f0ac21010aba231180`
- Arduino: `554de56f40e68d591b945bd1a8c931798e8a9167`
- TinyUSB: `8a78804ff91271fb32008288777fad9fe73f5735`

The full qio_opi SDK was rebuilt with `CONFIG_PM_ENABLE=y` and `CONFIG_PM_PROFILING=y`; generated `CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL=1` is the only additional header difference. `CONFIG_PM_DFS_INIT_AUTO` stays disabled. The original dependency lock is preserved; stock bootloader ELFs and speech model bytes are unchanged. The asset includes verification metadata, the dependency lock and upstream license notices.

## IMU acquisition

The BMI270 accelerometer is programmed to ±16 g and verified after every IMU initialization; the gyro stays at its ±2000 °/s maximum. Samples come from the header-mode FIFO (gyro 200 Hz, accelerometer 100 Hz), read in single I2C transactions and timestamped by a phase-locked sensor timeline. FIFO reads stop while flash is written: a save waits up to 250 ms for a read in progress and is otherwise deferred. Frames are timestamped at the FIFO length read. A full or stalled read is discarded and reported as a gap. Before the first bus use, startup clocks the internal I2C bus free if a reset interrupted a transfer (`I2C_RECOVERY`). Startup compares a resting FIFO frame with M5Unified's reading to prove the axis mapping and scale. Serial `p` reports `IMU_FIFO_STATUS`, `MOTION_CLIP` and recent `MOTION_GAP` records.

## RPM detection

The optical detector cannot see direction, so the cord rewind after a pull looks like a new pull. While the rearm window expects that rewind, irregular strong turns extend it; once the rewind is recognized (three turns gaining 15% in speed), only matching turns do. A second pull less than about 1.1 s after the first one's marks stop, with no rewind between, is therefore absorbed; from about 1.1 s it is measured. Sharp pulls confirm on monotonic windows, a missed mark near the peak waits for the next marks instead of ending the burst, and lost ADC samples keep a finished result. All recorded replays keep identical launch counts and RPM.

## USB serial

Output is drained with a bounded wait and never discarded; the Arduino core's `Serial.flush()` empties the whole transmit buffer after a 10 ms host pause, so it is not used. Backup `K`, restore `R` and the status letters run at once. Every other single-letter command (power off, sleep, settings, session, buttons, storage checks) runs only when it stands alone: after a newline or 20 ms with the receive queue empty, and followed by a newline or the same quiet. `PERF` reports `usb_drain_timeouts` and `usb_rejected_commands`.

## Custom themes

Every UI color comes from a palette with eight roles: background, text, muted, accent, number, warning, ready and alert. The other colors are derived from these. The ten built-in themes map to palettes that render pixel-identical to 0.13.9. A custom theme is a name of up to 14 characters plus the eight roles. Validation requires a dark background and at least 4.5:1 contrast for text roles, and a lower bound for muted text. Custom themes are stored in their own NVS namespace, travel in backup format 2 (format 1 backups still restore), and a damaged record falls back to the default theme. The website theme editor previews the screens with the firmware's own drawing code and uploads over USB.

## RPM milestones

Settings > Celebrate selects New best, Milestones or Both (the default from 0.18 for a unit that never saved a choice). The milestone table holds up to eight rows of an RPM threshold (1,000 to 30,000, unique, sorted) and an effect. A valid pull plays the effect of the highest threshold its displayed RPM reaches, on every pull; with Both, a pull that is also a personal best plays the milestone effect once and keeps the NEW BEST label. The default ladder is 9K to 15K, from Sparkles to Slash. The table and mode are stored together; a damaged record falls back to the defaults. Backup format 3 carries the table only when it differs from the defaults. The USB commands `M` (read) and `U` (write) follow the stand-alone command rule. The Slash effect's slowest frame is 7.8 ms on device.

## Celebration effects and milestone pools

Sixteen effects are available: Orbit, Flames, Sparkles, Shockwave, Lightning and Slash, plus Tidal, Frost, Gale, Eclipse, Radiance, Spin, Storm, Quickdraw, Leaves and Bloom (stored values 8 to 17; earlier values never move). Each runs about five seconds: an anticipation and an impact, a sustain that keeps moving, then a settle. Anything that dims, moves or covers the RPM ends by about 2.1 s, sounds stay 1.3 to 2.8 s, and the recap holds until the celebration ends. Every frame of every effect is bounded below 10 ms at 80 MHz, checked on the host and on device. Milestones (up to 12, at any RPM from 1,000 to 30,000) each hold a pool of effects; a pull reaching one plays a random pick from its pool without repeating the previous pick. 0.15 milestone tables import unchanged; backups stay format 3 when 0.15 can hold the table and use format 4 otherwise, which 0.15 refuses without writing. Custom themes accept any colors; the website warns about light backgrounds and low contrast.

## The 0.17 effects and the effect set

25 more effects are available, in five sets of five: Crimson, Bulwark, Crux, Klaxon and Prism; Sunwheel, Torrent, Blitz, Tigerclaw and Butterfly; Birdsong, Spiral, Pinwheel, Overdrive and Summon; Breach, Bladework, Moonpetal, Barrage and Glitch; Rupture, Boundless, Precision, Umbra and Ravage (stored values 18 to 42; earlier values never move). Each follows the same rules as the 0.16 effects: about five seconds, the RPM readable on every frame, a sound of 1.9 to 2.8 s, and every frame under 10 ms at 80 MHz on device (slowest 8.9 ms).
Every effect stays compiled in, and the effect set picks which ones Settings > Best effect offers and Random plays: a 28-byte record with a 128-bit mask of stored effect values, written only from the website (USB `U` op 3) or a restore. The default is the 16 effects of 0.16, so an updated device behaves as before until the set is changed; a saved Best effect outside the set keeps playing. Milestones can play any effect. `M` reports the set; 0.15 and 0.16 refuse op 3 without writing. Backup format 5 adds the set after the pooled milestones, only when the set differs from the default; other backups keep their format byte for byte, and 0.15 and 0.16 refuse format 5 without writing.

## The 0.18 effects and 128-bit milestone pools

40 more effects are available, in eight sets of five: Haymaker, Palms, Elastic, Chains and Waterleaf; Transmute, Ignition, Spires, Gate and Fracture; Timestop, Zipper, Rush, Menace and Sequel; Swordland, Treasury, Sunblade, Aegis and Trace; Volley, Starfall, Mimic, Scales and Meadow; Rev, Hammer, Foxbite, Detonate and Sharkfin; Slingshot, Presence, Threefold, Banner and Vapor; Crescent, Release, Petalfall, Mask and Blink (stored values 43 to 82; earlier values never move). Boundless is redesigned over the whole screen. Each follows the same rules as before, and every frame of all 81 effects is under 10 ms at 80 MHz on device (slowest: Sunblade, 9.1 ms).
Milestone pools reach every effect: USB request v3 carries 128-bit pools (v1 and v2 requests still apply; older firmware refuses v3 whole), and backup format 6 is written only when a pool holds an effect past 63; other backups keep their format byte for byte, and 0.15 to 0.17 refuse format 6 without writing. Every table stays in 0.16 and 0.17's own NVS slots: a narrow table is their record byte for byte, a wide one a packed record of the same size, with 0.16's view of it in the other slot, so a rolled-back unit keeps its table and the room its saves need.

## Saves and the history log

Automatic saves (history, personal bests and the battery log) go to a log in the `spiffs` partition, which no earlier firmware used and the updater never writes. A save only programs flash that was erased ahead while nothing was measured, a few milliseconds, and the ADC keeps sampling throughout, so a pull that starts during a save is measured in full. Its DMA frames are 4092 bytes, a 102 ms ring that rides one flash operation, and a watchdog restarts the ADC if frames stop for 250 ms (`PERF adc_stalls`). Each record counts only once it is complete, so a power loss leaves the previous save or the new one.
NVS keeps the 0.15 to 0.17 records as a mirror, written at sleep, power-off and restore, so a unit rolled back keeps its history and settings. At boot the newest wins: the log, unless NVS changed since the last mirror (an older firmware saved after a rollback). Settings, themes, milestone tables and the effect set still save to NVS. A save that would grow NVS (a first custom theme, a table or effect set slot filled for the first time) is refused unless 97 entries stay free, so history saves never slow down; the website says so plainly. Wipe all also erases the log.

## Defaults

A unit that never saved these choices now uses the 1-turn peak RPM method, 30% brightness and Celebrate Both. Saved settings are kept.

## 0.18.1

- **USB requests arrive whole.** Every build reads USB into an 8 KB queue (`USB_RX_BUFFER_BYTES`, set before `Serial.begin`). 0.18.0 kept the core's 256-byte queue, which dropped the end of a v3 milestone request (312 characters sent in one write), so milestone saves from the website went unanswered. `check_usb_config` holds one USB configuration for every build, above the longest request line. The website also sends each line in 64-byte pieces, which fixes 0.18.0 units. The queue costs about 8 KB of internal heap; after start-up about 54 KB of DMA-capable internal RAM stays free.
- **Refused lines run nothing.** When a restore, theme or milestone request refuses a line partway, the rest of that line is dropped up to its newline (or after 20 ms of quiet), so a letter inside it cannot start another transfer or run as a command.
- **Sound in step with the frames.** The effect clock moves at its cue to where the sound will be heard, from the measured present lag and the speaker's DMA ring; a sound ready only later joins at the matching sample, and the editor's preview holds its first frame while its sound renders. The speaker mixes mono (M5Unified sets the StickS3 up as stereo), which halves its work on the UI core during frames at the same loudness.
- **IMU task.** A late IMU poll skips the missed 5 ms periods instead of running through them back to back, which could keep the idle task off core 0 until the task watchdog reset the device.
- **Effects.** Sunblade and the slow effects step only what they can draw, with identical frames. Every effect's slowest frame is under 10 ms at 80 MHz with Sound on.

## 0.19.0: added celebration effects

- **Effects without a firmware update.** An added effect is an LLFX file: loop-free bytecode that calls the built-in renderer's own 35 drawing primitives (dots, lines, emitters, strokes, arc trails, sprites and more). It is never native code. The loader bounds every file before it plays (24,000 instructions and 3,000 draws a frame, 4 KB of internal RAM), and a guard stops any frame at 9 ms; after three stopped frames in one play the effect gives way to the plain recap and `FX_OVERRUN` is logged.
- **The store.** Up to 32 added effects live in their own region of the `spiffs` partition (36 slots of 16 KB, beside the history log), written a slot at a time so a power cut leaves the old effect or the new one whole. They take stored values 96 to 127 and play like built-ins: as Best effect, in Random through the effect set, and in milestone pools. Each names a built-in fallback, which plays wherever the effect is removed or cannot play.
- **Sound.** An added effect can carry its own sound: up to 16 native voices (tones and noise, each with a filter and an envelope) set every 2 ms, with notes timed to the sample, rendered into the same buffers as the built-in sounds and synced the same way.
- **USB.** `F` lists the store and each slot. `X` installs, removes, clears or reads back a slot's source as one framed, checksummed request, refused while a pull is measured. `M` adds `loaded=`, the added effects that play, and `CELEBRATION` names the effect that plays.
- **Rollback and backups.** Records that name an added effect are also saved with an older view (its fallback in its place), so 0.18 and 0.15 read their own records after a rollback; older firmware never opens the effect region, so a re-upgrade finds every effect where it was. Backup format 7 carries the added effects and is written only when a unit has or names one; every other backup keeps its format byte for byte, and older firmware refuses format 7 without writing.
- **Sharing.** Effects are shared as text codes (`LLFX1:` and the tokenized, compressed source, about 1,500 characters) or `.llfx` files. The website's effects library compiles them, checks them against the device's frame budget on real recaps and sends them over USB; its compiler, checker and renderer are this firmware's own code compiled to WebAssembly. The compiler and the example effects are in `firmware/tools/fx`.
- **Memory.** The runtime lives in PSRAM and takes 4 KB of internal RAM only when an added effect first plays (if it cannot have it then, the effect plays its stock fallback), so a unit without added effects keeps its DMA-capable memory: about 53 KB stays free after start-up.

## 0.20.0: songs

- **Songs.** An added effect can carry a song of up to 120 s on up to 8 channels, which plays on after its animation until it ends or a pull starts. A song is patterns of rows at a tempo with an order to play them in: notes, note offs, volume and voice changes, at most 64 patterns of 255 rows, 256 order entries and 8 KB compiled.
- **Voices.** Besides 0.19's tones and noise, engine level 2 adds a band-limited pulse with any duty, a 16-point wavetable, two-operator FM, stepped noise and drums. The drums are a bank of eight original synthesized samples (kick, snare, hat, open hat, clap, low tom, high tom, rim) in 16 kHz IMA ADPCM, 13.8 KB of the image; the bank only grows, and a file that names a drum the firmware lacks is refused as needing a newer engine.
- **Streaming.** A song renders in its own task on core 0 at priority 1, below the optical ADC and the IMU, into a ring of four 512-sample blocks (32 ms each) in PSRAM, which the UI feeds to the speaker. The task renders one block at a time and sleeps in proportion, busy at most 60% of the time and always yielding at least 1 ms, so the idle task always runs; a song that cannot keep up leaves gaps of silence, counted as underruns in `MUSIC_STATUS`, and never stalls. A pull fades the song to silence within 20 ms. RPM capture and the 10 ms effect frame budget are unchanged.
- **Render budget.** A song must fit a render budget, as an effect's frames fit the frame budget: at its busiest block it may take at most 20% of core 0 at 80 MHz, by a cost per sounding voice measured on the device for each voice type. The compiler's check (`fxc check`) and the website compute it by rendering the song and refuse a song over it. On the device a song at the budget, over the heaviest effect frames, played its 120 s with no underrun.
- **Compatibility.** An effect that uses no 0.20 feature compiles byte for byte to the 0.19.0 file and plays the same on 0.19.0. A file with a song or a new voice says engine level 2: 0.19.0 keeps it stored whole, lists it as needing an update and plays its fallback in its place, and coming back to 0.20.0 plays it. Share codes add 21 words for the new voices, songs and drums, after version 1's existing words.
- **Website and examples.** The effects library imports MIDI files (formats 0 and 1), turns them into song source, and previews the song with this firmware's own compiler and renderer compiled to WebAssembly; a part that would put a song over its budget is left out. Three original example songs are in `firmware/tools/fx/effects/music`.
- **Memory.** While a song plays the speaker holds about 8.4 KB of internal RAM, as any sound does. The automatic history, best and battery-log saves now build their records in PSRAM, so a save during a song keeps at least 32 KB of DMA-capable internal RAM free.
- **RPM range.** Readings are within about 2% up to 22,000 rpm. Above that they can read up to 6% low (at 25,000-29,000 rpm), as in 0.18 and 0.19: the detector places each mark on a 100 µs grid, which is a few percent of a turn at those speeds. Interpolating the mark between grid points is planned for 0.20.1.

## 0.20.1: RPM at high speed, 6 s songs, a start floor while sound plays

- **Sub-grid edges.** The detector evaluates its 0.5 ms mean every 100 µs and used to place each mark edge on the grid point after its threshold crossing, so periods were whole 100 µs steps: 15,500 rpm read +1.9% and 22,000 rpm +1.0%. Each edge is now interpolated at its crossing between grid points, for both estimators, with the same latency offset.
- **Noise band during a pull.** From about 25,000 rpm the mean sits on a mark's transition at most grid points, so the median slope the noise band is built from became a transition's slope; the band exceeded the marks' whole swing and the burst ended before its peak (27,000-29,000 rpm read up to 11% off). During a confirmed pull the band is now capped at the larger of the band the pull started in and half the marks' swing. Outside a pull it is unchanged.
- **Accuracy.** On the hardware-in-the-loop generator, three pulls at each of 8,000, 12,000, 15,500, 22,000, 25,000, 27,000 and 29,000 rpm read within +0.29% (0.20.0: -10% to +11% above 25,000 rpm). Over the recorded corpus no pull gains or loses validity and readings move by -0.57% to +0.71%.
- **Songs last as long as a celebration.** A song plays at most 6 s. The compiler and the validator the website runs refuse a longer one in plain words. Songs installed under 0.20.0 keep working: a longer one plays its first 6 s and fades out over 300 ms. The example songs are phrases that end, and the MIDI importer takes a 6 s excerpt in whole bars that fades out when the file runs on.
- **A pull's start while a sound plays.** The speaker couples into the optical input. While any sound plays and the detector waits for a pull, a launch must start on marks of at least half the median weakest start mark of the unit's last five confirmed pulls (at least the sensor's own floor, at most 400 counts; three pulls before it applies). It is kept per sensor profile, saved at shutdown within the storage reserve, and cleared by a sensor change or Wipe all. Every recorded pull starts and reads as before; on the host, speaker-shaped coupling that made phantom launches makes none.

## 0.20.2: a 1-minute default sleep and Shake to wake

- **Sleep after** defaults to 1 minute instead of 3 for a unit that never chose. A saved choice, the old 3-minute default included, is kept. An invalid saved value falls back to 1 and is kept as it is. A pull's history is saved before the unit sleeps, as before.
- **Shake to wake**, a new Settings row after Sleep after, On by default. Off makes the inactivity sleep the side key's power-off: the IMU is not armed and its supply not held, a deeper sleep on battery, and only the side key wakes the unit. It is stored in its own NVS key and in byte 6 of the backup's settings section (1 for Off): backups from units with it On are byte for byte as before, and firmware before 0.20.2 refuses one with it Off before writing anything.
- Otherwise the frozen 0.20.1.
- Known issue: a website save while a song plays can make the next song drop out once (the UI loop feeds the speaker); fixed in 0.21, which feeds it independently of the UI.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).

## 0.20.3: Wear check

- **Wear check**, a new Menu row (Back moves to row 6): a guided measurement of how fast the launcher's string rewinds, in three stages of three pulls, level, pulled down at 90 degrees and pulled up at 90 degrees, each within 12 degrees of its target. The first accepted pulled-down pull fixes the direction and the third stage must point the other way. The result shows the average retraction RPM over the nine pulls, the mean of the stage medians and each stage's average. There is no health percentage yet (`WearCheck::FRESH_BASELINE_RPM` is 0 until it is calibrated against a fresh launcher).
- Its pulls go to the wear check only: not to the history, the session, personal bests, milestones, celebrations, songs, the tilt replay, the start-mark reference or NVS. Leaving the page hands pulls back to history.
- The RPM detector is unchanged: a retraction is measured like any pull, from the 1,000 RPM launch floor. After each pull the detector re-arms for a second, extended while turns continue, and a release inside that time is the pull's own rewind and is never reported; on target the page shows WAIT until a release would be measured.
- A pull held on target before its burst may end up to 18 degrees off (release recoil). Saved wake navigation has a new layout, so an older Menu Back row still wakes on Back.
- Otherwise the frozen 0.20.2.
- Known issue: a website save while a song plays can make the next song drop out once (the UI loop feeds the speaker); fixed in 0.21, which feeds it independently of the UI.
- Known issue: rarely, after a restart, the device's USB connection stops responding while the device keeps working. Unplug and replug the USB cable to restore it (or hold the side key 6 s).

## Flash layout and checks

Bootloader: `0x0`; partitions: `0x8000`; boot selection: `0xe000`; application: `0x10000`. Ordinary updates and deliberate rollback write only the application and preserve settings/history. First install uses all four images after the updater's explicit confirmation.

Run `bash scripts/test-firmware.sh`, `python3 scripts/verify-artifacts.py`, and `npm test` in `site/` to check firmware behavior, frozen artifact identities and updater guards. `npm run build` in `site/` creates the compatibility redirect and copies the unchanged public artifacts; it builds no flashing interface.
