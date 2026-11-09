"""Scenario harness: device preparation, scripted pulls and assertions.

Every assertion is checked against the device's own serial output. A Rig
records results per scenario and watches for unexpected reboots (a new
HIL_BOOT id, a USB disconnect, or a fresh BOOT line).
"""
import json
import time
import traceback
from dataclasses import dataclass, field

from hil_device import DeviceError, number
import signals

GRAVITY = (0.0, 0.0, 1.0)


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ''
    severity: str = 'fail'  # fail | warn | info


@dataclass
class PullResult:
    label: str
    expected_rpm: float
    launch: dict = None      # LAUNCH_RESULT fields
    metrics: dict = None     # LAUNCH_METRICS
    added: dict = None       # HISTORY_ADDED
    motion: dict = None      # MOTION_RESULT
    issue: dict = None       # MOTION_ISSUE
    recap: dict = None       # RECAP_PRESENT
    states: list = field(default_factory=list)
    expected_gyro: float = 0  # largest single-axis gyro of the scripted motion
    expected_saturated: bool = False

    @property
    def rpm(self):
        if self.metrics:
            return number(self.metrics.get('selected_rpm'), 0)
        return number(self.launch.get('peak_rpm'), 0) if self.launch else 0

    @property
    def valid(self):
        return bool(self.launch) and self.launch.get('valid') == '1'

    def summary(self):
        m = self.motion or {}
        return (f'{self.label}: rpm={self.rpm:.0f} (expected {self.expected_rpm:.0f}) valid={int(self.valid)} '
                f'end={self.launch.get("end") if self.launch else "-"} history={int(bool(self.added))} '
                f'motion={m.get("quality", "-")} saturated={m.get("saturated", "-")} '
                f'peak={m.get("peak_accel_g", "-")}g/{m.get("peak_gyro_dps", "-")}dps recap={int(bool(self.recap))}')


class Rig:
    def __init__(self, dev, out_dir):
        self.dev = dev
        self.out = out_dir
        self.results = []
        self.scenario = None
        self.boot_id = None
        self.disconnects = dev.disconnects

    # ---- bookkeeping
    def begin(self, name):
        self.scenario = {'name': name, 'checks': [], 'notes': [], 'pulls': [], 'started': time.time(),
                         'start_line': self.dev.mark()}
        self.results.append(self.scenario)
        print(f'\n=== {name}')

    def check(self, name, ok, detail='', severity='fail'):
        c = Check(name, bool(ok), detail, severity)
        self.scenario['checks'].append(c.__dict__)
        flag = 'PASS' if ok else {'fail': 'FAIL', 'warn': 'WARN', 'info': 'INFO'}[severity]
        print(f'  [{flag}] {name}' + (f': {detail}' if detail else ''))
        return ok

    def note(self, text):
        self.scenario['notes'].append(text)
        print(f'  - {text}')

    def end(self):
        s = self.scenario
        s['seconds'] = round(time.time() - s['started'], 1)
        self.check_alive()
        s['passed'] = all(c['ok'] or c['severity'] != 'fail' for c in s['checks'])
        s['end_line'] = self.dev.mark()
        (self.out / 'results.json').write_text(json.dumps(self.results, indent=1))

    def run(self, name, fn, *args, **kw):
        self.begin(name)
        try:
            fn(self, *args, **kw)
        except Exception as e:  # a scenario error is a finding, not a rig crash
            self.check('scenario completed', False, f'{type(e).__name__}: {e}')
            self.scenario['traceback'] = traceback.format_exc()
            self.recover()
        self.end()

    # ---- device health
    def hil_boot(self):
        for attempt in range(3):
            start = self.dev.mark()
            try:
                self.dev.hil('hil', expect='HIL_FAULTS')
            except DeviceError:
                if attempt == 2:
                    raise
                continue
            line = self.dev.latest('HIL_STATUS', since=start)
            if line:
                return line.f
        return {}

    def check_alive(self):
        reboots = [l.text for l in self.dev.since(self.scenario['start_line'])
                   if l.text.startswith(('HIL_BOOT', 'BOOT ', 'HOST_DISCONNECT', 'Guru', 'abort()', 'Backtrace'))]
        try:
            status = self.hil_boot()
        except DeviceError as e:
            self.check('device responsive', False, str(e))
            return False
        same = self.boot_id is None or status.get('boot_id') == self.boot_id
        self.check('no reboot or disconnect', same and not reboots,
                   f"boot_id={status.get('boot_id')} reset={status.get('reset')} events={reboots[:4]}")
        if not same:
            self.boot_id = status.get('boot_id')
        return same and not reboots

    def recover(self):
        """Best-effort return to a known state after a scenario error."""
        for _ in range(3):
            try:
                self.dev.hil('reset', timeout=5)
                self.dev.hil('home')
                return
            except DeviceError:
                time.sleep(2)

    # ---- preparation
    def require_hil_build(self):
        line = self.dev.wait('STATUS v=', 6)
        version = line.f.get('v', '') if line else ''
        if '-hil' not in version:
            raise DeviceError(f'device runs {version or "unknown"}, not a HIL build; refusing to send ! commands')
        status = self.hil_boot()
        self.boot_id = status.get('boot_id')
        return version

    def idle_sources(self, baseline=1800, noise=3, gravity=GRAVITY, accel_noise=0.004, gyro_noise=0.08):
        d = self.dev
        d.hil('reset')
        d.hil(f'idle {baseline} {noise}')
        d.hil('opt idle')
        d.hil('const {:.4f} {:.4f} {:.4f} 0 0 0 {} {}'.format(*gravity, accel_noise, gyro_noise))
        d.hil('imu const')
        d.hil('home')

    def wait_ready(self, timeout=20, fusion=True):
        """Detector READY and (optionally) fused, settled orientation."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            block = self.dev.status()
            rpm = block.get('RPM_STATUS', {})
            ori = block.get('ORIENTATION_STATUS', {})
            motion = block.get('MOTION_STATUS', {})
            ready = rpm.get('state') == 'READY' and motion.get('pending') == '0'
            if ready and (not fusion or ori.get('ready') == '1'):
                return block
            time.sleep(0.5)
        return None

    def counters(self):
        block = self.dev.status()
        return block

    # ---- pulls
    def pull(self, optical, motion=None, offset_ms=0, lead_ms=60, timeout=None, expect_launch=True,
             wait_motion=True, program=True):
        """Program and play one synchronised pull; collect its result lines."""
        d = self.dev
        if program:
            signals.program_optical(d, optical)
            if motion:
                signals.program_imu(d, motion, offset_ms)
            else:
                d.hil('imu const')
        start = d.mark()
        d.hil(f'pull {lead_ms}', expect='HIL_PULL')
        result = PullResult(optical.label, optical.expected_rpm)
        if motion:
            result.expected_gyro, result.expected_saturated = motion.peak_gyro_dps, motion.expect_saturated
        limit = timeout or (optical.duration_ms / 1000 + 4.0)
        launch = d.wait('LAUNCH_RESULT', limit, since=start)
        if launch is None:
            if expect_launch:
                self.note(f'no LAUNCH_RESULT for {optical.label} within {limit:.1f}s')
            result.states = [l.text for l in d.since(start, 'RPM_STATE')]
            self.scenario['pulls'].append(result.__dict__)
            return result
        result.launch = launch.f
        metrics = d.wait('LAUNCH_METRICS', 1.0, since=launch.index)
        added = d.wait('HISTORY_ADDED', 1.0, since=launch.index)
        result.metrics = metrics.f if metrics else None
        result.added = added.f if added else None
        if wait_motion and added:
            motion_line = d.wait('MOTION_RESULT', 3.0, since=launch.index)
            if motion_line:
                result.motion = motion_line.f
                result.motion['quality'] = motion_line.text.split('quality=')[1].split(' points=')[0]
                issue = d.wait('MOTION_ISSUE', 0.3, since=motion_line.index)
                result.issue = issue.f if issue else None
                recap = d.wait('RECAP_PRESENT', 2.0, since=motion_line.index)
                result.recap = recap.f if recap else None
        result.states = [l.text for l in d.since(start, 'RPM_STATE')]
        self.scenario['pulls'].append(result.__dict__)
        print('   ', result.summary())
        return result

    def check_pull(self, r, tolerance=0.03, motion=True, saturated=None, require_valid_motion=True):
        label = r.label
        self.check(f'{label}: launch reported', r.launch is not None)
        if not r.launch:
            return False
        self.check(f'{label}: result valid', r.valid, f"end={r.launch.get('end')}")
        if r.expected_rpm:
            err = abs(r.rpm - r.expected_rpm) / r.expected_rpm
            self.check(f'{label}: rpm within {tolerance:.0%}', err <= tolerance,
                       f'{r.rpm:.0f} vs {r.expected_rpm:.0f} ({err:+.1%})')
        self.check(f'{label}: added to history', r.added is not None)
        if motion:
            q = (r.motion or {}).get('quality')
            self.check(f'{label}: motion result', r.motion is not None)
            self.check(f'{label}: never "Motion out of range"', q != 'Motion out of range', f'quality={q}')
            if require_valid_motion:
                self.check(f'{label}: tilt replay valid', q == 'valid', f'quality={q} issue={r.issue}')
                self.check(f'{label}: recap presented', r.recap is not None)
            if r.motion and r.expected_gyro > 50 and not r.expected_saturated:
                # The capture must hold the pull's own motion, not rest before/after it.
                got = number(r.motion.get('peak_gyro_dps'), 0)
                self.check(f'{label}: tilt replay aligned with the pull', abs(got - r.expected_gyro) <= 0.25 * r.expected_gyro,
                           f'peak gyro {got:.0f} dps vs scripted {r.expected_gyro:.0f}')
            if saturated is not None and r.motion:
                self.check(f'{label}: saturated={int(saturated)}', r.motion.get('saturated') == str(int(saturated)),
                           f"peak {r.motion.get('peak_accel_g')} g / {r.motion.get('peak_gyro_dps')} dps")
        return True

    def screenshot(self, name):
        snap = self.dev.snapshot()
        if snap:
            path = self.out / f'{name}.png'
            snap.write_png(path)
            self.scenario.setdefault('screens', []).append(str(path))
            return path
        self.note('screen snapshot failed')
        return None
