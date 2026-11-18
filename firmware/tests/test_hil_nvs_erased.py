#!/usr/bin/env python3
"""HIL nvs_erased against a model of the firmware, without a device.

The scenario plays Wipe all on 0.15-0.17 after a rollback (`!oldwipe`: the
history, bests and power log NVS namespaces erased, the history log left),
then boots and expects nothing back; then NVS refusing the copies, and the
Arduino core erasing NVS whole at boot (`!nvslost`: no namespace left),
where the log must still load. The model answers with the exact reply
formats hil.h and the sketch print (read from their sources here, so a
renamed field fails this first) and makes the boot choice of history_log.h:
fixed, as through 0.20.10, or telling a wipe only by the records (which
drops the log on a core erase). The scenario passes only on the fixed one."""
from pathlib import Path
import re
import sys
import types
import unittest
from unittest import mock

FIRMWARE = Path(__file__).resolve().parents[1]
TOOLS = FIRMWARE / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; the scenario needs none of that here.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import scenarios  # noqa: E402
from hil_device import Line  # noqa: E402

HIL = (FIRMWARE / 'LaunchLabMini/hil.h').read_text()
SKETCH = (FIRMWARE / 'LaunchLabMini/LaunchLabMini.ino').read_text()


def format_of(source, start):
    """The printf format that begins with `start` in source."""
    match = re.search(r'"(' + re.escape(start) + r'[^"]*)\\n"', source)
    assert match, start
    return match.group(1)


def render(fmt, values):
    """The line `fmt` prints with each key=%... filled from values (missing keys print 0)."""
    def one(m):
        key, conv = m.group(1), m.group(2)
        value = values.get(key, 0)
        if conv == 's':
            return f'{key}={value}'
        if conv.endswith('f'):
            return f'{key}={float(value):.1f}'
        return f'{key}={int(value)}'
    line = re.sub(r'(\w+)=%(\.1f|l?l?[ud]|d|s|02x)', one, fmt)
    assert '%' not in line, line
    return line


MIRROR = format_of(HIL, 'HIL_OK mirror')
OLDWIPE = format_of(HIL, 'HIL_OK oldwipe')
LOGSTATUS = format_of(HIL, 'HIL_OK logstatus')
BESTS = format_of(HIL, 'HIL_OK bests')
POWER_LOG = format_of(SKETCH, 'POWER_LOG schema')
HISTORY_STATUS = format_of(SKETCH, 'HISTORY_STATUS')


class Unit:
    """NVS, the history log and RAM for history, bests and the power log."""

    def __init__(self, fixed, virgin_check=True):
        self.fixed, self.virgin_check = fixed, virgin_check
        self.spaces = True  # NVS has namespaces (an old Wipe all keeps them; the core's erase does not)
        # NVS: (value boot compares, content) or None. The log: type -> (mirrored, content).
        self.nvs = {'history': (5, (6000,) * 5), 'bests': (2, 9000.0), 'power': (1007, 7)}
        self.log = {'history': (5, (6000,) * 5), 'bests': (2, 9000.0), 'power': (1007, 7)}
        self.boot_id = 0
        self.boot()

    # The boot choice (history_log.h pick), fixed or through 0.20.10.
    def pick(self, kind):
        nvs, log = self.nvs[kind], self.log.get(kind)
        if nvs is not None:
            return 'log' if log is not None and log[0] == nvs[0] else 'nvs'
        if log is None or (self.fixed and log[0] != 0):
            return 'none'
        return 'log'

    def boot(self):
        self.boot_id += 1
        self.lost = not self.spaces
        if self.fixed and self.virgin_check and self.lost:  # disown(): lost, not wiped
            self.log = {k: (0, v[1]) for k, v in self.log.items()}
        self.spaces = True
        self.picks = {k: self.pick(k) for k in ('history', 'bests', 'power')}
        content = {'history': (), 'bests': 0.0, 'power': 0}
        for kind, source in self.picks.items():
            if source == 'log':
                content[kind] = self.log[kind][1]
            elif source == 'nvs':
                content[kind] = self.nvs[kind][1]
            elif kind in self.log:  # none: the empty record is logged over the wiped one
                self.log[kind] = (0, content[kind])
        self.history, self.generation = list(content['history']), len(content['history'])
        self.saved_generation = self.nvs['history'][0] if self.nvs['history'] else 0
        self.best, self.revision = content['bests'], self.nvs['bests'][0] if self.picks['bests'] == 'nvs' else (
            self.log['bests'][0] + 1 if self.picks['bests'] == 'log' else 0)
        self.saved_revision = self.nvs['bests'][0] if self.nvs['bests'] else 0
        self.boots = content['power'] + 1
        self.power_mirror = self.nvs['power'][0] if self.nvs['power'] else 0

    def pull(self, rpm):
        self.history.append(rpm)
        self.generation += 1
        if rpm > self.best:
            self.best, self.revision = rpm, self.revision + 1
        # The coalesced save: to the log, mirrored to what NVS holds.
        self.log['history'] = (self.saved_generation, tuple(self.history))
        self.log['bests'] = (self.saved_revision, self.best)

    def mirror(self, refused):
        history = self.generation == self.saved_generation or not refused
        bests = self.revision == self.saved_revision or not refused
        if not refused:
            self.nvs['history'], self.saved_generation = (self.generation, tuple(self.history)), self.generation
            self.nvs['bests'], self.saved_revision = (self.revision, self.best), self.revision
            self.nvs['power'], self.power_mirror = (1000 + self.boots, self.boots), 1000 + self.boots
        self.log['history'] = (self.saved_generation, tuple(self.history))
        self.log['bests'] = (self.saved_revision, self.best)
        self.log['power'] = (self.power_mirror, self.boots)
        return render(MIRROR, {'ok': 1, 'refused': int(refused), 'history': int(history), 'bests': int(bests),
                               'power': int(not refused), 'logged': 1, 'generation': self.generation,
                               'nvs_generation': self.saved_generation, 'bests_revision': self.revision,
                               'nvs_bests_revision': self.saved_revision})


class Dev:
    def __init__(self, unit):
        self.unit, self.lines = unit, []

    def emit(self, text):
        self.lines.append(Line(len(self.lines), 0.0, text))
        return self.lines[-1]

    def mark(self):
        return len(self.lines)

    def since(self, index, tag=None):
        return [l for l in self.lines[index:] if tag is None or l.tag == tag]

    def wait(self, tag, timeout, since=None, fail=None):
        return next((l for l in self.lines[since or 0:] if l.tag == tag), None)

    def write(self, data):
        if data == 'P':
            self.emit(render(POWER_LOG, {'boots': self.unit.boots, 'storage_ready': 1}))

    def status(self):
        u = self.unit
        line = render(HISTORY_STATUS, {'records': len(u.history), 'generation': u.generation, 'pending': 0, 'store_ready': 1, 'page': 'main'})
        return {'HISTORY_STATUS': Line(0, 0.0, line).f, 'MOTION_STATUS': {'pending': '0'}}

    def hil(self, line, expect=None, timeout=4):
        u, words = self.unit, line.split()
        cmd, arg = words[0], (words[1] if len(words) > 1 else None)
        if cmd == 'reboot':
            reply = self.emit('HIL_REBOOT')
            u.boot()
            self.emit(f'HIL_BOOT boot_id={u.boot_id:08x}')
            return reply
        if cmd == 'mirror':
            return self.emit(u.mirror(arg == 'refused'))
        if cmd == 'nvslost':
            u.nvs, u.spaces = {k: None for k in u.nvs}, False
            reply = self.emit(render(format_of(HIL, 'HIL_OK nvslost'), {'ok': 1}))
            self.emit('HIL_REBOOT')
            u.boot()
            self.emit(f'HIL_BOOT boot_id={u.boot_id:08x}')
            return reply
        if cmd == 'oldwipe':
            u.nvs = {k: None for k in u.nvs}
            return self.emit(render(OLDWIPE, {'ok': 1}))
        if cmd == 'logstatus':
            picks = ','.join(u.picks[k] for k in ('history', 'bests', 'power'))
            assert LOGSTATUS.endswith(' from=%s,%s,%s'), LOGSTATUS
            fmt = LOGSTATUS[:-len(',%s,%s')]
            return self.emit(render(fmt, {'open': 1, 'history': 1, 'bests': 1, 'power': 1, 'nvs_lost': int(u.lost), 'from': picks}))
        if cmd == 'bests':
            if arg == 'clear':
                u.best, u.revision = 0.0, u.revision + 1
            return self.emit(render(BESTS, {'standard_three': u.best, 'revision': u.revision}))
        if cmd == 'nvs':
            return self.emit('HIL_OK nvs clear')
        raise AssertionError(f'unexpected !{line}')

    def backup(self):
        u = self.unit
        return f'BACKUP_BEGIN format=7 records={len(u.history)}\nBACKUP_DATA {",".join(map(str, u.history))}|{u.best}\nBACKUP_END\n'

    def restore(self, text):
        u = self.unit
        history, best = text.splitlines()[1].split(' ', 1)[1].split('|')
        history = tuple(int(x) for x in history.split(',') if x)
        u.generation += 1
        u.nvs['history'] = u.log['history'] = (u.generation, history)
        u.nvs['bests'] = u.log['bests'] = (u.revision + 1, float(best))
        reply = self.emit('RESTORE_OK restarting=1')
        u.boot()
        self.emit(f'HIL_BOOT boot_id={u.boot_id:08x}')
        return reply


class Pull:
    added = 1


class Rig:
    def __init__(self, fixed, virgin_check=True):
        self.unit = Unit(fixed, virgin_check)
        self.dev = Dev(self.unit)
        self.boot_id, self.scenario, self.checks = None, {}, []

    def check(self, name, ok, detail='', severity='fail'):
        self.checks.append((name, bool(ok), detail))

    def note(self, text):
        pass

    def idle_sources(self):
        pass

    def wait_ready(self, timeout):
        return {}

    def hil_boot(self):
        return {'boot_id': f'{self.unit.boot_id:08x}'}

    def pull(self, optical, motion=None):
        self.unit.pull(float(optical.expected_rpm))
        return Pull()

    def check_pull(self, r, tolerance=0.03):
        pass


def run(fixed, virgin_check=True):
    rig = Rig(fixed, virgin_check)
    with mock.patch.object(scenarios.time, 'sleep', lambda s: None):
        scenarios.nvs_erased(rig)
    return rig


LOST = {'boot after the core erase loads the log', 'history, bests and the power log kept through the core erase'}
WIPED = {'boot loads none of the wiped records', 'no history after the old wipe', 'no bests after the old wipe',
         'a fresh power log after the old wipe'}


class NvsErased(unittest.TestCase):
    def test_passes_on_the_fixed_firmware(self):
        rig = run(fixed=True)
        failed = [c for c in rig.checks if not c[1]]
        self.assertEqual(failed, [])
        self.assertGreaterEqual(len(rig.checks), 14)
        self.assertEqual(rig.unit.history, [6000] * 5)  # the backup restored

    def test_fails_where_0_20_10_brings_the_wiped_data_back(self):
        rig = run(fixed=False)
        failed = {c[0] for c in rig.checks if not c[1]}
        self.assertTrue(WIPED <= failed, failed)
        # Its NVS refusal itself is the same: the copies refused, the log taking them.
        self.assertIn('NVS refused the three copies, the log took them', {c[0] for c in rig.checks if c[1]})

    def test_fails_where_the_records_alone_decide_and_a_core_erase_drops_the_log(self):
        rig = run(fixed=True, virgin_check=False)
        failed = {c[0] for c in rig.checks if not c[1]}
        self.assertTrue(LOST <= failed and not WIPED & failed, failed)

    def test_it_runs_in_hil_all(self):
        run_hil = (TOOLS / 'run_hil.py').read_text()
        default = re.search(r"DEFAULT = \[(.*?)\]", run_hil, re.S).group(1)
        self.assertIn("'nvs_erased'", default)
        self.assertIs(scenarios.SCENARIOS['nvs_erased'], scenarios.nvs_erased)


if __name__ == '__main__':
    unittest.main()
