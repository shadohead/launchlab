# LaunchLab hardware-in-the-loop rig

Scripted pulls on a real M5StickS3 instead of manual Beyblade pulls.
A test-only firmware build replaces the optical ADC values and the BMI270 FIFO bytes with host-controlled inputs, while every production path above them runs unchanged: DMA cadence, detector, history, estimators, recap, saves, IMU timeline, fusion, ring, capture and UI.
A Python runner plays scenarios and asserts on the device's own serial output.

## What the test build changes

`LAUNCHLAB_HIL=1` (set only by `firmware/build-m5-hil.sh`) compiles these hooks in; production builds compile none of them.

- Optical (`hil_optical.h`): the real ADC keeps running at 50 kS/s and paces the detector; the values of each consumed DMA frame are replaced by an idle baseline with noise, an on-device mark generator (RPM/depth/noise keyframes), or a raw waveform uploaded to PSRAM (12-bit samples, the flight recorder / corpus `u16le` format, up to 10.5 s).
Playback follows the host clock, so samples that fall inside an ADC stop (flash checkpoint) are lost exactly as a real pull's would be.
- IMU (`hil_imu.h`): every FIFO length/data transfer still runs on the real I2C bus, but in a virtual mode the bytes come from a modelled header-mode FIFO (gyro frames every 5 ms on a virtual sensor clock, accelerometer every second frame, 2 KiB with real-chip overflow behaviour).
Values come from a constant vector or an uploaded 200 Hz profile (board axes, mg and 0.1 dps, saturating at +/-16 g and 2000 dps).
Faults: failed length/data reads, stalled transfers, forced overflow, oversize length, skip frames, corrupt headers, garbled payload bytes, a frozen sensor, sensor clock drift, frame jitter and length-read latency jitter.
- Pull sync: `!pull` gives optical sample 0 and IMU profile frame 0 one shared start time.
- History log (`hil.h`): the NVS write faults (`nvs slow`, `nvs fail`, `arm`) also act on the history log's writes (keys `history`, `best`, `log`); `!logstatus` reports the log's sectors, erase counts and newest records; `!newerforget` also erases its superblocks, so the next 0.18 boot formats it as on a unit 0.18 never ran on.
- Robustness (`hil.h`): Preferences' NVS setters are linker-wrapped to fail or slow chosen keys; a task can hold the shared IMU/I2C gate (the 0.13.8 checkpoint veto); `!set` applies a setting through the real `commitSetting()` path; `!usbidle` lets the sensor-idle pause run with USB attached; `!powerlog` makes the next loop run the real power-log checkpoint.
- Mic (`hil_mic.h`): `!mictone` replaces each captured chunk with launch noise and a falling tone after the next pull, so capture, ring, tracker and page run as in production on sound the host chose.
It proves a reading's cost and the page's timing, memory and paths, never what the mic really hears. `!micfloor` sets the heap floor (production's less the HIL image's own RAM, or an impossible one to force a refusal), `!micframe` draws the page's slowest frame, and `!miclag <ms>` hands each pull's result to the analysis task that much later, so a release the 8 s ring has lost is exercised.
- Heap (`hil.h`): `!heapwalk` prints every block of the internal DMA-capable heap (address, size, used, first words) and each task's TCB and stack, collected under the heap lock and printed after it; it named what the 0.20.7 mic's first session leaves (M5Unified's task semaphore and an interrupt record).
  `!feedgaps` lists each loop pass of 48 ms or more while a song streamed (a song has 64 ms queued), with whether it printed a status, saved history or drew a frame, then resets.
- The runtime reports `0.18.1-sticks3-anime-hil-pm` and prints `HIL_BOOT boot_id=... reset=...` at every boot.

With the flag off, the production PM image is identical in every loaded byte, and its ELF is byte-identical once debug sections are stripped (checked against origin/main on 2026-10-17).
Only `app_elf_sha256` in the app descriptor (a hash of the whole ELF, whose DWARF line tables move with the added `#if` lines) and the image checksum/digest derived from it differ.
`compare_app_images.py` checks this; build both images with the same `SOURCE_DATE_EPOCH`, because the Arduino core embeds `__DATE__`/`__TIME__`.

## Build

```bash
LAUNCHLAB_PM_SDK=<rebuilt PM SDK root> bash firmware/build-m5-hil.sh
```

Output: `firmware/build-m5-hil/LaunchLabMini.ino.bin`.
`bash firmware/build-m5-release.sh` builds production and HIL at once from one sketch copy, each in its own build directory, as the release gate does; both images are the bytes each build makes on its own.
A worktree needs `firmware/.arduino` and `firmware/tools/arduino-cli` (untracked) linked from the main checkout.
In the public launchlab repository, `bash scripts/build-firmware.sh --hil` builds it into `firmware/build-hil`, with the production image of the same tree in `firmware/build` and `hil-overhead.json`.

## One run on your own unit, put back afterwards

`python3 scripts/firmware_proof.py hil --port PORT --backup <updater backup file> [SCENARIO ...]` (`contributor_hil.py`) does every step below in order and adds the result to the contributor's proof: it checks the updater backup, takes a fresh `K` backup into the run folder, writes and reads back only the HIL application at `0x10000`, clears NVS filler and sets Mounting Top, runs `run_hil.py`, and then, even after a failure, writes the stable release's application back at `0x10000`, reads it back and restores the fresh backup with `R`.
The run folder (default `~/launchlab-hil/<time>`) holds the logs and backups and stays outside the repository; the proof keeps their hashes.
It refuses a unit that already runs a HIL image: an earlier run or a release gate did not finish, and a backup taken then would hold that run's test data, not the owner's (7C:E8, 2026-11-09).
Put such a unit back first: the stable application, then the backup from before that run.

## Flash (application only)

Take a portable backup first (`K`, or `Device.backup()`), keep it and any full-flash rollback read outside the repository, then write only the application at `0x10000`.
Never erase NVS or write the bootloader, partition table or OTA data.
Every tool here (`hil_device.Device`, `upgrade_check.py`, and esptool run through `port_guard.py`) checks `lsof` immediately before opening the port and refuses if another process holds it.

```bash
python firmware/tools/hil/flash.py --esptool esptool --port /dev/cu.usbmodem1101 firmware/build-m5-hil/LaunchLabMini.ino.bin
```

`flash.py` writes the application with the header kept as built, then runs verify-flash: write-flash's on-chip MD5 and verify-flash's digest each prove every byte, so a HIL install needs no readback.
`--readback` also reads the region back and compares every byte; the release gate uses it for the production image it leaves on a rig, which is the owner's own unit.
Find a unit's port from its USB serial (its MAC) with pyserial's `list_ports`, never from a port name or ioreg: ports move between units.

The rig adds synthetic pulls to the device's real history (128 records at most) and changes settings.
Restore the portable backup after testing (`R`, or `Device.restore()`), once the production application is reinstalled.

## Run

Needs Python 3 with pyserial.
Use a results directory outside the repository; its serial log contains the device's history.

```bash
python firmware/tools/hil/run_hil.py --port /dev/cu.usbmodem1101 --out ~/hil-runs/$(date +%Y%m%d-%H%M) all
python firmware/tools/hil/run_hil.py --port /dev/cu.usbmodem1101 --out ~/hil-runs/soak soak --soak-minutes 30
python firmware/tools/hil/run_hil.py --port /dev/cu.usbmodem1101 --out ~/hil-runs/corpus corpus --corpus <replay-corpus>
```

`all` runs the scenarios in `run_hil.py`'s `DEFAULT` list, about 61 minutes by `gate_plan.DURATIONS` (53 without `effect_times`, which the release gate's soak stands for when it runs).
The soaks, `corpus`, the variant benches (`effect_bench`, `effect_times_low`, `effect_stages`, which needs a profile build), `loaded_effects` and `sound_sync` run by name.
Each pull checks the RPM against the script, the history entry, the motion quality and saturation flag, the recap, and that the captured motion is the pull's own (its peak gyro matches the scripted motion, so a capture cut from the wrong time fails even when it reports `valid`).

The runner refuses to send `!` commands unless the device reports a `-hil` runtime: on production firmware `!hil` would be read as the single-letter commands `h` and `i`.
Before the first scenario it puts back what a stopped run or the owner's own settings leave on the unit (`scenarios.preflight`): `!reset`, nvs_tight's filler cleared (`!nvsfill 0`; the run stops if any stays) and Mounting Top, and records what it found as `preflight` in `run.json`.
Each scenario ends with a reboot check (same `HIL_BOOT` id, no USB disconnect).
Results go to `results.json` and `summary.md`; screenshots from `v` are saved as PNG.
`run.json` records the device runtime, the rig's source commit and the flashed build's `build-info.json` (written by the build gate: source commit, tree fingerprint, host suite status, image SHA-256).
A release needs a clean `all` run of its exact commit: `scripts/check-release-gate.py` checks `results.json` and `run.json` against the release and writes the manifest's `release_gate` record.
`release_gate.py` runs the whole gate, `all` split over one or two rigs with `--fail-fast` (stop at the first scenario that does not pass; SIGTERM also stops a run cleanly, recovering the device and writing its records), and leaves every rig on the production image; see docs/FIRMWARE_RELEASE_RUNBOOK.md, step 3.
A scenario must not depend on what ran before it in the same boot: a check decided on a figure counted from boot (`SINCE_BOOT` in scenarios.py) reboots first, takes a `counter()` difference, or is listed in `SINCE_BOOT_OK` with its guard (`firmware/tests/check_hil_since_boot.py`).

| Scenario | What it exercises |
| --- | --- |
| `normal` | 3k-12k RPM pulls with moderate tilt: RPM within 3%, history, valid tilt replay, recap |
| `hard` | 8-22 g ripcord stops, gyro to 3500 dps: valid, saturated flag ("Tilt approx."), never out of range |
| `weak` | weak/noisy marks, below-floor marks, noise-only bursts, dropouts, low-rail sensor fault and recovery |
| `back_to_back` | pulls 1-15 s apart, with the 10 s coalesced history save between them |
| `rearm` | a second pull 0.8-3 s after the first one's marks stop, with and without a settings Apply between |
| `history_save_pull` | a pull starting from inside a slowed (400 ms) history write, -150 to +600 ms |
| `long_save_pull` | a history save forced to take 1.5 s with pulls starting -150 to +1200 ms around it: every pull reported, recorded and within 3%, the save ok, no ADC stall (0.18 saves to the history log without stopping the ADC) |
| `adc_stall` | no frames reaching the acquisition owner for 300 ms (`!adcstall`), as from a DMA that stopped with its interrupt held off past the ring (reads find nothing, no overflow event): the silence watchdog restarts it within 250 ms and counts it (`PERF adc_stalls`), the detector returns to READY, the ADC samples at 50 kS/s again (re-timed to the clock it runs at, `PERF adc_retimes`, 0.20.6), and the next 9,000 RPM pull reads within 3% and is recorded |
| `frame_rows` | changed-row display pushes (0.20.6, `frame_rows.h`): the unit tilted between two poses 30 degrees apart four times on the Main page, the level settling at each (60 and 90 degrees there, in the mount's frame); `PERF` counts frames, whole frames, frames with nothing to send and rows sent, some frames send only part of the panel, fewer rows go out than whole frames would send, a whole frame still goes out, and a 9,000 RPM pull afterwards reads within 3%. The panel itself cannot be read back, so its look is a bench check |
| `power_log_pull` | a pull starting from inside a slowed (300 ms) power-log write |
| `imu_timeline` | sensor clock -3% to +3% and back, length-read latency jitter: no re-anchoring, capture aligned |
| `imu_faults` | every FIFO fault at idle, a frozen sensor, a 1.6 s gate hold, clock drift and jitter |
| `imu_faults_pull` | FIFO faults and a gate hold inside a pull |
| `imu_backlog` | the IMU gate held for 60 s, so every IMU poll starts late: late polls restart the schedule and skip missed periods (`MOTION_STATUS late_polls`/`missed_polls`) instead of spinning through them, IDLE0 never misses 1 s (`!idle0`; the task watchdog resets at 5 s), then a pull with valid, aligned tilt replay |
| `settings` | Apply while idle, after a pull, mid-burst, with a held gate, NVS failure, slow write, resting |
| `about_page` | Settings, About (0.20.8) by the buttons: the row after Wipe all opens the page, B does nothing there, A and hold B return to its row, nothing saved |
| `battery_icon` | Battery icon (0.20.8) on the real screen: the Menu's top right corner holds the outline and a fill as wide as the reading, Settings shows it, Main and the Battery page none; by the buttons the row after Mounting previews Off, hold B cancels, A applies and saves; Off survives a reboot |
| `corpus` | recorded waveforms from the private replay corpus (`--corpus`) |
| `soak` | random pulls, faults, settings and gaps for `--soak-minutes` |
| `themes` | custom themes over USB `L`/`T`: upload, refused uploads (contrast, corrupt request, empty slot), select, reboot, delete of the selected theme, a damaged stored record falling back to the default until cleared; clears the device's custom themes |
| `milestones` | Celebrate modes and the milestone table over USB `M`/`U` and Settings: pulls at 8.5k, 9k, 12.3k and 15.5k RPM play the right effect (or none) for the device's own RPM, refused tables, a custom table across a reboot, a damaged record falling back to the defaults until replaced; leaves the defaults and Best effect Orbit |
| `request_burst` | the unit's milestone table sent back as one 312-character v3 request in a single write: received whole and saved. HIL builds use production's USB configuration (`check_usb_config`); 0.18.0's 256-byte production RX queue lost the end of every v3 request while HIL builds, with 8 KB, passed |
| `effect_set` | the effect set over USB `U` op 3 and `M`: upload, the Best effect editor's cycle through the real controls (`!cycle`) from Random and from a saved effect outside the set, Random picks over 16 `E` previews and new bests over six pulls staying in the set (the bests cleared first with `!bests clear`, so every pull after the first is one), a milestone row outside the set, refused sets (empty, Off, Random, an ID past the last, a corrupt request), the set across a reboot, a damaged record (`!esetcorrupt`) offering the default until replaced; leaves the default set and Best effect Orbit |
| `nvs_tight` | NVS at the reserve edge (87 + 16 free entries: a full history, a twelve-row table of every effect and an effect set in both slots, 0.15's table in both of its slots via `!legacytable write`, then `!nvsfill` filler): a first custom theme refused for space, a table rewrite saved and Shake to wake saved Off and back On (0.20.2: settings never wait for the reserve); natural history saves at their normal median (320 ms or less) and within the 400 ms checkpoint budget; pulls -150 to +380 ms around natural history writes reported and recorded; then `history_save_pull` there; leaves no filler, the themes cleared, the default table and set, and 0.15's keys removed |
| `effect_times` | each drawn effect previewed (`E0`-`E4`, `E7`) on a 15.5k recap: `PERF max_effect_us` within 10 ms (Slash fails over it, the others warn), a screenshot mid-effect |
| `effect_times_low` | `effect_times` with Sound at Low (the setting the 9 ms budget is held to), then Sound back to Off |
| `effect_bench` | `!effectbench` on a 15.5k recap: the device cycles per call of each cost-model primitive (table sine, rnd, roundi, floori, q8, smooth, float divide, colour mix, root, addw rejected/blocked/written, float plot, pixel step), then every drawn effect drawn over the saved recap every 100 ms, each frame timed and hashed: equal hashes across two builds mean the same pixels. A profiling build (`BEST_EFFECT_PROFILE`) adds a `HIL_EFFECT_BENCH_STAGES` line per effect that marks stages (`sorcMark`): each frame's cycles per stage |
| `effect_bench_fixed` | `effect_bench` drawn over `Renderer::benchCanvas()` with fixed colours, then held to the host: fails unless every effect's frame hash equals `ui_render --bench-hashes 100`'s (built from the same checkout), so the device draws exactly the frames the tests, the cost model and the web previews use |
| `loaded_store` | The loaded-effect store (`effect_store.h`, USB `F` and `X`), in `all`: the first install formats the effect region, an installed effect plays as E96 within the 9 ms guard, the same uid replaces itself, a full store refuses the 33rd, a power cut at any point of a replace leaves the old or the new effect whole, a deleted effect that a milestone row names celebrates as its fallback, and everything survives a reboot |
| `effect_stages` | a profiling build only (`BEST_EFFECT_PROFILE`, see below): Boundless previewed on a 15.5k recap, then `!effectstages` lists every pass's renderer cycles per stage (setup, glint, dark, eyes, header mask, disc edges, header fill, covered-row fill, band edges, band fill, detail, stars) |
| `sound_sync` | Sound at Low: USB previews of Orbit, Slash, Boundless and Pinwheel from a cold codec and Tidal cutting into its own playing sound; notes each start's `SOUND_SYNC` (present lag, output latency, clock shift) and the codec's start time, then sets Sound back to Off |
| `standby` | Sleep after's 1-minute default and Shake to wake (0.20.2), in `all`, with `!sleepdry 1`: a pull then 60 s of idle saves its history before the sleep with no pull lost; with Shake to wake On the IMU's motion wake is armed (PM1 wake GPIO4, the IMU supply held, `SHAKE_SLEEP wake=imu_motion`); with Off nothing is armed (PM1 wake and hold clear, `wake=side_key mode=power_off`), `POWER_STATUS` reports it and a pull afterwards is measured. From 0.20.6 the dry run also shows the standby leaves the green LED (PM1 `LED_EN`) off and the speaker powered down. A real wake (a shake, the side key) is the owner's battery hand test. Leaves Shake to wake On and Sleep after 1 min |
| `sleep_blockers` | Four ways a unit could stay awake until its battery was flat (three from 0.20.6, one from 0.20.7's Mic RPM page), in `all`, each through the inactivity sleep's dry run (`!sleepdry 1`) at Sleep after's 1-minute default, with the sleep limits ten times faster (`!sleepscale 10`) only while cases 1 and 3 wait for a sleep: about a minute and a half for those (9 minutes in 0.20.6's gate at the real limits). `standby` keeps the real 1-minute timeout, so one sleep in every gate runs at the real limits. 1: a pull, then jostling from `!const` (20 dps and 0.03 g of noise, a unit in a pocket or bag): movement extends the sleep, but it runs within 5 minutes of the last use, 30 s at the scale (through 0.20.5 it never ran). 2: once the history log holds a pull, `!nvs fail 1000 history` refuses the shutdown's NVS copy, as a full NVS does: the sleep still runs with `POWER_OFF_NOTICE nvs_copy_failed` (through 0.20.5 `POWER_OFF_DEFERRED history_save_failed` every minute). 3: steady 7,200 RPM marks hold the detector in MEASURING: after 30 s (3 s at the scale) they stop counting as a pull, two sleeps are refused a minute (6 s) apart (`POWER_OFF_DEFERRED optical_busy`) and the third stops sampling anyway (`POWER_OFF ... forced=1`); a pull afterwards is measured. 4: with the Mic RPM page open and its task held before it takes the mic down (`!micstall 1`), the sleep leaves the page, asks the mic to stop and runs anyway the real 30 s later (`MicSession::STOP_LIMIT_MS` is not a `sleepLimit()`) with `POWER_OFF_NOTICE mic_stop_timeout`, and the mic stops once its task goes on. Leaves Shake to wake On, Sleep after 1 min and no dry run |
| `usb_standby` | The USB-retained standby (0.20.6), by name, not in `all` (the unit leaves USB for about 20 s): on USB the PM1 power-off cannot cut the ESP32's rail, so the unit deep-sleeps; `!usbstandby 20` takes the side key's real power-off path with a 20 s timer re-check that boots instead of sleeping again. Checks the power-off saved, the unit left USB and came back by itself by the timer (`esp_cause=4`) still in the side-key standby, the re-check read PM1 before the boot (5VIN present) and the 5VIN-removal IRQ stayed unmasked through the power-off, and a pull afterwards is measured. The unplug and the re-sleep are a bench check (docs/battery/README.md). A USB link that stays silent after the wake needs a cable replug, as after a restart |
| `songs` | 0.20 songs (engine level 2), in `all`. The three example songs installed over USB and previewed at Sound Low, with nothing of the rig's own running: each streamed to its end with no underrun and every block played, its channel never empty for 40 ms (`max_dry_ms`, 0.20.8; M5's speaker writes silence after 48 ms), the music task's share of core 0, IDLE0's longest gap and its stack (at least 1 KB free), and HEAP_STATUS before, mid-song and after (a song takes the speaker's 8.4 KB and, on the first added-effect play, 0.19's 4.5 KB hot block). Then the worst case (Boundless 0.17's frames, the heaviest the M5 accepts, with a 6 s song of the five dearest voice kinds at the render budget, 20% of core 0 by the cost model refitted in 0.21; a song is as long as a celebration from 0.20.1), the heap sampled every second (`!heap`, not a full status, which prints from the loop that feeds the speaker): no underrun, IDLE0 running, and the music task's share 1-5 s into each play within the budget plus a point of noise (21%), with a warning more than a point from the model over the same window. `song_cost.py` measures the model's songs on a HIL build and `song_cost_fit.py` fits the model to them (docs/FIRMWARE_SOUND_0_20.md, "Render budget"). Then the same song after 1 s of frames (a history save waits while a celebration is shown), back to back until the save a pull just before leaves pending lands inside a play: no underrun in any play, and the save succeeds. The heap holds `hil-overhead.json`'s floor (32 KB less the HIL image's measured extra internal RAM) mid-song, through the worst case and at its low-water mark. Only then the ADC on the real input (`!reset`) against 2 s of silence (`!sndwatch`, which takes 3.5 KB of heap itself); its checks count only with `run_hil.py --sensor fitted` and are listed as NOT COVERED otherwise. Then the generator holds a noiseless idle input while each song plays: no detector edge and no launch. While a song plays the reported start floor is half the run's own marks (400 at most, 0.20.1), a pull during the song is measured as ever and fades it, and with no sound the floor is off; a new best (the bests cleared first, so the second pull is one) with a song as Best effect plays it whole. Leaves no added effects, Sound Off, Best effect Orbit and Celebrate Both |
| `wear_check` | Wear check, in `all`, through the real buttons (USB `h`/`a`/`b`): five B presses reach Menu row 5 and A opens it. Each scripted pull is pulled out (a measured burst that stops dead), held still, and let go (0.20.4): its `WEAR_CHECK_PULL` lines must be `extension` with the pull out's RPM (never counted) and then the judged retraction within 3% of the retraction's RPM, never the pull out's. Three pulls level, three with the ripcord down and three up, with the unit held still at each tilt (`!const`) until its level reads it; a pull 30 degrees off level (`rejected_level`) and one flipped the wrong way (`rejected_flip`) are rejected and retried; none reaches history, bests, motion, a celebration or a recap; `WEAR_CHECK_RESULT` has the nine retractions and each stage's average. Screenshots show HOLD after the pull out, LET GO after a 3 s hold, and WAIT inside the rearm second after a retraction. Then A starts again: 1,200 and 25,000 RPM retractions are measured; a release 400 ms after the pull out is `early_release` (the detector takes it as the pull's rewind: no result, nothing counted); an 800 RPM retraction under the launch floor is `missed`; a slow 600 RPM pull out the detector does not measure, held, then let go is measured; hold B leaves for Menu row 5 and Main, and the next pull is recorded in history. Last, Mounting Sidecar: the unit on its side (board +X up) is level and accepted, the Top level pose is 90 degrees off and refused, a pull-down is accepted, and Mounting goes back to Top |
| `mic_page` | The Mic RPM page (0.20.7), in `all`, from a fresh boot (its `MIC_STATUS` maxima and the heap's low-water count from boot): the mic is off until the page opens, comes up and down without holding the IMU bus, keeps the DMA heap floor, gives the speaker back by every way out, and a reading's real cost at 80 MHz with the page open. The sound is synthetic (`!mictone`, `hil_mic.h`, replacing each captured chunk), so it proves timing, memory and paths, not what the mic hears; the speaker is exercised for real |
| `soak_milestones` | the soak with Celebrate Both and the default table, pulls up to 16k RPM, every pull's celebration checked |
| `stray_bytes` | random USB bytes with runs of the dangerous letters inside bursts: no power-off, sleep, setting, session, page or export action (`usb_command_guard.h`); last in the default run |

`upgrade_check.py` is a separate run for milestone storage on a unit with a full history: it installs frozen 0.15.0 over the HIL build, then 0.18, saves tables of every kind, and with `--rollback` goes back to frozen 0.15.0 and 0.17.0 (and HIL builds of their sources for scripted pulls), checking that history and table saves still succeed there and that 0.18 loads the right table after each.

`loaded_rollback.py` is the same kind of run for added effects (0.19): it adds two effects and records naming them on the HIL build, rolls back to frozen 0.18.0 and 0.15.0, checks that each shows its view of the records (0.18: each added effect replaced by its fallback), refuses the format 7 backup whole and saves its own table, and that 0.19 then finds both effects untouched, loads the newest table and restores the format 7 backup.

`songs_rollback.py` does the same for songs (0.20): it adds a song (Victory Lap) and a level-1 effect on the HIL build and makes the song the Best effect, rolls back to frozen 0.19.0, checks that 0.19 runs, lists the song as needing a newer engine (stored whole, not damaged) and the other effect as before, plays the song's fallback or refuses its preview without restarting, and that 0.20 then lists both as before and streams the song to its end.
Slots are matched by uid, because 0.19 lists a slot that needs a newer engine without its name.
Its docstring has the steps and arguments.
`nvs_sweep.py` measures history-save latency and pull capture against free NVS entries (`!nvsfill` filler at each level); its results set `NvsBudget::RESERVE_ENTRIES`.

## Serial protocol

Lines start with `!` and end with a newline; replies start with `HIL_`.

| Command | Effect |
| --- | --- |
| `!hil` | `HIL_STATUS`, `HIL_IMU`, `HIL_FAULTS` |
| `!reset` | real optical and IMU, faults cleared, pull cancelled |
| `!opt pass\|idle\|buf\|gen` | optical source |
| `!idle <baseline> <noise>` | idle level and noise (counts) |
| `!gen clear`, `!gen duty <f>`, `!gen key <ms> <rpm> <depth> <noise>` | mark generator keyframes (linear between keys, one mark per turn) |
| `!up opt <samples> <fnv1a>` | upload u16le samples; then 4 KiB chunks, each answered by `HIL_ACK <bytes>` |
| `!sync` | `HIL_SYNC reason=idle\|sync\|quiet discarded=<n>`: after any failed upload the device swallows all input until this line or 1 s of quiet, so chunk data never reaches a command parser; the host stops streaming at the first error, stale ready or missing ack and sends it |
| `!imu pass\|const\|prof` | IMU source |
| `!const ax ay az gx gy gz [noise_g noise_dps]` | constant board-axis vector (g, dps) |
| `!up imu <frames> <fnv1a>` | upload 200 Hz frames, 6 x int16 (mg, 0.1 dps) |
| `!offset <ms>` | IMU profile start relative to the pull |
| `!pull [lead_ms]` | start optical playback and the IMU profile together |
| `!fault lenfail\|datafail\|corrupt\|garble\|overflow\|oversize <n>` | next n transfers |
| `!fault stall <ms> <n>`, `!fault skip <frames>` | stalled data reads; one skip frame |
| `!fault drift <ppm>`, `!fault sensorjitter <us>`, `!fault jitter <us>`, `!fault freeze 0\|1`, `!fault clear` | continuous faults |
| `!nvs fail <n> [key]`, `!nvs slow <ms> <n> [key]`, `!nvs clear` | next n matching NVS writes (key prefix) |
| `!arm <key> <delay_ms>` | start the programmed pull from inside the next NVS write of `key`, `delay_ms` later (the loop task that reads `!pull` is busy saving) |
| `!gate <ms>` | hold the IMU/I2C gate from another task |
| `!idle0 [reset]` | `HIL_IDLE0 max_gap_us=`: the longest time IDLE0 did not run on core 0 (an idle hook), since boot or the last reset |
| `!tasks` | one `HIL_TASK name= core= priority= run_us= stack_free=` per task, then `HIL_TASKS`: per-task CPU time, for a core's load as the difference of two reports |
| `!sndbench` | the 0.20 sound study's mixing kernels (`hil_sound_kernels.h`) over 16,000 samples at 80, 160 and 240 MHz: one `HIL_SND kernel= mhz= data= us_per_sample= cycles_per_sample=` per kernel and clock, then `HIL_SND_END` (docs/FIRMWARE_SOUND_0_20.md) |
| `!sndplay <demo> <level>` | render a 4 s demo (0 chiptune, 1 sample voices, 2 SoundFont-class voices and reverb), play it at Low (1) or High (2) and report the ADC's range and the detector's edges and launches during playback against 2 s of silence: `HIL_SNDPLAY ...` |
| `!sndwatch <ms>` | from its own task (the UI loop keeps feeding a streamed song): the ADC's range and the detector's edges and launches over the next 100-30000 ms, then `HIL_SNDWATCH ms= min= max= edges= launches=` |
| `!set <row> <value>` | Apply a setting via `commitSetting()`; `HIL_SET ... error= ms=` |
| `!home`, `!usbidle 0\|1`, `!powerlog` | main page; sensor idle with USB; run the power-log checkpoint |
| `!sleepok 0\|1` | allow the inactivity standby (off after boot and `!reset`): a HIL build never sleeps on its own, so a rig whose USB link drops after a reboot stays reachable; `reboot()` reopens the port when no `HIL_BOOT` arrives within 5 s |
| `!sleepscale <n>` | every sleep limit divided by n, 1 to 60 (`sleepLimit()` in power_status.h: Sleep after, the unused shake wake's 30 s and 2 minutes, the 5-minute movement limit, the 30 s measured-pull and held-button limits, the 60 s retry after a deferred sleep), so a scenario waits seconds for a sleep; 1 from boot and after `!reset`, and each scenario's end checks it is 1 again (`HIL_STATUS sleep_scale`). Production has no divisor (`check_sleep_scale.py`) |
| `!sleepdry 0\|1` | the inactivity sleep's dry run (off after `!reset`): with `!sleepok 1` the sleep runs its whole path with the rig's USB host attached (the history flush, the wake markers, the IMU arm or not) and stops where PM1 would power off, printing `SLEEP_DRY_RUN` with PM1's wake and hold registers and the IMU's power control, then restores; the unit never sleeps |
| `!usbstandby <s>`, `!standbywake` | a real USB-retained standby by the side key's power-off path whose timer re-check after s seconds (5-600) boots rather than sleeping again; the boot's `USB_STANDBY_WAKE` fields (wake cause, standby receipt, the re-check's PM1 power source and system IRQ mask) |
| `!reboot`, `!themecorrupt`, `!milecorrupt`, `!esetcorrupt` | restart through the normal boot path; overwrite the stored custom themes record, both milestone slots or both effect set slots with junk (inside a checkpoint) |
| `!cycle <presses>` | open Settings › Best effect through the real controls, press B `presses` times, cancel; `HIL_CYCLE start=<id> values=<id>,...` (nothing saves) |

Every NVS write prints `HIL_NVS key=...`, and every injected NVS fault prints `HIL_FAULT ...`, so saves can be correlated with pulls.

`HIL_STATUS` ends with `commands=<n> last=<word> tx_short=<n> sleep_scale=<n>`: commands completed, the last one's first word, replies the USB TX path cut short (its 10 ms timeout) and the `!sleepscale` divisor.
When a reply is missing, the host reads `HIL_STATUS`, logs `HOST_RETRY !<command> reason=reply_lost|command_lost ...` and re-sends a state-setting command once (including `!reset` and `!set`, whose repeat with the same value changes and saves nothing).
Replies are pushed out with `UsbTx::drain()`, never `Serial.flush()`: the core's flush deletes its whole TX ring after a 10 ms host stall, which lost a `!reset` reply before 0.13.9.

## Limits

- Replacing the optical values costs about 9% of core 0 (processing about 26% instead of 17%); the IMU model costs a few microseconds per 5 ms poll.
- Generator keyframes are evaluated once per 5.12 ms DMA frame, so a dropout or step is placed to that resolution.

- Values are injected after the ADC and after the I2C transfer; analog front-end and bus electrical faults are not modelled (real bus errors still occur and are reported).
- A slow NVS write is a delay inside the write, inside its checkpoint; it does not stall the cache of both cores as a real flash erase does.
- Motion profiles are integrated from angular rates; they test capture, fusion and saturation handling, not the accuracy of real launcher motion.

## Profiling an effect on the device

A debug image that times each stage of a celebration with the cycle counter, for finding where an effect's frame time goes (never ship it):

```bash
wrap="-Wl,--wrap=nvs_set_blob -Wl,--wrap=nvs_set_u8 -Wl,--wrap=nvs_set_u32"
LAUNCHLAB_SKIP_TESTS=1 LAUNCHLAB_PM_BUILD_DIR="$PWD/firmware/build-m5-prof" LAUNCHLAB_EXTRA_CPP_FLAGS="-DLAUNCHLAB_HIL=1 -DBEST_EFFECT_PROFILE=1" LAUNCHLAB_EXTRA_LINK_FLAGS="$wrap" bash firmware/build-m5-pm.sh
```

Then run the `effect_stages` scenario. Stages are marked in the renderer with `sorcMark(stage)` (best_effect_ui.h), which compiles to nothing in every other build; add marks to a new effect the same way.

## Songs on a production image

`build-m5-hil.sh` also builds the production image of the same tree (unless it already has) and writes `hil-overhead.json` beside the HIL image (`firmware/tools/hil_overhead.py`): the HIL image's extra internal RAM.
`run_hil.py` loads it for the flashed build, and the `songs` scenario holds the heap to 32 KB less it, so a HIL run means what the production check means; a production image is held to the full 32 KB, by this check and by the release gate from 0.20.

0.18.0's production image kept the core's 256-byte USB receive buffer while HIL images had 8 KB, so a request written in one piece past 256 bytes was lost only on a unit; 0.18.1 gives every build 8 KB, and this still checks the production image end to end.
`production_songs.py` checks that song effects, the largest kind, still install whole through the paced `X` path (96-character lines, each after the last one's `FX_CHUNK`, as the effects page writes them) on a production image (`LAUNCHLAB_PM_SDK=... bash firmware/build-m5-pm.sh`):

```bash
python3 firmware/tools/hil/production_songs.py --port /dev/cu.usbmodemNNN --out /tmp/production-songs
```

It refuses a HIL image and a unit that already holds added effects (unless `--replace-added`).
It installs the three example songs with their packed source (the same request bytes as the page), reads each back with `F` and `X source`, previews each with `E<id>` and checks `MUSIC_STATUS` for a full play without an underrun, notes `HEAP_STATUS` before, mid-song and after, checks it holds the 32 KB floor mid-song and after and is back within 1 KB, plays the same worst-case 6 s song effect with the heap sampled every second, then removes them.
Playback needs Sound not Off and a pull on the unit to preview on; without them it is listed as NOT COVERED, never as a pass.
