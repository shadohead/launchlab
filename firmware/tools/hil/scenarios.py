"""Scenario library. Each scenario takes a Rig and records checks.

Severity: 'fail' marks behaviour the owner's goals rule out (no RPM, a crash,
a hung save, "Motion out of range", a lost valid pull); 'warn' marks
degradations worth a look (e.g. a pull that starts inside the rearm second).
"""
import base64
import random
import re
import subprocess
import time
from pathlib import Path

import fx_client
import milestones
import ble_scenarios
import signals
import themes
from hil_device import DeviceError, Line, number
from rig import GRAVITY

PROFILES = {'standard': 0, 'tcrt5000': 1}


def settle(rig, seconds=2.5):
    """Idle sources, then a fused, settled orientation and a READY detector."""
    rig.idle_sources()
    time.sleep(seconds)
    block = rig.wait_ready(25)
    rig.check('detector ready and fusion settled', block is not None)
    return block


def drain_history(rig, timeout=25):
    """Let the coalesced history save run so the next scenario starts clean."""
    d = rig.dev
    end = time.time() + timeout
    while time.time() < end:
        block = d.status()
        if block.get('HISTORY_STATUS', {}).get('pending') == '0' and block.get('MOTION_STATUS', {}).get('pending') == '0':
            return True
        time.sleep(1)
    return False


def counter(block, tag, key):
    """A status count, for a before/after difference: most counts are since boot (SINCE_BOOT)."""
    return int(number(block.get(tag, {}).get(key), 0))


# Status figures that count from boot: low-water marks, maxima and counters the
# firmware resets only at a restart (a whole line when the tag has no fields:
# every status reprints the recent MOTION_GAP lines). In HIL all they hold what
# every earlier scenario did, so a check decided on one reboots first (reboot()),
# takes a before/after difference (counter()), or is listed in SINCE_BOOT_OK
# with how it guards against an earlier scenario. check_hil_since_boot.py
# enforces it; 0.20.7's gate lost two reruns to such reads.
SINCE_BOOT = {
    'HEAP_STATUS': ('internal_min_free', 'dma_min_free'),  # heap_caps_get_minimum_free_size()
    'STACK': ('loop_min_free_bytes',),  # uxTaskGetStackHighWaterMark()
    'PERF': ('max_batch_us', 'max_read_gap_us', 'invalid_frames', 'lost_events', 'adc_stalls', 'ui_stalls',
             'usb_drain_timeouts', 'usb_rejected_commands', 'adc_retimes'),
    'MOTION_STATUS': ('max_gap_us', 'gaps', 'max_poll_us', 'gate_misses', 'late_polls', 'missed_polls'),
    'IMU_FIFO_STATUS': ('errors', 'skipped_frames', 'anchors', 'max_bytes', 'length_errors', 'data_errors', 'parse_errors',
                        'oversize', 'overflows', 'stalls'),
    'HISTORY_STATUS': ('save_errors', 'max_save_us'),
    'SOUND_STATUS': ('max_slice_us', 'max_begin_us', 'max_end_us', 'lock_misses', 'codec_failures'),
    # mic_listener.h keeps these over every session (only the interval pair resets, at each status).
    'MIC_STATUS': ('samples', 'late_chunks', 'record_misses', 'restarts', 'starts', 'stop_max_ms', 'gate_max_us',
                   'heap_low', 'behind_max_ms', 'overruns', 'late_starts', 'lost_steps', 'reading_us_max'),
    'MOTION_GAP': (),
}
# {scenario: {figure: how the scenario guards it}}: deliberate since-boot reads with no reboot first.
_WORST_SONG_LOW = ("worst_song reads the low-water mark before its plays (earlier) and counts the one after "
                   "them only when an earlier scenario had not already taken it under the floor; otherwise that "
                   "check is recorded as not covered (HIL all), and production_songs.py covers it after a boot")
SINCE_BOOT_OK = {
    'songs': {'HEAP_STATUS dma_min_free': _WORST_SONG_LOW, 'HEAP_STATUS internal_min_free': _WORST_SONG_LOW},
    'sound_sync': {'SOUND_STATUS max_begin_us': 'only noted (each preview\'s begin time in the closing note), never checked'},
}


# --------------------------------------------------------------------------- 1
# Internal DMA-capable heap a unit keeps after init, for 0.19's loaded effects
# (their placed code) and 0.20's music on top of today's speaker ring.
DMA_FLOOR_BYTES = 32 * 1024


def normal_pulls(rig, rpms=(3000, 4500, 6000, 7500, 9000, 10500, 12000)):
    """Normal pulls 3k-12k RPM with moderate tilt: valid RPM and tilt replay. First,
    the internal DMA-capable heap after init: the ADC, the speaker's I2S ring and
    USB allocate from it, and a build once boot-looped when the ADC found none."""
    settle(rig)
    heap = rig.dev.status().get('HEAP_STATUS', {})
    dma_free = int(number(heap.get('dma_free'), 0))
    rig.check(f'internal DMA-capable heap after init at least {DMA_FLOOR_BYTES // 1024} KB', dma_free >= DMA_FLOOR_BYTES,
              f"dma_free={heap.get('dma_free')} dma_largest={heap.get('dma_largest')} internal_free={heap.get('internal_free')}")
    rig.note(f"heap after init: dma_free={heap.get('dma_free')} dma_largest={heap.get('dma_largest')} dma_min_free={heap.get('dma_min_free')} "
             f"internal_free={heap.get('internal_free')} internal_largest={heap.get('internal_largest')}")
    for i, rpm in enumerate(rpms):
        optical = signals.standard_pull(rpm)
        motion = signals.launch_motion(tilt_rate_dps=250 + 30 * i, pull_g=3 + i * 0.4, seed=10 + i)
        r = rig.pull(optical, motion)
        rig.check_pull(r, tolerance=0.03, saturated=False)
        if i == 2:
            rig.screenshot('normal_recap')
        time.sleep(1.5)
    drain_history(rig)


# --------------------------------------------------------------------------- 2
def hard_pulls(rig):
    """8-13 g stops and gyro saturation: valid or Tilt approx., never out of range."""
    settle(rig)
    cases = [
        ('8 g stop, 1500 dps', dict(tilt_rate_dps=1500, pull_g=6, stop_g=8)),
        ('10 g stop, 1900 dps', dict(tilt_rate_dps=1900, pull_g=7, stop_g=10)),
        ('13 g stop, 2200 dps (gyro saturates)', dict(tilt_rate_dps=2200, pull_g=8, stop_g=13)),
        ('13 g stop, 3000 dps (gyro saturates)', dict(tilt_rate_dps=3000, pull_g=9, stop_g=13)),
        ('21 g stop (accel saturates)', dict(tilt_rate_dps=1200, pull_g=8, stop_g=21)),
        ('24 g stop, 3500 dps (both saturate)', dict(tilt_rate_dps=3500, pull_g=10, stop_g=24)),
    ]
    for i, (label, kw) in enumerate(cases):
        rpm = 7000 + 900 * i
        optical = signals.standard_pull(rpm, label=f'{label} @ {rpm}')
        motion = signals.launch_motion(seed=30 + i, **kw)
        r = rig.pull(optical, motion)
        rig.check_pull(r, saturated=motion.expect_saturated)
        if motion.expect_saturated and i == 2:
            rig.screenshot('hard_pull_tilt_approx')
        time.sleep(1.5)
    drain_history(rig)


# --------------------------------------------------------------------------- 3
def weak_noisy_optical(rig):
    """Weak, noisy and interrupted marks: RPM whenever the floor allows, no false pulls."""
    settle(rig)
    d = rig.dev
    before = rig.counters()
    launches = counter(before, 'RPM_STATUS', 'launches')
    # Marks above the 80-count floor with heavy noise: still measured.
    for depth, noise in ((150, 10), (110, 14), (400, 40)):
        r = rig.pull(signals.standard_pull(6000, depth=depth, noise=noise, label=f'depth {depth} noise {noise}'))
        rig.check(f'depth {depth} noise {noise}: rpm reported', r.valid and abs(r.rpm - 6000) / 6000 < 0.05,
                  f'valid={int(r.valid)} rpm={r.rpm:.0f}')
        time.sleep(1.5)
    # Below the floor: no launch, no crash.
    r = rig.pull(signals.standard_pull(6000, depth=40, noise=4, label='depth 40 (below floor)'), expect_launch=False)
    rig.check('below-floor marks: no launch', not r.valid, f'launch={r.launch}')
    time.sleep(1)
    # Pure noise bursts: never a phantom pull.
    pull = signals.OpticalPull([(0, 0, 0, 60), (1500, 0, 0, 60)], label='noise burst 60 counts')
    r = rig.pull(pull, expect_launch=False, timeout=3)
    rig.check('noise-only burst: no phantom pull', r.launch is None or not r.valid, f'launch={r.launch}')
    # Dropouts inside the pull: RPM still reported (honest end if the gap is long).
    for gaps in ([(250, 20)], [(200, 15), (450, 15)], [(300, 120)]):
        r = rig.pull(signals.dropout_pull(8000, gaps))
        rig.check(f'{r.label}: rpm reported', r.valid, f"rpm={r.rpm:.0f} end={(r.launch or {}).get('end')}")
        if r.valid:
            rig.check(f'{r.label}: rpm plausible', abs(r.rpm - 8000) / 8000 < 0.05, f'{r.rpm:.0f}', severity='warn')
        time.sleep(1.5)
    # Low rail for >1 s: CHECK SENSOR, then recovery and a good pull.
    start = d.mark()
    d.hil('idle 2 0')
    time.sleep(2.0)
    fault = d.wait(lambda l: l.text.startswith('RPM_STATE state=CHECK SENSOR'), 1, since=start)
    rig.check('low rail reports CHECK SENSOR', fault is not None)
    d.hil('idle 1800 3')
    time.sleep(2.5)
    r = rig.pull(signals.standard_pull(7000, label='after sensor fault'))
    rig.check_pull(r, motion=False)
    after = rig.counters()
    rig.note(f"launches {launches} -> {counter(after, 'RPM_STATUS', 'launches')}")
    drain_history(rig)


# --------------------------------------------------------------------------- 4
def back_to_back(rig, gaps_s=(1, 1.5, 2, 3, 5, 8, 12, 15)):
    """Pulls 1-15 s apart (end to start); the 10 s coalesced history save lands between them."""
    settle(rig)
    d = rig.dev
    added, missed = 0, []
    for i, gap in enumerate(gaps_s):
        rpm = 5000 + 700 * i
        r = rig.pull(signals.standard_pull(rpm, label=f'gap {gap}s @ {rpm}'),
                     signals.launch_motion(tilt_rate_dps=400, pull_g=4, seed=50 + i))
        if r.added:
            added += 1
        else:
            missed.append(gap)
        sev = 'fail' if gap >= 2 else 'warn'
        rig.check(f'gap {gap}s: rpm reported and recorded', r.valid and r.added is not None,
                  f"rpm={r.rpm:.0f} end={(r.launch or {}).get('end')}", severity=sev)
        if r.motion:
            rig.check(f'gap {gap}s: tilt replay', r.motion.get('quality') == 'valid',
                      f"quality={r.motion.get('quality')} issue={r.issue}", severity='warn')
        time.sleep(gap)
    rig.note(f'recorded {added}/{len(gaps_s)}; missed gaps {missed}')
    saves = [l.text for l in d.since(rig.scenario['start_line'], 'HISTORY_SAVE')]
    rig.note(f'history saves during scenario: {saves}')
    rig.check('every history save succeeded', all(' ok=1' in s for s in saves))
    drain_history(rig)


def armed_pull(rig, optical, key, delay_ms, slow_ms, motion=None, then=None):
    """Start `optical` from inside the next NVS write of `key`, slowed by slow_ms.

    The loop task that would read a USB !pull is busy writing, so the device
    starts the pull itself (!arm). delay_ms is relative to the write's start.
    """
    d = rig.dev
    signals.program_optical(d, optical)
    if motion:
        signals.program_imu(d, motion)
    else:
        d.hil('imu const')
    d.hil(f'nvs slow {slow_ms} 1 {key}')
    start = d.mark()
    d.hil(f'arm {key} {delay_ms}')
    if then:
        d.hil(then)
    trigger = d.wait(lambda l: l.text.startswith('HIL_PULL') and 'trigger=nvs' in l.text, 40, since=start)
    if not trigger:
        return None, None
    return collect(rig, optical, trigger.index, motion), trigger


def pull_during_history_save(rig, delays_ms=(-150, 0, 100, 250, 380, 600)):
    """A pull that starts inside the coalesced history save (10 s after a pull).

    The history write is slowed to 400 ms; the pull starts from inside it. Its
    first revolutions are lost to the stopped ADC, but an RPM must still be
    reported and recorded, and the save must succeed.
    """
    settle(rig)
    d = rig.dev
    for i, delay in enumerate(delays_ms):
        first = rig.pull(signals.standard_pull(6000, label=f'priming pull {i}'))
        rig.check(f'priming pull {i}: recorded', first.added is not None)
        optical = signals.standard_pull(9000, label=f'pull {delay:+d} ms into a 400 ms history write')
        r, trigger = armed_pull(rig, optical, 'history', delay, 400, signals.launch_motion(seed=60 + i))
        if r is None:
            rig.check(f'history save started (delay {delay})', False)
            continue
        save = d.wait('HISTORY_SAVE', 5, since=trigger.index)
        rig.check(f'save with pull {delay:+d} ms: HISTORY_SAVE ok', save is not None and ' ok=1' in save.text,
                  save.text if save else 'none')
        rig.check(f'{optical.label}: rpm reported', r.valid, f"rpm={r.rpm:.0f} end={(r.launch or {}).get('end')}")
        rig.check(f'{optical.label}: recorded', r.added is not None)
        if r.valid:
            rig.check(f'{optical.label}: rpm within 3%', abs(r.rpm - 9000) / 9000 <= 0.03, f'{r.rpm:.0f}', severity='warn')
        q = (r.motion or {}).get('quality')
        rig.check(f'{optical.label}: motion honest', q in ('valid', 'Motion incomplete'), f'quality={q} issue={r.issue}', severity='warn')
        drain_history(rig)
    d.hil('nvs clear')


def pull_during_long_save(rig, delays_ms=(-150, 0, 300, 700, 1200)):
    """A history save forced to take 1.5 s (`nvs slow` on the history write)
    with a pull starting before and inside it. 0.18 saves to the history log
    without stopping the ADC, so every pull is reported and recorded in full
    and the save succeeds, however long it takes."""
    settle(rig)
    d = rig.dev
    stalls = counter(d.status(), 'PERF', 'adc_stalls')
    for i, delay in enumerate(delays_ms):
        first = rig.pull(signals.standard_pull(6000, label=f'priming pull {i}'))
        rig.check(f'priming pull {i}: recorded', first.added is not None)
        optical = signals.standard_pull(9000, label=f'pull {delay:+d} ms into a 1.5 s history save')
        r, trigger = armed_pull(rig, optical, 'history', delay, 1500, signals.launch_motion(seed=90 + i))
        if r is None:
            rig.check(f'history save started (delay {delay})', False)
            continue
        save = d.wait('HISTORY_SAVE', 8, since=trigger.index)
        rig.check(f'save with pull {delay:+d} ms: HISTORY_SAVE ok', save is not None and ' ok=1' in save.text, save.text if save else 'none')
        rig.check(f'save with pull {delay:+d} ms: took the forced 1.5 s', save is not None and int(number(save.f.get('duration_us'), 0)) >= 1500000,
                  save.text if save else 'none')
        rig.check(f'{optical.label}: rpm reported', r.valid, f"rpm={r.rpm:.0f} end={(r.launch or {}).get('end')}")
        rig.check(f'{optical.label}: recorded', r.added is not None)
        if r.valid:
            rig.check(f'{optical.label}: rpm within 3%', abs(r.rpm - 9000) / 9000 <= 0.03, f'{r.rpm:.0f}')
        drain_history(rig)
    d.hil('nvs clear')
    status = d.status()
    # adc_stalls counts from boot, and adc_stall (after this in all) adds one on purpose.
    rig.check('no ADC stall or overflow', counter(status, 'PERF', 'adc_stalls') == stalls,
              f'adc_stalls {stalls} -> {status.get("PERF", {}).get("adc_stalls")}: {status.get("PERF", {})}')


def adc_stall(rig):
    """No frames reach the acquisition owner for 300 ms (`!adcstall`), as from
    a DMA that stopped with its interrupt held off past the ring: reads find
    nothing and nothing reports an overflow. The silence watchdog must
    restart it within 250 ms and count it (PERF adc_stalls), the detector come
    back to READY, and the next pull read correctly."""
    settle(rig)
    d = rig.dev
    before = counter(d.status(), 'PERF', 'adc_stalls')
    line = d.hil('adcstall 300', expect='HIL_OK adcstall')
    rig.check('frames stopped for 300 ms', line is not None, line.text if line else 'none')
    time.sleep(1.5)
    after = counter(d.status(), 'PERF', 'adc_stalls')
    rig.check('the stall is counted (adc_stalls)', after == before + 1, f'{before} -> {after}')
    rig.check('sampling again: READY', rig.wait_ready(10) is not None)
    # The restart configures the ADC while DFS may hold the CPU at 40 MHz and
    # starts it at 80: every start re-times it (adc_clock.h), so the rate is
    # still 50 kS/s, never 100.
    time.sleep(1.0)
    status = d.status()
    sps = number(status.get('STATUS', {}).get('delivered_sps'), 0)
    rig.check('sampling at 50 kS/s after the restart', abs(sps - 50000) <= 500, f'delivered_sps={sps:.0f}')
    rig.note(f"adc_retimes={status.get('PERF', {}).get('adc_retimes')} (starts whose timing had to be corrected)")
    r = rig.pull(signals.standard_pull(9000, label='pull after the stall'), signals.launch_motion(seed=77))
    rig.check_pull(r, tolerance=0.03, saturated=False)
    drain_history(rig)


def pull_during_power_log_save(rig, delays_ms=(-100, 0, 150, 280, 450)):
    """A pull starting inside, or just after, the 5-minute power-log checkpoint write (300 ms)."""
    settle(rig)
    d = rig.dev
    for delay in delays_ms:
        drain_history(rig)
        time.sleep(11)  # practiceIdle: 10 s quiet since the last accepted pull
        optical = signals.standard_pull(8000, label=f'pull {delay:+d} ms into a 300 ms power-log write')
        r, trigger = armed_pull(rig, optical, 'log', delay, 300, signals.launch_motion(seed=65), then='powerlog')
        rig.check(f'power-log write ran (delay {delay})', trigger is not None)
        if r is None:
            continue
        rig.check(f'{optical.label}: rpm reported', r.valid, f"rpm={r.rpm:.0f} end={(r.launch or {}).get('end')}")
        rig.check(f'{optical.label}: recorded', r.added is not None)
        if r.valid:
            rig.check(f'{optical.label}: rpm within 3%', abs(r.rpm - 8000) / 8000 <= 0.03, f'{r.rpm:.0f}', severity='warn')
        notice = [l.text for l in d.since(trigger.index, 'POWER_LOG_NOTICE')]
        rig.check(f'{optical.label}: power log saved', not notice, str(notice))
    d.hil('nvs clear')


SHAKE_ROW, SLEEP_ROW = 10, 0  # Controls::Setting::ShakeWake and Sleep (0.20.2)


def preflight(rig):
    """Before the first scenario, put back what a stopped run or the owner's
    own settings leave on the unit: the HIL fault modes (`!reset`), nvs_tight's
    filler (a 0.20.8 gate run stopped inside nvs_tight left 45 entries, and the
    next HIL all's themes failed 21 checks on nvs_space) and Mounting Top, which
    the scenarios assume (the owner's unit is set to Sidecar, and the 0.20.8
    gate's first HIL all failed 54 wear_check checks on it). Bluetooth On, which
    the owner's unit may have from 0.20.9, is turned Off first, with the restart
    that applies it: the scenarios assume it Off and turn it on themselves.
    Returns what it found, for run.json."""
    d = rig.dev
    bluetooth = d.status().get('BLE_STATUS', {}).get('started')
    if bluetooth not in (None, '0'):
        start = d.mark()
        line = d.hil(f'set {BLUETOOTH_ROW} 0', expect='HIL_SET', timeout=8)
        if line is None or line.f.get('error') != '0':
            raise RuntimeError(f'preflight: Bluetooth Off refused: {line.text if line else "no HIL_SET"}')
        if d.wait('HIL_BOOT', 15, since=start) is None:
            d.reopen()  # a reset can leave the USB link silent
        time.sleep(1.5)
        rig.boot_id = rig.hil_boot().get('boot_id')
        if d.status().get('BLE_STATUS', {}).get('started') != '0':
            raise RuntimeError('preflight: Bluetooth is still on after its restart')
    reset = d.hil('reset', expect='HIL_OK reset')
    before = d.status().get('NVS_STATUS', {}).get('used_entries')
    fill = d.hil('nvsfill 0', expect='HIL_OK nvsfill', timeout=8)
    status = d.status()
    mounting = status.get('MOUNT_STATUS', {}).get('selected')
    if mounting != 'Top':
        line = d.hil(f'set {MOUNT_ROW} 0', expect='HIL_SET', timeout=8)
        if line is None or line.f.get('error') != '0':
            raise RuntimeError(f'preflight: Mounting Top refused: {line.text if line else "no HIL_SET"}')
        d.hil('home')
    after = status.get('NVS_STATUS', {}).get('used_entries')
    if reset is None or fill is None or fill.f.get('keys') != '0':
        raise RuntimeError(f'preflight: reset {reset.text if reset else None}, filler {fill.text if fill else None}')
    found = {'mounting_was': mounting, 'nvs_used_before': before, 'nvs_used_after_filler': after}
    if bluetooth not in (None, '0'):
        found['bluetooth_was'] = 'On'
    return found


def standby_dry_run(rig, wait_s, request=False, timeout=20):
    """The inactivity sleep with !sleepdry 1: its whole path up to PM1's power-off
    and deep sleep, then back. Waits `wait_s` of idle for the timeout (or asks at
    once with `S`); returns the lines from POWER_OFF to its POWER_OFF_DEFERRED."""
    d = rig.dev
    start = d.mark()
    if request:
        d.write('S\n')
    end = d.wait(lambda l: l.text.startswith('POWER_OFF_DEFERRED reason='), wait_s + timeout, since=start)
    return [l for l in d.since(start, '') if end is None or l.index <= end.index], end


def standby(rig):
    """Sleep after's 1-minute default and Shake to wake (0.20.2), with the
    inactivity sleep's dry run (!sleepdry 1): the rig's USB host is attached and
    a real sleep there needs the side key to recover, so the path runs to the
    point where PM1 would power off, reports the wake state it set up, and
    restores. A real wake (shake, side key) is the owner's battery hand test.

    1. A pull, then 60 s of idle at the 1-minute default: the pull's history is
       saved before the sleep, and none is lost.
    2. Shake to wake On: the IMU's motion wake is armed (PM1 wake GPIO4, the
       IMU supply held), SHAKE_SLEEP wake=imu_motion.
    3. Shake to wake Off: nothing is armed (no SHAKE_WAKE_ARM, PM1 wake and
       hold clear), SHAKE_SLEEP wake=side_key mode=power_off; POWER_STATUS
       reports it, and a pull afterwards is measured as ever.
    Leaves Shake to wake On, Sleep after 1 min and no dry run.
    """
    settle(rig)
    d = rig.dev
    for row, value in ((SLEEP_ROW, 1), (SHAKE_ROW, 1)):
        line = d.hil(f'set {row} {value}', expect='HIL_SET', timeout=6)
        rig.check(f'set row {row} to {value}', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    d.hil('home')
    d.hil('sleepdry 1', expect='HIL_OK sleepdry')
    d.hil('sleepok 1', expect='HIL_OK sleepok')
    try:
        power = d.status().get('POWER_STATUS', {})
        rig.check('POWER_STATUS: 1-minute default sleep, Shake to wake On',
                  power.get('auto_off_s') == '60' and power.get('shake_wake') == '1' and power.get('wake') == 'shake', str(power))
        # 1. A pull, then a minute of idle: saved before the sleep, nothing lost.
        before = int(number(d.status().get('HISTORY_STATUS', {}).get('generation'), -1))
        r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=81))
        rig.check_pull(r, tolerance=0.03)
        lines, end = standby_dry_run(rig, 60 + 5)
        off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
        saves = [l for l in lines if l.text.startswith('HISTORY_SAVE ')]
        rig.check('60 s idle: the inactivity sleep ran (dry run)', off is not None and end is not None, end.text if end else 'no POWER_OFF_DEFERRED')
        rig.check('60 s idle: the pull is saved before the sleep',
                  off is not None and ' saved=1' in off.text and all(' ok=1 ' in l.text for l in saves) and
                  any(l.index < off.index and int(number(l.f.get('generation'), -1)) >= before + 1 for l in saves),
                  [l.text for l in saves] + [off.text if off else 'no POWER_OFF'])
        after = d.status().get('HISTORY_STATUS', {})
        rig.check('60 s idle: no pull lost (one more generation, nothing pending)',
                  r is not None and r.added is not None and int(number(after.get('generation'), -2)) >= before + 1 and after.get('pending') == '0',
                  f"generation {before} -> {after.get('generation')}, pending {after.get('pending')}")
        # 2. Shake to wake On: armed.
        armed = next((l for l in lines if l.text.startswith('SHAKE_WAKE_ARM ')), None)
        dry = next((l for l in lines if l.text.startswith('SLEEP_DRY_RUN ')), None)
        sleep = next((l for l in lines if l.text.startswith('SHAKE_SLEEP ')), None)
        rig.check('Shake to wake On: the IMU motion wake is armed', armed is not None and armed.f.get('ok') == '1' and
                  dry is not None and dry.f.get('imu_wake_armed') == '1' and dry.f.get('arm_imu') == '1',
                  f"{armed.text if armed else 'no SHAKE_WAKE_ARM'} | {dry.text if dry else 'no SLEEP_DRY_RUN'}")
        rig.check('Shake to wake On: SHAKE_SLEEP wake=imu_motion', sleep is not None and sleep.f.get('wake') == 'imu_motion', sleep.text if sleep else 'none')
        rig.check('Shake to wake On: the green LED is off and the speaker powered down for standby (0.20.6)',
                  dry is not None and dry.f.get('led') == '0' and dry.f.get('speaker_powered') == '0', dry.text if dry else 'no SLEEP_DRY_RUN')
        rig.check('Shake to wake On: restored after the dry run', end is not None and end.f.get('restored') == '1', end.text if end else 'none')
        # 3. Shake to wake Off: nothing armed, the side key's power-off state.
        line = d.hil(f'set {SHAKE_ROW} 0', expect='HIL_SET', timeout=6)
        rig.check('Shake to wake set Off', line is not None and line.f.get('error') == '0', line.text if line else 'none')
        d.hil('home')
        power = d.status().get('POWER_STATUS', {})
        rig.check('POWER_STATUS reports Shake to wake Off', power.get('shake_wake') == '0' and power.get('wake') == 'side_key' and power.get('sleep') == 'power_off', str(power))
        lines, end = standby_dry_run(rig, 0, request=True)
        armed = [l for l in lines if l.text.startswith('SHAKE_WAKE_ARM ')]
        dry = next((l for l in lines if l.text.startswith('SLEEP_DRY_RUN ')), None)
        sleep = next((l for l in lines if l.text.startswith('SHAKE_SLEEP ')), None)
        wake = int(dry.f.get('pm1_wake', '0xff'), 16) if dry else 0xff
        hold = int(dry.f.get('pm1_hold', '0xff'), 16) if dry else 0xff
        rig.check('Shake to wake Off: the IMU is not armed (no SHAKE_WAKE_ARM, PM1 wake and hold clear)',
                  not armed and dry is not None and dry.f.get('ok') == '1' and dry.f.get('arm_imu') == '0' and not (wake & 0x18) and not (hold & 0x60),
                  f"{[l.text for l in armed]} | {dry.text if dry else 'no SLEEP_DRY_RUN'}")
        rig.check('Shake to wake Off: SHAKE_SLEEP wake=side_key mode=power_off',
                  sleep is not None and sleep.f.get('wake') == 'side_key' and sleep.f.get('mode') == 'power_off', sleep.text if sleep else 'none')
        rig.check('Shake to wake Off: restored after the dry run', end is not None and end.f.get('restored') == '1', end.text if end else 'none')
        time.sleep(2)
        r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=82))
        rig.check_pull(r, tolerance=0.03)
    finally:
        d.hil('sleepok 0', expect='HIL_OK sleepok')
        d.hil('sleepdry 0', expect='HIL_OK sleepdry')
        d.hil(f'set {SHAKE_ROW} 1', expect='HIL_SET', timeout=6)
        d.hil('home')


MOTION_LIMIT_S = 300  # InactivityTimer::MOTION_LIMIT_MS (0.20.6)
UNUSED_SHAKE_S = 15   # InactivityTimer::UNUSED_SHAKE_MS (30 through 0.20.9)
UNUSED_SHAKE_LIMIT_S = 60  # InactivityTimer::UNUSED_SHAKE_LIMIT_MS (120 through 0.20.9)
UNUSED_WAKES_LIMIT = 3     # UnusedWakes::LIMIT
MEASURE_LIMIT_S = 30  # OpticalSleepGuard::MEASURE_LIMIT_MS
FORCE_AFTER = 2       # OpticalSleepGuard::FORCE_AFTER
MIC_STOP_LIMIT_S = 30  # MicSession::STOP_LIMIT_MS (0.20.7)
SLEEP_RETRY_S = 60    # autoPowerOff's retry after a deferred sleep
# sleep_blockers waits for each sleep with every sleep limit divided by this
# (!sleepscale, power_status.h sleepLimit()): seconds, not the real minutes.
# standby keeps the real limits, so one sleep in every gate runs at them.
SLEEP_SCALE = 10


class sleep_scale:
    """`with sleep_scale(rig):` every sleep limit divided by SLEEP_SCALE inside the
    block, the real ones again after it (also on an error)."""

    def __init__(self, rig, scale=SLEEP_SCALE):
        self.rig, self.scale = rig, scale

    def __enter__(self):
        line = self.rig.dev.hil(f'sleepscale {self.scale}', expect='HIL_OK sleepscale')
        self.rig.check(f'sleep limits divided by {self.scale}', line is not None and line.text.endswith(f' {self.scale}'),
                       line.text if line else 'no HIL_OK sleepscale')
        return self.scale

    def __exit__(self, *exc):
        self.rig.dev.hil('sleepscale 1', expect='HIL_OK sleepscale')
        return False


def mic_stop_wait(lines, asked):
    """Seconds from `asked`, the host's time as it sent the sleep request, to
    the sleep's POWER_OFF. The device asks the mic to stop only after the
    request and holds the sleep MIC_STOP_LIMIT_S from that, so the wait is at
    least the limit however late a line is read. Timed from the host's read of
    MIC_STATE stopping it was not: in the 0.20.8 gate that line arrived 1.3 s
    late, with the unit's 2 s status print, and a sleep 30.0 s after the
    request measured 28.7 s."""
    off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
    return off.t - asked if off else None


def sleep_blockers(rig):
    """Ways a unit could stay awake until its battery was flat, each through
    the inactivity sleep's dry run (!sleepdry 1: the rig's host is attached) at
    Sleep after's 1-minute default. The first three were fixed in 0.20.6.

    1. Movement after use. A pull, then the IMU reports jostling (20 dps and
       0.03 g of noise, a unit in a pocket or bag; the movement threshold is
       2 dps). Movement keeps a used unit awake at most 5 minutes after its
       last button, pull or command: the sleep runs 4-6 minutes after the pull.
    2. A failed NVS history copy. With the history log holding the pull, NVS
       refusing the shutdown copy (!nvs fail, as when NVS is full) no longer
       defers the sleep every minute: it runs, with POWER_OFF_NOTICE.
    3. Optical marks that never end. A steady 7,200 RPM signal (a light
       flickering into the sensor, a fan) holds the detector in Launch: after
       30 s it no longer counts as a pull being measured, and after two sleeps
       refused a minute apart the next one stops sampling anyway.
    4. A Mic RPM stop that never ends (0.20.7). Sleep leaves the page and
       waits for the mic, which holds the codec, to stop; with its task held
       before it takes the mic down (!micstall, as one stuck in a spin or in
       M5.Mic's end() would be), the sleep runs 30 s after the stop was asked,
       with POWER_OFF_NOTICE mic_stop_timeout, and the mic stops once the task
       goes on.
    5. A carried unit. A shake wake nobody uses (`!shakewake`: the rig cannot
       be shaken) sleeps 15 s after the wake when still and 60 s after it while
       it keeps moving (30 s and 2 minutes through 0.20.9). Each is counted;
       the third in a row sleeps for the side key alone (no IMU wake armed),
       and a sleep after use arms the shake wake again and clears the count.
    Then a pull is measured as ever. Leaves Shake to wake On, Sleep after 1 min,
    no unused wakes counted and no dry run.

    Cases 1, 3 and 5 wait for their sleeps with every sleep limit divided by
    SLEEP_SCALE (`!sleepscale`): 30 s for the 5-minute movement limit, 3 s
    for the 30 s measured-pull limit and 6 s between refused sleeps, about a
    minute and a half in all instead of nine. Case 4 waits the real 30 s:
    MicSession::STOP_LIMIT_MS is not a sleepLimit().
    """
    settle(rig)
    d = rig.dev
    for row, value in ((SLEEP_ROW, 1), (SHAKE_ROW, 1)):
        line = d.hil(f'set {row} {value}', expect='HIL_SET', timeout=6)
        rig.check(f'set row {row} to {value}', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    d.hil('home')
    d.hil('sleepdry 1', expect='HIL_OK sleepdry')
    d.hil('sleepok 1', expect='HIL_OK sleepok')
    still = 'const {:.4f} {:.4f} {:.4f} 0 0 0 0.004 0.08'.format(*GRAVITY)
    try:
        # 1. Movement after use. HIL commands and `p` never count as use.
        r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=83))
        rig.check_pull(r, tolerance=0.03)
        pulled = time.monotonic()
        d.hil('const {:.4f} {:.4f} {:.4f} 0 0 0 0.03 20'.format(*GRAVITY))
        d.hil('imu const')
        time.sleep(3)
        activity = d.status().get('MOTION_ACTIVITY', {})
        rig.check('the injected jostling counts as movement', activity.get('moving') == '1', str(activity))
        # Scaled once the unit moves: the 6 s timeout alone would have run out first.
        with sleep_scale(rig) as scale:
            limit, timeout = MOTION_LIMIT_S / scale, 60 / scale
            lines, end = standby_dry_run(rig, limit + timeout - (time.monotonic() - pulled), timeout=30)
        off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
        after = (off.t if off else time.monotonic()) - pulled
        rig.note(f'moving after a pull: sleep {"ran" if off else "never ran"} {after:.0f} s after the pull '
                 f'(limits / {scale}: {limit:.0f} s for movement, {timeout:.0f} s Sleep after)')
        rig.check(f'moving: still awake past the {timeout:.0f} s timeout (movement extends it)', off is None or after >= 0.8 * limit, f'{after:.1f} s')
        rig.check(f'moving: the sleep runs within the {limit:.0f} s movement limit of the last use',
                  off is not None and end is not None and after <= limit + 5, f'{after:.1f} s, {end.text if end else "no sleep"}')
        d.hil(still)
        # 2. NVS refuses the history copy once the log holds the pull.
        time.sleep(2)
        r = rig.pull(signals.standard_pull(8000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=84))
        rig.check_pull(r, tolerance=0.03)
        rig.check('the history log holds the pull', drain_history(rig))
        d.hil('nvs fail 1000 history', expect='HIL_OK nvs')
        lines, end = standby_dry_run(rig, 0, request=True, timeout=30)
        d.hil('nvs clear', expect='HIL_OK nvs')
        off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
        deferred = [l.text for l in lines if l.text.startswith('POWER_OFF_DEFERRED ') and 'sleep_dry_run' not in l.text]
        notice = next((l for l in lines if l.text.startswith('POWER_OFF_NOTICE ')), None)
        rig.check('NVS copy refused: the sleep still runs (the log holds the pull)',
                  off is not None and end is not None and not deferred, deferred or (end.text if end else 'no sleep'))
        rig.check('NVS copy refused: POWER_OFF_NOTICE names it', notice is not None and 'history' in notice.text, notice.text if notice else 'none')
        rig.check('NVS copy refused: nothing pending afterwards', d.status().get('HISTORY_STATUS', {}).get('pending') == '0')
        # 3. Marks that never stop.
        steady = signals.OpticalPull([(0, 7200, 1000, 4), (600000, 7200, 1000, 4)], expected_rpm=7200, label='steady 7200 rpm marks')
        signals.program_optical(d, steady)
        d.hil('pull 60', expect='HIL_PULL')
        started = time.monotonic()
        # Scaled once the marks hold the detector: before them the 6 s timeout
        # since the last dry run could run out and sleep without a refusal.
        with sleep_scale(rig) as scale:
            time.sleep(1)
            rig.note(f"steady marks: detector {d.status().get('RPM_STATUS', {}).get('state')}")
            wait = (MEASURE_LIMIT_S + SLEEP_RETRY_S * (FORCE_AFTER + 1)) / scale
            lines, end = standby_dry_run(rig, wait - (time.monotonic() - started), timeout=20)
        off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
        busy = [l for l in lines if l.text.startswith('POWER_OFF_DEFERRED optical_busy')]
        after = (off.t if off else time.monotonic()) - started
        rig.note(f'steady marks: sleep {"ran" if off else "never ran"} {after:.1f} s after they began (limits / {scale}); '
                 f'refusals {[l.text for l in busy]}')
        rig.check('steady marks: the sleep runs anyway', off is not None and end is not None, f'{after:.0f} s')
        rig.check(f'steady marks: refused {FORCE_AFTER} times first, then forced',
                  len(busy) == FORCE_AFTER and off is not None and 'forced=1' in off.text, [l.text for l in busy] + [off.text if off else 'no POWER_OFF'])
        d.hil('gen clear')
        rig.idle_sources()
        d.hil('sleepdry 1', expect='HIL_OK sleepdry')
        d.hil('sleepok 1', expect='HIL_OK sleepok')
        block = rig.wait_ready(25)
        rig.check('READY again after the forced sleep', block is not None)
        # 4. A mic stop that never ends. The HIL image has less internal heap than
        # production: the mic's floor drops by the same amount, as in mic_page.
        d.hil(f'micfloor {heap_floor(rig)}', expect='HIL_OK micfloor')
        rig.check('the Mic RPM page opens', mic_open(rig) is not None)
        d.hil('micstall 1', expect='HIL_OK micstall')
        asked = time.monotonic()
        lines, end = standby_dry_run(rig, MIC_STOP_LIMIT_S, request=True, timeout=30)
        start = d.mark()
        d.hil('micstall 0', expect='HIL_OK micstall')
        stopping = next((l for l in lines if l.text.startswith('MIC_STATE ') and l.f.get('state') == 'stopping'), None)
        notice = next((l for l in lines if l.text.startswith('POWER_OFF_NOTICE mic_stop_timeout')), None)
        off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
        after = mic_stop_wait(lines, asked)
        rig.note(f'stuck mic stop: sleep {"ran" if off else "never ran"}' + (f' {after:.1f} s after it was asked' if after is not None else ''))
        rig.check('stuck mic stop: the sleep leaves the page and asks the mic to stop', stopping is not None and (off is None or stopping.index < off.index),
                  ' / '.join(l.text for l in lines[:4]))
        rig.check(f'stuck mic stop: the sleep runs anyway, {MIC_STOP_LIMIT_S} s after it was asked, with its notice',
                  off is not None and end is not None and notice is not None and after is not None and MIC_STOP_LIMIT_S - 1 <= after <= MIC_STOP_LIMIT_S + 5,
                  f'{after} s, {notice.text if notice else "no notice"}, {end.text if end else "no sleep"}')
        rig.check('stuck mic stop: the mic stops once its task goes on', mic_wait(rig, 'off', 5, start) is not None)
        rig.check('stuck mic stop: the sleep left the Menu row, not the page', ui_state(rig).get('page') == MENU_PAGE, str(ui_state(rig)))
        d.hil('sleepdry 1', expect='HIL_OK sleepdry')
        d.hil('sleepok 1', expect='HIL_OK sleepok')
        d.hil('home')
        # The dry run restarts sampling and the IMU: its motion warms up again first.
        rig.check('READY again after the stuck stop\'s sleep', rig.wait_ready(25) is not None)
        # 5. A carried unit: shake wakes nobody uses.
        d.hil('unusedwakes 0', expect='HIL_OK unusedwakes')

        def unused_wake(moving):
            # One shake wake nobody uses, to its sleep: (seconds from wake to sleep, the sleep's lines).
            if moving:
                d.hil('const {:.4f} {:.4f} {:.4f} 0 0 0 0.03 20'.format(*GRAVITY))
                d.hil('imu const')
                time.sleep(3)
            with sleep_scale(rig) as scale:
                d.hil('shakewake', expect='HIL_OK shakewake')
                woke = time.monotonic()
                lines, end = standby_dry_run(rig, (UNUSED_SHAKE_LIMIT_S if moving else UNUSED_SHAKE_S) / scale, timeout=25)
            if moving:
                d.hil(still)
            off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
            return (off.t - woke if off else None), lines, end, scale

        def wake_state(lines):
            count = next((l for l in lines if l.text.startswith('UNUSED_WAKES ')), None)
            slept = next((l for l in lines if l.text.startswith('SHAKE_SLEEP ')), None)
            dry = next((l for l in lines if l.text.startswith('SLEEP_DRY_RUN ')), None)
            armed = any(l.text.startswith('SHAKE_WAKE_ARM ') for l in lines)
            return count, slept, dry, armed

        after, lines, end, scale = unused_wake(moving=False)
        count, slept, dry, armed = wake_state(lines)
        limit = UNUSED_SHAKE_S / scale
        rig.note(f'unused shake wake, still: sleep {"ran" if after is not None else "never ran"} '
                 f'{after if after is None else round(after, 1)} s after the wake (limit {limit:.1f} s)')
        rig.check(f'unused wake, still: the sleep runs {limit:.1f} s after the wake',
                  after is not None and end is not None and 0.8 * limit <= after <= limit + 4, f'{after} s')
        rig.check('unused wake 1: counted, the shake wake armed',
                  count is not None and count.f.get('count') == '1' and count.f.get('shake_armed') == '1' and armed and
                  slept is not None and slept.f.get('wake') == 'imu_motion' and dry is not None and dry.f.get('imu_wake_armed') == '1',
                  ' / '.join(l.text for l in (count, slept, dry) if l))
        after, lines, end, scale = unused_wake(moving=True)
        count, slept, dry, armed = wake_state(lines)
        limit, quiet = UNUSED_SHAKE_LIMIT_S / scale, UNUSED_SHAKE_S / scale
        rig.note(f'unused shake wake, moving: sleep {"ran" if after is not None else "never ran"} '
                 f'{after if after is None else round(after, 1)} s after the wake (limit {limit:.1f} s)')
        rig.check(f'unused wake, moving: awake past {quiet:.1f} s, asleep within {limit:.1f} s of the wake',
                  after is not None and end is not None and after >= 0.8 * limit and after <= limit + 4, f'{after} s')
        rig.check('unused wake 2: counted, the shake wake armed',
                  count is not None and count.f.get('count') == '2' and count.f.get('shake_armed') == '1' and armed,
                  count.text if count else 'none')
        after, lines, end, scale = unused_wake(moving=False)
        count, slept, dry, armed = wake_state(lines)
        rig.check(f'unused wake {UNUSED_WAKES_LIMIT}: the sleep is for the side key alone',
                  end is not None and count is not None and count.f.get('count') == str(UNUSED_WAKES_LIMIT) and count.f.get('shake_armed') == '0' and
                  not armed and slept is not None and slept.f.get('wake') == 'side_key' and slept.f.get('mode') == 'power_off' and
                  dry is not None and dry.f.get('imu_wake_armed') == '0',
                  ' / '.join(l.text for l in (count, slept, dry) if l) or 'no sleep')
        policy = d.status().get('POWER_POLICY', {})
        rig.check('the count is kept', policy.get('unused_wakes') == str(UNUSED_WAKES_LIMIT), str(policy))
        # Use (the USB S command), then its sleep: armed again, the count cleared.
        lines, end = standby_dry_run(rig, 0, request=True, timeout=30)
        count, slept, dry, armed = wake_state(lines)
        rig.check('a sleep after use arms the shake wake again and clears the count',
                  end is not None and count is not None and count.f.get('count') == '0' and count.f.get('shake_armed') == '1' and armed and
                  slept is not None and slept.f.get('wake') == 'imu_motion' and dry is not None and dry.f.get('imu_wake_armed') == '1',
                  ' / '.join(l.text for l in (count, slept, dry) if l) or 'no sleep')
        rig.check('READY again after the unused wakes', rig.wait_ready(25) is not None)
        r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=86))
        rig.check_pull(r, tolerance=0.03)
    finally:
        d.hil('sleepscale 1', expect='HIL_OK sleepscale')
        d.hil('unusedwakes 0')
        d.hil('micstall 0')
        d.hil('micfloor')  # MicSession::FLOOR_BYTES again
        d.hil('nvs clear')
        d.hil('gen clear')
        d.hil(still)
        d.hil('sleepok 0', expect='HIL_OK sleepok')
        d.hil('sleepdry 0', expect='HIL_OK sleepdry')
        d.hil(f'set {SHAKE_ROW} 1', expect='HIL_SET', timeout=6)
        d.hil('home')


def usb_standby(rig, recheck_s=20):
    """The USB-retained standby (0.20.6). On USB the PM1 power-off cannot cut
    the ESP32's rail, so the unit deep-sleeps instead; through 0.20.5 nothing
    re-checked once the cable was pulled, and that rail also feeds the sensor's
    emitter (about 20 mA). Now the standby also wakes on PM1's 5VIN-removed
    IRQ (0x44 bit 1 unmasked before the power-off) and on a timer that
    re-checks USB. `!usbstandby <s>` takes the side key's real power-off path
    with an s-second re-check that boots rather than sleeping again, so the
    rig gets the unit back; the unplug and the re-sleep stay a bench check.

    1. The power-off saves (POWER_OFF reason=power_key) and the unit leaves USB.
    2. It comes back by the timer (esp_cause 4) still in standby: in_standby=1
       key_only=1 finish=0, the re-check read PM1 (5VIN present) and found the
       5VIN-removal IRQ still unmasked after the power-off.
    3. It is a normal unit again: a pull is measured.
    """
    settle(rig)
    d = rig.dev
    start = d.mark()
    d.hil(f'usbstandby {recheck_s}', expect='HIL_OK usbstandby')
    off = d.wait(lambda l: l.text.startswith(('POWER_OFF reason=power_key', 'POWER_KEY_OFF', 'POWER_KEY_IGNORED')), 20, since=start)
    rig.check('the side key power-off path ran and saved', off is not None and off.text.startswith('POWER_OFF reason=power_key') and ' saved=1' in off.text,
              off.text if off else 'none')
    gone = d.wait('HOST_DISCONNECT', 15, since=start)
    rig.check('the unit left USB for its deep sleep', gone is not None, gone.text if gone else 'still on USB')
    back = d.wait(lambda l: l.text.startswith(('HOST_RECONNECT', 'HIL_BOOT')), recheck_s + 60, since=start)
    if back is None:
        d.reopen(30)
    before = rig.boot_id
    status = rig.hil_boot()
    rig.check('it came back by itself after the re-check', status.get('boot_id') not in (None, before), f"boot_id {before} -> {status.get('boot_id')}")
    rig.idle_sources()
    rig.boot_id = status.get('boot_id')
    rig.scenario['start_line'] = d.mark()
    line = d.hil('standbywake', expect='HIL_STANDBY_WAKE', timeout=5)
    f = line.f if line else {}
    mask = int(f.get('recheck_irq_mask', '0xff'), 16)
    source = int(f.get('recheck_source', '0x00'), 16)
    rig.check('the timer woke it (esp_cause 4), still in the side-key standby, not finished',
              f.get('esp_cause') == '4' and f.get('in_standby') == '1' and f.get('key_only') == '1' and f.get('finish') == '0', line.text if line else 'none')
    rig.check('the re-check read PM1 before the boot: 5VIN present', f.get('recheck') == '1' and f.get('recheck_read') == '1' and source & 0x01,
              line.text if line else 'none')
    rig.check('the 5VIN-removal IRQ stayed unmasked through the power-off', f.get('recheck_read') == '1' and not mask & 0x02, line.text if line else 'none')
    time.sleep(1.5)
    settle(rig)
    r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=83))
    rig.check_pull(r, tolerance=0.03)


# --------------------------------------------------------------------------- 5
IMU_FAULTS = [
    ('length read failures x5', 'fault lenfail 5', 'length_errors'),
    ('data read failures x3', 'fault datafail 3', 'data_errors'),
    ('corrupt header x3', 'fault corrupt 3', 'parse_errors'),
    ('stalled transfer 25 ms x2', 'fault stall 25 2', 'stalls'),
    ('FIFO overflow x2', 'fault overflow 2', 'overflows'),
    ('oversize length x2', 'fault oversize 2', 'oversize'),
    ('skip frame 20 frames', 'fault skip 20', 'skipped_frames'),
]


def imu_faults(rig):
    """I2C/FIFO faults at idle: counted honestly, no crash, fusion recovers."""
    settle(rig)
    d = rig.dev
    for label, command, field in IMU_FAULTS:
        before = rig.counters()
        d.hil(command)
        time.sleep(1.5)
        after = rig.wait_ready(15) or rig.counters()
        delta = counter(after, 'IMU_FIFO_STATUS', field) - counter(before, 'IMU_FIFO_STATUS', field)
        rig.check(f'{label}: counted in IMU_FIFO_STATUS {field}', delta > 0, f'+{delta}')
        rig.check(f'{label}: fusion ready again', after.get('ORIENTATION_STATUS', {}).get('ready') == '1')
        gaps = counter(after, 'MOTION_STATUS', 'gaps') - counter(before, 'MOTION_STATUS', 'gaps')
        rig.note(f'{label}: motion gaps +{gaps}, max_gap_us={after.get("MOTION_STATUS", {}).get("max_gap_us")}')
    # Silent value corruption (a flipped payload byte) must stay finite.
    d.hil('fault garble 20')
    time.sleep(1)
    rig.check('garbled payloads: device alive', rig.check_alive())
    # Sensor stops producing frames (frozen), then resumes.
    before = rig.counters()
    d.hil('fault freeze 1')
    time.sleep(2)
    d.hil('fault freeze 0')
    after = rig.wait_ready(15) or rig.counters()
    rig.check('frozen sensor 2 s: gap reported', counter(after, 'MOTION_STATUS', 'gaps') > counter(before, 'MOTION_STATUS', 'gaps'))
    rig.check('frozen sensor: fusion recovers', after.get('ORIENTATION_STATUS', {}).get('ready') == '1')
    # Long gate hold: the real FIFO model overflows (> ~1 s of frames).
    before = rig.counters()
    d.hil('gate 1600')
    time.sleep(2.5)
    after = rig.wait_ready(15) or rig.counters()
    ov = counter(after, 'IMU_FIFO_STATUS', 'overflows') - counter(before, 'IMU_FIFO_STATUS', 'overflows')
    rig.check('1.6 s gate hold: overflow reported as a gap', ov > 0, f'overflows +{ov}')
    rig.check('1.6 s gate hold: fusion recovers', after.get('ORIENTATION_STATUS', {}).get('ready') == '1')
    # Clock drift and timestamp jitter: the timeline follows without anchors piling up.
    for label, command in (('sensor clock +3%', 'fault drift 30000'), ('sensor clock -3%', 'fault drift -30000'),
                           ('sensor frame jitter 3 ms', 'fault sensorjitter 3000'),
                           ('length-read latency jitter 4 ms', 'fault jitter 4000')):
        before = rig.counters()
        d.hil(command)
        time.sleep(4)
        after = rig.counters()
        d.hil('fault clear')
        anchors = counter(after, 'IMU_FIFO_STATUS', 'anchors') - counter(before, 'IMU_FIFO_STATUS', 'anchors')
        gaps = counter(after, 'MOTION_STATUS', 'gaps') - counter(before, 'MOTION_STATUS', 'gaps')
        period = after.get('IMU_FIFO_STATUS', {}).get('period_us')
        rig.check(f'{label}: no gaps', gaps == 0, f'gaps +{gaps} anchors +{anchors} period_us={period}', severity='warn')
        r = rig.pull(signals.standard_pull(7000, label=f'pull after {label}'), signals.launch_motion(seed=70))
        rig.check_pull(r)
    d.hil('fault clear')
    drain_history(rig)


def imu_faults_during_pull(rig):
    """Faults inside a pull: RPM always reported; motion honest (incomplete), never a crash."""
    settle(rig)
    d = rig.dev
    cases = [('data failure', 'fault datafail 1'), ('stall', 'fault stall 30 1'), ('overflow', 'fault overflow 1'),
             ('corrupt', 'fault corrupt 1'), ('gate held 600 ms', 'gate 600')]
    for i, (label, command) in enumerate(cases):
        optical = signals.standard_pull(8000 + 400 * i, label=f'{label} mid-pull')
        motion = signals.launch_motion(tilt_rate_dps=500, pull_g=5, seed=80 + i)
        signals.program_optical(d, optical)
        signals.program_imu(d, motion)
        start = d.mark()
        d.hil('pull 60', expect='HIL_PULL')
        time.sleep(0.35)
        d.hil(command)
        r = collect(rig, optical, start, motion)
        rig.check(f'{label} mid-pull: rpm reported', r.valid, f"rpm={r.rpm:.0f}")
        rig.check(f'{label} mid-pull: recorded', r.added is not None)
        q = (r.motion or {}).get('quality')
        rig.check(f'{label} mid-pull: motion honest', q in ('valid', 'Motion incomplete'), f'quality={q} issue={r.issue}')
        rig.check(f'{label} mid-pull: never out of range', q != 'Motion out of range')
        time.sleep(2)
    drain_history(rig)


def imu_backlog(rig, hold_s=60):
    """The IMU loop held up for a minute must not spin through its missed polls.

    Another task holds the IMU gate, so every poll waits 20 ms for it and
    starts late. The 0.15-0.18 loop (5 ms polls) fell about 45 s behind and
    then ran back-to-back through every missed period without blocking, IDLE0
    never ran on core 0 and the task watchdog reset the device. Late polls
    must skip the backlog (counted), IDLE0 keep running, and tilt replay stay
    valid.
    """
    settle(rig)
    d = rig.dev
    d.hil('idle0 reset', expect='HIL_IDLE0')
    before = rig.counters()
    d.hil(f'gate {hold_s * 1000}')
    # The old loop's spin began at the release and lasted well past 5 s.
    time.sleep(hold_s + 12)
    alive = rig.check_alive()
    after = rig.wait_ready(25) or rig.counters()
    idle = d.hil('idle0', expect='HIL_IDLE0')
    gap = int(number(idle.f.get('max_gap_us'), 0)) if idle else -1
    rig.check('IDLE0 kept running on core 0 (watchdog margin)', alive and 0 <= gap < 1000000, f'max_gap_us={gap}')

    def delta(key):
        return counter(after, 'MOTION_STATUS', key) - counter(before, 'MOTION_STATUS', key)
    misses, late, missed = delta('gate_misses'), delta('late_polls'), delta('missed_polls')
    rig.check('gate held: polls missed the gate', misses > hold_s * 25, f'gate_misses +{misses}')
    rig.check('late polls restart the schedule instead of catching up', late >= misses,
              f'late_polls +{late} missed_polls +{missed}')
    rig.check('fusion ready again', after.get('ORIENTATION_STATUS', {}).get('ready') == '1')
    r = rig.pull(signals.standard_pull(8000, label='pull after a 60 s IMU stall'),
                 signals.launch_motion(tilt_rate_dps=400, pull_g=4, seed=91))
    rig.check_pull(r)
    drain_history(rig)


def collect(rig, optical, start, motion=None):
    """Collect the result lines of a pull already started at line index `start`."""
    from rig import PullResult
    d = rig.dev
    r = PullResult(optical.label, optical.expected_rpm)
    if motion:
        r.expected_gyro, r.expected_saturated = motion.peak_gyro_dps, motion.expect_saturated
    launch = d.wait('LAUNCH_RESULT', optical.duration_ms / 1000 + 4, since=start)
    if launch:
        r.launch = launch.f
        m = d.wait('LAUNCH_METRICS', 1, since=launch.index)
        a = d.wait('HISTORY_ADDED', 1, since=launch.index)
        r.metrics, r.added = (m.f if m else None), (a.f if a else None)
        mo = d.wait('MOTION_RESULT', 3, since=launch.index)
        if mo:
            r.motion = mo.f
            r.motion['quality'] = mo.text.split('quality=')[1].split(' points=')[0]
            issue = d.wait('MOTION_ISSUE', 0.3, since=mo.index)
            r.issue = issue.f if issue else None
            recap = d.wait('RECAP_PRESENT', 2.0, since=mo.index)
            r.recap = recap.f if recap else None
    r.states = [l.text for l in d.since(start, 'RPM_STATE')]
    rig.scenario['pulls'].append(r.__dict__)
    print('   ', r.summary())
    return r


# --------------------------------------------------------------------------- 6
def settings_saves(rig):
    """Settings Apply while idle, resting, during/just after a pull, with vetoes and NVS faults."""
    settle(rig)
    d = rig.dev
    brightness = [50, 40]
    def apply(label, value=None, severity='fail', expect_saved=True):
        v = value if value is not None else brightness[len(rig.scenario['checks']) % 2]
        line = d.hil(f'set 2 {v}', expect='HIL_SET', timeout=6)
        f = line.f
        saved = f.get('error') == '0'
        rig.check(f'{label}: Apply returns promptly', number(f.get('ms'), 99999) < 3000, f"{f.get('ms')} ms")
        if expect_saved:
            rig.check(f'{label}: saved', saved, f"error={f.get('error')} page={f.get('page')}", severity=severity)
        d.hil('home')
        return saved

    apply('idle')
    # Immediately after a pull (recap shown, capture pending, rearm second).
    for after_ms in (0, 300, 1200, 2500, 6000):
        signals.program_optical(d, signals.standard_pull(6000))
        signals.program_imu(d, signals.launch_motion(seed=90))
        start = d.mark()
        d.hil('pull 60', expect='HIL_PULL')
        r = collect(rig, signals.standard_pull(6000, label=f'pull before Apply +{after_ms} ms'), start)
        time.sleep(after_ms / 1000)
        saved = apply(f'Apply {after_ms} ms after pull end', severity='warn')
        if not saved:
            # A user would press Apply again: it must succeed within a few seconds.
            ok = False
            for attempt in range(8):
                time.sleep(1)
                if apply(f'retry {attempt + 1} after +{after_ms} ms', expect_saved=False):
                    ok = True
                    break
            rig.check(f'Apply +{after_ms} ms: succeeds on retry', ok)
    # During a pull (MEASURING): rejected, never hung, never mid-burst write.
    signals.program_optical(d, signals.standard_pull(7000, ramp_ms=700, hold_ms=300))
    d.hil('imu const')
    start = d.mark()
    d.hil('pull 30', expect='HIL_PULL')
    time.sleep(0.3)
    apply('Apply during MEASURING', expect_saved=False)
    r = collect(rig, signals.standard_pull(7000, label='pull with Apply mid-burst'), start)
    rig.check('pull with Apply mid-burst: rpm reported', r.valid, f'rpm={r.rpm:.0f}')
    time.sleep(3)
    # IMU gate held beyond the 250 ms quiet wait: vetoed, then retry succeeds.
    d.hil('gate 700')
    time.sleep(0.05)
    vetoed = not apply('Apply with IMU gate held 700 ms', expect_saved=False)
    rig.check('gate held: checkpoint vetoed (no write with a transfer in flight)', vetoed, severity='warn')
    time.sleep(1)
    apply('Apply after gate released')
    # NVS write failure: Apply still returns at once (applied in RAM), the
    # failed write is counted, and the queued record is retried until saved.
    before = d.status().get('SETTINGS_SAVE_STATUS', {})
    d.hil('nvs fail 1 bright_pct')
    apply('Apply with NVS failure', value=70, expect_saved=False)
    deadline = time.time() + 12
    after = before
    while time.time() < deadline:
        after = d.status().get('SETTINGS_SAVE_STATUS', {})
        retried = number(after.get('errors'), 0) > number(before.get('errors'), 0)
        if retried and number(after.get('saves'), 0) > number(before.get('saves'), 0) and after.get('pending') == '0x000':
            break
        time.sleep(1)
    rig.check('NVS failure counted as a save error', number(after.get('errors'), 0) > number(before.get('errors'), 0),
              f"errors {before.get('errors')} -> {after.get('errors')}")
    rig.check('failed setting retried and saved', after.get('pending') == '0x000' and after.get('failed') == '0'
              and number(after.get('saves'), 0) > number(before.get('saves'), 0),
              f"pending={after.get('pending')} failed={after.get('failed')} saves {before.get('saves')} -> {after.get('saves')}")
    apply('Apply after NVS failure')
    # Slow flash write (600 ms): completes, IMU quiet window covers it.
    d.hil('nvs slow 600 1 bright_pct')
    apply('Apply with 600 ms flash write')
    # Resting (sensor idle pause): Apply must resume or save, not hang.
    d.hil('usbidle 1')
    deadline = time.time() + 75
    resting = None
    while time.time() < deadline:
        resting = d.wait(lambda l: l.text.startswith('SENSOR_IDLE_CHANGE pause=1 ok=1'), 5)
        if resting:
            break
    rig.check('sensor idle pause reached with USB idle override', resting is not None, severity='warn')
    if resting:
        apply('Apply while resting')
        r = rig.pull(signals.standard_pull(6500, label='pull after resting Apply'))
        rig.check('pull after resting Apply: rpm reported', r.valid)
    d.hil('usbidle 0')
    apply('restore brightness 40', value=40)
    drain_history(rig)


# --------------------------------------------------------------------------- 7
def corpus_replay(rig, corpus, cases=None, margin_s=2.0):
    """Recorded optical waveforms from the private replay corpus, at true speed."""
    import json
    from pathlib import Path
    root = Path(corpus)
    manifest = json.loads((root / 'manifest.json').read_text())
    settle(rig)
    d = rig.dev
    seen = set()
    for case in manifest['cases']:
        if case['sample_file'] in seen or (cases and case['id'] not in cases):
            continue
        if case.get('estimator') != 'three_turn_peak' or case.get('classification') != 'confirmed':
            continue
        events = case['oracle'].get('events') or []
        if not events:
            continue
        seen.add(case['sample_file'])
        profile = PROFILES[case['profile']]
        d.hil(f'set 4 {profile}', expect='HIL_SET', timeout=6)
        d.hil('home')
        for n, event in enumerate(events):
            a, b = event['time_us']
            t0, t1 = max(0, a / 1e6 - margin_s), b / 1e6 + margin_s
            clip = signals.load_clip(root / case['sample_file'], t0, t1)
            base = sorted(clip[:5000])[2500]
            d.hil(f'idle {base} 2')
            d.upload_optical(clip)
            d.hil('opt buf')
            d.hil('imu const')
            start = d.mark()
            d.hil('pull 30', expect='HIL_PULL')
            label = f"{case['sample_file'].split('/')[1]} event {n + 1}"
            launch = d.wait('LAUNCH_RESULT', (t1 - t0) + 3, since=start)
            ok = launch is not None and launch.f.get('valid') == '1'
            rig.check(f'{label}: launch measured', ok, launch.text if launch else 'none')
            d.hil('opt idle')
            time.sleep(2)
    d.hil('set 4 0', expect='HIL_SET', timeout=6)
    d.hil('home')
    drain_history(rig)


# --------------------------------------------------------------------------- 8
def soak(rig, minutes=10, seed=7, switch_themes=False, milestone_mode=False):
    """Random pulls, gaps, faults and settings for a long run: no crash, no lost pull.

    With switch_themes, the theme also changes between pulls (USB `T` select),
    among the built-ins and the custom themes already on the device, and the
    device must show each one. With milestone_mode, Celebrate is Both with the
    default table, pulls reach up to 16k RPM, and every pull's celebration
    must follow the rule; the defaults are restored at the end.
    """
    rng = random.Random(seed)
    settle(rig)
    d = rig.dev
    choices = []
    if switch_themes:
        header, slots = theme_list(rig)
        customs = [themes.BUILT_INS + i for i, slot in sorted(slots.items()) if slot['state'] == 'valid']
        rig.check('soak themes: custom themes on the device', len(customs) >= 3, str(customs))
        choices = customs * 3 + [0, 3, 8]
    switches = 0
    if milestone_mode:
        f, text = milestone_request(rig, milestones.request_line('set', 'both', milestones.DEFAULTS))
        rig.check('soak milestones: Both with the default table', text.startswith('MILESTONE_OK'), text)
    celebrations = {}
    end = time.time() + minutes * 60
    pulls = recorded = valid = 0
    qualities = {}
    first = rig.counters()
    while time.time() < end:
        action = rng.random()
        if action < 0.65:
            rpm = rng.uniform(3000, 16000 if milestone_mode else 12500)
            hard = rng.random() < 0.3
            motion = signals.launch_motion(tilt_rate_dps=rng.uniform(150, 3000 if hard else 900),
                                           pull_g=rng.uniform(2, 9), stop_g=rng.uniform(8, 20) if hard else None,
                                           seed=rng.randrange(1 << 30))
            optical = signals.standard_pull(rpm, depth=rng.choice((1000, 900, 600, 300)), noise=rng.uniform(2, 15))
            if rng.random() < 0.15:
                d.hil(rng.choice(['fault datafail 1', 'fault stall 30 1', 'fault overflow 1', 'fault lenfail 2']))
            mark = d.mark()
            r = rig.pull(optical, motion)
            pulls += 1
            if milestone_mode and r.added is not None:
                line = d.wait('CELEBRATION', 2, since=mark)
                if line is None:
                    rig.check(f'soak pull {pulls}: celebration decided', False, r.summary())
                else:
                    f = line.f
                    # Best effect is the device's own (Random may pick any): only the milestone part is checked.
                    show, milestone, effect = milestones.decide('milestones', milestones.DEFAULTS, f['valid'] == '1',
                                                                int(f['shown_rpm']), False, 'Off')
                    if milestone:
                        ok = f['milestone'] == str(milestone) and f['effect'] == effect
                    else:
                        ok = f['milestone'] == '0' and f['celebrate'] == f['best']
                    rig.check(f"soak pull {pulls} ({f['shown_rpm']} rpm): celebration", ok, line.text)
                    key = f['effect'] if f['celebrate'] == '1' else 'none'
                    celebrations[key] = celebrations.get(key, 0) + 1
            recorded += r.added is not None
            valid += r.valid
            if not r.valid or r.added is None:
                rig.check(f'soak pull {pulls} ({rpm:.0f} rpm): recorded', False, r.summary())
            elif abs(r.rpm - rpm) / rpm > 0.03:
                rig.check(f'soak pull {pulls}: rpm within 3%', False, r.summary(), severity='warn')
            q = (r.motion or {}).get('quality')
            qualities[q] = qualities.get(q, 0) + 1
            if q not in ('valid', None):
                rig.check(f'soak pull {pulls}: tilt replay', False, f'quality={q} ({r.summary()})', severity='warn')
            if q == 'Motion out of range':
                rig.check(f'soak pull {pulls}: never out of range', False, r.summary())
        elif action < 0.75:
            line = d.hil(f'set 2 {rng.choice((30, 40, 50))}', expect='HIL_SET', timeout=6)
            rig.check('soak Apply returns promptly', number(line.f.get('ms'), 1e9) < 3000, line.text, severity='warn')
            d.hil('home')
        elif action < 0.82:
            d.hil(f'gate {rng.choice((100, 300, 800))}')
        elif choices and action < 0.92:
            theme = rng.choice(choices)
            f, text = theme_request(rig, themes.request_line('select', theme))
            switches += 1
            if not text.startswith('THEME_OK'):
                rig.check(f'soak theme switch {switches} to {theme}', False, text)
            elif switches % 4 == 0:
                header, _ = theme_list(rig)
                rig.check(f'soak theme switch {switches}: showing {theme}', header.get('showing') == str(theme), str(header))
        time.sleep(rng.choice((0.5, 1, 2, 4, 8, 11, 15)))
    last = rig.counters()
    rig.check('soak: every pull recorded', recorded == pulls, f'{recorded}/{pulls} recorded, {valid} valid')
    heap0 = counter(first, 'HEAP_STATUS', 'internal_min_free')
    heap1 = counter(last, 'HEAP_STATUS', 'internal_min_free')
    rig.check('soak: internal heap low-water stable', heap1 > heap0 - 4096, f'{heap0} -> {heap1}', severity='warn')
    saves = [l.text for l in d.since(rig.scenario['start_line'], 'HISTORY_SAVE')]
    rig.check('soak: every history save ok', all(' ok=1' in s for s in saves), f'{len(saves)} saves')
    rig.note(f'motion qualities: {qualities}')
    rig.note(f"PERF {last.get('PERF')}")
    if milestone_mode:
        rig.note(f'celebrations: {celebrations}')
        rig.check('soak milestones: several milestones celebrated', sum(v for k, v in celebrations.items() if k != 'none') >= 5, str(celebrations))
        f, text = milestone_request(rig, milestones.request_line('reset'))
        rig.check('soak milestones: defaults restored', text.startswith('MILESTONE_OK'), text)
    if choices:
        rig.check('soak themes: switched during the soak', switches >= 5, f'{switches} switches')
        theme_request(rig, themes.request_line('select', choices[0]))
        rig.note(f'theme switches: {switches}; left on theme {choices[0]}')
    drain_history(rig)


def aligned(rig, label, seed=0):
    """One pull with a 383 dps scripted motion; True when its capture holds that motion."""
    motion = signals.launch_motion(seed=seed)
    r = rig.pull(signals.standard_pull(7000, label=label), motion)
    got = number((r.motion or {}).get('peak_gyro_dps'), 0)
    ok = r.motion is not None and abs(got - motion.peak_gyro_dps) <= 0.25 * motion.peak_gyro_dps
    rig.check(f'{label}: tilt replay aligned with the pull', ok,
              f"peak gyro {got:.0f} vs {motion.peak_gyro_dps:.0f} dps, quality={(r.motion or {}).get('quality')}")
    return ok


def timeline_window(rig, seconds):
    a = rig.counters()
    time.sleep(seconds)
    b = rig.counters()
    anchors = counter(b, 'IMU_FIFO_STATUS', 'anchors') - counter(a, 'IMU_FIFO_STATUS', 'anchors')
    return anchors, b.get('IMU_FIFO_STATUS', {}).get('period_us')


def imu_timeline(rig):
    """FIFO timestamp timeline under sensor-clock error and its recovery.

    The timeline stamps gyro frames from the FIFO length-read time with a
    phase-locked period. Captures are cut by host time, so a timeline that
    drifts away from the host clock silently captures the wrong motion.
    """
    settle(rig)
    d = rig.dev
    anchors, period = timeline_window(rig, 4)
    rig.check('nominal clock: no re-anchoring', anchors == 0, f'anchors +{anchors} period_us={period}')
    aligned(rig, 'nominal clock')
    for ppm in (10000, 20000, 30000, -10000, -20000, -30000):
        rig.idle_sources()  # resets faults; the IMU source stays virtual and continuous
        time.sleep(3)
        d.hil(f'fault drift {ppm}')
        time.sleep(15)
        anchors, period = timeline_window(rig, 5)
        rig.check(f'steady {ppm / 1e4:+.0f}% clock: locked (no re-anchoring)', anchors <= 1, f'anchors +{anchors} period_us={period}')
        aligned(rig, f'steady {ppm / 1e4:+.0f}% clock', seed=ppm % 97)
        d.hil('fault drift 0')
        time.sleep(10)
        anchors, period = timeline_window(rig, 5)
        rig.check(f'after {ppm / 1e4:+.0f}% clock returns to nominal: re-locks within 10 s', anchors <= 1,
                  f'anchors +{anchors} in 5 s, period_us={period}')
        aligned(rig, f'after {ppm / 1e4:+.0f}% clock returns to nominal', seed=ppm % 89)
        # A FIFO discard resets the timeline; use one so the next case starts clean.
        d.hil('fault overflow 1')
        time.sleep(2)
    for jitter in (2000, 4000):
        d.hil(f'fault jitter {jitter}')
        time.sleep(8)
        anchors, period = timeline_window(rig, 5)
        rig.check(f'length-read latency jitter {jitter} us: no re-anchoring', anchors <= 1, f'anchors +{anchors} period_us={period}')
        aligned(rig, f'length-read latency jitter {jitter} us')
        d.hil('fault jitter 0')
        d.hil('fault overflow 1')
        time.sleep(2)
    drain_history(rig)


def rearm(rig, seconds=(0.8, 1.2, 1.6, 2.0, 2.5, 3.0)):
    """A second pull starting `s` seconds after the first pull's marks stop.

    The detector needs one quiet second after a burst (REARM) before READY.
    Each pair is one generator program, so the spacing is exact. Then the same
    with a settings Apply 0.3 s after the first result (its checkpoint restarts
    the quiet second).
    """
    settle(rig)
    d = rig.dev
    for apply in (False, True):
        for gap in seconds:
            a = signals.standard_pull(6000, label='first')
            b = signals.standard_pull(9000, label='second')
            start_b = a.duration_ms + gap * 1000
            program = signals.sequence([(0, a), (start_b, b)])
            signals.program_optical(d, program)
            d.hil('imu const')
            start = d.mark()
            d.hil('pull 60', expect='HIL_PULL')
            first = d.wait('LAUNCH_RESULT', 4, since=start)
            if apply and first:
                time.sleep(0.3)
                d.hil('set 2 50', expect='HIL_SET', timeout=6)
                d.hil('home')
            second = d.wait(lambda l: l.text.startswith('LAUNCH_RESULT') and l.index != (first.index if first else -1),
                            start_b / 1000 + 4, since=start)
            label = f"second pull {gap:.1f} s after the first's marks stop" + (' (Apply between)' if apply else '')
            ok = second is not None and second.f.get('valid') == '1' and abs(number(second.f.get('peak_rpm'), 0) - 9000) < 300
            ext = [l.f.get('rearm_extensions') for l in d.since(start, 'RPM_STATUS')][-1:]
            rig.check(f'{label}: measured', ok, (second.text[:90] if second else 'no LAUNCH_RESULT') + f' rearm_extensions={ext}',
                      severity='warn' if gap < 1.5 else 'fail')
            time.sleep(4)
    d.hil('set 2 40', expect='HIL_SET', timeout=6)
    d.hil('home')
    drain_history(rig)


# --------------------------------------------------------------------------- stray bytes
# Lines only a state-changing USB letter prints (usb_command_guard.h): power
# off, sleep, a new session, settings/GPIO/5 V, IMU/storage tests, the recap
# demo, exports and screen dumps. K (backup), R (restore, validated) and the
# status letters p/P/C are allowed to run inside a burst.
STRAY_FORBIDDEN = ('POWER_KEY_TEST', 'POWER_SLEEP_TEST', 'POWER_OFF', 'SHAKE_SLEEP', 'HISTORY_SESSION',
                   'SETTING_APPLY', 'COMMAND_ERROR gpio', 'EXT_POWER', 'SHAKE_WAKE_CHECK', 'IMU_QUIET_TEST',
                   'HISTORY_CHECK', 'MOTION_CHECK', 'RECAP_DEMO', 'HISTORY_DEMO', 'HISTORY_EXPORT',
                   'MOTION_EXPORT', 'FLIGHT_EXPORT', 'RAW_BEGIN', 'SNAP ', 'THEME_')


# Each line transfer and the line that opens it (stray_bytes refuses a line in each).
REFUSED_TRANSFERS = (('R', 'RESTORE'), ('T\n', 'THEME'), ('U\n', 'MILESTONE'))
REFUSED_TRANSFERS += (('X\n', 'FX'),)


def stray_bytes(rig, bursts=12, size=2048, seed=17):
    """Random USB bytes with runs of the dangerous letters inside bursts.

    Bytes come as one write per burst (USB speed) with 100-300 ms pauses, like
    an upload gone wrong or a buggy updater. '!' is left out: HIL lines are a
    test-only parser. Asserts no power-off, sleep, setting, session, page or
    export action, and that the device stays up and answers.
    """
    settle(rig)
    d = rig.dev
    before = rig.counters()
    rng = random.Random(seed)
    letters = 'OSxgiaodhceQEqbnutyzvwjsLT'
    start = d.mark()
    for index in range(bursts):
        # Some bursts start or end on a dangerous letter; inside, runs of them,
        # and long-printing letters (P, K, p, C) right before one: a slow pass
        # once made the next byte look quiet (a stray 'n' after 'P').
        burst = bytearray(rng.choice(letters).encode() if index % 3 == 1 else b'')
        burst.append(0x80 | rng.randrange(128))
        while len(burst) < size:
            r = rng.random()
            if r < 0.01:
                burst += b'OOSSxxnnggaa\n' + rng.choice(letters).encode() + b'\n'
            elif r < 0.02:
                burst += rng.choice('PKpC').encode() + rng.choice(letters).encode() + b'\n'
            else:
                value = rng.randrange(256)
                burst.append(value if value != ord('!') else 0)
        if index % 3 == 2:
            burst += b'\n' + rng.choice(letters).encode()
        d.write(bytes(burst))
        time.sleep(rng.uniform(0.1, 0.3))
    d.write(b'\n')
    time.sleep(1.5)
    after = rig.counters()
    acted = [l.text for l in d.since(start) if l.text.startswith(STRAY_FORBIDDEN)]
    rig.check('no state-changing command ran', not acted, str(acted[:6]))
    for tag, key in (('SETTINGS_SAVE_STATUS', 'saves'), ('SETTINGS_SAVE_STATUS', 'pending'),
                     ('HISTORY_STATUS', 'session'), ('HISTORY_STATUS', 'records'), ('UI_STATUS', 'page'),
                     ('RPM_STATUS', 'launches')):
        b, a = before.get(tag, {}).get(key), after.get(tag, {}).get(key)
        rig.check(f'{tag} {key} unchanged', b == a, f'{b} -> {a}')
    rejected = [l.text for l in d.since(start, 'PERF')][-1:]
    rig.note(f'PERF after: {rejected}')
    # The rest of a line a transfer refused runs nothing, K and R included: an
    # R in a backup line 0.18 refused started another restore, which took the
    # commands after it. '*' is refused as a bad character.
    opened = ('RESTORE_READY', 'THEME_READY', 'MILESTONE_READY', 'FX_READY', 'BACKUP_BEGIN')
    for opener, tag in REFUSED_TRANSFERS:
        for rest in ('KR', 'X'):
            time.sleep(0.05)
            mark = d.mark()
            d.write(opener)
            if d.wait(lambda l: l.text.startswith(opened), 5, since=mark) is None:
                rig.check(f'{tag}: opens', False)
                continue
            mark = d.mark()
            d.write(f'QUJD*{rest}\n')
            error = d.wait(f'{tag}_ERROR', 5, since=mark)
            time.sleep(0.5)
            ran = [l.text for l in d.since(mark) if l.text.startswith(opened)]
            rig.check(f'{tag}: the rest of a refused line ("{rest}") runs nothing', error is not None and not ran,
                      (error.text if error else None, ran[:2]))
    time.sleep(0.05)
    mark = d.mark()
    d.write('L\n')
    rig.check('a command after the refused lines runs', d.wait('THEME_LIST_END', 5, since=mark) is not None)


# ---- custom themes (USB L / T, theme_library.h)
HIL_THEME = ('HIL Navy', (0x0006, 0xffff, 0x5aeb, 0x2d7f, 0xfea0, 0xfda0, 0x07e0, 0xf800))
# Paper from fixtures/theme_vectors.json: a white background, every role readable.
LIGHT_THEME = ('HIL Paper', (0xffff, 0x0000, 0x738e, 0x001f, 0x0000, 0x9a00, 0x0400, 0xc000))


def theme_list(rig):
    d = rig.dev
    time.sleep(0.05)  # a guarded command starts after input quiet
    start = d.mark()
    d.write('L\n')
    end = d.wait('THEME_LIST_END', 5, since=start)
    if end is None:
        raise DeviceError('no THEME_LIST_END')
    return themes.parse_list(l.text for l in d.since(start))


def theme_request(rig, line):
    """One framed `T` request; returns the THEME_OK / THEME_ERROR fields and text."""
    d = rig.dev
    time.sleep(0.05)
    start = d.mark()
    d.write('T\n')
    if not d.wait('THEME_READY', 5, since=start):
        raise DeviceError('theme request not ready')
    mark = d.mark()
    d.write(line + '\n')
    reply = d.wait(lambda l: l.text.startswith(('THEME_CHUNK', 'THEME_ERROR')), 5, since=mark)
    if reply is None:
        raise DeviceError('no THEME_CHUNK')
    if reply.text.startswith('THEME_CHUNK'):
        mark = d.mark()
        d.write('.\n')
        reply = d.wait(lambda l: l.text.startswith(('THEME_OK', 'THEME_ERROR')), 8, since=mark)
        if reply is None:
            raise DeviceError('no THEME_OK/THEME_ERROR')
    return reply.f, reply.text


def main_screen(rig, name):
    """The main page's frame: background and RPM readout colors (RGB565)."""
    rig.dev.hil('home')
    time.sleep(0.4)
    snap = rig.dev.snapshot()
    if snap is None:
        raise DeviceError('no screen snapshot')
    rig.screenshot(name)
    def pixel(x, y):
        i = (y * snap.width + x) * 2
        return snap.data[i] << 8 | snap.data[i + 1]
    background = pixel(2, 120)
    readout = {pixel(x, y) for y in range(50, 95) for x in range(10, 125)} - {background}
    return background, readout


def reboot(rig):
    """A deliberate restart: wait for the new boot, then track it as the current one."""
    d = rig.dev
    start = d.mark()
    before = rig.boot_id
    d.hil('reboot', expect='HIL_REBOOT')
    boot = d.wait('HIL_BOOT', 5, since=start)
    if boot is None:
        # A reset can leave the USB link silent (2026-10-22): reopen the port and
        # ask for the new boot's id instead of waiting for its lost HIL_BOOT.
        reopened = d.reopen()
        status = rig.hil_boot() if reopened else {}
        rig.note(f'reboot: no HIL_BOOT in 5 s, port reopened={reopened} boot_id={status.get("boot_id")}')
        boot = status if status.get('boot_id') not in (None, before) else None
    rig.check('rebooted', boot is not None)
    # A HIL build reads the real (on the rig, floating) input from boot until the
    # host idles its sources: do that at once, or noise there can make a launch
    # (0.20.2 gate: 9,901 rpm, amplitude 134, 1.5 s after boot, shown as a recap).
    rig.idle_sources()
    time.sleep(1.5)
    rig.boot_id = rig.hil_boot().get('boot_id')
    rig.scenario['start_line'] = d.mark()
    settle(rig)
    return [l.text for l in d.since(start) if l.text.startswith('THEME_NOTICE')]


def effect_preview(rig, digit):
    """USB E<digit>: that effect on the latest pull's recap; the EFFECT_PREVIEW(_ERROR) line."""
    time.sleep(0.1)
    start = rig.dev.mark()
    rig.dev.write(f'E{digit}\n')
    return rig.dev.wait('EFFECT_PREVIEW', 5, since=start)


def custom_themes(rig):
    """Upload, list, select, reboot, delete and corrupt-record fallback of custom themes;
    low-contrast and light palettes accepted with warnings, a light one drawn under celebrations.

    Clears the device's custom themes: take a `K` backup first and restore it after.
    """
    settle(rig)
    name, roles = HIL_THEME
    theme_id = themes.BUILT_INS + 1
    f, text = theme_request(rig, themes.request_line('clear'))
    rig.check('clear: ok', text.startswith('THEME_OK') and f.get('op') == 'clear', text)
    black, readout = main_screen(rig, 'themes_0_default')
    rig.check('default: black background', black == 0x0000, f'{black:04x}')
    # Upload: a valid theme lands in its slot; checkpointed NVS write.
    start = rig.dev.mark()
    f, text = theme_request(rig, themes.request_line('put', 1, name, roles))
    rig.check('put: ok and valid', f.get('op') == 'put' and f.get('state') == 'valid', text)
    rig.check('put: one NVS write of the themes key', any('key=themes' in l.text for l in rig.dev.since(start, 'HIL_NVS')))
    header, slots = theme_list(rig)
    rig.check('list: slot 1 holds the theme', slots.get(1, {}).get('state') == 'valid' and slots[1]['name'] == name,
              str(slots.get(1)))
    rig.check('list: other slots empty', all(slots.get(i, {}).get('state') == 'empty' for i in (0, 2, 3)))
    rig.check('list: any colors accepted (colors=any)', header.get('colors') == 'any', str(header))
    # Refused uploads change nothing: only a malformed name or request is refused.
    f, text = theme_request(rig, themes.request_line('put', 2, ' Lead', roles))
    rig.check('name with a leading space refused', f.get('reason') == 'name', text)
    corrupt = bytearray(themes.request('put', 2, name, roles))
    corrupt[20] ^= 0x01
    f, text = theme_request(rig, base64.b64encode(bytes(corrupt)).decode())
    rig.check('corrupt upload refused', f.get('reason') == 'invalid_request', text)
    f, text = theme_request(rig, themes.request_line('select', themes.BUILT_INS + 2))
    rig.check('selecting an empty slot refused', f.get('reason') == 'unavailable', text)
    header, slots = theme_list(rig)
    rig.check('refusals left the library unchanged', slots.get(2, {}).get('state') == 'empty' and header.get('revision') == '1',
              str(header))
    # Low contrast is a warning, not a refusal.
    for grey, warning in ((0x4a49, 'low_contrast:text'), (0x2104, 'invisible:text')):
        unreadable = list(roles)
        unreadable[1] = grey  # grey text on navy
        f, text = theme_request(rig, themes.request_line('put', 2, 'Low Ink', unreadable))
        rig.check(f'{warning} accepted with its warning', f.get('state') == 'valid'
                  and f.get('warnings') == themes.warnings(unreadable) == warning, text)
    f, text = theme_request(rig, themes.request_line('delete', 2))
    rig.check('low-contrast theme deleted', f.get('state') == 'empty', text)
    # Select: the screen uses the theme at once and after a reboot.
    f, text = theme_request(rig, themes.request_line('select', theme_id))
    rig.check('select: ok', text.startswith('THEME_OK'), text)
    time.sleep(1.5)
    rig.check('select: saved', rig.dev.status().get('SETTINGS_SAVE_STATUS', {}).get('pending') == '0x000')
    background, readout = main_screen(rig, 'themes_1_custom')
    rig.check('custom: navy background', background == roles[0], f'{background:04x}')
    rig.check('custom: RPM in the number color', roles[4] in readout, str(sorted(f'{v:04x}' for v in readout)[:8]))
    notices = reboot(rig)
    rig.check('reboot: no theme notice', not notices, str(notices))
    header, slots = theme_list(rig)
    rig.check('reboot: selection and slot kept', header.get('selected') == str(theme_id) and header.get('showing') == str(theme_id)
              and slots.get(1, {}).get('state') == 'valid', str(header))
    background, readout = main_screen(rig, 'themes_2_after_reboot')
    rig.check('reboot: custom colors shown', background == roles[0] and roles[4] in readout, f'{background:04x}')
    # Settings Apply refuses a theme that is not there.
    line = rig.dev.hil(f'set 5 {themes.BUILT_INS + 3}', expect='HIL_SET')
    rig.check('Settings Apply refuses an empty slot', line.f.get('error') == '1', line.text)
    rig.dev.hil('home')
    # Delete the selected theme: the default returns and is saved.
    f, text = theme_request(rig, themes.request_line('delete', 1))
    rig.check('delete: ok', f.get('op') == 'delete' and f.get('state') == 'empty', text)
    time.sleep(1.5)
    header, _ = theme_list(rig)
    rig.check('delete: selection returns to the default', header.get('selected') == '0' and header.get('showing') == '0', str(header))
    background, _ = main_screen(rig, 'themes_3_deleted')
    rig.check('delete: black background', background == 0x0000, f'{background:04x}')
    # A damaged stored record: the default shows, the record is kept until cleared.
    theme_request(rig, themes.request_line('put', 1, name, roles))
    theme_request(rig, themes.request_line('select', theme_id))
    time.sleep(1.5)
    rig.check('corrupt record written', rig.dev.hil('themecorrupt', expect='HIL_OK').f.get('ok') == '1')
    notices = reboot(rig)
    rig.check('corrupt record: boot notice', any('invalid_record_retained' in n for n in notices), str(notices))
    header, slots = theme_list(rig)
    rig.check('corrupt record: unreadable, default shown', header.get('store') == 'unreadable' and header.get('showing') == '0'
              and header.get('selected') == str(theme_id), str(header))
    background, readout = main_screen(rig, 'themes_4_corrupt_fallback')
    rig.check('corrupt record: black background, white RPM', background == 0x0000 and 0xffff in readout, f'{background:04x}')
    f, text = theme_request(rig, themes.request_line('put', 0, name, roles))
    rig.check('corrupt record: uploads refused until cleared', f.get('reason') == 'store_unreadable', text)
    f, text = theme_request(rig, themes.request_line('clear'))
    rig.check('corrupt record: clear ok', text.startswith('THEME_OK'), text)
    f, text = theme_request(rig, themes.request_line('put', 0, name, roles))
    rig.check('after clear: upload ok', f.get('state') == 'valid', text)
    # A light palette: accepted with its warning, selected, drawn on Main and
    # under a celebration on the recap.
    light, light_roles = LIGHT_THEME
    f, text = theme_request(rig, themes.request_line('put', 2, light, light_roles))
    rig.check('light palette accepted with a light_background warning', f.get('state') == 'valid'
              and f.get('warnings') == themes.warnings(light_roles) == 'light_background', text)
    f, text = theme_request(rig, themes.request_line('select', themes.BUILT_INS + 2))
    rig.check('light palette selected', text.startswith('THEME_OK'), text)
    time.sleep(1.5)
    header, slots = theme_list(rig)
    rig.check('light palette in use', header.get('showing') == str(themes.BUILT_INS + 2) and slots.get(2, {}).get('name') == light,
              str(header))
    background, readout = main_screen(rig, 'themes_5_light_main')
    rig.check('light: white background, RPM in the number color', background == light_roles[0] and light_roles[4] in readout,
              f'{background:04x} {sorted(f"{v:04x}" for v in readout)[:8]}')
    for digit, effect in ((0, 'Orbit'), (7, 'Slash')):
        line = effect_preview(rig, digit)
        if line is not None and line.text.startswith('EFFECT_PREVIEW_ERROR'):
            # The reboots above cleared the latest pull: make a recap to celebrate on.
            r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=50))
            rig.check_pull(r, tolerance=0.03)
            time.sleep(2.5)
            line = effect_preview(rig, digit)
        rig.check(f'light: {effect} preview started', line is not None and line.f.get('effect') == effect, line.text if line else 'none')
        time.sleep(0.5 if effect == 'Slash' else 0.35)
        snap = rig.dev.snapshot()
        rig.screenshot(f'themes_6_light_{effect.lower()}')
        colors = {snap.data[i] << 8 | snap.data[i + 1] for i in range(0, len(snap.data), 2)} if snap else set()
        # The recap draws its readout in the accent and its labels in muted.
        rig.check(f'light: {effect} frame keeps the background and the accent RPM readout',
                  light_roles[0] in colors and light_roles[3] in colors, f'{len(colors)} colors: {sorted(f"{c:04x}" for c in colors)[:6]}')
        time.sleep(2.0)
    rig.check('light: device alive after the celebrations', rig.check_alive())
    rig.dev.hil('home')
    # Leave no custom theme selected or stored.
    theme_request(rig, themes.request_line('clear'))
    theme_request(rig, themes.request_line('select', 0))
    time.sleep(1.5)
    header, _ = theme_list(rig)
    rig.check('clean end state', header.get('selected') == '0' and header.get('store') in ('empty', 'ok'), str(header))


# ---- RPM milestones (USB M / U, milestones.h)
def milestone_list(rig):
    d = rig.dev
    time.sleep(0.05)  # a guarded command starts after input quiet
    start = d.mark()
    d.write('M\n')
    if d.wait('MILESTONE_LIST_END', 5, since=start) is None:
        raise DeviceError('no MILESTONE_LIST_END')
    return milestones.parse_list(l.text for l in d.since(start))


def milestone_request(rig, line, paced=False):
    """One framed `U` request; returns the MILESTONE_OK / MILESTONE_ERROR fields and text.
    `paced` sends the line in 64-byte pieces 30 ms apart, as beywatch does: firmware
    up to 0.18.0 reads USB into a 256-byte queue that drops the end of a longer burst."""
    d = rig.dev
    time.sleep(0.05)
    start = d.mark()
    d.write('U\n')
    if not d.wait('MILESTONE_READY', 5, since=start):
        raise DeviceError('milestone request not ready')
    mark = d.mark()
    data = (line + '\n').encode()
    if paced:
        for at in range(0, len(data), 64):
            if at:
                time.sleep(0.03)
            d.write(data[at:at + 64])
    else:
        d.write(data)
    reply = d.wait(lambda l: l.text.startswith(('MILESTONE_CHUNK', 'MILESTONE_ERROR')), 5, since=mark)
    if reply is None:
        raise DeviceError('no MILESTONE_CHUNK')
    if reply.text.startswith('MILESTONE_CHUNK'):
        mark = d.mark()
        d.write('.\n')
        reply = d.wait(lambda l: l.text.startswith(('MILESTONE_OK', 'MILESTONE_ERROR')), 8, since=mark)
        if reply is None:
            raise DeviceError('no MILESTONE_OK/MILESTONE_ERROR')
    return reply.f, reply.text



def request_burst(rig):
    """A v3 milestone request (312 characters) written in one burst arrives whole.
    Tools send each request line in one write; 0.18.0's production RX queue (256
    bytes) lost its end while HIL builds, with a larger queue, passed
    (2026-10-23). The HIL build now has production's USB configuration
    (check_usb_config), so this is what a customer's unit receives. The unit's
    own table is sent back unchanged."""
    settle(rig)
    header, rows = milestone_list(rig)
    table = [(rpm, list(e) if isinstance(e, tuple) else e) for rpm, e in rows]
    line = milestones.request_line('set', header.get('mode', 'best'), table, version=3)
    rig.check('a v3 request is one 312-character line', len(line) == 312, str(len(line)))
    f, text = milestone_request(rig, line)  # the whole line in one write
    after_header, after = milestone_list(rig)
    rig.check('a v3 request sent in one burst is received and saved', text.startswith('MILESTONE_OK') and after == rows,
              f'{text} rows={after}')


def celebrated_pull(rig, rpm, rows, best_effect, screenshot=None, seed=0, expect_pick=None):
    """One pull; checks its CELEBRATION against the rule for the device's own RPM, and that the effect starts."""
    d = rig.dev
    start = d.mark()
    r = rig.pull(signals.standard_pull(rpm), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=40 + seed))
    rig.check_pull(r, tolerance=0.03)
    line = d.wait('CELEBRATION', 2, since=start)
    rig.check(f'{rpm} rpm: celebration decided', line is not None)
    if line is None:
        return None
    f = line.f
    show, milestone, effect = milestones.decide(f['mode'], rows, f['valid'] == '1', int(f['shown_rpm']), f['best'] == '1', best_effect)
    # A pool plays one of its effects: the one the device announced as the row's next pick.
    pooled = isinstance(effect, tuple)
    expected = (expect_pick or effect) if pooled else effect
    rig.check(f"{rpm} rpm ({f['shown_rpm']} shown, {f['mode']}, best={f['best']}): "
              f"{expected if show else 'nothing'}{f' at {milestone}' if milestone else ''}",
              f['celebrate'] == str(int(show)) and int(f['milestone']) == milestone and
              (f['effect'] in effect and (expect_pick is None or f['effect'] == expect_pick) if pooled and show else f['effect'] == (effect if show else 'none')),
              line.text)
    effect = f['effect'] if pooled and show else effect
    started = d.wait(lambda l: l.text.startswith('CELEBRATION_START') and l.f.get('launch') == f['launch'], 3, since=line.index)
    moves = show and effect != 'Off'
    rig.check(f"{rpm} rpm: {'the effect starts' if moves else 'no effect starts'}",
              (started is not None) == show and (not started or started.f.get('effect') == effect), started.text if started else 'none')
    if started and screenshot:
        time.sleep(0.5 if effect == 'Slash' else 0.35)
        rig.screenshot(screenshot)
    # Effects run about 5 s and the recap holds 1 s after; let both finish.
    time.sleep(6.5)
    return f


def rpm_milestones(rig):
    """Celebrate modes and the milestone table: pulls at 8.5k, 9k, 12.3k and 15.5k RPM
    play the right effect or none; Settings changes the mode; upload, refused
    requests, reboot and a damaged record falling back to the defaults.

    Leaves the device on the default mode and table, and sets Best effect to
    Orbit: take a `K` backup first and restore it after.
    """
    settle(rig)
    d = rig.dev
    f, text = milestone_request(rig, milestones.request_line('reset'))
    rig.check('reset to the defaults', text.startswith('MILESTONE_OK'), text)
    header, rows = milestone_list(rig)
    rig.check(f'defaults: {milestones.DEFAULT_MODE}, 9K..15K, Slash on top', header.get('mode') == milestones.DEFAULT_MODE and header.get('default') == '1'
              and rows == milestones.DEFAULTS, str((header, rows)))
    # A fixed Best effect, so a new best's effect is known.
    line = d.hil('set 6 0', expect='HIL_SET', timeout=6)
    rig.check('Best effect Orbit applied', line.f.get('error') == '0', line.text)
    d.hil('home')
    # Milestones only: the table decides, pull by pull.
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', milestones.DEFAULTS))
    rig.check('upload: Milestones with the default table', text.startswith('MILESTONE_OK') and f.get('mode') == 'milestones', text)
    for i, rpm in enumerate((8500, 9000, 12300, 15500)):
        celebrated_pull(rig, rpm, milestones.DEFAULTS, 'Orbit', screenshot=f'milestones_{rpm}' if rpm >= 12000 else None, seed=i)
    # Settings (the device's own Apply path): New best ignores the table, Both combines.
    for mode, value in (('best', 0), ('both', 2)):
        line = d.hil(f'set 7 {value}', expect='HIL_SET', timeout=6)
        rig.check(f'Celebrate {mode} applied on the device', line.f.get('error') == '0', line.text)
        d.hil('home')
        time.sleep(1.5)
        header, _ = milestone_list(rig)
        rig.check(f'Celebrate {mode} saved', header.get('mode') == mode, str(header))
        for i, rpm in enumerate((12300, 15500)):
            celebrated_pull(rig, rpm, milestones.DEFAULTS, 'Orbit', seed=10 + value * 3 + i)
    rig.check('settings saved', d.status().get('SETTINGS_SAVE_STATUS', {}).get('pending') == '0x000')
    # A custom table survives a reboot.
    table = [(8000, 'Orbit'), (12000, 'Lightning'), (15000, 'Slash')]
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', table))
    rig.check('upload: a three-row table', text.startswith('MILESTONE_OK') and f.get('rows') == '3', text)
    drain_history(rig)  # the pulls so far reach flash before the restart
    reboot(rig)
    header, rows = milestone_list(rig)
    rig.check('reboot: mode and table kept', header.get('mode') == 'milestones' and rows == table and header.get('store') == 'ok', str((header, rows)))
    celebrated_pull(rig, 12300, table, 'Orbit', seed=20)
    # Refused requests change nothing.
    revision = header.get('revision')
    bad = milestones.request('set', 'milestones', [(9000, 'Orbit'), (9000, 'Slash')])
    f, text = milestone_request(rig, base64.b64encode(bad).decode())
    rig.check('two rows at one RPM refused', f.get('reason') == 'invalid_table' and f.get('problem') == 'order' and f.get('row') == '1', text)
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', [(999, 'Orbit')]))
    rig.check('999 RPM refused', f.get('problem') == 'range', text)
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', [(9000, 'Random')]))
    rig.check('Random refused as a row effect', f.get('problem') == 'effect', text)
    corrupt = bytearray(milestones.request('set', 'both', table))
    corrupt[14] ^= 1
    f, text = milestone_request(rig, base64.b64encode(bytes(corrupt)).decode())
    rig.check('corrupt request refused', f.get('reason') == 'invalid_request', text)
    header, rows = milestone_list(rig)
    rig.check('refusals left the table unchanged', rows == table and header.get('revision') == revision, str(header))
    # The 0.16 effects play from a milestone like the first six; ids past the
    # firmware's last effect are refused.
    rig.check('the listing reports every effect id', header.get('effects') == str(len(milestones.EFFECTS)), str(header))
    newer = [(9000, 'Tidal'), (12000, 'Eclipse'), (15000, 'Bloom')]
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', newer))
    rig.check('upload: a table of 0.16 effects', text.startswith('MILESTONE_OK'), text)
    for i, rpm in enumerate((9500, 12300, 15500)):
        celebrated_pull(rig, rpm, newer, 'Orbit', seed=30 + i)
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', [(9000, len(milestones.EFFECTS))]))
    rig.check('an effect id past the last refused', f.get('problem') == 'effect', text)
    # An effect pool: each pull at the row plays its announced next pick, never
    # the same effect twice in a row while the pool has another.
    pool = ['Tidal', 'Frost', 'Bloom']
    pooled = [(9000, 'Sparkles'), (12000, pool), (15000, 'Slash')]
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', pooled))
    rig.check('upload: a three-effect pool', text.startswith('MILESTONE_OK'), text)
    header, rows = milestone_list(rig)
    rig.check('the listing reports version 3 and the pool', header.get('version') == '3' and rows[1] == (12000, tuple(pool)), str((header, rows)))
    played = []
    for i in range(4):
        header, _ = milestone_list(rig)
        announced = header['next'][1]
        f = celebrated_pull(rig, 12300, pooled, 'Orbit', seed=40 + i, expect_pick=announced)
        played.append(f['effect'] if f else None)
    rig.check('pool: never the same effect twice in a row', all(a != b for a, b in zip(played, played[1:])) and None not in played, str(played))
    # 0.18: a pool holding effects past value 63 (the 128-bit pool), saved,
    # listed and played; and 0.16's and 0.17's request format still applies.
    wide_pool = ['Volley', 'Starfall', 'Sharkfin']
    wide = [(9000, 'Haymaker'), (12000, wide_pool), (15000, 'Meadow')]
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', wide))
    header, rows = milestone_list(rig)
    rig.check('upload: a pool past value 63', text.startswith('MILESTONE_OK') and rows == [(9000, 'Haymaker'), (12000, tuple(wide_pool)), (15000, 'Meadow')], str((text, rows)))
    for i in range(3):
        header, _ = milestone_list(rig)
        celebrated_pull(rig, 12300, wide, 'Orbit', seed=70 + i, expect_pick=header['next'][1])
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', pooled, version=2))
    header, rows = milestone_list(rig)
    rig.check('a v2 (0.16, 0.17) request still applies', text.startswith('MILESTONE_OK') and rows[1] == (12000, tuple(pool)), str((text, rows)))
    # A custom ladder: twelve rows every 500 RPM from 9,000, a different effect each.
    ladder = [(9000 + 500 * i, milestones.EFFECTS[milestones.DRAWN[i % len(milestones.DRAWN)]]) for i in range(12)]
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', ladder))
    header, rows = milestone_list(rig)
    rig.check('upload: a twelve-row 500 RPM ladder', text.startswith('MILESTONE_OK') and rows == ladder and header.get('max_rows') == '12', str((text, rows)))
    for i, rpm in enumerate((9250, 11800, 14700)):
        celebrated_pull(rig, rpm, ladder, 'Orbit', seed=50 + i)
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', ladder + [(15500, 'Slash')]))
    rig.check('a thirteenth row refused', f.get('problem') == 'count', text)
    # 0.15's request format still works for a table it can hold.
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', table, version=1))
    rig.check('a v1 (0.15) request still applies', text.startswith('MILESTONE_OK'), text)
    header, rows = milestone_list(rig)
    rig.check('three-row table back', rows == table, str(rows))
    header, rows = milestone_list(rig)
    revision = header.get('revision')
    # A damaged record: the defaults show until the next change replaces it.
    drain_history(rig)
    rig.check('record damaged', d.hil('milecorrupt', expect='HIL_OK').f.get('ok') == '1')
    start = d.mark()
    reboot(rig)
    notices = [l.text for l in d.since(start) if l.text.startswith('MILESTONE_NOTICE')]
    rig.check('damaged record: boot notice', any('invalid_record_using_defaults' in n for n in notices), str(notices))
    header, rows = milestone_list(rig)
    rig.check('damaged record: defaults shown', header.get('store') == 'damaged' and header.get('mode') == milestones.DEFAULT_MODE
              and rows == milestones.DEFAULTS, str((header, rows)))
    celebrated_pull(rig, 15500, milestones.DEFAULTS, 'Orbit', seed=21)
    f, text = milestone_request(rig, milestones.request_line('reset'))
    header, rows = milestone_list(rig)
    rig.check('damaged record replaced by a reset', text.startswith('MILESTONE_OK') and header.get('store') == 'ok'
              and header.get('default') == '1', str(header))
    drain_history(rig)
    reboot(rig)
    header, _ = milestone_list(rig)
    rig.check('clean end state', header.get('store') == 'ok' and header.get('mode') == milestones.DEFAULT_MODE, str(header))
    drain_history(rig)


# ---- The effect set (USB M / U op 3, effect_set.h)
def effect_set_state(rig):
    """MILESTONE_LIST's effect set fields: (offered IDs, store, default flag, header)."""
    header, _ = milestone_list(rig)
    return milestones.effect_ids(header.get('effect_set', '0')), header.get('effect_set_store'), header.get('effect_set_default'), header


def cycle(rig, presses):
    """!cycle: the Best effect editor through the real controls; (start, [drafts])."""
    line = rig.dev.hil(f'cycle {presses}', expect='HIL_CYCLE', timeout=6)
    values = line.f.get('values', '')
    return int(line.f.get('start', -1)), [int(v) for v in values.split(',') if v]


def expected_cycle(start, offered, presses):
    out, effect = [], start
    for _ in range(presses):
        effect = milestones.next_choice(effect, offered)
        out.append(effect)
    return out


def check_cycle(rig, label, offered, presses=None):
    presses = presses or len(offered) + 3
    start, values = cycle(rig, presses)
    want = expected_cycle(start, offered, presses)
    allowed = set(offered) | {milestones.EFFECTS.index('Off'), milestones.EFFECTS.index('Random')}
    rig.check(f'{label}: the Best effect editor visits only offered effects, in Settings order',
              values == want and set(values) <= allowed, f'start={start} values={values} expected={want}')
    return start, values


def effect_set(rig):
    """The web-picked effect set: upload over U op 3, the M fields, the Best
    effect editor's cycle through the real controls, Random picks (USB E
    previews and pulls) staying in the set, refused requests, a reboot, a
    damaged record falling back to the default and the reset to it.

    Leaves the default set and Best effect Orbit: take a `K` backup first and restore it after.
    """
    settle(rig)
    d = rig.dev
    names = milestones.EFFECTS
    f, text = milestone_request(rig, milestones.effect_set_line(mask=milestones.effect_mask(milestones.DEFAULT_EFFECT_SET)))
    rig.check('start from the default set', text.startswith('MILESTONE_OK') and f.get('op') == 'set_effects', text)
    offered, store, default, header = effect_set_state(rig)
    rig.check('M reports the default set', offered == milestones.DEFAULT_EFFECT_SET and store == 'ok' and default == '1', str(header))
    check_cycle(rig, 'default set', milestones.DEFAULT_EFFECT_SET)
    # A picked set: Slash, Tidal and two anime effects.
    mine = frozenset(names.index(n) for n in ('Slash', 'Tidal', 'Crimson', 'Ravage'))
    f, text = milestone_request(rig, milestones.effect_set_line(mine))
    rig.check('upload: four effects', text.startswith('MILESTONE_OK') and f.get('op') == 'set_effects' and f.get('effects') == '4', text)
    offered, store, default, header = effect_set_state(rig)
    rig.check('M reports the picked set', offered == mine and store == 'ok' and default == '0', str(header))
    # Random first: the cycle from Random.
    line = d.hil('set 6 6', expect='HIL_SET', timeout=6)
    rig.check('Best effect Random applied', line.f.get('error') == '0', line.text)
    d.hil('home')
    start, _ = check_cycle(rig, 'picked set from Random', mine)
    rig.check('the editor opens on Random', start == names.index('Random'), str(start))
    # A saved effect outside the set stays selected; B continues from its position.
    line = d.hil('set 6 0', expect='HIL_SET', timeout=6)
    rig.check('Best effect Orbit (not offered) still applies', line.f.get('error') == '0', line.text)
    d.hil('home')
    start, values = check_cycle(rig, 'picked set from Orbit', mine, 3)
    rig.check('the editor opens on Orbit and steps to Slash', start == 0 and values[:1] == [names.index('Slash')], f'{start} {values}')
    line = d.hil('set 6 6', expect='HIL_SET', timeout=6)
    d.hil('home')
    # Random's picks stay in the set: USB E previews take the real pick path
    # (celebrationEffect, then the next pick at the first frame).
    r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=70))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(3.0)
    picks = []
    for i in range(16):
        time.sleep(0.1)
        start = d.mark()
        d.write('E\n')
        line = d.wait('EFFECT_PREVIEW', 5, since=start)
        if line is not None:
            picks.append(line.f.get('effect'))
        time.sleep(0.6)
    rig.check('Random previews: every pick offered', len(picks) == 16 and all(p in names and names.index(p) in mine for p in picks), str(picks))
    rig.check('Random previews: never the same twice running', all(a != b for a, b in zip(picks, picks[1:])), str(picks))
    rig.check('Random previews: the whole set over 16 picks', {names.index(p) for p in picks if p in names} == mine, str(picks), severity='warn')
    time.sleep(3.0)
    # New best only: under Both (the default) these pulls would play the 15K milestone instead.
    f, text = milestone_request(rig, milestones.request_line('set', 'best', milestones.DEFAULTS))
    rig.check('Celebrate New best for the best pulls', text.startswith('MILESTONE_OK') and f.get('mode') == 'best', text)
    # Pulls: each new best plays an offered effect. The bests start cleared, as
    # on a new unit, so the first pull seeds them and every later one beats them
    # (the 0.20.1 gate's rig held a 29,000 rpm best from a sweep: no new best).
    line = d.hil('bests clear', expect='HIL_OK bests')
    rig.check('bests cleared', line is not None and line.f.get('standard_three') == '0.0' and line.f.get('standard_single') == '0.0', line.text if line else 'none')
    bests = []
    for i, rpm in enumerate((16000, 17500, 19000, 20500, 22000, 23500)):
        start = d.mark()
        r = rig.pull(signals.standard_pull(rpm), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=72 + i))
        rig.check_pull(r, tolerance=0.03)
        line = d.wait('CELEBRATION', 2, since=start)
        if line is not None and line.f.get('best') == '1':
            bests.append(line.f.get('effect'))
        time.sleep(3.0)
    rig.check('pulls: every new best plays an offered effect', all(b in names and names.index(b) in mine for b in bests), str(bests))
    rig.check('pulls: every pull after the first is a new best', len(bests) == 5, str(bests))
    # A milestone may still play an effect outside the set.
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', [(9000, 'Bloom')]))
    rig.check('a milestone row may name an effect outside the set', text.startswith('MILESTONE_OK'), text)
    f, text = milestone_request(rig, milestones.request_line('reset'))
    # Refused requests change nothing.
    for label, line, want in (('an empty set', milestones.effect_set_line(mask=0), ('invalid_effects', 'empty', None)),
                              ('Off', milestones.effect_set_line(mine | {names.index('Off')}), ('invalid_effects', 'effect', '5')),
                              ('Random', milestones.effect_set_line(mask=1 << names.index('Random')), ('invalid_effects', 'effect', '6')),
                              ('an id past the last', milestones.effect_set_line(mask=1 << len(names)), ('invalid_effects', 'effect', str(len(names))))):
        f, text = milestone_request(rig, line)
        rig.check(f'{label} refused', f.get('reason') == want[0] and f.get('problem') == want[1] and (want[2] is None or f.get('effect') == want[2]), text)
    corrupt = bytearray(milestones.effect_set_request(mine))
    corrupt[9] ^= 1
    f, text = milestone_request(rig, base64.b64encode(bytes(corrupt)).decode())
    rig.check('corrupt request refused', f.get('reason') == 'invalid_request', text)
    offered, store, default, header = effect_set_state(rig)
    rig.check('refusals left the set unchanged', offered == mine and store == 'ok', str(header))
    # Reboot: the set is kept.
    drain_history(rig)
    reboot(rig)
    offered, store, default, header = effect_set_state(rig)
    rig.check('reboot: the set kept', offered == mine and store == 'ok' and default == '0', str(header))
    check_cycle(rig, 'after reboot', mine)
    # A damaged record: the default set until the next change replaces it.
    drain_history(rig)
    rig.check('record damaged', d.hil('esetcorrupt', expect='HIL_OK').f.get('ok') == '1')
    start = d.mark()
    reboot(rig)
    notices = [l.text for l in d.since(start) if l.text.startswith('EFFECT_SET_NOTICE')]
    rig.check('damaged record: boot notice', any('invalid_record_using_defaults' in n for n in notices), str(notices))
    offered, store, default, header = effect_set_state(rig)
    rig.check('damaged record: the default set offered', offered == milestones.DEFAULT_EFFECT_SET and store == 'damaged' and default == '1', str(header))
    check_cycle(rig, 'damaged record', milestones.DEFAULT_EFFECT_SET)
    # The reset to the default replaces it.
    f, text = milestone_request(rig, milestones.effect_set_line(mask=milestones.effect_mask(milestones.DEFAULT_EFFECT_SET)))
    offered, store, default, header = effect_set_state(rig)
    rig.check('damaged record replaced by the default set', text.startswith('MILESTONE_OK') and store == 'ok' and default == '1', str(header))
    line = d.hil('set 6 0', expect='HIL_SET', timeout=6)
    d.hil('home')
    drain_history(rig)
    reboot(rig)
    offered, store, default, header = effect_set_state(rig)
    rig.check('clean end state', offered == milestones.DEFAULT_EFFECT_SET and store == 'ok' and default == '1', str(header))
    drain_history(rig)


# Every drawn effect by its stored value (USB E<id>), with the moment its
# mid-effect screenshot is taken. Orbit, Flames, Shockwave and Lightning
# measured 9.4-12 ms before 0.15 and stay warnings until they are reworked;
# every other effect must keep within the 10 ms budget.
EFFECTS = ((0, 'Orbit', .35), (1, 'Flames', .35), (2, 'Sparkles', .35), (3, 'Shockwave', .35), (4, 'Lightning', .35),
           (7, 'Slash', .5), (8, 'Tidal', .8), (9, 'Frost', 1.0), (10, 'Gale', .6), (11, 'Eclipse', 1.1),
           (12, 'Radiance', .75), (13, 'Spin', 1.3), (14, 'Storm', 1.6), (15, 'Quickdraw', 1.0), (16, 'Leaves', 1.15),
           (17, 'Bloom', 1.35),
           # 0.17's anime-inspired sets.
           (18, 'Crimson', 1.1), (19, 'Bulwark', 1.1), (20, 'Crux', 0.8), (21, 'Klaxon', 0.5), (22, 'Prism', 1.0),
           (23, 'Sunwheel', 1.05), (24, 'Torrent', 1.1), (25, 'Blitz', 0.85), (26, 'Tigerclaw', 0.85), (27, 'Butterfly', 1.25),
           (28, 'Birdsong', 1.05), (29, 'Spiral', 1.65), (30, 'Pinwheel', 1.35), (31, 'Overdrive', 1.0), (32, 'Summon', 1.15),
           (33, 'Breach', 1.45), (34, 'Bladework', 1.05), (35, 'Moonpetal', 1.3), (36, 'Barrage', 1.55), (37, 'Glitch', 0.8),
           (38, 'Rupture', 0.7), (39, 'Boundless', 1.05), (40, 'Precision', 1.15), (41, 'Umbra', 1.3), (42, 'Ravage', 1.55),
           # 0.18's six sets.
           (43, 'Haymaker', 1.0), (44, 'Palms', 1.0), (45, 'Elastic', 1.0), (46, 'Chains', 1.0), (47, 'Waterleaf', 1.0),
           (48, 'Transmute', 1.0), (49, 'Ignition', 1.0), (50, 'Spires', 1.0), (51, 'Gate', 1.0), (52, 'Fracture', 1.0),
           (53, 'Timestop', 1.0), (54, 'Zipper', 1.0), (55, 'Rush', 1.0), (56, 'Menace', 1.0), (57, 'Sequel', 1.0),
           (58, 'Swordland', 1.0), (59, 'Treasury', 1.0), (60, 'Sunblade', 1.0), (61, 'Aegis', 1.0), (62, 'Trace', 1.0),
           (63, 'Volley', 1.0), (64, 'Starfall', 1.0), (65, 'Mimic', 1.0), (66, 'Scales', 1.0), (67, 'Meadow', 1.0),
           (68, 'Rev', 1.0), (69, 'Hammer', 1.0), (70, 'Foxbite', 1.0), (71, 'Detonate', 1.0), (72, 'Sharkfin', 1.0),
           (73, 'Slingshot', 1.0), (74, 'Presence', 1.0), (75, 'Threefold', 1.0), (76, 'Banner', 1.0), (77, 'Vapor', 1.0),
           (78, 'Crescent', 1.0), (79, 'Release', 1.0), (80, 'Petalfall', 1.0), (81, 'Mask', 1.0), (82, 'Blink', 1.0))
KNOWN_SLOW_EFFECTS = {'Orbit', 'Flames', 'Shockwave', 'Lightning'}


# The default effect set: every drawn effect below Crimson (value 18), 0.16's sixteen.
DEFAULT_SET_MASK = sum(1 << i for i in range(18) if milestones.EFFECTS[i] not in ('Off', 'Random'))


HISTORY_RECORDS = 128  # the history's capacity


def fill_history(rig, full=HISTORY_RECORDS):
    """Pulls until the history holds `full` records; returns the count it reached.

    nvs_tight needs a full history and used to count on the scenarios before
    it for one. In HIL all they leave it full, but a resumed gate
    (release_gate.py --resume) starts after them: the 0.20.9 gate's resume ran
    nvs_tight on 107 records and failed on that alone. A history that is
    already full costs one status read."""
    def records():
        return int(number(rig.dev.status().get('HISTORY_STATUS', {}).get('records'), 0))
    have = records()
    for i in range(full):
        if have >= full:
            break
        r = rig.pull(signals.standard_pull(6000 + 250 * (i % 16), label=f'history fill {i + 1}'), signals.launch_motion(seed=300 + i))
        rig.check_pull(r, tolerance=0.03)
        drain_history(rig)
        after = records()
        if after <= have:
            break  # a pull that adds nothing will not fill it: the caller's check says so
        have = after
    return have


def nvs_tight(rig, delays_ms=(-150, 0, 100, 250, 380)):
    """NVS at the reserve edge (NvsBudget::RESERVE_ENTRIES): a full history,
    a twelve-row table of every effect and an effect set in both slots, 0.15's
    table in both of its slots, then `!nvsfill` filler up to the edge, 87 +
    10 free entries. A first custom theme (8 entries more) is refused there
    for space; a table rewrite in place is not, nor is Shake to wake's first
    record (0.20.2), a setting. History saves must stay at
    their normal latency and within the 400 ms an automatic checkpoint may
    stop the ADC (history_checkpoint.h), pulls around natural history writes
    must be reported and recorded, and history_save_pull's slowed-write pulls
    must still count.

    Ends with no filler, the themes cleared, the default table and set, and
    0.15's keys removed; take a `K` backup first and restore it after.
    """
    settle(rig)
    d = rig.dev
    name, roles = HIL_THEME
    edge = 87 + 10  # NvsBudget::RESERVE_ENTRIES
    f, text = theme_request(rig, themes.request_line('clear'))
    rig.check('themes cleared', text.startswith('THEME_OK'), text)
    d.hil('nvsfill 0', expect='HIL_OK nvsfill')
    every = [e for e in milestones.EFFECTS if e not in ('Off', 'Random')]
    rows = [(8000 + 1500 * i, every) for i in range(milestones.MAX_ROWS)]
    for mode in ('both', 'milestones'):  # two saves: both slots written
        f, text = milestone_request(rig, milestones.request_line('set', mode, rows))
        rig.check(f'twelve rows of every effect saved ({mode})', text.startswith('MILESTONE_OK'), text)
    for effects in (every, every[:-1]):
        f, text = milestone_request(rig, milestones.effect_set_line(effects))
        rig.check(f'effect set of {len(effects)} saved', text.startswith('MILESTONE_OK'), text)
    line = d.hil('legacytable write', expect='HIL_OK legacytable')
    rig.check("0.15's table in both of its slots", line is not None and line.f.get('ok') == '1', line.text if line else 'none')
    records = fill_history(rig)
    rig.check('a full history (128 records)', records == HISTORY_RECORDS, str(records))
    free = int(number(d.status().get('NVS_STATUS', {}).get('available_entries'), -1))
    rig.note(f'records in place, before filler: available_entries={free}')
    # Blob overhead and page packing vary with the store's layout (a freshly
    # erased store landed 4 over), so refill, measuring each time, until free
    # lands at the edge; each fill replaces the last.
    want, nvs = 0, d.status().get('NVS_STATUS', {})
    for attempt in range(4):
        if edge <= free < edge + 4 or want + free - edge <= 0:
            break
        want += free - edge
        d.hil(f'nvsfill {want}', expect='HIL_OK nvsfill')
        nvs = d.status().get('NVS_STATUS', {})
        free = int(number(nvs.get('available_entries'), -1))
        rig.note(f'filler of {want} entries: available_entries={free}')
    rig.check(f'at the reserve edge ({edge} free)', edge <= free < edge + 4, str(nvs))
    rig.scenario['nvs_tight'] = {'available_entries': free, 'edge': edge}
    # A first theme would take 8 more entries: refused for space; a rewrite in place is not.
    f, text = theme_request(rig, themes.request_line('put', 0, name, roles))
    rig.check('a first custom theme is refused for space at the edge', f.get('reason') == 'nvs_space', text)
    f, text = milestone_request(rig, milestones.request_line('set', 'both', rows[:-1]))
    rig.check('a table rewrite in place is saved at the edge', text.startswith('MILESTONE_OK'), text)
    # Shake to wake (0.20.2) is a setting: its first record is saved at the edge
    # (settings never wait for the optional-record reserve), then turned back On.
    for value in (0, 1):
        start = d.mark()
        line = d.hil(f'set {SHAKE_ROW} {value}', expect='HIL_SET', timeout=6)
        save = d.wait('SETTINGS_SAVE', 3, since=start)
        rig.check(f'Shake to wake {"Off" if value == 0 else "On"} saved at the edge',
                  line is not None and line.f.get('error') == '0' and save is not None and int(save.f.get('saved', '0'), 16) & 0x400,
                  f"{line.text if line else 'none'} | {save.text if save else 'no SETTINGS_SAVE'}")
    d.hil('home')

    # Natural history saves: each within the checkpoint budget.
    durations, stores = [], []
    for i in range(6):
        start = d.mark()
        r = rig.pull(signals.standard_pull(6000 + 1000 * i, label=f'pull {i + 1} on the tight store'),
                     signals.launch_motion(seed=70 + i))
        rig.check_pull(r, tolerance=0.03, saturated=False)
        save = d.wait('HISTORY_SAVE', 40, since=start)
        ok = save is not None and ' ok=1' in save.text
        us = int(number(save.f.get('duration_us'), 0)) if save else 0
        durations.append(us)
        stores.append(save.f.get('store', 'nvs') if save else None)
        rig.check(f'history save {i + 1}: ok within 400 ms', ok and us <= 400000, save.text if save else 'none')
        nvs = d.status().get('NVS_STATUS', {})
        rig.check(f'history save {i + 1}: still at the edge', int(number(nvs.get('available_entries'), -1)) >= 87, str(nvs))
        drain_history(rig)
    rig.note('history save us: ' + ', '.join(str(u) for u in durations))
    # Normal latency: a full history's median with room is 290-311 ms (one page
    # erase); below the knee two-erase saves of 460-470 ms appear (nvs_sweep.py).
    median = sorted(durations)[len(durations) // 2] if durations else 0
    rig.check('history saves at their normal median (320 ms or less)', 0 < median <= 320000, f'median_us={median}')
    # To the flash log (0.18 on): 12.8 ms median in the 0.19 and 0.20 gates. A
    # record written straight from PSRAM took 30 ms (ESP-IDF's 32-byte bounce
    # path); it goes through an internal chunk (flash_chunks.h).
    if stores and all(x == 'log' for x in stores):
        rig.check('log saves at their normal median (20 ms or less)', 0 < median <= 20000, f'median_us={median}')
    rig.scenario['history_save_us'] = durations

    # Pulls around natural (unslowed) history writes.
    for i, delay in enumerate(delays_ms):
        first = rig.pull(signals.standard_pull(6000, label=f'priming pull {i}'))
        rig.check(f'priming pull {i}: recorded', first.added is not None)
        optical = signals.standard_pull(9000, label=f'pull {delay:+d} ms into a natural history write')
        r, trigger = armed_pull(rig, optical, 'history', delay, 0, signals.launch_motion(seed=80 + i))
        if r is None:
            rig.check(f'history save started (delay {delay})', False)
            continue
        save = d.wait('HISTORY_SAVE', 5, since=trigger.index)
        rig.check(f'save with pull {delay:+d} ms: HISTORY_SAVE ok', save is not None and ' ok=1' in save.text, save.text if save else 'none')
        rig.check(f'{optical.label}: rpm reported', r.valid, f"rpm={r.rpm:.0f} end={(r.launch or {}).get('end')}")
        rig.check(f'{optical.label}: recorded', r.added is not None)
        drain_history(rig)
    d.hil('nvs clear')
    # The slowed-write pulls of history_save_pull, on the tight store.
    pull_during_history_save(rig)

    # Back to the defaults; the filler and 0.15's keys go.
    d.hil('nvsfill 0', expect='HIL_OK nvsfill')
    theme_request(rig, themes.request_line('clear'))
    milestone_request(rig, milestones.request_line('reset'))
    milestone_request(rig, milestones.effect_set_line(mask=DEFAULT_SET_MASK))
    d.hil('legacytable forget', expect='HIL_OK legacytable')
    nvs = d.status().get('NVS_STATUS', {})
    rig.note(f"after: available_entries={nvs.get('available_entries')}")


def effect_frame_times(rig, rounds=1):
    """Each drawn effect previewed on the recap (USB E<id>): its slowest
    effect pass (PERF max_effect_us) within the 10 ms budget, and a
    screenshot mid-effect. `rounds` repeats the whole cycle (a soak).
    Sound plays at Low throughout: the speaker task refills its DMA on the UI
    core during frames, about 0.6-1 ms of a worst frame (0.18.1 A/B), so the
    budget is held with sound on. The unit's own level is put back."""
    d = rig.dev
    levels = {'Off': 0, 'Low': 1, 'High': 2}
    sound = d.status().get('SOUND_STATUS', {})
    before, plays = levels.get(sound.get('level'), 0), int(number(sound.get('plays'), 0))
    # `set` leaves the Settings page open; settle() goes home, so the pull's recap shows.
    line = d.hil('set 8 1', expect='HIL_SET', timeout=6)
    rig.check('Sound set to Low', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    settle(rig)
    try:
        _effect_frame_times(rig, rounds)
        played = int(number(d.status().get('SOUND_STATUS', {}).get('plays'), 0)) - plays
        rig.check('every preview played its sound', played >= rounds * len(EFFECTS), f'{played} plays for {rounds * len(EFFECTS)} previews')
    finally:
        d.hil(f'set 8 {before}', expect='HIL_SET', timeout=6)
        d.hil('home')


def _effect_frame_times(rig, rounds):
    d = rig.dev
    r = rig.pull(signals.standard_pull(15500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=60))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    times, frame_times = {}, {}
    for round_ in range(rounds):
        for effect_id, name, shot in EFFECTS:
            time.sleep(0.1)
            start = d.mark()
            d.write(f'E{effect_id}\n')
            line = d.wait('EFFECT_PREVIEW', 5, since=start)
            rig.check(f'{name}: preview started', line is not None and line.f.get('effect') == name, line.text if line else 'none')
            began = time.time()
            time.sleep(shot)
            if round_ == 0:
                rig.screenshot(f'effect_{name}')
            # The whole effect (about 5 s) counts, not only its impact.
            duration = int(line.f.get('duration_ms', 5200)) / 1000 if line else 5.2
            time.sleep(max(0.5, duration + 0.6 - (time.time() - began)))
            perf = d.status().get('PERF', {})
            measured = (int(number(perf.get('max_effect_us'), 0)), int(number(perf.get('max_draw_us'), 0)))
            times[name] = max(times.get(name, (0, 0)), measured)
            frames = d.hil('effectframes', expect='HIL_EFFECT_FRAMES')
            if frames is not None and round_ == 0:
                frame_times[name] = [tuple(int(v) for v in pair.split(':')) for pair in frames.text.split()[2:]]
            rig.check(f'{name}: slowest effect pass within 10 ms', measured[0] <= 10000,
                      f'max_effect_us={measured[0]} max_draw_us={measured[1]}',
                      severity='warn' if name in KNOWN_SLOW_EFFECTS else 'fail')
    rig.note('max_effect_us/max_draw_us: ' + ', '.join(f'{k} {v[0]}/{v[1]}' for k, v in times.items()))
    rig.scenario['effect_times'] = times
    rig.scenario['effect_frame_times'] = frame_times


def effect_frame_times_low(rig):
    """effect_times with Sound at Low, the setting the 9 ms budget is held to:
    the speaker's task shares the drawing core while a sound plays."""
    line = rig.dev.hil('set 8 1', expect='HIL_SET', timeout=6)
    rig.check('Sound set to Low', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    try:
        effect_frame_times(rig)
    finally:
        rig.dev.hil('set 8 0', expect='HIL_SET', timeout=6)


def sound_sync(rig):
    """Effect sounds in step with their frames (SoundSync, 0.18.1): with Sound
    at Low, USB previews from a cold codec and one cutting into a playing
    sound; SOUND_SYNC reports each start's present lag, output latency and
    the clock shift that lines the frames up with the sound."""
    d = rig.dev
    # `set` leaves the Settings page open; settle() goes home, so the pull's recap shows.
    line = d.hil('set 8 1', expect='HIL_SET', timeout=6)
    rig.check('Sound set to Low', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    settle(rig)
    r = rig.pull(signals.standard_pull(15500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=61))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    results = []
    for effect_id, name, interrupt in ((0, 'Orbit', False), (7, 'Slash', False), (39, 'Boundless', False), (30, 'Pinwheel', False), (8, 'Tidal', True)):
        start = d.mark()
        d.write(f'E{effect_id}\n')
        preview = d.wait('EFFECT_PREVIEW', 8, since=start)
        rig.check(f'{name}: preview started', preview is not None and preview.f.get('effect') == name, preview.text if preview else 'none')
        if interrupt:
            # A second preview while the first still plays: the whole DMA ring is ahead of it.
            time.sleep(0.8)
            start = d.mark()
            d.write(f'E{effect_id}\n')
            d.wait('EFFECT_PREVIEW', 8, since=start)
        time.sleep(0.6)
        status = d.status()
        sync, sound = status.get('SOUND_SYNC', {}), status.get('SOUND_STATUS', {})
        results.append((name, interrupt, dict(sync), sound.get('max_begin_us'), sound.get('plays')))
        rig.check(f'{name}: sound started', int(number(sound.get('plays'), 0)) > 0, str(sound))
        time.sleep(6)
    d.hil('set 8 0', expect='HIL_SET', timeout=6)
    rig.note('sound sync: ' + '; '.join(f'{n}{" (into a playing sound)" if i else ""} {s} begin_us={b} plays={p}' for n, i, s, b, p in results))
    rig.scenario['sound_sync'] = results


STAGE_NAMES = ('setup', 'glint', 'dark', 'eyes', 'header_mask', 'disc_edges', 'header_fill', 'covered_fill',
               'band_edges', 'band_fill', 'detail', 'stars')


def effect_stage_times(rig):
    """A profiling build only (BEST_EFFECT_PROFILE): Boundless previewed on the
    recap, then each pass's cycles per renderer stage (`!effectstages`)."""
    settle(rig)
    d = rig.dev
    r = rig.pull(signals.standard_pull(15500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=60))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    start = d.mark()
    d.write('E39\n')
    line = d.wait('EFFECT_PREVIEW', 5, since=start)
    rig.check('Boundless: preview started', line is not None and line.f.get('effect') == 'Boundless', line.text if line else 'none')
    time.sleep(6)
    start = d.mark()
    done = d.hil('effectstages', expect='HIL_EFFECT_STAGES', timeout=15)
    rig.check('effectstages answered (a BEST_EFFECT_PROFILE build)', done is not None and done.text.startswith('HIL_EFFECT_STAGES'),
              done.text if done else 'none')
    stages = []
    for l in d.lines[start:]:
        if l.text.startswith('HIL_EFFECT_STAGE '):
            stages.append(l.text)
    rig.scenario['effect_stages'] = stages
    rig.note(f'{len(stages)} stage lines')


def host_bench_hashes(step=100):
    """Effect -> frame hash from ui_render --bench-hashes, built from this checkout: what a device build that draws
    exactly the host's frames reports from `!effectbench ... fixed`."""
    firmware = Path(__file__).resolve().parents[2]
    build = firmware / 'build-ui-render'
    subprocess.run(['bash', str(firmware / 'tools/ui_render/build.sh'), str(build)], check=True, capture_output=True)
    out = subprocess.run([str(build / 'ui_render'), '--bench-hashes', str(step)], check=True, capture_output=True, text=True).stdout
    return dict(line.split() for line in out.splitlines())


def loaded_effects(rig):
    """The loadable-effects spike (hil_fx.h, docs/FIRMWARE_LOADABLE_EFFECTS.md):
    each built-in file previewed on the recap through the real UI loop with
    the 9 ms guard, next to its native effect; then the offscreen bench of
    every file, guarded and (for the worst case and the instruction
    benchmark) unguarded."""
    settle(rig)
    d = rig.dev
    r = rig.pull(signals.standard_pull(15500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=60))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    start = d.mark()
    d.hil('fxlist', expect='HIL_FX_LIST_END')
    files = [line.f for line in d.since(start, 'HIL_FX_FILE')]
    rig.check('fx files load', bool(files) and all(f.get('load') == 'none' for f in files), str(files))
    native_ids = {name: effect_id for effect_id, name, _ in EFFECTS}
    played = {}
    for f in files:
        name, index = f['name'], int(f['index'])
        title = name.split('/')[-1].capitalize()
        runs = [('loaded', f'fxplay {index}')]
        if title in native_ids:
            runs.append(('native', None))
        for kind, command in runs:
            time.sleep(0.1)
            if kind == 'loaded':
                line = d.hil(command, expect='HIL_FX_PLAY')
                duration = int(line.f.get('duration_ms', 5000)) / 1000
            else:
                mark = d.mark()
                d.write(f'E{native_ids[title]}\n')
                line = d.wait('EFFECT_PREVIEW', 5, since=mark)
                duration = int(line.f.get('duration_ms', 5000)) / 1000 if line else 5.2
            time.sleep(duration + 0.6)
            perf = d.status().get('PERF', {})
            frames = d.hil('effectframes', expect='HIL_EFFECT_FRAMES')
            stops = d.hil('fxstops', expect='HIL_FX_STOPS').f.get('stops') if kind == 'loaded' else None
            pairs = [tuple(int(v) for v in pair.split(':')) for pair in frames.text.split()[2:]]
            played[f'{name}:{kind}'] = {'max_effect_us': int(number(perf.get('max_effect_us'), 0)),
                                        'max_draw_us': int(number(perf.get('max_draw_us'), 0)),
                                        'frames': pairs, 'guard_stops': stops}
            if name not in ('worst', 'opbench'):
                rig.check(f'{name} {kind}: slowest effect pass within 9 ms', played[f'{name}:{kind}']['max_effect_us'] <= 9000,
                          str(played[f'{name}:{kind}']['max_effect_us']), severity='warn')
    bench = []
    # Guarded as on a real unit, then unguarded to see each file's true cost.
    for args in ('', '-1 0'):
        start = d.mark()
        d.hil(('fxbench ' + args).strip(), expect='HIL_FX_BENCH_END', timeout=300)
        bench += [line.text for line in d.since(start, 'HIL_FX_BENCH') if 'name=' in line.text]
        bench += [line.text for line in d.since(start, 'HIL_FX_FRAMES')]
        bench += [line.text for line in d.since(start, 'HIL_FX_PRIMS')]
    start = d.mark()
    d.hil('fxsound 0', expect='HIL_FX_SOUND', timeout=30)
    bench += [line.text for line in d.since(start, 'HIL_FX_SOUND')]
    rig.note('fx play: ' + ', '.join(f'{k} {v["max_effect_us"]}/{v["max_draw_us"]}' for k, v in played.items()))
    for line in bench:
        rig.note(line)
    rig.scenario['loaded_effects'] = {'play': played, 'bench': bench}


def cut_boot(rig, start):
    """After a simulated power cut: wait for the new boot and track it as the current one."""
    d = rig.dev
    boot = d.wait('HIL_BOOT', 30, since=start)
    rig.check('power cut: the unit restarted', boot is not None)
    time.sleep(1.5)
    rig.boot_id = rig.hil_boot().get('boot_id')
    rig.scenario['start_line'] = d.mark()
    settle(rig)
    return boot


def loaded_store(rig):
    """The loaded-effect store (effect_store.h, USB F and X): the first install
    formats the effect region, an installed effect plays as E96 within the 9 ms
    guard, the same uid replaces itself, a full store refuses the 33rd, a
    power cut at any point of a replace leaves the old or the new effect whole,
    a deleted effect that a milestone row names celebrates as its fallback,
    and everything survives a reboot."""
    settle(rig)
    d = rig.dev
    r = rig.pull(signals.standard_pull(15500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=61))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    # Records that name a deleted effect keep its slot (an earlier run's, a
    # rollback's): start from a table and an effect set without added effects.
    milestone_request(rig, milestones.request_line('reset'))
    header, _ = milestone_list(rig)
    kept = [i for i in milestones.effect_ids(header.get('effect_set', '0')) if i < 96]
    if kept:
        milestone_request(rig, milestones.effect_set_line(kept))
    fx_client.clear(d)
    store, slots = fx_client.listing(d)
    rig.check('F lists the store', store.get('limit') == '32' and store.get('first_id') == '96', str(store))
    held = {s: f['references'] for s, f in slots.items() if f['references'] != 'none'}
    rig.check('no record names an added effect', not held, str(held))
    comet = fx_client.compile_text(fx_client.effect_source(0xC0FFEE01, 'Comet', fallback='Tidal'))
    start = d.mark()
    reply = fx_client.put(d, comet, b'packed source', timeout=60)
    formatted = [l.text for l in d.since(start) if l.text.startswith('FX_NOTICE store_formatted')]
    rig.check('install: FX_OK in slot 0 (id 96)', reply.text.startswith('FX_OK') and reply.f.get('slot') == '0' and reply.f.get('id') == '96', reply.text)
    rig.note(f'first install: {formatted[0] if formatted else "store already formatted"}')
    store, slots = fx_client.listing(d)
    rig.check('F lists Comet as ok', slots.get(0, {}).get('state') == 'ok' and slots[0]['name'] == 'Comet' and slots[0]['fallback'] == 'Tidal', str(slots.get(0)))
    packed, reply = fx_client.source(d, 0)
    rig.check('X source reads the packed source back', packed == b'packed source', reply.text)
    _, reply = fx_client.source(d, 5)
    rig.check('X source of an empty slot: not_found', reply.f.get('reason') == 'not_found', reply.text)
    # It plays, within the guard.
    line = effect_preview(rig, 96)
    rig.check('E96 previews the loaded effect', line is not None and not line.text.startswith('EFFECT_PREVIEW_ERROR'), line.text if line else 'none')
    time.sleep(3.6)
    perf = d.status().get('PERF', {})
    rig.check('E96: slowest effect pass within the guard', int(number(perf.get('max_effect_us'), 0)) <= 10000, str(perf.get('max_effect_us')))
    # The same uid replaces itself in its slot.
    comet2 = fx_client.compile_text(fx_client.effect_source(0xC0FFEE01, 'Comet', fallback='Tidal', revision=2, size=3))
    reply = fx_client.put(d, comet2)
    store, slots = fx_client.listing(d)
    rig.check('replace: same slot, revision 2', reply.text.startswith('FX_OK') and reply.f.get('slot') == '0' and slots[0].get('revision') == '2', reply.text)
    # Power cut at points through a replace (payload start, middle, inside the
    # header written last): old or new, whole.
    for point in ('start', 'middle', 'header'):
        replacement = fx_client.compile_text(fx_client.effect_source(0xC0FFEE01, 'Comet', fallback='Tidal', revision=10 + len(point), size=2))
        cut = {'start': 10, 'middle': len(replacement) // 2, 'header': len(replacement) + 10}[point]
        d.hil(f'fxcut {cut}')
        start = d.mark()
        try:
            fx_client.put(d, replacement, timeout=5)
        except Exception:
            pass  # the unit restarts mid-request
        cut_boot(rig, start)
        store, slots = fx_client.listing(d)
        state = slots.get(0, {})
        rig.check(f'power cut in the {point}: slot 0 ok, one copy', state.get('state') == 'ok' and store.get('installed') == '1', f'{store} {state}')
    # A full store refuses the 33rd effect; clear empties it.
    for k in range(31):
        reply = fx_client.put(d, fx_client.compile_text(fx_client.effect_source(0xF00D0000 + k, f'Fill {k}')))
        if not reply.text.startswith('FX_OK'):
            break
    store, _ = fx_client.listing(d)
    rig.check('32 installed', store.get('installed') == '32' and store.get('free_slots') == '4', str(store))
    reply = fx_client.put(d, fx_client.compile_text(fx_client.effect_source(0xF00DFFFF, 'One too many')))
    rig.check('the 33rd: slots_full', reply.f.get('reason') == 'slots_full', reply.text)
    reply = fx_client.clear(d)
    rig.check('clear: none installed', reply.text.startswith('FX_OK') and reply.f.get('installed') == '0', reply.text)
    # A milestone row names the effect; deleted, it celebrates as its fallback (Tidal).
    reply = fx_client.put(d, comet)
    rows = [(9000, 96)]
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', rows))
    rig.check('milestone row names id 96', text.startswith('MILESTONE_OK'), text)
    header, _ = milestone_list(rig)
    rig.check('M lists the added effect as playable (loaded=)', milestones.effect_ids(header.get('loaded', '0')) == {96}, header.get('loaded'))
    reply = fx_client.delete(d, 0)
    rig.check('delete: names the milestone row', reply.text.startswith('FX_OK') and reply.f.get('references') == 'milestones', reply.text)
    start = d.mark()
    r = rig.pull(signals.standard_pull(9800), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=62))
    line = d.wait('CELEBRATION', 2, since=start)
    rig.check('the deleted effect celebrates as Tidal', line is not None and line.f.get('effect') == 'Tidal', line.text if line else 'none')
    # Reboot: the store (empty slot 0 with its tombstone) and the table persist.
    reboot(rig)
    store, slots = fx_client.listing(d)
    rig.check('after reboot: slot 0 deleted, fallback kept', slots.get(0, {}).get('state') == 'deleted' and slots[0].get('fallback') == 'Tidal', str(slots.get(0)))
    # A backup carries the effects (format 7) and a restore puts them back in their own slots.
    fx_client.clear(d)
    first = fx_client.compile_text(fx_client.effect_source(0xBAC0, 'Kept one', fallback='Sparkles'))
    second = fx_client.compile_text(fx_client.effect_source(0xBAC1, 'Kept two', fallback='Slash'))
    # The table still names slot 0's tombstone, so these go to other slots.
    kept = [int(fx_client.put(d, first, b'first source').f.get('slot', -1)), int(fx_client.put(d, second).f.get('slot', -1))]
    milestone_request(rig, milestones.request_line('set', 'milestones', [(9000, 96 + kept[1])]))
    text = d.backup()
    begin = next((l for l in text.splitlines() if l.startswith('BACKUP_BEGIN')), '')
    rig.check('backup: format 7 with two effects', 'format=7' in begin and 'effects=2' in begin, begin)
    fx_client.clear(d)
    milestone_request(rig, milestones.request_line('reset'))
    start = d.mark()
    d.restore(text)
    cut_boot(rig, start)
    store, slots = fx_client.listing(d)
    rig.check(f'restore: both effects back in their slots {kept}', slots.get(kept[0], {}).get('name') == 'Kept one' and
              slots.get(kept[1], {}).get('name') == 'Kept two' and slots[kept[1]].get('references') == 'milestones', str({k: slots.get(k) for k in kept}))
    milestone_request(rig, milestones.request_line('reset'))
    fx_client.clear(d)


# run_hil.py --sensor: 'fitted', 'absent' or None (not declared).
SENSOR = None
# run_hil.py: the flashed HIL build's hil-overhead.json (tools/hil_overhead.py), or None.
HIL_OVERHEAD = None


def heap_floor(rig):
    """The DMA-capable heap floor for this HIL run: production's 32 KB less the
    HIL image's measured extra internal RAM, so the check means what it means
    on production (where production_songs.py and the release gate hold the
    full 32 KB). Without the measurement, the full 32 KB."""
    if not HIL_OVERHEAD:
        rig.note('heap floor 32768: no hil-overhead.json for the flashed build (build it with build-m5-hil.sh)')
        return 32768
    o = HIL_OVERHEAD
    rig.note(f"heap floor {o['hil_floor']} = {o['production_floor']} - {o['overhead_bytes']} HIL-only internal bytes "
             f"(static {o['static_delta']:+d}, IRAM shadow {o['iram_shadow_delta']:+d}; tree {o['tree_fingerprint']})")
    return int(o['hil_floor'])
# The example songs (firmware/tools/fx/effects/music) and their lengths in seconds (test_fx_music pins them).
SONGS = (('victory_lap', 'Victory Lap', 5.143), ('night_spin', 'Night Spin', 6.0), ('ripcord', 'Ripcord', 2.0))


def task_run_us(rig):
    """`!tasks`: each task's CPU time so far, by name."""
    d = rig.dev
    start = d.mark()
    d.hil('tasks', expect='HIL_TASKS', timeout=6)
    return {l.f.get('name'): int(number(l.f.get('run_us'), 0)) for l in d.since(start, 'HIL_TASK ')}


def worst_song(rig, kind, floor, until_save=False, short_frames=False):
    """fx_client.worst_song_source(), a 6 s song at the render budget,
    previewed to its end, back to back: once, or with `until_save` until a
    history save (the pull just before leaves one pending, 10-60 s later)
    lands inside a play, 90 s at most. A save waits while a celebration is
    shown, so for that the song comes after 1 s of frames (`short_frames`). Every play streams every block with no
    underrun; IDLE0 keeps running; the music task's share while the song
    plays (1-5 s into each play) is within the budget plus noise and near the
    cost model's over the same window; the `kind` heap stays above `floor`,
    sampled every second, and its low-water mark after the last play."""
    d = rig.dev
    source = fx_client.worst_song_source(short_frames)
    reply = fx_client.put(d, fx_client.compile_text(source), timeout=60)
    rig.check('worst song: installed', reply.text.startswith('FX_OK'), reply.text)
    if not reply.text.startswith('FX_OK'):
        return None
    seconds = fx_client.WORST_SONG_SECONDS
    expected = -(-round(seconds * 16000) // 512)
    earlier = int(number(d.status().get('HEAP_STATUS', {}).get(f'{kind}_min_free'), 0))
    d.hil('idle0 reset', expect='HIL_IDLE0')
    t0 = time.monotonic()
    samples, plays, saves, inside, busy = [], [], [], False, []
    while not plays or (until_save and not inside and time.monotonic() - t0 < 90):
        before = d.status().get('MUSIC_STATUS', {})
        start = d.mark()
        d.write(f"E{reply.f['id']}\n")
        preview = d.wait('EFFECT_PREVIEW', 8, since=start)
        played_from = time.monotonic()
        window = []
        while time.monotonic() - played_from < seconds + 0.8:
            time.sleep(1)
            # The music task's run time 1 s and 5 s in: its share while the song plays.
            if len(window) < 2 and time.monotonic() - played_from >= (1.0, 5.0)[len(window)]:
                window.append((task_run_us(rig).get('music', 0), time.monotonic()))
            # !heap, not a full status: that prints from the loop that feeds the speaker.
            mark = d.mark()
            d.hil('heap', expect='HIL_OK heap')
            h = (d.latest('HEAP_STATUS', since=mark) or Line(0, 0, '')).f
            m = (d.latest('MUSIC_STATUS', since=mark) or Line(0, 0, '')).f
            samples.append({'s': round(time.monotonic() - t0, 1), 'free': int(number(h.get(f'{kind}_free'), 0)),
                            'min': int(number(h.get(f'{kind}_min_free'), 0)), 'streaming': m.get('streaming')})
        after = d.status().get('MUSIC_STATUS', {})
        # Inside the play: between its preview and the song's end (device line times).
        end = preview.t + seconds if preview is not None else 0
        here = [l.text for l in d.since(start, 'HISTORY_SAVE ') if l.t <= end]
        saves += [l.text for l in d.since(start, 'HISTORY_SAVE ')]
        inside = inside or bool(here)
        if len(window) == 2:
            busy.append((window[1][0] - window[0][0], window[1][1] - window[0][1]))
        plays.append({'preview': preview is not None and 'effect=Worst Song' in preview.text,
                      'plays': int(number(after.get('plays'), 0)) - int(number(before.get('plays'), 0)),
                      'underruns': int(number(after.get('underruns'), 0)),  # start() resets the count
                      'blocks': int(number(after.get('played_blocks'), 0)), 'streaming': after.get('streaming'), 'saves_inside': len(here),
                      'max_dry_ms': int(number(after.get('max_dry_ms'), -1)), 'begin_ms': int(number(after.get('begin_ms'), -1))})
    idle = d.hil('idle0', expect='HIL_IDLE0')
    gap = int(number(idle.f.get('max_gap_us'), -1)) if idle else -1
    share = sum(u for u, _ in busy) / (sum(w for _, w in busy) * 1e6) if busy else -1
    model = fx_client.song_model(source, 1000, 5000)
    rig.check('worst song: every preview started', all(p['preview'] for p in plays), plays)
    # At the render budget (20% of core 0 by the cost model) the song plays with no underrun,
    # IDLE0 runs throughout, and the music task's measured share stays near the model's.
    rig.check('worst song: IDLE0 kept running (watchdog margin)', 0 <= gap < 1000000, f'max_gap_us={gap}')
    # The model refitted in 0.21 reads the device within a few percent (2.2% rms over 31
    # songs; repeated runs differ by up to 0.5 points): the budget plus a point of noise.
    rig.check('worst song: the music task within the 20% budget plus noise while it plays (at most 21%)', 0 <= share <= 0.21,
              f'{share:.1%} over {len(busy)} plays, modelled {model:.1%} over the same window')
    rig.check('worst song: the music task within a point of the cost model', 0 <= share and abs(share - model) <= 0.01,
              f'{share:.1%} measured, {model:.1%} modelled', severity='warn')
    rig.check(f'worst song: each of {len(plays)} plays streamed its 6 s without an underrun',
              all(p['plays'] == 1 and p['underruns'] == 0 and p['streaming'] == '0' and p['blocks'] == expected for p in plays),
              [(p['plays'], p['underruns'], p['streaming'], p['blocks']) for p in plays] + [f'blocks expected {expected}'])
    rig.check('worst song: its channel never empty long enough for the speaker to write silence',
              all(0 <= p['max_dry_ms'] < fx_client.MUSIC_DRY_LIMIT_MS for p in plays),
              [(p['max_dry_ms'], p['begin_ms']) for p in plays] + [f'max_dry_ms, begin_ms; limit {fx_client.MUSIC_DRY_LIMIT_MS}'])
    playing = [x for x in samples if x['streaming'] == '1']
    # The low-water mark is since boot: it speaks for the plays (and the save
    # inside one) only if nothing before them had already taken it under the floor.
    low = min([x['free'] for x in playing] + ([x['min'] for x in samples[-1:]] if earlier >= floor else [])) if samples else 0
    rig.check(f'worst song: {kind} heap above the {floor}-byte floor while it plays', bool(playing) and low >= floor,
              f"lowest {low}; " + ', '.join(f"{x['s']} s {x['free']}" for x in samples[:12]))
    if earlier < floor:
        rig.check(f'worst song: {kind} low-water mark through the plays', False,
                  f'not covered: earlier runs had already taken it to {earlier}, under the {floor}-byte floor', severity='info')
    rig.note(f"worst song: {len(plays)} plays, music {share:.1%} of core 0, IDLE0 gap {gap} us, {kind} heap low-water "
             f"{samples[-1]['min'] if samples else '?'}, saves {saves}")
    return {'plays': plays, 'low': low, 'share': round(share, 4), 'max_gap_us': gap, 'saves': saves, 'save_inside': inside}


def songs(rig):
    """Songs (engine level 2, 0.20; docs/FIRMWARE_SOUND_0_20.md), installed
    over USB as the effects page sends them and played at Sound Low.

    1. Each song previews and streams to its end through the music task (core
       0, priority 1): no underrun, every block played, the task's share of
       core 0 and IDLE0's longest gap; HEAP_STATUS before, in the middle of
       each song and after. Nothing of the rig's own runs meanwhile, so the
       heap is the firmware's: a song takes what any sound does, the speaker
       task and its I2S buffers (8.4 KB), plus 0.19's hot block on the first
       added-effect play, and gives the rest back.
    2. The worst case (worst_song): the heaviest frames the M5 accepts with a
       6 s song of every voice type at the render budget, the heap sampled
       every second; then the same song after 1 s of frames (a save waits
       while a celebration is shown), back to back until a history save (from
       a pull just before) lands inside a play.
    3. The heap holds heap_floor() mid-song, through the worst case and at its
       low-water mark: production's 32 KB less the HIL image's measured extra
       internal RAM (production_songs.py and the release gate hold a
       production image to the full 32 KB).
    4. ADC: the real input (`!reset`) while each song plays against 2 s of
       silence before it (`!sndwatch`, from its own task so the UI loop keeps
       feeding the song; it takes 3.5 KB of heap, so it runs only after the
       heap is judged). Interference is a property of the sensor's circuit:
       its checks count only on a run declared `--sensor fitted` and are
       listed as not covered otherwise, with the readings noted either way.
       Then the generator holds a noiseless idle input while each song plays:
       no detector edge and no launch.
    5. A pull during a song is measured as without one, with the start floor
       (half the unit's own marks, 400 at most) in force, and fades the song out;
       then a new best with a song as Best effect (Celebrate Best effect, the
       bests cleared first so the second pull is one) plays it whole.
    Leaves no added effects, Sound Off, Best effect Orbit and Celebrate Both.
    """
    d = rig.dev
    # Sound first: set leaves the Settings page open, which would hide the opening pull's recap.
    line = d.hil('set 8 1', expect='HIL_SET', timeout=6)
    rig.check('Sound set to Low', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    d.hil('home')
    settle(rig)
    fx_client.clear(d)
    # A recap to preview on.
    r = rig.pull(signals.standard_pull(9000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=71))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    music = fx_client.ROOT / 'tools/fx/effects/music'
    ids, results = {}, []
    for key, name, _ in SONGS:
        reply = fx_client.put(d, fx_client.compile_text((music / f'{key}.fx').read_text()), timeout=60)
        rig.check(f'{name}: installed', reply.text.startswith('FX_OK'), reply.text)
        ids[key] = int(reply.f.get('id', 0))

    # 1. Streaming, core 0 and the heap, with nothing of the rig's own running.
    heap0 = d.status().get('HEAP_STATUS', {})
    heaps = []
    for key, name, seconds in SONGS:
        d.hil('idle0 reset', expect='HIL_IDLE0')
        before, tasks0, t0 = d.status().get('MUSIC_STATUS', {}), task_run_us(rig), time.monotonic()
        start = d.mark()
        d.write(f'E{ids[key]}\n')
        preview = d.wait('EFFECT_PREVIEW', 8, since=start)
        # Song names carry spaces, which the field parser splits at.
        rig.check(f'{name}: preview started', preview is not None and f'effect={name} duration_ms=' in preview.text, preview.text if preview else 'none')
        time.sleep(min(1.0, seconds / 2))
        mid = d.status()
        heaps.append((name, mid.get('HEAP_STATUS', {}), mid.get('MUSIC_STATUS', {}).get('streaming')))
        time.sleep(max(0.0, seconds + 1.0 - (time.monotonic() - t0)))
        after, tasks1, wall = d.status().get('MUSIC_STATUS', {}), task_run_us(rig), time.monotonic() - t0
        idle = d.hil('idle0', expect='HIL_IDLE0')
        plays = int(number(after.get('plays'), 0)) - int(number(before.get('plays'), 0))
        # start() resets the count: this play's own (a difference failed the next song too).
        underruns = int(number(after.get('underruns'), 0))
        blocks, expected = int(number(after.get('played_blocks'), 0)), -(-round(seconds * 16000) // 512)
        rig.check(f'{name}: streamed to its end without an underrun', plays == 1 and underruns == 0 and after.get('streaming') == '0' and blocks == expected,
                  f'plays +{plays} underruns {underruns} streaming={after.get("streaming")} blocks {blocks}/{expected}')
        dry = int(number(after.get('max_dry_ms'), -1))
        rig.check(f'{name}: its channel never empty long enough for the speaker to write silence', 0 <= dry < fx_client.MUSIC_DRY_LIMIT_MS,
                  f'max_dry_ms={dry} (limit {fx_client.MUSIC_DRY_LIMIT_MS}), begin_ms={after.get("begin_ms")}')
        share = (tasks1.get('music', 0) - tasks0.get('music', 0)) / (wall * 1e6)
        gap = int(number(idle.f.get('max_gap_us'), -1)) if idle else -1
        rig.check(f'{name}: music task under 25% of core 0', 0 <= share < 0.25, f'{share:.1%}', severity='warn')
        rig.check(f'{name}: IDLE0 kept running (watchdog margin)', 0 <= gap < 1000000, f'max_gap_us={gap}')
        results.append({'song': name, 'share': round(share, 4), 'max_gap_us': gap, 'blocks': blocks,
                        'max_dry_ms': dry, 'begin_ms': int(number(after.get('begin_ms'), -1))})
    # The music task's stack (MUSIC_STACK, 3,584 bytes internal) at its deepest: sized from a
    # 2,016-byte peak over the three songs with 1.5 KB to spare, so at least 1 KB must stay free.
    start = d.mark()
    d.hil('tasks', expect='HIL_TASKS', timeout=6)
    task = next((l.f for l in d.since(start, 'HIL_TASK ') if l.f.get('name') == 'music'), {})
    free = int(number(task.get('stack_free'), -1))
    rig.note(f"music task: stack_free {free} bytes, core {task.get('core', '?')}, priority {task.get('priority', '?')}")
    rig.check('music task stack keeps at least 1 KB free', free >= 1024, f'stack_free={free}')
    rig.scenario['songs_music_task'] = dict(task)

    # 2. The worst case over a whole song, and 3. the floor. A pull just
    # before it leaves a history save pending, which runs 10-60 s later; the
    # 6 s song plays back to back until the save lands inside a play: its
    # buffers must not come from internal RAM (0.20 gate, 2026-10-23: 5.4 KB of
    # it, under the floor; history_log.h).
    kind = 'dma' if 'dma_free' in heap0 else 'internal'
    floor = heap_floor(rig)
    rig.scenario['songs_floor'] = floor
    rig.scenario['songs_worst'] = worst_song(rig, kind, floor)
    r = rig.pull(signals.standard_pull(9500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=72))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    worst = worst_song(rig, kind, floor, until_save=True, short_frames=True)
    rig.scenario['songs_worst_save'] = worst
    saves = worst['saves'] if worst else []
    rig.check('worst song: the pull\'s history save ran while it played, and succeeded',
              bool(worst) and worst['save_inside'] and all(' ok=1 ' in t for t in saves), [(p['saves_inside']) for p in (worst or {}).get('plays', [])] + saves)
    time.sleep(1.0)
    heap1 = d.status().get('HEAP_STATUS', {})
    def heap(h, field):
        return int(number(h.get(f'{kind}_{field}'), 0))
    for name, h, streaming in heaps:
        used = heap(heap0, 'free') - heap(h, 'free')
        # The speaker's own (8,364 bytes on a production image, 2026-10-23), and 0.19's hot block on the first added-effect play.
        rig.check(f'{name}: while it plays, only the speaker\'s own heap is taken (<= 9 KB, plus 0.19\'s 4.5 KB hot block)', streaming == '1' and used <= 9216 + 4608,
                  f"streaming={streaming} {kind}_free {heap(heap0, 'free')} -> {heap(h, 'free')} ({used:+d} bytes)")
        rig.check(f'{name}: {kind} heap above the {floor}-byte floor while it plays', heap(h, 'free') >= floor,
                  f"{kind}_free={heap(h, 'free')} {kind}_min_free={heap(h, 'min_free')}")
    kept = heap(heap0, 'free') - heap(heap1, 'free')
    rig.check(f'after the songs the {kind} heap is back (within 1 KB, besides 0.19\'s 4.5 KB hot block)', kept <= 1024 + 4608, f"{kind}_free {heap(heap0, 'free')} -> {heap(heap1, 'free')} ({kept:+d} bytes)")
    # The low-water mark is since boot: it says something about the songs only
    # if the scenarios before them left it above the floor.
    low, earlier = heap(heap1, 'min_free'), heap(heap0, 'min_free')
    if earlier >= floor:
        rig.check(f'{kind} heap low-water mark above the {floor}-byte floor after the songs and the worst case', low >= floor, f'{kind}_min_free={low}')
    else:
        rig.check(f'{kind} heap low-water mark through the songs', False,
                  f'not covered: earlier scenarios had already taken it to {earlier}, under the {floor}-byte floor (now {low})', severity='info')
    rig.note(f"HEAP_STATUS {kind} free / largest / min: before {heap(heap0, 'free')} / {heap(heap0, 'largest')} / {heap(heap0, 'min_free')}, "
             + ', '.join(f"{n} playing {heap(h, 'free')} / {heap(h, 'largest')} / {heap(h, 'min_free')}" for n, h, _ in heaps)
             + f", after {heap(heap1, 'free')} / {heap(heap1, 'largest')} / {heap(heap1, 'min_free')}")
    rig.scenario['songs_heap'] = {'before': dict(heap0), 'playing': {n: dict(h) for n, h, _ in heaps}, 'after': dict(heap1)}

    # 4. ADC interference: the real input, now that the heap is judged.
    d.hil('reset', expect='HIL_OK')
    rest = d.hil('sndwatch 2000', expect='HIL_SNDWATCH', timeout=6)
    covered = SENSOR == 'fitted'
    rig.note(f"ADC at rest (real input, sensor {SENSOR or 'not declared'}): "
             f"{rest.f.get('min')}..{rest.f.get('max')} edges={rest.f.get('edges')}" if rest else 'ADC at rest: no reply')
    if not covered:
        rig.check('ADC interference while songs play', False,
                  'no sensor' if SENSOR == 'absent' else 'sensor not declared (run with --sensor fitted)', severity='info')
    for (key, name, seconds), result in zip(SONGS, results):
        quiet = d.hil('sndwatch 2000', expect='HIL_SNDWATCH', timeout=6)
        start = d.mark()
        d.write(f'!sndwatch {int(seconds * 1000)}\n')
        d.write(f'E{ids[key]}\n')
        playing = d.wait('HIL_SNDWATCH', seconds + 6, since=start)
        time.sleep(1.5)
        q, p = (quiet.f if quiet else {}), (playing.f if playing else {})
        if covered:
            rig.check(f'{name}: no detector edges or launches while it plays', playing is not None and p.get('edges') == '0' and p.get('launches') == '0',
                      playing.text if playing else 'none')
            widened = (int(number(q.get('min'), 0)) - int(number(p.get('min'), 0)), int(number(p.get('max'), 0)) - int(number(q.get('max'), 0)))
            rig.check(f'{name}: ADC range while it plays within the quiet range (+-100)', playing is not None and quiet is not None and max(widened) <= 100,
                      f'quiet {q.get("min")}..{q.get("max")} playing {p.get("min")}..{p.get("max")}', severity='warn')
        result.update(quiet=dict(q), playing=dict(p))
    rig.scenario['songs_adc'] = {'sensor': SENSOR or 'not declared', 'covered': covered, 'rest': dict(rest.f) if rest else None}

    # 4b. The generator holding an idle input (the HIL baseline, noise 4) while
    # each song plays: the music task's load must not disturb acquisition, so
    # the detector takes no edge and no launch (0.20.1: the noise band is held
    # to stricter bars than before; the speaker cannot couple into generated
    # samples, test_sound_interference covers that on the host).
    # A noiseless input (any edge would be the music disturbing acquisition:
    # with the generator's usual 4 counts of noise, edges come and go at
    # random, songs or not): no edge, no launch.
    d.hil('idle 1800 0', expect='HIL_OK idle')
    d.hil('opt idle', expect='HIL_OK opt')
    time.sleep(2.0)
    idle_watch = {}
    for key, name, seconds in SONGS:
        start = d.mark()
        d.write(f'!sndwatch {int(seconds * 1000)}\n')
        d.write(f'E{ids[key]}\n')
        playing = d.wait('HIL_SNDWATCH', seconds + 6, since=start)
        time.sleep(1.5)
        p = playing.f if playing else {}
        rig.check(f'{name} over a noiseless idle generator: no detector edge or launch while it plays',
                  playing is not None and p.get('edges') == '0' and p.get('launches') == '0' and p.get('min') == p.get('max'),
                  playing.text if playing else 'none')
        idle_watch[name] = dict(p)
    rig.scenario['songs_idle_generator'] = idle_watch
    d.hil('reset', expect='HIL_OK')

    # 5. A pull while a song plays: measured as ever, and the song faded out.
    start = d.mark()
    d.write(f'E{ids["night_spin"]}\n')
    d.wait('EFFECT_PREVIEW', 8, since=start)
    time.sleep(2.0)
    st = d.status()
    streaming, rs = st.get('MUSIC_STATUS', {}).get('streaming'), st.get('RPM_STATUS', {})
    # While a sound plays a pull must start on marks half as strong as this
    # unit's own (0.20.1, StartMarkReference): the run's earlier pulls set it.
    pulls, median = int(number(rs.get('ref_pulls'), 0)), number(rs.get('ref_median'), 0)
    min_mark = number(rs.get('min_mark'), 80)
    expected = min(max(0.5 * median, min_mark), max(min_mark, 400)) if pulls >= 3 else min_mark
    rig.check('while a song plays the start floor is half the unit\'s own marks (400 at most)',
              streaming == '1' and abs(number(rs.get('start_floor'), -1) - expected) < 1,
              f"start_floor={rs.get('start_floor')} ref_pulls={pulls} ref_median={median} expected {expected:.0f}")
    r = rig.pull(signals.standard_pull(10500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=72))
    rig.check_pull(r, tolerance=0.03)
    rig.check('a pull during a song is measured with the floor in force', r.valid and r.added is not None, f'rpm={r.rpm:.0f}')
    after = d.status().get('MUSIC_STATUS', {})
    rig.check('a pull during a song fades it out', streaming == '1' and after.get('streaming') == '0', f'before={streaming} after={after}')
    time.sleep(6.5)
    st = d.status()
    rs, playing = st.get('RPM_STATUS', {}), st.get('SOUND_STATUS', {}).get('playing')
    rig.check('with no sound playing the start floor is off', playing == '0' and number(rs.get('start_floor'), -1) == 0,
              f"start_floor={rs.get('start_floor')} sound playing={playing}")
    # A new best with a song as Best effect: the song outlasts the frames.
    # Celebrate Best effect only, so the milestone table cannot pick another effect for the new best.
    d.hil('set 7 0', expect='HIL_SET', timeout=6)
    line = d.hil(f'set 6 {ids["night_spin"]}', expect='HIL_SET', timeout=6)
    rig.check('Best effect Night Spin applied', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    d.hil('home')
    # Bests persist across sessions: cleared, the first pull seeds them and the next beats them.
    line = d.hil('bests clear', expect='HIL_OK bests')
    rig.check('bests cleared', line is not None and line.f.get('standard_three') == '0.0', line.text if line else 'none')
    celebration = None
    for seed, rpm in enumerate((19000, 20500, 22000)):
        start = d.mark()
        r = rig.pull(signals.standard_pull(rpm), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=73 + seed))
        rig.check_pull(r, tolerance=0.03)
        celebration = d.wait('CELEBRATION', 3, since=start)
        if celebration is not None and celebration.f.get('best') == '1':
            break
        time.sleep(6.5)
    best = celebration is not None and celebration.f.get('best') == '1'
    rig.check('a new best plays a song as Best effect', best, celebration.text if celebration is not None else 'no celebration')
    if best:
        rig.check('the new best plays Night Spin', ' effect=Night Spin ' in celebration.text, celebration.text)
        # Night Spin's song is as long as its frames, 6 s (0.20.1): it plays whole, then ends.
        time.sleep(3.0)
        status = d.status().get('MUSIC_STATUS', {})
        rig.check('the song plays with the celebration', status.get('streaming') == '1', str(status))
        time.sleep(5.0)
        status = d.status().get('MUSIC_STATUS', {})
        rig.check('and ends whole (188 blocks, 6 s) with no underrun', status.get('streaming') == '0' and
                  status.get('underruns') == after.get('underruns') and status.get('played_blocks') == '188', str(status))
    d.hil('set 6 0', expect='HIL_SET', timeout=6)
    d.hil(f"set 7 {milestones.MODES.index(milestones.DEFAULT_MODE)}", expect='HIL_SET', timeout=6)
    d.hil('set 8 0', expect='HIL_SET', timeout=6)
    fx_client.clear(d)
    rig.note('songs: ' + '; '.join(f"{x['song']} music {x['share']:.1%} of core 0, IDLE0 gap {x['max_gap_us']} us, "
                                   f"ADC quiet {x.get('quiet', {}).get('min')}..{x.get('quiet', {}).get('max')} "
                                   f"playing {x.get('playing', {}).get('min')}..{x.get('playing', {}).get('max')}"
                                   for x in results))
    rig.scenario['songs'] = results


def effect_bench(rig, fixed=False):
    """`!effectbench` on the recap: each cost-model primitive's cycles per
    call, then every drawn effect drawn over the recap every 100 ms, each
    frame timed and hashed, so two builds can be compared frame for frame
    (a frame hash that changes between builds means different pixels)."""
    settle(rig)
    d = rig.dev
    r = rig.pull(signals.standard_pull(15500), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=60))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(2.5)
    start = d.mark()
    done = d.hil('effectbench 2000 100 fixed' if fixed else 'effectbench', expect='HIL_EFFECT_BENCH_DONE', timeout=240)
    lines = [l.text for l in d.since(start, 'HIL_EFFECT_BENCH')]
    ops = next((l for l in lines if l.startswith('HIL_EFFECT_BENCH_OPS ')), '')
    effects = [l for l in lines if l.startswith('HIL_EFFECT_BENCH effect=')]
    rig.check('effectbench timed every primitive', ' ok=1 ' in ops, ops or 'no HIL_EFFECT_BENCH_OPS')
    drawn = int(number(done.f.get('effects'), 0))
    rig.check('effectbench drew every effect', drawn > 0 and len(effects) == drawn, f'{len(effects)} of {drawn}; {done.text}')
    if fixed:
        # The device must draw exactly the host's frames (tests, cost model and web previews use the host's).
        try:
            host = host_bench_hashes()
        except (OSError, subprocess.CalledProcessError) as e:
            rig.check('host frames to compare with (ui_render --bench-hashes)', False, str(e), severity='warn')
        else:
            device = {m.group(1): m.group(2) for m in (re.match(r'HIL_EFFECT_BENCH effect=(\w+) frames=([0-9a-f]+)', l) for l in effects) if m}
            differ = sorted(e for e in host if device.get(e) != host[e])
            rig.check('every effect draws exactly the host\'s frames', not differ and len(device) == len(host),
                      f'{len(differ)} of {len(host)} differ: {", ".join(differ[:20])}' if differ else f'{len(host)} effects equal')
    rig.scenario['effect_bench'] = lines
    rig.note(ops)


# ---------------------------------------------------------------- wear check
WEAR_PAGE, MENU_PAGE = '10', '1'  # Controls::Page::WearCheck, Menu
# Accelerometer at rest (board axes, Top frame): level; ripcord (+Y) toward the
# floor (2/3 Pull Down); ripcord toward the ceiling (3/3 Pull Up); 30 degrees off level.
WEAR_LEVEL, WEAR_DOWN, WEAR_UP, WEAR_OFF = (0.0, 0.0, 1.0), (0.0, -1.0, 0.0), (0.0, 1.0, 0.0), (0.5, 0.0, 0.866)
# A Sidecar unit sits on its side: board +X up with the launcher level (its
# screen faces up only with the launcher on end). The ripcord poses are as Top.
WEAR_SIDECAR_LEVEL = (1.0, 0.0, 0.0)
MOUNT_ROW = 9  # Controls::Setting::Mount
BLUETOOTH_ROW = 14  # Controls::Setting::Bluetooth: no Settings row, changed on the Bluetooth page


def ui_state(rig):
    return rig.dev.status().get('UI_STATUS', {})


def key(rig, letter, wait=0.4):
    """One USB button letter on its own line: a=A, h=B, b=hold B."""
    rig.dev.write(letter + '\n')
    time.sleep(wait)


# The Menu top to bottom (Controls::MenuRow, 0.20.9's order): B from Main opens it on
# row 0, and each B after that goes one row down.
MENU_ROWS = ['History', 'New session', 'Tournament', 'String wear check', 'Mic RPM', 'Battery', 'Bluetooth', 'Settings', 'Back']
SETTINGS_MENU_ROW, BATTERY_MENU_ROW, WEAR_MENU_ROW = MENU_ROWS.index('Settings'), MENU_ROWS.index('Battery'), MENU_ROWS.index('String wear check')

ABOUT_PAGE, SETTINGS_PAGE, ABOUT_ROW = '12', '5', '12'  # Controls::Page::About, Page::Settings, Setting::About
# The Settings rows top to bottom (Controls::ORDER, then Back as SETTING_COUNT):
# Battery icon after Mounting, then Wipe all and About.
SETTINGS_ORDER = ['0', '10', '1', '2', '4', '3', '5', '6', '7', '8', '9', '13', '11', '12', '15']


def settings_walk(rig, target):
    """B on Settings as an owner presses it: one full lap of the rows, then on
    to `target`. Settings keeps the row it was last on (a `!set` leaves it on
    its row), so the lap starts wherever that is. Returns whether the cursor
    went through the rows in SETTINGS_ORDER, and the rows it stood on."""
    rows = [ui_state(rig).get('setting')]
    if rows[0] not in SETTINGS_ORDER:
        return False, rows
    start = SETTINGS_ORDER.index(rows[0])
    steps = len(SETTINGS_ORDER) + (SETTINGS_ORDER.index(target) - start) % len(SETTINGS_ORDER)
    for _ in range(steps):
        key(rig, 'h', 0.2)
        rows.append(ui_state(rig).get('setting'))
    return rows == [SETTINGS_ORDER[(start + i) % len(SETTINGS_ORDER)] for i in range(steps + 1)], rows


def bluetooth_off(rig):
    """Bluetooth (0.20.9, experimental) is Off unless the owner turns it on
    under Menu, Bluetooth. Off, nothing of it runs: the stack was never started
    (no init task, no reply buffer, no controller, so no radio and no
    advertising), from boot. The rig's unit never has it on: a unit that does
    fails here; run_hil's preflight turns it off, with a restart, before the
    first scenario."""
    settle(rig)
    b = rig.dev.status().get('BLE_STATUS', {})
    rig.check('Bluetooth is Off: its status line is there and the stack never started',
              b.get('started') == '0' and b.get('init_failed') == '0' and b.get('state') == '0', str(b))
    rig.check('no radio: not advertising, no link since boot, nothing allocated for it',
              b.get('adv_active') == '-1' and b.get('connects') == '0' and b.get('notifies') == '0' and b.get('heap_before') == '0', str(b))


def about_page(rig):
    """Settings, About (0.20.8), by the buttons as an owner reaches it: the row
    after Wipe all opens a page of the firmware, the unit's ID and its saved
    pulls. B does nothing there, A and hold B return to its row, and nothing
    is applied or saved."""
    settle(rig)
    d = rig.dev
    d.hil('home')
    key(rig, 'h')  # Main: B opens the Menu on its first row
    for _ in range(SETTINGS_MENU_ROW):  # down to Settings
        key(rig, 'h', 0.2)
    key(rig, 'a')
    u = ui_state(rig)
    rig.check('the Menu opens Settings', u.get('page') == SETTINGS_PAGE, str(u))
    ordered, rows = settings_walk(rig, ABOUT_ROW)
    u = ui_state(rig)
    rig.check('B goes through the rows in order, About after Battery icon and Wipe all, and stops on About',
              ordered and u.get('page') == SETTINGS_PAGE and u.get('setting') == ABOUT_ROW, f'rows={rows} {u}')
    rig.screenshot('settings_about_row')
    before = d.status().get('SETTINGS_SAVE_STATUS', {})
    key(rig, 'a')
    u = ui_state(rig)
    rig.check('A opens About', u.get('page') == ABOUT_PAGE, str(u))
    rig.screenshot('about')
    key(rig, 'h')
    rig.check('B does nothing on About', ui_state(rig).get('page') == ABOUT_PAGE)
    key(rig, 'a')
    u = ui_state(rig)
    rig.check('A returns to the About row', u.get('page') == SETTINGS_PAGE and u.get('setting') == ABOUT_ROW, str(u))
    key(rig, 'a')
    key(rig, 'b', 1.0)
    u = ui_state(rig)
    rig.check('hold B returns to the About row too', u.get('page') == SETTINGS_PAGE and u.get('setting') == ABOUT_ROW, str(u))
    after = d.status().get('SETTINGS_SAVE_STATUS', {})
    rig.check('nothing was applied or saved', after.get('saves') == before.get('saves') and after.get('pending') == before.get('pending'),
              f'{before} -> {after}')
    d.hil('home')


BATTERY_ICON_ROW, EDIT_PAGE, BATTERY_PAGE = '13', '6', '4'  # Setting::BatteryIcon, Page::Edit, Page::Battery
CORNER_FILL = 14  # ControlsUI::CornerBattery::FILL


def corner(rig, name):
    """The top right corner of the frame on screen (ControlsUI::CornerBattery:
    an 18 x 8 outline from 24 px left of the right edge, 2 px down, and a
    1-column terminal): the outline's and terminal's pixels, the fill's width
    in its middle row, whether the charging bolt is drawn, and whether
    anything is drawn just left of it."""
    time.sleep(0.4)
    snap = rig.dev.snapshot()
    if snap is None:
        raise DeviceError('no screen snapshot')
    rig.screenshot(name)
    def pixel(x, y):
        i = (y * snap.width + x) * 2
        return snap.data[i] << 8 | snap.data[i + 1]
    background, x0 = pixel(2, 120), snap.width - 24
    outline = all(pixel(x, y) != background for x, y in ((x0 + 3, 2), (x0 + 3, 9), (x0, 5), (x0 + 17, 5), (x0 + 18, 5)))
    fill = sum(1 for x in range(x0 + 2, x0 + 2 + CORNER_FILL) if pixel(x, 5) != background)
    # The charging bolt's two ends, a row above and below the outline.
    bolt = all(pixel(x, y) != background for x, y in ((x0 + 10, 1), (x0 + 11, 1), (x0 + 6, 10), (x0 + 7, 10)))
    beside = any(pixel(x, y) != background for x in range(x0 - 4, x0) for y in range(0, 11))
    blank = not any(pixel(x, y) != background for x in range(x0, snap.width) for y in range(0, 11))
    return {'outline': outline, 'fill': fill, 'beside': beside, 'blank': blank, 'bolt': bolt}


def expected_fill(status):
    """The fill ControlsUI::cornerBattery draws for the unit's reading."""
    power = status.get('POWER_STATUS', {})
    pct = int(number(power.get('battery_pct'), -1))
    return max(1, (min(pct, 100) * CORNER_FILL + 50) // 100) if power.get('battery_valid') == '1' and pct >= 0 else 0


def charging(status):
    return status.get('POWER_STATUS', {}).get('charging') == 'charging'


def expected_fill_pixels(status):
    """What corner() counts in the icon's middle row: the fill, or while the
    unit charges the fill outside the bolt's halo (columns 5 to 12 of the
    icon) and the bolt's own five columns."""
    fill = expected_fill(status)
    if not fill or not charging(status):
        return fill
    return sum(1 for column in range(2, 2 + fill) if column < 5 or column > 12) + 5


def battery_icon(rig):
    """Battery icon (0.20.8): the charge in the top right corner of the Menu
    and Settings pages, on by default. The Menu's corner holds the outline and
    a fill as wide as the unit's reading, with a bolt through it while the unit
    charges; Main has it only while charging, and the Battery page never. Settings by the buttons: the row after Mounting
    edits it, B previews Off in the corner, hold B cancels, A applies and
    saves; Off survives a reboot, and On brings it back."""
    settle(rig)
    d = rig.dev
    try:
        d.hil('set 13 1', expect='HIL_SET')
        d.hil('home')
        time.sleep(5.5)  # a battery poll on Main, so the corner follows the charge state read below
        status = d.status()
        c = corner(rig, 'battery_icon_main')
        u = status.get('UI_STATUS', {})
        rig.check('Battery icon is On', u.get('battery_icon') == '1', str(u))
        # Main shows the corner battery only while the unit charges (0.20.9), with the bolt.
        if charging(status):
            rig.check('Main, charging: the corner battery with its bolt',
                      c['outline'] and c['bolt'] and not c['beside'] and u.get('corner_bolt') == '1' and u.get('corner_fill') == str(expected_fill(status)),
                      f"{c} {u} {status.get('POWER_STATUS')}")
        else:
            rig.check('Main, not charging: no corner battery', c['blank'] and u.get('corner_fill') == '0' and u.get('corner_bolt') == '0',
                      f"{c} {u} {status.get('POWER_STATUS')}")
        key(rig, 'h')  # Main: B opens the Menu
        status = d.status()
        c = corner(rig, 'battery_icon_menu')
        u = status.get('UI_STATUS', {})
        want = expected_fill(status)
        rig.check('the Menu draws the corner battery: outline and terminal', u.get('page') == MENU_PAGE and c['outline'] and not c['beside'], f'{c} {u}')
        rig.check('the fill is as wide as the reading', want > 0 and c['fill'] == expected_fill_pixels(status) and u.get('corner_fill') == str(want),
                  f"{c} corner_fill={u.get('corner_fill')} want={want} {status.get('POWER_STATUS', {}).get('battery_pct')}%")
        rig.check('the bolt shows exactly while the unit charges', c['bolt'] == charging(status) and u.get('corner_bolt') == ('1' if charging(status) else '0'),
                  f"{c} {u} {status.get('POWER_STATUS')}")
        # The Battery page shows its own battery, not the corner one.
        for _ in range(BATTERY_MENU_ROW):  # down to Battery
            key(rig, 'h', 0.2)
        key(rig, 'a')
        u = ui_state(rig)
        rig.check('the Battery page has no corner battery', u.get('page') == BATTERY_PAGE and u.get('corner_fill') == '0', str(u))
        d.hil('home')
        key(rig, 'h')
        for _ in range(SETTINGS_MENU_ROW):  # down to Settings
            key(rig, 'h', 0.2)
        key(rig, 'a')
        ordered, rows = settings_walk(rig, BATTERY_ICON_ROW)
        u = ui_state(rig)
        c = corner(rig, 'settings_battery_icon_row')
        rig.check('B goes through the rows in order and stops on Battery icon, after Mounting',
                  ordered and u.get('page') == SETTINGS_PAGE and u.get('setting') == BATTERY_ICON_ROW, f'rows={rows} {u}')
        rig.check('Settings draws the corner battery', c['outline'] and c['fill'] > 0 and not c['beside'], str(c))
        key(rig, 'a')
        key(rig, 'h')
        u = ui_state(rig)
        c = corner(rig, 'battery_icon_editor_off')
        rig.check('B previews Off: the editor shows no corner battery, nothing applied yet',
                  u.get('page') == EDIT_PAGE and u.get('draft') == '0' and u.get('battery_icon') == '1' and c['blank'], f'{u} {c}')
        key(rig, 'b', 1.0)
        u = ui_state(rig)
        c = corner(rig, 'battery_icon_cancelled')
        rig.check('hold B cancels: still On, the corner battery back', u.get('page') == SETTINGS_PAGE and u.get('battery_icon') == '1' and c['outline'],
                  f'{u} {c}')
        start = d.mark()
        key(rig, 'a')
        key(rig, 'h')
        key(rig, 'a', 1.0)
        u = ui_state(rig)
        applied = d.wait(lambda l: l.text.startswith('SETTING_APPLY row=13'), 3, since=start)
        saved = d.wait(lambda l: l.tag == 'SETTINGS_SAVE' and l.f.get('saved') == '0x800', 5, since=start)
        rig.check('A applies Off and saves its key', u.get('battery_icon') == '0' and applied is not None and 'value=0 applied=1 changed=1' in applied.text
                  and saved is not None, f"{u} {applied.text if applied else 'no SETTING_APPLY'} {saved.text if saved else 'no SETTINGS_SAVE 0x800'}")
        c = corner(rig, 'battery_icon_off_settings')
        rig.check('Off: Settings has no corner battery', c['blank'] and ui_state(rig).get('corner_fill') == '0', str(c))
        d.hil('home')
        key(rig, 'h')
        c = corner(rig, 'battery_icon_off_menu')
        rig.check('Off: the Menu has no corner battery', c['blank'] and ui_state(rig).get('corner_fill') == '0', str(c))
        reboot(rig)
        settle(rig)
        d.hil('home')
        key(rig, 'h')
        c = corner(rig, 'battery_icon_off_rebooted')
        u = ui_state(rig)
        rig.check('Off survives a reboot', u.get('battery_icon') == '0' and u.get('page') == MENU_PAGE and c['blank'], f'{c} {u}')
        d.hil('set 13 1', expect='HIL_SET')
        d.hil('home')
        key(rig, 'h')
        c = corner(rig, 'battery_icon_on_again')
        rig.check('On brings the corner battery back to the Menu', c['outline'] and c['fill'] > 0, str(c))
    finally:
        d.hil('set 13 1', expect='HIL_SET')
        d.hil('home')


def frame_rows(rig):
    """Changed-row display pushes (frame_rows.h, 0.20.6): tilting the unit on
    the Main page redraws only the level, so most frames send far fewer than
    the panel's 240 rows, a whole frame goes out at least every 10 s, and a
    pull still reads correctly. PERF counts frames, whole frames, frames with
    nothing to send and rows sent. The panel cannot be read back: a person
    checks that it looks right while tilting."""
    settle(rig)
    d = rig.dev
    first = d.status().get('PERF', {})
    rig.check('PERF reports the push counters', all(k in first for k in ('frames', 'whole_frames', 'unchanged_frames', 'pushed_rows')), str(first))
    rig.check('the row hashes were allocated (in PSRAM)', first.get('row_hashes') == '1', str(first.get('row_hashes')))
    # The Main page's level is in the mount's frame (these poses read 60 and
    # 90 there, 30 and 0 on the Wear check page): wait for each to settle.
    readings = [settle_level(rig, gravity) for gravity in (WEAR_OFF, WEAR_LEVEL, WEAR_OFF, WEAR_LEVEL)]
    rig.check('the level settles at each pose and moves between them',
              None not in readings and min(abs(a - b) for a, b in zip(readings, readings[1:])) >= 20, str(readings))
    last = d.status().get('PERF', {})
    delta = {k: int(number(last.get(k), 0)) - int(number(first.get(k), 0)) for k in ('frames', 'whole_frames', 'unchanged_frames', 'pushed_rows')}
    partial = delta['frames'] - delta['whole_frames'] - delta['unchanged_frames']
    rig.note(f'frames={delta["frames"]} whole={delta["whole_frames"]} unchanged={delta["unchanged_frames"]} partial={partial} rows={delta["pushed_rows"]}')
    rig.check('the level redrew', delta['frames'] >= 10, str(delta))
    rig.check('frames sent only part of the panel', partial > 0, str(delta))
    rig.check('fewer rows than whole frames would send', delta['pushed_rows'] < 240 * delta['frames'], str(delta))
    rig.check('a whole frame still goes out', delta['whole_frames'] >= 1, str(delta))
    if partial > 0:
        rig.note(f"rows per partial frame: {(delta['pushed_rows'] - 240 * delta['whole_frames']) / partial:.0f} of 240")
    r = rig.pull(signals.standard_pull(9000, label='pull after tilting'), signals.launch_motion(seed=78))
    rig.check_pull(r, tolerance=0.03, saturated=False)
    drain_history(rig)


def settle_level(rig, gravity, timeout=45):
    """The unit held still at `gravity` until its level reading stops moving;
    the degrees it settled at, or None."""
    d = rig.dev
    d.hil('const {:.4f} {:.4f} {:.4f} 0 0 0 0.004 0.08'.format(*gravity))
    d.hil('imu const')
    time.sleep(1.5)  # fusion has started to follow the new pose
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        level = d.status().get('LEVEL_STATUS', {})
        degrees = number(level.get('degrees'), None) if level.get('state') == 'valid' else None
        if degrees is not None and last is not None and abs(degrees - last) <= 0.2:
            return degrees
        last = degrees
        time.sleep(1.0)
    return None


def hold_still(rig, gravity, degrees, timeout=45):
    """The unit held still at `gravity` until its level reads `degrees`: fusion
    follows the accelerometer over a few seconds. Both vertical poses read 90,
    so a 180-degree flip between them goes through level first."""
    d = rig.dev
    d.hil('const {:.4f} {:.4f} {:.4f} 0 0 0 0.004 0.08'.format(*gravity))
    d.hil('imu const')
    deadline = time.time() + timeout
    level = {}
    while time.time() < deadline:
        level = d.status().get('LEVEL_STATUS', {})
        if level.get('state') == 'valid' and abs(number(level.get('degrees'), -99) - degrees) <= 1.0:
            return True
        time.sleep(0.5)
    rig.check(f'level reads {degrees} degrees', False, f"state={level.get('state')} degrees={level.get('degrees')}")
    return False


JUDGED = ('accepted', 'completed', 'rejected_level', 'rejected_vertical', 'rejected_flip', 'rejected_no_imu', 'rejected_signal')


def wear_pull(rig, optical, outcomes, stage, count, out_rpm=4800, tolerance=0.03, shots=()):
    """One scripted Wear check pull (signals.wear_pull: out, hold, let go): its
    WEAR_CHECK_PULL lines in order (`outcomes`, e.g. ('extension', 'accepted')),
    each showing `count` pulls of `stage` counted after it (the pull out only
    after the retraction), the pull out's RPM only on its extension line, the
    retraction's on the judged one, and no trace in history or motion. shots: screenshots taken
    'hold' (after the pull out's result), 'let_go' (at LET GO) and 'wait'
    (inside the rearm second after the retraction, where PULL OUT waits)."""
    d = rig.dev
    rig.wait_ready(15, fusion=False)
    signals.program_optical(d, optical)
    d.hil('imu const')
    start = d.mark()
    d.hil('pull 60', expect='HIL_PULL')
    def final(l):
        return l.text.startswith('WEAR_CHECK_PULL') and l.f.get('outcome') != 'extension'
    label = f'{optical.label}: {" then ".join(outcomes)}'
    if 'hold' in shots:
        out = d.wait(lambda l: l.text.startswith('WEAR_CHECK_PULL') and l.f.get('outcome') == 'extension', optical.duration_ms / 1000, since=start)
        if out:
            rig.screenshot('wear_check_hold')
    if 'let_go' in shots:
        if d.wait(lambda l: l.text.startswith('WEAR_CHECK_STEP') and l.f.get('step') == 'let_go', optical.duration_ms / 1000 + 2, since=start):
            rig.screenshot('wear_check_let_go')
    end = d.wait(final, optical.duration_ms / 1000 + 4.0, since=start)
    if 'wait' in shots and end:
        rig.screenshot('wear_check_wait')
        taken = time.monotonic()
        ready = d.wait(lambda l: l.text.startswith('RPM_STATE state=READY'), 5, since=end.index)
        rig.check(f'{label}: screenshot inside the rearm second (WAIT)', ready is not None and ready.t > taken,
                  f'READY {ready.t - taken:+.2f} s after the screenshot' if ready else 'no READY')
    time.sleep(0.3)
    lines = d.since(start, 'WEAR_CHECK_PULL')
    got = tuple(l.f.get('outcome') for l in lines)
    rig.check(f'{label}: outcomes in order', got == tuple(outcomes), ' | '.join(l.text for l in lines) or 'no WEAR_CHECK_PULL')
    counted = outcomes[-1] in ('accepted', 'completed')
    for line in lines:
        f, rpm = line.f, number(line.f.get('rpm'), 0)
        before = f.get('outcome') == 'extension' and counted
        rig.check(f'{label}: {f.get("outcome")} line shows {stage} {count - before}/3',
                  f.get('stage') == stage and f.get('pull') == f'{count - before}/3', line.text)
        if f.get('outcome') == 'extension':
            rig.check(f'{label}: the pull out carries its own RPM', abs(rpm - out_rpm) <= tolerance * out_rpm, line.text)
        elif f.get('outcome') in JUDGED:
            rig.check(f'{label}: the judged pull is the retraction, never the pull out', abs(rpm - optical.expected_rpm) <=
                      tolerance * optical.expected_rpm and abs(rpm - out_rpm) > tolerance * out_rpm, line.text)
    rig.check(f'{label}: kept out of history and motion', not any(d.since(start, t) for t in ('HISTORY_ADDED', 'MOTION_RESULT', 'CELEBRATION')))
    rig.scenario['pulls'].append({'label': label, 'wear': [l.text for l in lines]})
    print('   ', label, '->', ' | '.join(l.text for l in lines) or 'no WEAR_CHECK_PULL')
    return lines[-1] if lines else None


def wear_check(rig):
    """Wear check through the real buttons: its Menu row opens it; three pulls
    level, three with the ripcord down, three with it up. Each pull is pulled
    out (a measured burst), held, and let go (0.20.4): only the retraction is
    judged, the pull out is never counted. A pull 30 degrees off level and one
    flipped the wrong way are rejected and retried. Its pulls never reach
    history, bests, motion or a celebration; leaving it hands pulls back to
    history. Then the steps' edges: 1,200 and 25,000 RPM retractions, a slow
    pull out the detector does not measure, a long hold (LET GO), the string
    let go too early (no measurement, retry) and a retraction too slow to
    measure (missed, retry)."""
    d = rig.dev
    settle(rig)
    began = d.mark()
    # With no bests, any pull that leaked into them would be a new best. The
    # clear saves; take the baseline after that save.
    mark = d.mark()
    d.hil('bests clear')
    d.wait('HISTORY_SAVE', 20, since=mark)
    time.sleep(1)
    before = d.status()
    bests = d.hil('bests', expect='HIL_OK bests').f
    start = d.mark()
    key(rig, 'h')
    for _ in range(WEAR_MENU_ROW):
        key(rig, 'h', 0.3)
    u = ui_state(rig)
    rig.check('B goes down the Menu to String wear check', u.get('page') == MENU_PAGE and u.get('menu') == str(WEAR_MENU_ROW), str(u))
    key(rig, 'a')
    u = ui_state(rig)
    rig.check('A on row 5 opens Wear check', u.get('page') == WEAR_PAGE, str(u))
    rig.screenshot('wear_check_level')

    # 1/3 Level, with a pull 30 degrees off level rejected and retried.
    ok = ('extension', 'accepted')
    wear_pull(rig, signals.wear_pull(3200), ok, 'level', 1, shots=('hold', 'wait'))
    hold_still(rig, WEAR_OFF, 30)
    wear_pull(rig, signals.wear_pull(3300), ('extension', 'rejected_level'), 'level', 1)
    rig.screenshot('wear_check_retry')
    hold_still(rig, WEAR_LEVEL, 0)
    wear_pull(rig, signals.wear_pull(3260, held_ms=3000), ok, 'level', 2, shots=('let_go',))
    wear_pull(rig, signals.wear_pull(3320), ok, 'level', 3)
    # 2/3 Pull Down; the first accepted pull fixes which way is down, so a
    # pull flipped 180 degrees is rejected.
    hold_still(rig, WEAR_DOWN, 90)
    rig.screenshot('wear_check_down')
    wear_pull(rig, signals.wear_pull(2000), ok, 'pull_down', 1)
    hold_still(rig, WEAR_LEVEL, 0)
    hold_still(rig, WEAR_UP, 90)
    wear_pull(rig, signals.wear_pull(2100), ('extension', 'rejected_flip'), 'pull_down', 1)
    rig.screenshot('wear_check_flip')
    hold_still(rig, WEAR_LEVEL, 0)
    hold_still(rig, WEAR_DOWN, 90)
    wear_pull(rig, signals.wear_pull(2040), ok, 'pull_down', 2)
    wear_pull(rig, signals.wear_pull(2080), ok, 'pull_down', 3)
    # 3/3 Pull Up.
    hold_still(rig, WEAR_LEVEL, 0)
    hold_still(rig, WEAR_UP, 90)
    wear_pull(rig, signals.wear_pull(3700), ok, 'pull_up', 1)
    wear_pull(rig, signals.wear_pull(3740), ok, 'pull_up', 2)
    done = d.mark()
    wear_pull(rig, signals.wear_pull(3780), ('extension', 'completed'), 'pull_up', 3)
    result = d.wait('WEAR_CHECK_RESULT', 2.0, since=done)
    f = result.f if result else {}
    want = {'level_rpm': 3260, 'pull_down_rpm': 2040, 'pull_up_rpm': 3740, 'rpm': (3260 + 2040 + 3740) / 3}
    rig.check('wear check result: nine retractions, averages per stage within 3%', result is not None and f.get('pulls') == '9' and
              all(abs(number(f.get(k), 0) - v) <= 0.03 * v for k, v in want.items()), result.text if result else 'no WEAR_CHECK_RESULT')
    rig.screenshot('wear_check_result')

    after = d.status()
    bests_after = d.hil('bests', expect='HIL_OK bests').f
    for tag, field in (('HISTORY_STATUS', 'records'), ('HISTORY_STATUS', 'generation'), ('HISTORY_STATUS', 'saves')):
        rig.check(f'wear check leaves {tag} {field} unchanged', before.get(tag, {}).get(field) == after.get(tag, {}).get(field),
                  f"{before.get(tag, {}).get(field)} -> {after.get(tag, {}).get(field)}")
    rig.check('wear check leaves personal bests unchanged', bests_after.get('revision') == bests.get('revision'),
              f"revision {bests.get('revision')} -> {bests_after.get('revision')}")
    rig.check('no history, motion, celebration or recap during the check',
              not any(d.since(start, t) for t in ('HISTORY_ADDED', 'MOTION_RESULT', 'CELEBRATION', 'RECAP_PRESENT')))

    # A starts again; a reset check accepts pulls from 1/3.
    key(rig, 'a')
    hold_still(rig, WEAR_LEVEL, 0)
    rig.check('A on the result starts a new check', ui_state(rig).get('page') == WEAR_PAGE)
    # The detector's window: its 1,000 RPM launch floor, and fast retractions.
    wear_pull(rig, signals.wear_pull(1200, ramp_ms=500, hold_ms=600), ok, 'level', 1)
    wear_pull(rig, signals.wear_pull(25000, ramp_ms=300, hold_ms=150), ok, 'level', 2)
    # Let go 400 ms after the pull out, before LET GO: the detector takes the
    # retraction as the pull's rewind; nothing is counted and the page asks
    # for a longer hold.
    early = d.mark()
    wear_pull(rig, signals.wear_pull(3150, held_ms=400), ('extension', 'early_release'), 'level', 2)
    rig.check('let go early: the retraction is not measured', len(d.since(early, 'LAUNCH_RESULT')) == 1)
    rig.screenshot('wear_check_early')
    # A retraction under the 1,000 RPM launch floor after LET GO: missed.
    wear_pull(rig, signals.wear_pull(800), ('extension', 'missed'), 'level', 2)
    # Pulled out slowly (600 RPM, under the launch floor), held, let go: the
    # retraction is measured although its pull out was not.
    wear_pull(rig, signals.wear_pull(3100, out_rpm=600), ('accepted',), 'level', 3, out_rpm=600)

    # Leaving mid-check: hold B to the Menu (row 5), hold B to Main; pulls go to history again.
    hold_still(rig, WEAR_DOWN, 90)
    wear_pull(rig, signals.wear_pull(2050), ok, 'pull_down', 1)
    key(rig, 'b', 0.6)
    u = ui_state(rig)
    rig.check('hold B leaves Wear check for its Menu row', u.get('page') == MENU_PAGE and u.get('menu') == str(WEAR_MENU_ROW), str(u))
    key(rig, 'b', 0.6)
    rig.check('hold B again returns to Main', ui_state(rig).get('page') == '0')
    hold_still(rig, WEAR_LEVEL, 0)
    rig.wait_ready(15)
    start = d.mark()
    r = rig.pull(signals.standard_pull(6000), signals.launch_motion(seed=41))
    rig.check_pull(r)
    rig.check('a pull after leaving Wear check is not a wear pull', not d.since(start, 'WEAR_CHECK_PULL'))
    drain_history(rig)
    wear_check_sidecar(rig)
    # The only start-over is the scenario's own A on the result; nothing
    # presses the rig's buttons.
    resets = d.since(began, 'WEAR_CHECK_RESET')
    rig.check('the only reset is the scenario A after the result', len(resets) == 1 and resets[0].f.get('source') == 'usb_a'
              and resets[0].f.get('stage') == 'complete', ' | '.join(l.text for l in resets) or 'no WEAR_CHECK_RESET')
    buttons = d.since(began, 'HIL_BUTTON')
    rig.check('no physical button edge on the rig', not buttons, ' | '.join(l.text for l in buttons[:3]))


def open_wear_check(rig):
    """From Main: B until the Menu is on its row (a recap still on Main takes
    the first B), then A opens it."""
    for _ in range(10):
        u = ui_state(rig)
        if u.get('page') == MENU_PAGE and u.get('menu') == str(WEAR_MENU_ROW):
            break
        key(rig, 'h', 0.3)
    key(rig, 'a')
    return ui_state(rig).get('page') == WEAR_PAGE


def wear_check_sidecar(rig):
    """Settings, Mounting = Sidecar (0.20.4, owner): the tilt is read in the
    Sidecar frame. The unit on its side is level and accepted, the Top level
    pose (screen up) is 90 degrees off and refused, and the ripcord poses work
    as on Top. The page names its Mounting. Mounting goes back to Top."""
    d = rig.dev
    line = d.hil(f'set {MOUNT_ROW} 1', expect='HIL_SET')
    try:
        rig.check('Mounting set to Sidecar', line is not None and line.f.get('error') == '0', line.text if line else 'no HIL_SET')
        d.hil('home')
        mount = d.status().get('MOUNT_STATUS', {})
        rig.check('IMU maps the Sidecar frame to the launcher', mount.get('selected') == 'Sidecar' and mount.get('imu_frame') == 'Sidecar'
                  and mount.get('accel_gyro_frame') == 'launcher', str(mount))
        hold_still(rig, WEAR_SIDECAR_LEVEL, 0)
        rig.check('Sidecar: the Menu row opens String wear check', open_wear_check(rig))
        rig.screenshot('wear_check_sidecar')
        ok = ('extension', 'accepted')
        wear_pull(rig, signals.wear_pull(3150), ok, 'level', 1)
        hold_still(rig, WEAR_LEVEL, 90)
        wear_pull(rig, signals.wear_pull(3250), ('extension', 'rejected_level'), 'level', 1)
        hold_still(rig, WEAR_SIDECAR_LEVEL, 0)
        wear_pull(rig, signals.wear_pull(3200), ok, 'level', 2)
        wear_pull(rig, signals.wear_pull(3300), ok, 'level', 3)
        hold_still(rig, WEAR_DOWN, 90)
        wear_pull(rig, signals.wear_pull(2100), ok, 'pull_down', 1)
    finally:
        d.hil('home')
        line = d.hil(f'set {MOUNT_ROW} 0', expect='HIL_SET')
        d.hil('home')
        hold_still(rig, WEAR_LEVEL, 0)
        mount = d.status().get('MOUNT_STATUS', {})
        rig.check('Mounting back to Top', line is not None and line.f.get('error') == '0' and mount.get('selected') == 'Top', str(mount))



MIC_PAGE = '11'  # Controls::Page::MicRpm
MIC_WARMUP_S = 1.0  # MicListener::WARMUP_MS: the ES8311 gives zeros until then
MIC_ROW = MENU_ROWS.index('Mic RPM')


def mic_wait(rig, state, timeout, since):
    """The session reaching `state`, by its own MIC_STATE line."""
    return rig.dev.wait(lambda l: l.text.startswith('MIC_STATE ') and l.f.get('state') == state, timeout, since=since)


def mic_open(rig):
    """The page by its Menu row, as an owner reaches it; returns MIC_STATE on."""
    d = rig.dev
    d.hil('home')
    start = d.mark()
    for _ in range(MIC_ROW + 4):  # a recap still on Main takes the first B
        u = ui_state(rig)
        if u.get('page') == MENU_PAGE and u.get('menu') == str(MIC_ROW):
            break
        key(rig, 'h', 0.2)
    key(rig, 'a', 0.1)
    return mic_wait(rig, 'on', 4, start)


def mic_tone_pull(rig, rpm, tone):
    """A scripted pull with the sound of `tone` (hz, fall, seconds, noise_s) after it."""
    d = rig.dev
    rig.wait_ready(15, fusion=False)
    d.hil('mictone ' + ' '.join(str(v) for v in tone), expect='HIL_OK')
    return rig.pull(signals.standard_pull(rpm), wait_motion=False)


def mic_spin(rig, launch, timeout, since):
    return rig.dev.wait(lambda l: l.text.startswith('MIC_SPIN ') and l.f.get('launch') == launch, timeout, since=since)


def gaps_beyond_saves(lines, before=0):
    """The IMU gaps in `lines` that a history log save does not explain.

    Every status reprints the unit's recent gaps (MOTION_GAP n=...), so only
    those numbered past `before`, the gaps count when the window opened, are
    new: in HIL all (0.20.7 gate) a gap from an earlier scenario was counted.

    A log append runs without an IMU quiet window (its comment says why), so its
    flash stall can stretch a poll past the 35 ms that counts as a gap. The
    0.20.5 release gate's run, with no mic, has two: 41,872 and 44,799 us, polls
    of 17-19 ms, on saves. Only that shape, on a save, passes here.

    A gap carries the unit's uptime (at_ms) and a save only the host's read
    time, so the two clocks are tied by the HIL_STATUS lines in the window:
    each prints uptime_ms as it is sent, and the smallest difference is the one
    the host read soonest. LAUNCH_RESULT cannot tie them: its t_us is the
    launch and it prints when the pull has ended, a second later, which put a
    save's gap a second after its save (0.20.9 gate: 1.04 s against 0.03 s)."""
    offsets = [l.t - number(l.f.get('uptime_ms'), 0) / 1000 for l in lines
               if l.text.startswith('HIL_STATUS ') and 'uptime_ms' in l.f]
    saves = [l.t for l in lines if l.text.startswith('HISTORY_SAVE ')]
    seen, unexplained = {}, []
    for line in lines:
        if line.text.startswith('MOTION_GAP ') and number(line.f.get('n'), 0) > before:
            seen[line.f.get('at_ms')] = line.f
    for at, f in seen.items():
        stretched = number(f.get('gap_us'), 1e9) < 60000 and number(f.get('polls'), 99) <= 4 and number(f.get('max_poll_us'), 0) >= 10000
        when = int(at) / 1000 + min(offsets) if offsets else None
        if not (stretched and when is not None and any(abs(t - when) < 0.3 for t in saves)):
            unexplained.append(f)
    return len(seen), unexplained


def mic_gaps_beyond_saves(rig, start, before=0):
    """gaps_beyond_saves over the rig's lines since `start`; the caller reads a
    status inside that window first, for the clock tie."""
    return gaps_beyond_saves(rig.dev.since(start), before)


SAVE_STALL_POLLS = 3  # a save's stall counts under 60 ms (gaps_beyond_saves) and a poll is 20 ms


def missed_polls_beyond_saves(lines, base, after):
    """The polls the IMU task missed between two counter readings that the
    history log saves in `lines` do not cover.

    The same flash stall that stretches a poll into a gap (gaps_beyond_saves)
    can instead fall while the task sleeps and wake it a period late: a late
    poll that skips ahead, with nothing lost (the FIFO holds the samples). On
    the 14:C1 rig every save in mic_page did that (0.20.9 gate: nine saves of
    17-24 ms, one missed poll each, no gap), where the 7C:E8 rig's saves made
    none. Each save covers what a stall under 60 ms can skip; anything past
    that, or with no save, is the mic's."""
    saves = sum(1 for l in lines if l.text.startswith('HISTORY_SAVE '))
    return max(0, after['missed_polls'] - base['missed_polls'] - SAVE_STALL_POLLS * saves)


def mic_counters(block):
    motion = block.get('MOTION_STATUS', {})
    return {k: int(number(motion.get(k), 0)) for k in ('gaps', 'gate_misses', 'late_polls', 'missed_polls')}


def mic_page(rig):
    """The Mic RPM page on a device: off until it opens, the mic up and down
    without holding the IMU bus, the heap floor kept, the speaker given back by
    every way out, and a reading's real cost at 80 MHz with the page open.

    The sound is synthetic (`!mictone`, hil_mic.h, replacing each captured chunk),
    so this proves timing, memory and paths, not what the mic hears. The speaker
    is exercised for real.

    It starts from a fresh boot: MIC_STATUS's maxima (stop_max_ms,
    behind_max_ms) and the heap's low-water count from boot, and in HIL all
    sleep_blockers' deliberately stuck stop (30 s) runs earlier in the same one.
    """
    reboot(rig)
    d = rig.dev
    floor = heap_floor(rig)
    d.hil(f'micfloor {floor}', expect='HIL_OK')
    d.hil('home')
    before = d.status()
    mic = before.get('MIC_STATUS', {})
    rig.check('the mic is off until the page opens', mic.get('state') == 'off', str(mic))
    heap_off = int(number(before.get('HEAP_STATUS', {}).get('dma_free'), 0))
    ui = before.get('PERF', {})
    stalls = int(number(ui.get('ui_stalls'), 0))
    rig.note(f'dma_free {heap_off} with the mic off, floor {floor}')

    # Up, by the Menu row.
    started = mic_open(rig)
    rig.check('the Menu row opens the page and starts the mic', started is not None and ui_state(rig).get('page') == MIC_PAGE, str(ui_state(rig)))
    rig.screenshot('mic_warming')
    time.sleep(1.5)
    rig.screenshot('mic_ready')
    on = d.status()
    mic, heap = on.get('MIC_STATUS', {}), on.get('HEAP_STATUS', {})
    # The mic's own low-water (MIC_STATUS heap_low, taken each loop pass while it
    # runs), not dma_min_free: that is since boot, and in HIL all an earlier
    # scenario's effect preview had already taken it under the floor (0.20.6's
    # all run too) before this page opened.
    rig.check('the DMA heap stays above the floor with the mic on', min(int(number(heap.get('dma_free'), 0)), int(number(mic.get('heap_low'), 0))) >= floor,
              f"dma_free={heap.get('dma_free')} mic heap_low={mic.get('heap_low')} floor={floor} (since boot: dma_min_free={heap.get('dma_min_free')})")
    rig.check('a codec write holds the IMU gate for under 5 ms', 0 < int(number(mic.get('gate_max_us'), 0)) < 5000, f"gate_max_us={mic.get('gate_max_us')}")
    rig.check('the mic starts within 2 s', 0 < int(number(mic.get('start_ms'), 0)) < 2000, f"start_ms={mic.get('start_ms')}")
    rig.check('no speaker power while it runs', on.get('SOUND_STATUS', {}).get('powered') == '0', str(on.get('SOUND_STATUS')))
    rig.note(f"mic on: dma_free={heap.get('dma_free')} (was {heap_off}) psram_free={heap.get('psram_free')} start_ms={mic.get('start_ms')} gate_max_us={mic.get('gate_max_us')}")

    # The bench runs by itself on the first open.
    deadline = time.time() + 20
    block = on
    while time.time() < deadline and number(block.get('MIC_BENCH', {}).get('runs'), 0) < 1:
        time.sleep(1)
        block = d.status()
    bench = block.get('MIC_BENCH', {})
    rig.check('the bench ran on the first open', number(bench.get('runs'), 0) >= 1, str(bench))
    # The bench times each FFT once, by wall clock, in an idle-priority task: a
    # frame drawn in between lands in either figure (HIL all, 0.20.7 gate:
    # esp-dsp 31,113 us against 10,732, the smoke 7,986 against 8,684). Up to
    # three runs (USB Y), each FFT's fastest: preemption only ever adds.
    runs = [bench]
    while len(runs) < 3 and min(number(b.get('fft_us'), 1e9) for b in runs) > min(number(b.get('plain_fft_us'), 1e9) for b in runs):
        have = number(block.get('MIC_BENCH', {}).get('runs'), 0)
        d.write('Y\n')
        deadline = time.time() + 20
        while time.time() < deadline and number(block.get('MIC_BENCH', {}).get('runs'), 0) <= have:
            time.sleep(1)
            block = d.status()
        runs.append(block.get('MIC_BENCH', {}))
    fft, plain = min(number(b.get('fft_us'), 1e9) for b in runs), min(number(b.get('plain_fft_us'), 1e9) for b in runs)
    rig.check('esp-dsp and the portable FFT agree', all(number(b.get('fft_error'), 1) < 0.001 for b in runs) and 0 < fft <= plain,
              f'fastest of {len(runs)}: esp-dsp {fft:.0f} us, portable {plain:.0f} us; ' + ' | '.join(str(b) for b in runs))
    rig.note('bench at 80 MHz: ' + ' '.join(f'{k}={v}' for k, v in bench.items()))

    # A 30 s spin with the page open: past the 8 s ring, where a slow tracker
    # falls off. It must keep up, and cost under 50 ms a reading.
    base = mic_counters(block)
    start = d.mark()
    r = mic_tone_pull(rig, 7500, (118, 1, 30, 0.5))
    rig.check('a pull on the page is measured', r.valid and abs(r.rpm - 7500) <= 7500 * 0.03, r.summary())
    spin = mic_spin(rig, r.launch.get('launches'), 50, start)
    rig.check('MIC_SPIN reports the spin', spin is not None)
    rig.screenshot('mic_spin')
    block = d.status()
    mic = block.get('MIC_STATUS', {})
    rig.check('the tracker keeps up past the ring', number(mic.get('behind_max_ms'), 1e9) < 1000 and mic.get('overruns') == '0',
              f"behind_max_ms={mic.get('behind_max_ms')} overruns={mic.get('overruns')}")
    rig.check('no capture chunk lost or late', mic.get('late_chunks') == '0' and mic.get('record_misses') == '0' and mic.get('lost_steps') == '0', str(mic))
    if spin:
        f = spin.f
        rig.check('the spin is heard from the prior, at one cycle a turn', f.get('cycles') == '1' and abs(number(f.get('first_hz'), 0) - 118) <= 3, spin.text)
        rig.check('the first reading comes within 1.5 s', 0 < number(f.get('first_ms'), 0) <= 1500, spin.text)
        rig.check('the spin is followed to its end', number(f.get('valid'), 0) >= 100, spin.text)
        rig.note('spin: ' + spin.text)
    readings = {l.f.get('search'): l.f for l in d.since(0, 'MIC_READING ')}
    rig.note('readings: ' + str(readings))
    near = readings.get('near', {})
    rig.check('a locked reading costs under 50 ms with the page open', 0 < number(near.get('mean_us'), 0) < 50000, str(near))
    rig.check('a search reading fits its 100 ms hop', 0 < number(readings.get('prior', {}).get('mean_us'), 0) < 100000, str(readings.get('prior')))
    after = mic_counters(block)
    gaps, unexplained = mic_gaps_beyond_saves(rig, start, base['gaps'])
    rig.check('no IMU gap with the mic on, beyond a history save\'s', after['gaps'] - base['gaps'] == gaps and not unexplained, f'{base} -> {after}; {gaps} logged; unexplained {unexplained}')
    beyond = missed_polls_beyond_saves(d.since(start), base, after)
    rig.check('no missed poll or gate miss with the mic on, beyond a history save\'s', beyond == 0 and after['gate_misses'] == base['gate_misses'],
              f'{base} -> {after}; {beyond} missed beyond the saves')
    rig.check('the UI did not stall', int(number(block.get('PERF', {}).get('ui_stalls'), 0)) == stalls, str(block.get('PERF')))

    # The page's worst frame, with the tracker idle.
    d.hil('micframe', expect='HIL_OK')
    time.sleep(0.6)
    rig.screenshot('mic_long')
    rig.note(f"max_draw_us={d.status().get('PERF', {}).get('max_draw_us')}")

    # Two sound cycles a turn.
    start = d.mark()
    r = mic_tone_pull(rig, 7500, (236, 2, 4, 0.5))
    spin = mic_spin(rig, r.launch.get('launches') if r.launch else '', 20, start)
    rig.check('a tone at twice the turn rate reads as two cycles a turn', spin is not None and spin.f.get('cycles') == '2', spin.text if spin else 'none')

    # A new pull takes over from the spin being heard, on the same mic.
    start = d.mark()
    first = mic_tone_pull(rig, 7500, (118, 1, 30, 0.5))
    time.sleep(3)
    second = mic_tone_pull(rig, 6000, (94, 1, 3, 0.5))
    spin = mic_spin(rig, first.launch.get('launches') if first.launch else '', 5, start)
    rig.check('a new pull ends the last spin at once', spin is not None, str(first.launch))
    spin = mic_spin(rig, second.launch.get('launches') if second.launch else '', 20, start)
    rig.check('the new pull is followed from its own prior', spin is not None and abs(number(spin.f.get('first_hz'), 0) - 94) <= 3, spin.text if spin else 'none')

    # The owner's a8ee2234 session: pulls 3-6 were never analyzed (frames=0,
    # overruns=4), each taken with its release over 8 s behind the ring.
    def heard(label, spin, fresh=True):
        rig.check(f'{label}: heard', spin is not None and number(spin.f.get('frames'), 0) > 0 and number(spin.f.get('valid'), 0) > 0, spin.text if spin else 'none')
        if spin and fresh:
            rig.check(f'{label}: taken at once, from its release', number(spin.f.get('wake_ms'), 1e9) < 500 and spin.f.get('skipped_ms') == '0', spin.text)
        if spin:
            rig.note(f'{label}: ' + spin.text)

    # Back to back, as the owner pulls: each relaunch while the last is heard.
    start = d.mark()
    pulls = []
    for rpm, hz in ((7000, 110), (6500, 102), (6200, 97)):
        pulls.append(mic_tone_pull(rig, rpm, (hz, 1, 20, 0.5)))
        time.sleep(4)
    for i, r in enumerate(pulls):
        heard(f'back-to-back pull {i + 1}', mic_spin(rig, r.launch.get('launches') if r.launch else '', 40, start))

    # After a spin followed to its end, the next pull is still heard.
    start = d.mark()
    r = mic_tone_pull(rig, 7500, (118, 1, 20, 0.5))
    mic_spin(rig, r.launch.get('launches') if r.launch else '', 45, start)
    start = d.mark()
    r = mic_tone_pull(rig, 6500, (102, 1, 6, 0.5))
    heard('a pull after a long follow', mic_spin(rig, r.launch.get('launches') if r.launch else '', 25, start))

    # A result that reaches the task 9 s late: the ring has lost the release,
    # and the spin is heard from its oldest sample on the spin's own clock.
    before = d.status().get('MIC_STATUS', {})
    d.hil('miclag 9000', expect='HIL_OK')
    start = d.mark()
    r = mic_tone_pull(rig, 7500, (118, 1, 25, 0.5))
    spin = mic_spin(rig, r.launch.get('launches') if r.launch else '', 40, start)
    d.hil('miclag 0', expect='HIL_OK')
    heard('a release the ring has lost', spin, fresh=False)
    after = d.status().get('MIC_STATUS', {})
    if spin:
        rig.check('the lost seconds are skipped, not the spin', 8000 <= number(spin.f.get('lag_ms'), 0) and number(spin.f.get('skipped_ms'), 0) > 0
                  and number(spin.f.get('first_ms'), 0) >= number(spin.f.get('skipped_ms'), 0), spin.text)
    rig.check('a late start is counted as one', number(after.get('late_starts'), 0) == number(before.get('late_starts'), 0) + 1, f'{before} -> {after}')

    # Out and back in, then a pull.
    key(rig, 'a', 0.1)
    mic_open(rig)
    time.sleep(1.2)
    start = d.mark()
    r = mic_tone_pull(rig, 6800, (106, 1, 6, 0.5))
    heard('a pull after reopening the page', mic_spin(rig, r.launch.get('launches') if r.launch else '', 25, start))
    mic = d.status().get('MIC_STATUS', {})
    rig.check('no spin dropped mid-way', mic.get('overruns') == '0', str(mic))
    rig.check('the stop in between was quick', 0 < number(mic.get('stop_max_ms'), 0) < 500, str(mic))

    # Every way out, each with the mic down and the memory back.
    def gone(label, way, heap_slack=512):
        start = d.mark()
        way()
        off = mic_wait(rig, 'off', 2, start)
        rig.check(f'{label}: the mic stops', off is not None)
        time.sleep(0.4)
        block = d.status()
        now = int(number(block.get('HEAP_STATUS', {}).get('dma_free'), 0))
        rig.check(f'{label}: the heap comes back', now >= heap_off - heap_slack, f'{now} vs {heap_off}')
        rig.check(f'{label}: the task stopped within 500 ms', 0 < int(number(block.get('MIC_STATUS', {}).get('stop_ms'), 0)) < 500, str(block.get('MIC_STATUS')))
        rig.check(f'{label}: the task left no stack short', int(number(block.get('MIC_STATUS', {}).get('task_stack_free'), 0)) >= 256, str(block.get('MIC_STATUS')))
    gone('tap A', lambda: key(rig, 'a', 0.1))
    rig.check('tap A returns to the Menu', ui_state(rig).get('page') == MENU_PAGE, str(ui_state(rig)))
    mic_open(rig)
    gone('hold B', lambda: key(rig, 'b', 0.1))

    # The speaker works again, on the same ES8311.
    sound = d.status().get('SOUND_STATUS', {})
    plays = int(number(sound.get('plays'), 0))
    d.hil('set 8 1', expect='HIL_SET', timeout=6)  # Sound quiet
    d.hil('home')
    time.sleep(1)
    rig.pull(signals.standard_pull(5000), signals.launch_motion(tilt_rate_dps=250, pull_g=3, seed=10))
    time.sleep(1)
    effect_preview(rig, 2)
    time.sleep(1.5)
    sound = d.status().get('SOUND_STATUS', {})
    rig.check('the speaker plays after the mic', int(number(sound.get('plays'), 0)) > plays and sound.get('lock_misses', '0') == '0', str(sound))

    # A sound on the mic's way in: it stops, then the mic starts. The sound
    # ends with the codec powered down (Es8311Power, 0.20.6); with no tone the
    # chunks are the real mic, and a codec left down gives exact zeros.
    d.hil('mictone off', expect='HIL_OK')
    start = d.mark()
    effect_preview(rig, 2)
    d.write('m\n')
    rig.check('a sound playing gives way to the mic', mic_wait(rig, 'on', 4, start) is not None)
    block = d.status()
    rig.check('the speaker is off with the mic on', block.get('SOUND_STATUS', {}).get('powered') == '0', str(block.get('SOUND_STATUS')))
    time.sleep(MIC_WARMUP_S + 0.5)
    d.status()  # the peak counts from here, past the warm-up's zeros
    time.sleep(1)
    heard = d.status().get('MIC_STATUS', {})
    rig.note(f"real mic after a sound's codec power-down: peak={heard.get('peak')}")
    rig.check('after a sound powered the codec down, the mic hears the room (peak above 0)', int(number(heard.get('peak'), 0)) > 0, str(heard))
    d.hil('home')  # leaving by the Main screen
    rig.check('home leaves it too', mic_wait(rig, 'off', 2, start) is not None)

    # In and out as fast as USB goes: it always lands on the page's state.
    d.hil('home')
    for _ in range(6):
        d.write('m\n')
        time.sleep(0.12)
    time.sleep(1.5)
    block = d.status()
    rig.check('quick toggles land off, on the Menu', block.get('MIC_STATUS', {}).get('state') == 'off' and block.get('UI_STATUS', {}).get('page') == MENU_PAGE, str(block.get('MIC_STATUS')))
    rig.check('quick toggles break no capture chain', block.get('MIC_STATUS', {}).get('record_misses') == '0', str(block.get('MIC_STATUS')))

    # Sleep never meets a running mic: the page closes first.
    mic_open(rig)
    d.hil('sleepok 1')
    d.hil('sleepdry 1')
    lines, end = standby_dry_run(rig, 0, request=True)
    off = [i for i, l in enumerate(lines) if l.text.startswith('MIC_STATE ') and l.f.get('state') == 'off']
    power = [i for i, l in enumerate(lines) if l.text.startswith('POWER_OFF reason=')]
    rig.check('the inactivity sleep stops the mic before it powers off', bool(off and power and off[0] < power[0] and end is not None), ' / '.join(l.text for l in lines[:6]))
    stopped = d.status().get('MIC_STATUS', {})
    rig.check('a stop just after a start is as quick', 0 < int(number(stopped.get('stop_ms'), 0)) < 500, str(stopped))
    rig.check('sleep leaves the Menu row, never the page', ui_state(rig).get('page') == MENU_PAGE, str(ui_state(rig)))
    d.hil('sleepdry 0')
    d.hil('sleepok 0')

    # The floor: the page refuses, and a heap that falls through it stops the mic.
    d.hil('micfloor 4000000', expect='HIL_OK')
    start = d.mark()
    d.write('m\n')
    refused = mic_wait(rig, 'refused', 3, start)
    rig.check('too little heap refuses the mic', refused is not None and refused.f.get('why') == 'memory', refused.text if refused else 'none')
    rig.screenshot('mic_memory')
    rig.check('a refused page leaves the speaker free', d.status().get('MIC_STATUS', {}).get('state') == 'refused')
    d.hil(f'micfloor {floor}', expect='HIL_OK')
    start = d.mark()
    d.write('m\n')
    rig.check('leaving a refused page ends it', mic_wait(rig, 'off', 2, start) is not None)
    started = mic_open(rig)
    rig.check('the mic starts again', started is not None)
    start = d.mark()
    d.hil('micfloor 4000000', expect='HIL_OK')
    refused = mic_wait(rig, 'refused', 3, start)
    rig.check('falling through the floor stops the mic', refused is not None and refused.f.get('why') == 'memory', refused.text if refused else 'none')
    mic = d.status().get('MIC_STATUS', {})
    rig.check('the capture chain never broke', mic.get('record_misses') == '0' and mic.get('restarts') == '0', str(mic))
    rig.check('every stop took under 500 ms', 0 < int(number(mic.get('stop_max_ms'), 0)) < 500, str(mic))
    d.hil(f'micfloor {floor}', expect='HIL_OK')
    d.hil('mictone off', expect='HIL_OK')
    d.hil('home')
    d.hil('set 8 0', expect='HIL_SET', timeout=6)


SCENARIOS = {
    'rearm': rearm,
    'imu_timeline': imu_timeline,
    'normal': normal_pulls,
    'hard': hard_pulls,
    'weak': weak_noisy_optical,
    'back_to_back': back_to_back,
    'history_save_pull': pull_during_history_save,
    'long_save_pull': pull_during_long_save,
    'adc_stall': adc_stall,
    'frame_rows': frame_rows,
    'power_log_pull': pull_during_power_log_save,
    'standby': standby,
    'sleep_blockers': sleep_blockers,
    'usb_standby': usb_standby,
    'imu_faults': imu_faults,
    'imu_faults_pull': imu_faults_during_pull,
    'imu_backlog': imu_backlog,
    'settings': settings_saves,
    'about_page': about_page,
    'bluetooth_off': bluetooth_off,
    'ble_tap': ble_scenarios.ble_tap,
    'bluetooth_storage': ble_scenarios.bluetooth_storage,
    'battery_icon': battery_icon,
    'corpus': corpus_replay,
    'soak': soak,
    'soak_themes': lambda rig, minutes=10: soak(rig, minutes, switch_themes=True),
    'stray_bytes': stray_bytes,
    'themes': custom_themes,
    'milestones': rpm_milestones,
    'nvs_tight': nvs_tight,
    'effect_set': effect_set,
    'request_burst': request_burst,
    'effect_times': effect_frame_times,
    'loaded_effects': loaded_effects,
    'loaded_store': loaded_store,
    'effect_times_low': effect_frame_times_low,
    'effect_stages': effect_stage_times,
    'effect_bench': effect_bench,
    # Over Renderer::benchCanvas(): frame hashes comparable with ui_render --bench-hashes 100.
    'effect_bench_fixed': lambda rig: effect_bench(rig, fixed=True),
    'sound_sync': sound_sync,
    'songs': songs,
    'wear_check': wear_check,
    'mic_page': mic_page,
    # One round previews every drawn effect for its whole length (about 6 s each with the readout).
    'soak_effects': lambda rig, minutes=10: effect_frame_times(rig, rounds=max(1, int(minutes * 60 // (6.2 * len(EFFECTS))))),
    'soak_milestones': lambda rig, minutes=10: soak(rig, minutes, milestone_mode=True),
}
