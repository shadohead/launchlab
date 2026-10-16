# FIFO capture timing fixes — 2026-10-16

Both defects have deterministic host reproduction. Read-only probes on the
attached physical StickS3 also reproduced the early-return race and measured the
burst-dependent difference in timestamp anchoring. No unsafe flash/read overlap,
corruption, or optical-launch impact was induced. The clean fix is installed as
a local development build; this does not change any release recommendation.

## Source and scope

Revalidated public `main` at `e066fb6f997ce88b49862a31095d7f74cb256996` and
latest release `firmware-v0.13.7`. Both still contain the reported paths:

- [Checkpoint timeout finding](https://github.com/shadohead/launchlab/pull/12#discussion_r4297834931).
- [FIFO timestamp finding](https://github.com/shadohead/launchlab/pull/12#discussion_r4297834944).

There were no open public LaunchLab PRs when work began. The existing private
checkout had no tracked edits but held many untracked artifacts; it was left
untouched. The latest firmware owner's coordination record was stopped at
18:15 UTC. This patch uses an isolated checkout and branch. An older hardware
session still had a running coordination flag; its checkout and work were not
changed. Beywatch PR 168 is now merged at head `d40bf44c`; no site or recommended
version was changed here.

## Changes and regression evidence

`quiet()` now makes one 250 ms attempt to acquire the IMU gate, allowing more
than the ~90 ms maximum accepted FIFO transfer. On success, it publishes its
capped quiet window while holding the gate, releases the gate, and returns
true. Starting the window after acquisition prevents a short requested window
from expiring during the wait. Ordinary `lock()` calls retain their 20 ms
timeout. On failure, quiet markers/window remain unchanged and it returns false.

The ADC request hook now supports a preflight veto. A failed barrier returns
false before `send()`, command enqueue, request-ID increment or ADC state change;
there is no completion callback on veto. Every flash-capable begin caller checks
the request result before writing. If the barrier succeeds but sending/receiving
fails, the completion callback still ends the newly established quiet window.
The USB `Q` diagnostic reports whether it obtained the barrier. Optional power-log
attempts now record their retry time before preflight, so a busy IMU cannot
repeat the 250 ms wait on every UI tick; the dirty log stays pending.

### Reviewed lock contract and liveness

- No current quiet/checkpoint caller holds the IMU gate or ring gate. Shared
  I2C/power/speaker callers release the IMU gate before requesting checkpoints.
- No reverse lock ordering or recursive acquisition path was found. Acquisition
  releases the IMU gate before fusion takes the ring gate. The UI releases it
  before enqueueing a command or waiting for an ADC reply.
- There is no retry loop or unbounded wait in the barrier. A held/unavailable
  gate, including an accidental recursive call, gets one 250 ms RTOS wait and
  rejects the checkpoint. Scheduler starvation can still extend wall time;
  this is a semaphore wait budget, not a hard real-time deadline.
- A permanently unavailable owner cannot be promised eventual flash saving.
  Saves safely remain pending/deferred, while each preflight returns failure.
  No bus reset, device restart or forced write is used to manufacture progress.
- The prior draft head `bab9306` used `portMAX_DELAY`. A new deterministic
  regression against that head waits 60000000 us for a simulated 60-second
  gate hold and accepts the checkpoint. The corrected version waits 250000 us
  and rejects it. Unavailable-gate and existing-window preservation cases pass.

`readFifo()` now returns the host time immediately before its FIFO length
request. The returned gyro batch and held accelerometer timestamps use this
anchor. The completion clock still measures poll/transfer duration and detects
stalls. Sensor-frame spacing, drift tracking, overflow handling and the release
version remain unchanged.

The new `test_motion_acquisition_timing.cpp` compiles the actual acquisition
header, FIFO parser, timeline and fusion. Only RTOS, clock and I2C are simulated.
It uses virtual gate release times and transfer delays, without host sleeps.
The test against the baseline headers fails in both targeted modes:

```text
Baseline quiet: read holds gate 90000 us, checkpoint starts after 20000 us
FAIL: quiet returned while a FIFO read still owns the bus
Baseline timestamp: 48000 us burst, length captured at 1000100, newest sample 1048100
FAIL: FIFO samples were shifted by burst transfer time

Fixed quiet: read holds gate 90000 us, checkpoint starts after 90000 us
Fixed quiet: read holds gate 120000 us, checkpoint starts after 120000 us
Fixed timestamp: 48000 us burst, length captured at 1000100, newest sample 1000000
PASS: acquisition timing regressions
```

A 100 us simulated length transaction explains the difference between the
fixed timestamp and length completion. Other cases cover immediate/19 ms gate
availability, no task/gate, checkpoint suppression/resume, a 10 ms window after
a 90 ms gate wait, the 10 s missing-end cap, 0/23/58 ms bursts, a valid
1980-byte/90 ms burst, subsequent batches, accelerometer-only data and
length/data/stall failures. Existing mount, CPU-power and wipe fixtures were
adapted to the RTOS constant, read signature and barrier return type. A separate
extracted-production test executes the actual request/send/hook functions to
prove veto side effects, all four flash-capable begin actions, successful
completion, send-failure cleanup and ordinary/no-hook requests. The optional
power-log fixture also verifies busy-veto backoff and eventual save after the
gate becomes available.

Run the host suite with `bash scripts/test-firmware.sh`; it includes this test
with AddressSanitizer and UndefinedBehaviorSanitizer. To compare baseline,
compile this test with `motion_acquisition.h` and `bmi270_fifo.h` extracted from
`e066fb6` earlier in the include path; invoke the binary with `quiet` and
`timestamp` separately. Both return 1 on baseline and 0 on the fix.

## Verification and review

- Focused timing regressions: pass with ASan/UBSan.
- Full firmware sanitizer/pipeline suite: pass, 43 sanitizer binaries and 17 pipeline scripts (`bash scripts/test-firmware.sh`).
- Updater/core guard tests: 78 pass (`cd site && npm test`).
- Redirect build: pass (`cd site && npm run build`); local build only.
- ESP32-S3 PM compile: pass, Arduino CLI 1.3.1, esp32 core 3.3.0,
  M5Unified 0.2.23 and M5GFX 0.2.30, installed rebuilt IDF 5.5 `b66b5448` PM SDK (all 4002 manifest hashes verified).
  Options: CDC on boot, hardware USB CDC, 8 MB flash, `default_8MB`, OPI PSRAM;
  flags `VQF_SINGLE_PRECISION`, `-fstack-usage`, `LAUNCHLAB_MOTION_ADC_GATE=1`,
  `LAUNCHLAB_CPU_PM=1`.
- Compile size: 838603 flash bytes, 126916 global RAM bytes. Stack checker:
  `loop()` 816 bytes against 1536 budget; other frames within budget.
- Local candidate binary SHA-256:
  `3ccbd4cb84cc07c60eb21ad71053bad71769b90476c1d2d0af720628513f2c2a`.
  This is the clean development build installed for this validation, not a new release or recommendation.
- Independent reviewer found no blocking defects, independently ran the timing
  regressions, bounded wait/veto, busy retry and updated fixtures with sanitizers, and checked all checkpoint callers and lock ordering.

`python3 scripts/verify-artifacts.py` intentionally fails at its working-source
comparison: candidate source differs from frozen 0.13.7. This is a strict release
gate, not a checksum failure. It is left intact; the PR workflow will also stop
there. The original [CI run on `bab9306`](https://github.com/shadohead/launchlab/actions/runs/38954515529) completed with updater and firmware tests passing, artifact verification failing at line 74 on `motion_acquisition.h`, later build/upload steps skipped and deploy skipped. The [bounded-wait head `adbd6c7` CI run](https://github.com/shadohead/launchlab/actions/runs/38956207045) also completed: updater tests and the full firmware suite passed; artifact verification failed at line 74 on `LaunchLabMini.ino`; later build/upload and deploy were skipped. Separately, the unchanged verifier passed 1296 checksum checks in a
temporary validation view using frozen 0.13.7 source and the preserved release,
site and hardware artifacts. The direct candidate run had already checked 3MF
archive integrity before reaching the source assertion. No published source,
binary, catalog, version, workflow or recommendation was edited.

The review also noted a preexisting limitation outside these two findings:
parse-error recovery calls `discardFifo()` / `FLUSH` after releasing the gate.
This patch guarantees gated FIFO-read quiescence, not exclusion of every IMU
bus operation. The host tests use a virtual RTOS; the device probes below separately exercise actual FreeRTOS waits and I2C transfers.

## Physical validation on the attached StickS3

The user attached the M5 at 19:35 UTC and explicitly authorized necessary test
firmware installation while preserving settings and recordings. No serial owner
was active. The original runtime was a private `0.13.6-sticks3-power-key-pm`
build, which predates this FIFO path; it was not a suitable physical baseline.

Before writing, two identical 3936-byte portable backups passed the production
decoder and exact re-encoding check. A digest-verified 1 MiB raw recovery snapshot
covers every sector affected by the app-only installs, plus boot, partition,
NVS/OTA prefix and a suffix margin. The installed original app also matched its
prior private install receipt. A full 8 MiB read at 460800 baud failed; default
115200 baud reliably read and verified the required recovery span. There was no
write during that failed read. Backups, device identity and personal logs stay
private. No erase-all, bootloader/partition install or restore of user data was
performed.

The clean candidate and a private diagnostic variant were installed at app0
`0x10000` only. Each installation had digest verification of the app and protected
prefix/suffix. The diagnostic variant added temporary serial probes; its test
commands performed no flash write. It used actual sensor backlog, I2C transfers,
FreeRTOS tasks and the fixed production acquisition path. Diagnostic instrumentation
was independently reviewed before use and is not part of the PR or release.

For each trial, a 700 ms quiet window accumulated a real backlog below FIFO
capacity. A helper ran the exact old quiet body during the selected read, recorded
its return, then ran the fixed quiet barrier. No checkpoint write followed the
unsafe old return. All timestamps below are software-delimited measurements,
not logic-analyzer observations.

| Trial | FIFO bytes / gyro frames | Actual burst | Old wait / read still active | Fixed wait / return after burst |
| --- | --- | --- | --- | --- |
| 1 | 1413 / 141 | 48.789 ms | 19.612 ms / yes | 29.189 ms / 72 us |
| 2 | 1413 / 141 | 50.180 ms | 19.970 ms / yes | 30.206 ms / 71 us |
| 3 | 1407 / 141 | 47.926 ms | 19.938 ms / yes | 28.001 ms / 71 us |

All three batches parsed and added all 141 paired samples. The fixed timeline's
newest sample was within 1.821 ms of the pre-length anchor. A counterfactual old
timeline processing those same real batches with completion-time anchors placed
the newest samples 45.628–51.090 ms later than the fixed timeline. Drift smoothing
means that difference need not equal each burst duration exactly. This confirms
the anchor behavior on physical data; it does not establish absolute event-time
accuracy against an external reference.

An intentional self-held gate exercised the actual checkpoint preflight:
`accepted=0`, wait `249749 us`, ADC Ready before/after, not suspended, and 12288
additional ADC samples. This confirms bounded rejection and continued ADC
sampling on this FreeRTOS device. After the three races, FIFO errors, parse
errors, stalls, overflows, anchors and sample gaps were all zero; ADC errors and
overflows were zero. Ordinary sampling resumed.

A separate smoke check captured a real stationary IMU window into a valid
48-point/200 ms trace and displayed it. Playback reported `started=1`, with
presented frame time advancing from 130 to 280 ms; the exported LCD frame showed
the tilt replay. This used a synthetic capture window and synthetic 6000 RPM
metadata, with no optical trigger or stored recording. The prior volatile trace
was restored. It is not an end-to-end launch measurement.

Portable backups after the clean install and again after diagnostics retained
all five user-data sections byte-for-byte: practice, appearance, bests, saved
motion/reference, and settings. Inventory remained 128 records / 24 sessions,
with the same latest recording and reference. GPIO and external-power state
were also unchanged. The final app-only reinstall restored the clean candidate
(SHA-256 above), verified its app/prefix/suffix digests, and booted successfully.
Two final backups were byte-identical to the first clean-build backups and all
five user-data sections still exactly matched the original device. Final status:
ADC Ready at 50007 samples/s, no ADC overflow/read errors, no FIFO errors, stalls,
overflows or sample gaps, no gate misses; demo and feedback off. The temporary
diagnostic executable is no longer installed.

## Remaining hardware limits

- The deliberately constructed backlog proves the physical timing condition;
  natural occurrence rate and user-visible corruption/tilt impact remain unknown.
- No unsafe flash/read collision was induced. Actual flash checkpoint-boundary
  stress, fault injection and flash-wear tests were not performed on personal data.
- A controlled optical launch synchronized to an external reference is still
  needed for absolute timestamp accuracy, real capture pre/post-roll and tilt
  quality under normal and backlog reads. Both runtime sensor profiles need that
  end-to-end coverage; this device was tested on its saved standard profile.
- Power-loss persistence, prolonged load and near-capacity physical FIFO cases
  remain separate validation work. Host regressions cover the near-capacity
  1980-byte/90 ms case and failure paths.

No release, deployment, merge, rollback or monitor-calibration change is part
of this fix.
