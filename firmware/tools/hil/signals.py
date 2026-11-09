"""Scripted inputs for the HIL rig: optical RPM profiles and IMU motion.

Optical pulls are described as generator keyframes (played by the device at
50 kS/s), or as raw recorded clips uploaded to its playback buffer. IMU pulls
are 200 Hz board-axis frames (g, dps) produced by integrating an angular-rate
profile, so accelerometer gravity and gyro rates stay physically consistent
and the device's VQF fusion sees a real rotation.
"""
import json
import math
import random
import struct
from dataclasses import dataclass, field
from pathlib import Path

SAMPLE_HZ = 50000
IMU_HZ = 200


# ---------------------------------------------------------------- optical
@dataclass
class OpticalPull:
    """Keyframes (ms, rpm, depth, noise) for the on-device mark generator.

    depth is the mark swing in ADC counts (real enclosure marks are 900-1,100;
    the standard profile needs >= 80, the launch floor >= 300 for TCRT is 60).
    """
    keys: list
    baseline: float = 1800
    idle_noise: float = 3
    duty: float = 0.3
    expected_rpm: float = 0
    label: str = ''

    @property
    def duration_ms(self):
        return self.keys[-1][0] if self.keys else 0


def standard_pull(peak_rpm, depth=1000, noise=4, ramp_ms=400, hold_ms=120, decay_ms=500,
                  start_rpm=None, end_fraction=0.55, baseline=1800, label=''):
    """Ramp to peak, hold, decay, then the marks stop (Beyblade released)."""
    start = start_rpm if start_rpm is not None else max(1500, peak_rpm * 0.25)
    t1, t2, t3 = ramp_ms, ramp_ms + hold_ms, ramp_ms + hold_ms + decay_ms
    keys = [(0, start, depth, noise), (t1, peak_rpm, depth, noise), (t2, peak_rpm, depth, noise),
            (t3, peak_rpm * end_fraction, depth, noise), (t3 + 1, 0, 0, noise)]
    return OpticalPull(keys, baseline=baseline, expected_rpm=peak_rpm, label=label or f'{peak_rpm:.0f} rpm')


def retraction(peak_rpm, depth=1000, noise=4, ramp_ms=350, hold_ms=200, stop_ms=120, extend_rpm=0, extend_ms=1500, pause_ms=1500,
               baseline=1800, label=''):
    """The wear check's pull: the string rewinds from rest to its speed, and when
    it is home the gear stops within stop_ms (to half speed, then no marks: a
    mark cut off at full speed would fake a short last turn). With extend_rpm,
    a steady slow extension (below the 1,000 RPM launch floor) comes first,
    then pause_ms with the string held out."""
    keys = []
    if extend_rpm:
        keys = [(0, extend_rpm, depth, noise), (extend_ms, extend_rpm, depth, noise), (extend_ms + 0.1, 0, 0, noise),
                (extend_ms + pause_ms, 0, 0, noise)]
    t = keys[-1][0] + 0.1 if keys else 0
    start = min(700, peak_rpm * 0.25)
    home = t + ramp_ms + hold_ms
    keys += [(t, start, depth, noise), (t + ramp_ms, peak_rpm, depth, noise), (home, peak_rpm, depth, noise),
             (home + stop_ms, peak_rpm * 0.5, depth, noise), (home + stop_ms + 0.1, 0, 0, noise)]
    what = f'{peak_rpm:.0f} rpm retraction' + (f' after a {extend_rpm:.0f} rpm extension and {pause_ms} ms hold' if extend_rpm else '')
    return OpticalPull(keys, baseline=baseline, expected_rpm=peak_rpm, label=label or what)


def wear_pull(in_rpm, out_rpm=4800, held_ms=1500, out_ramp_ms=150, out_hold_ms=60, label='', **kw):
    """A Wear check pull (0.20.4): the string pulled out (a burst of out_rpm,
    stopping dead at full length), held still for held_ms, then let go: the
    retraction (in_rpm) is the pull's expected_rpm. kw shape the retraction. out_rpm below the 1,000 RPM
    launch floor is a slow pull out the detector does not measure."""
    out = retraction(out_rpm, ramp_ms=out_ramp_ms, hold_ms=out_hold_ms) if out_rpm >= 1000 else \
        OpticalPull([(0, out_rpm, 1000, 4), (600, out_rpm, 1000, 4), (600.1, 0, 0, 4)])
    back = retraction(in_rpm, **kw)
    pull = sequence([(0, out), (out.duration_ms + held_ms, back)])
    pull.expected_rpm = in_rpm
    pull.label = label or f'{out_rpm:.0f} rpm out, {held_ms} ms hold, {in_rpm:.0f} rpm retraction'
    return pull


def dropout_pull(peak_rpm, gaps, depth=1000, noise=4, **kw):
    """standard_pull with mark-free windows [(start_ms, length_ms)]: lost marks."""
    pull = standard_pull(peak_rpm, depth=depth, noise=noise, **kw)
    keys = []
    for k in pull.keys:
        keys.append(k)
    for start, length in gaps:
        rpm = interpolate(pull.keys, start)[0]
        keys += [(start, rpm, depth, noise), (start + 0.1, rpm, 0, noise),
                 (start + length, rpm, 0, noise), (start + length + 0.1, rpm, depth, noise)]
    keys.sort(key=lambda k: k[0])
    pull.keys = keys
    pull.label = f'{peak_rpm:.0f} rpm with {len(gaps)} dropouts'
    return pull


def sequence(pulls):
    """[(start_ms, OpticalPull)] -> one generator program with exact spacing."""
    keys = []
    for start, pull in pulls:
        if keys:
            keys.append((start - 0.2, 0, 0, pull.keys[0][3]))
        keys += [(start + k[0], *k[1:]) for k in pull.keys]
    first = pulls[0][1]
    return OpticalPull(keys, baseline=first.baseline, idle_noise=first.idle_noise, duty=first.duty,
                       expected_rpm=first.expected_rpm, label=' + '.join(p.label for _, p in pulls))


def interpolate(keys, ms):
    for a, b in zip(keys, keys[1:]):
        if a[0] <= ms <= b[0]:
            f = (ms - a[0]) / (b[0] - a[0]) if b[0] > a[0] else 1
            return tuple(a[i] + (b[i] - a[i]) * f for i in range(1, 4))
    return keys[-1][1:]


def program_optical(dev, pull):
    dev.hil(f'idle {pull.baseline:.0f} {pull.idle_noise:.1f}')
    dev.hil('gen clear')
    dev.hil(f'gen duty {pull.duty:.3f}')
    for ms, rpm, depth, noise in pull.keys:
        dev.hil(f'gen key {ms:.1f} {rpm:.1f} {depth:.1f} {noise:.2f}'.replace('.0 ', ' '))
    dev.hil('opt gen')


def load_clip(path, start_s, end_s):
    """Raw 12-bit samples (u16le, flight-recorder/corpus format) of [start, end)."""
    data = Path(path).read_bytes()
    a, b = int(start_s * SAMPLE_HZ), int(end_s * SAMPLE_HZ)
    raw = data[a * 2:b * 2]
    return [v & 0x0fff for v in struct.unpack(f'<{len(raw) // 2}H', raw)]


# ---------------------------------------------------------------- IMU
def quat_mul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2, w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2)


def rotate_inverse(q, v):
    """Rotate world vector v into the body frame of orientation q."""
    w, x, y, z = q
    qi = (w, -x, -y, -z)
    p = quat_mul(quat_mul(qi, (0, *v)), q)
    return p[1:]


def rotate(q, v):
    w, x, y, z = q
    p = quat_mul(quat_mul(q, (0, *v)), (w, -x, -y, -z))
    return p[1:]


@dataclass
class MotionPull:
    """Body-frame IMU frames at 200 Hz from t=0 (aligned with optical t=0)."""
    frames: list = field(repr=False)
    peak_accel_g: float
    peak_gyro_dps: float
    expect_saturated: bool
    label: str = ''


def launch_motion(duration_ms=1000, tilt_rate_dps=400, axis=(0.3, 1.0, 0.1), pull_g=4.0, stop_g=None,
                  stop_ms=None, vibration_g=0.05, vibration_dps=3, gravity=(0, 0, 1), seed=1, label=''):
    """A pull: the launcher rotates out and back (net zero turn) while the
    ripcord accelerates it along board X; an optional sharp stop jerk at the
    end (stop_g, the hard-pull ripcord stop). Returns to rest at the end.
    gravity is the resting accelerometer vector in board axes.
    """
    rng = random.Random(seed)
    n = int(duration_ms * IMU_HZ / 1000)
    norm = math.sqrt(sum(a * a for a in axis))
    axis = tuple(a / norm for a in axis)
    # World frame = resting board frame; specific force at rest = gravity.
    q = (1.0, 0.0, 0.0, 0.0)
    frames = []
    peak_a = peak_g = 0
    stop_ms = duration_ms * 0.8 if stop_ms is None else stop_ms
    for i in range(n):
        t = i / IMU_HZ
        phase = t / (duration_ms / 1000)
        rate = tilt_rate_dps * math.sin(2 * math.pi * phase)  # out, then back
        omega = tuple(a * rate for a in axis)
        # Linear acceleration (world): half-sine pull, plus a short stop jerk.
        lin = pull_g * max(0.0, math.sin(math.pi * min(1.0, phase / 0.8)))
        if stop_g:
            dt_ms = t * 1000 - stop_ms
            if 0 <= dt_ms < 25:
                lin = -stop_g * math.sin(math.pi * dt_ms / 25)
        world = (gravity[0] + lin, gravity[1], gravity[2])
        body = rotate_inverse(q, world)
        a = [body[j] + rng.gauss(0, vibration_g) for j in range(3)]
        g = [omega[j] + rng.gauss(0, vibration_dps) for j in range(3)]
        frames.append((*a, *g))
        peak_a = max(peak_a, *(abs(v) for v in a))
        peak_g = max(peak_g, *(abs(v) for v in g))
        # Integrate body rate over one 5 ms step.
        speed = math.radians(math.sqrt(sum(w * w for w in omega)))
        if speed > 1e-9:
            half = speed / IMU_HZ / 2
            s = math.sin(half) / speed
            dq = (math.cos(half), *(math.radians(w) * s for w in omega))
            q = quat_mul(q, dq)
            m = math.sqrt(sum(c * c for c in q))
            q = tuple(c / m for c in q)
    # Settle exactly back at rest (remove residual integration error).
    for _ in range(int(0.3 * IMU_HZ)):
        frames.append((*[gravity[j] + rng.gauss(0, vibration_g / 5) for j in range(3)],
                       *[rng.gauss(0, vibration_dps / 5) for _ in range(3)]))
    saturated = peak_a >= 0.975 * 16 or peak_g >= 0.975 * 2000
    return MotionPull(frames, peak_a, peak_g, saturated, label or f'{tilt_rate_dps:.0f} dps {pull_g:.0f} g')


def shake(duration_ms=2000, rate_dps=600, accel_g=2.0, hz=6, gravity=(0, 0, 1), seed=2):
    """Handling between pulls: oscillating rotation and acceleration."""
    rng = random.Random(seed)
    frames = []
    for i in range(int(duration_ms * IMU_HZ / 1000)):
        t = i / IMU_HZ
        s = math.sin(2 * math.pi * hz * t)
        frames.append((gravity[0] + accel_g * s + rng.gauss(0, .05), gravity[1] + rng.gauss(0, .05),
                       gravity[2] + rng.gauss(0, .05), rate_dps * s, rate_dps * 0.3 * s, rng.gauss(0, 5)))
    return MotionPull(frames, accel_g + 1, rate_dps, False, 'shake')


def program_imu(dev, motion, offset_ms=0):
    dev.upload_imu(motion.frames)
    dev.hil(f'offset {offset_ms}')
    dev.hil('imu prof')
