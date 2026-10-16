# FIFO capture timing fixes — 2026-10-16

These are confirmed code defects with deterministic host reproduction. Their
frequency and user-visible consequences on the physical M5 have not been
reproduced by this work. No device was flashed or reset and no settings or
recordings were changed.

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

`quiet()` now waits for the IMU gate without the normal 20 ms UI timeout, then
publishes its capped quiet window while holding that gate. It returns only after
the in-flight gated FIFO read finishes. Starting the window after acquisition
also prevents a short requested window from expiring during the wait. Ordinary
`lock()` calls retain their 20 ms timeout. A permanently stuck owner can block a
checkpoint; this deliberately does not permit flash writes without quiescence.

`readFifo()` now returns the host time immediately before its FIFO length
request. The returned gyro batch and held accelerometer timestamps use this
anchor. The completion clock still measures poll/transfer duration and detects
stalls. Sensor-frame spacing, drift tracking, overflow handling and the release
version remain unchanged.

The new `test_motion_acquisition_timing.cpp` compiles the actual acquisition
header, FIFO parser, timeline and fusion. Only RTOS, clock and I2C are simulated.
It uses virtual gate release times and transfer delays, without host sleeps.
The identical test against the baseline headers fails in both targeted modes:

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
a 90 ms gate wait, the 10 s missing-end cap, 0/23/58 ms bursts, a valid 1980-byte/90 ms burst, subsequent batches,
accelerometer-only data and length/data/stall failures. Existing mount and
CPU-power fixtures were adapted to the new RTOS constant and read signature.

Run the host suite with `bash scripts/test-firmware.sh`; it includes this test
with AddressSanitizer and UndefinedBehaviorSanitizer. To compare baseline,
compile this test with `motion_acquisition.h` and `bmi270_fifo.h` extracted from
`e066fb6` earlier in the include path; invoke the binary with `quiet` and
`timestamp` separately. Both return 1 on baseline and 0 on the fix.

## Verification and review

- Focused timing regressions: pass with ASan/UBSan.
- Full firmware sanitizer/pipeline suite: pass, 43 sanitizer binaries and 16 pipeline scripts (`bash scripts/test-firmware.sh`).
- Updater/core guard tests: 78 pass (`cd site && npm test`).
- Redirect build: pass (`cd site && npm run build`); local build only.
- ESP32-S3 PM compile: pass, Arduino CLI 1.3.1, esp32 core 3.3.0,
  M5Unified 0.2.23 and M5GFX 0.2.30, installed rebuilt IDF 5.5 `b66b5448` PM SDK (all 4002 manifest hashes verified).
  Options: CDC on boot, hardware USB CDC, 8 MB flash, `default_8MB`, OPI PSRAM;
  flags `VQF_SINGLE_PRECISION`, `-fstack-usage`, `LAUNCHLAB_MOTION_ADC_GATE=1`,
  `LAUNCHLAB_CPU_PM=1`.
- Compile size: 838599 flash bytes, 126916 global RAM bytes. Stack checker:
  `loop()` 816 bytes against 1536 budget; other frames within budget.
- Local candidate binary SHA-256:
  `fa5bd8e15a19b4d0583f280d3ef19b95c59a70489ac4426e23cb21ba860790d0`.
  This is an uninstalled development build, not a new release or recommendation.
- Independent reviewer found no blocking defects, independently ran the timing
  regressions and both updated pipeline fixtures with sanitizers, and checked
  callers for recursive gate acquisition.

`python3 scripts/verify-artifacts.py` intentionally fails at its working-source
comparison: candidate source differs from frozen 0.13.7. This is a strict release
gate, not a checksum failure. It is left intact; the PR workflow will also stop
there. Separately, the unchanged verifier passed 1296 checksum checks in a
temporary validation view using frozen 0.13.7 source and the preserved release,
site and hardware artifacts. The direct candidate run had already checked 3MF
archive integrity before reaching the source assertion. No published source,
binary, catalog, version, workflow or recommendation was edited.

The review also noted a preexisting limitation outside these two findings:
parse-error recovery calls `discardFifo()` / `FLUSH` after releasing the gate.
This patch guarantees gated FIFO-read quiescence, not exclusion of every IMU
bus operation. The virtual RTOS tests do not prove FreeRTOS scheduling behavior.

## Remaining physical validation

The attached Mac exposed only `/dev/cu.Bluetooth-Incoming-Port` and
`/dev/cu.debug-console`; USB inventory returned no entries. The existing M5 test
device was not available for a non-destructive session. The following remains:

1. Reconnect the dedicated M5StickS3 K150 and confirm no active device owner.
   Start with read-only `p` status and inspect installed firmware identity,
   FIFO diagnostics, settings and recording/reference inventory. Preserve a
   verified backup before any authorized firmware change; keep it private.
2. On existing firmware, the explicit `Q` command requests a 300 ms quiet period
   with `flash_write=0`. With device-owner agreement, collect before/after `p`
   diagnostics to observe catch-up burst sizes, stalls/parse errors and gaps.
   This alone does not test the candidate fix or reproduce checkpoint overlap.
3. Candidate installation needs separate explicit flash authorization and a
   preservation/restore plan. No erase, downgrade, restore, new session, history
   wipe, stress write, automatic update or test recording is authorized here.
4. After authorized installation, instrument FIFO read start/end, quiet return,
   length-capture time and actual checkpoint write boundaries. Accumulate a
   safe backlog and verify checkpoint writes begin after any in-flight read,
   no gated read starts in the window, reads resume, and returned sample times
   exclude burst duration. Check normal and long reads plus failed checkpoints.
   Use a dedicated backed-up test device for flash/checkpoint stress; never
   exhaust its flash or alter personal practice history for reproduction.
5. Correlate a controlled optical/motion event with the FIFO timeline across
   normal and backlog reads, then check tilt quality, gaps, capture pre/post-roll
   and both runtime sensor profiles. Code and host tests establish the defects;
   only these device observations can establish real-world frequency and impact.

No release, deployment, merge, rollback or monitor-calibration change is part
of this fix.
