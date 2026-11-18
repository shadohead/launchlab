#!/usr/bin/env python3
"""HIL restore_partial (scenarios.py) against simulated units: it passes on a
unit whose restore is all or nothing (device_restore.h) and fails on one that
behaves as 0.20.10 did, where a failed restore left the backup's history in
NVS above the device's and the next boot loaded it, and on one that calls a
restore rolled back while a history write it could not read back still holds
the backup's, and on a unit whose own history save, pending after a restart,
takes the armed fault before the restore writes (the scenario's first rig run,
14:C1, 2026-11-17, armed its history faults too early). Not a device test."""
from pathlib import Path
import sys
import types
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; the scenario's logic needs none of that.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import hil_device  # noqa: E402
import scenarios  # noqa: E402

Line = hil_device.Line


class Unit:
    """Storage, RAM and the USB lines of one unit.

    fixed: the restore as device_restore.h does it. Otherwise 0.20.10's: a
    refused record leaves the backup's history in NVS at a higher generation
    (written=partial, no restart), and a save at that generation is skipped.
    full_restores: with NVS below a history rewrite, whether the restore
    succeeds through the log (else it is refused whole).
    verifies: a history write that lands without reading back is written
    over with the unit's own. Otherwise the backup's history stays in NVS
    above the unit's, which has nothing pending and so never saves over it.

    As on a device, a restart and closing the history log each leave a save of
    the unit's own history pending. It runs within seconds: at the next status,
    or while a backup uploads, where it takes an armed history fault before
    the restore writes anything."""

    def __init__(self, fixed=True, full_restores=True, verifies=True):
        self.fixed, self.full_restores, self.verifies = fixed, full_restores, verifies
        self.pending = False
        self.unread, self.log_open, self.unseen = None, True, False
        self.stored = {'newest': (40, 9000), 'records': 40, 'bests': (9000.0, 9100.0, 0.0, 0.0)}
        self.ram = dict(self.stored)
        self.lines, self.boot, self.fault, self.fill, self.backups, self.shadow = [], 1, None, 0, {}, False

    # ---- what the scenario reads
    def emit(self, text):
        line = Line(len(self.lines), 0.0, text)
        self.lines.append(line)
        return line

    def mark(self):
        return len(self.lines)

    def since(self, index, tag=None):
        return [l for l in self.lines[index:] if tag is None or l.text.startswith(tag)]

    def latest(self, tag, since=0):
        return next((l for l in reversed(self.lines[since:]) if l.text.startswith(tag + ' ')), None)

    def wait(self, predicate, timeout, since=None, fail=None):
        test = predicate if callable(predicate) else (lambda l: l.text.startswith(predicate))
        return next((l for l in self.lines[since or 0:] if test(l)), None)

    def reopen(self, timeout=30):
        return True

    def save(self):
        # The coalesced save: 0.20.10 skipped one whose generation equalled the restored record's.
        if (self.shadow and not self.fixed) or self.unseen:
            return
        self.stored = dict(self.ram)

    def own_save(self):
        """The pending save of the unit's own history: it takes an armed history fault."""
        if not self.pending:
            return
        self.pending = False
        if self.fault == 'history':
            self.fault = None
            self.emit('HIL_FAULT nvs_fail key=history bytes=2668')
        if self.unread == 'history' and not self.log_open:
            self.unread = None
            self.emit('HIL_FAULT nvs_unread key=history1')

    def status(self):
        self.own_save()
        self.save()
        return {'HISTORY_STATUS': {'records': str(self.ram['records']), 'pending': '0'}, 'MOTION_STATUS': {'pending': '0'},
                'NVS_STATUS': {'available_entries': str(140 - self.fill)}}

    def newest_launch(self):
        return self.ram['newest']

    def restart(self):
        self.boot += 1
        self.ram, self.log_open, self.unseen, self.pending = dict(self.stored), True, False, True
        self.emit('HISTORY_LOG_LOAD from=log')
        self.emit(f'HIL_BOOT boot_id={self.boot}')

    def hil(self, command, expect=None, timeout=5):
        words = command.split()
        if command == 'hil':
            self.emit(f'HIL_STATUS boot_id={self.boot}')
            return self.emit('HIL_FAULTS nvs_writes=0')
        if command == 'reboot':
            line = self.emit('HIL_REBOOT')
            self.restart()
            return line
        if words[0] == 'bests':
            if words[1:] == ['clear']:
                self.ram['bests'] = (0.0, 0.0, 0.0, 0.0)
            b = self.ram['bests']
            return self.emit(f'HIL_OK bests standard_three={b[0]:.1f} standard_single={b[1]:.1f} tcrt_three={b[2]:.1f} tcrt_single={b[3]:.1f} revision=1')
        if command == 'logstatus':
            return self.emit(f'HIL_OK logstatus open={int(self.log_open)} ready=3')
        if command == 'logclose':
            self.log_open, self.pending = False, True
            return self.emit('HIL_OK logclose open=0')
        if words[0] == 'nvs':
            self.fault = words[3] if words[1] == 'fail' else None
            self.unread = words[3] if words[1] == 'unread' else None
            return self.emit(f'HIL_OK nvs {command}')
        if words[0] == 'nvsfill':
            self.fill = int(words[1])
            return self.emit(f'HIL_OK nvsfill ok=1 entries={self.fill} keys=1')
        if words[0] in ('sleepdry', 'sleepok'):
            return self.emit(f'HIL_OK {words[0]} {words[1]}')
        raise AssertionError(command)

    def write(self, data):
        assert data == 'S\n', data
        self.save()
        self.emit('POWER_OFF reason=inactivity records=1 saved=1')
        self.emit('POWER_OFF_DEFERRED reason=sleep_dry_run restored=1 retry_s=60')

    def backup(self):
        text = f'BACKUP_BEGIN format=1\nBACKUP_DATA {len(self.backups)}\nBACKUP_END\n'
        self.backups[text] = dict(self.ram)
        return text

    def restore(self, text, timeout=60, refused_ok=False):
        backup = self.backups[text]
        self.own_save()  # while the backup uploads
        self.emit('RESTORE_VALID records=1')
        fault, self.fault = self.fault, None
        unread, self.unread = self.unread, None
        if unread == 'history' and not self.log_open:
            # NVS took the backup's history; its readback failed.
            self.emit('HIL_FAULT nvs_unread key=history1')
            if not self.verifies:
                self.stored, self.unseen = dict(backup), True
            return self.emit('RESTORE_ERROR reason=write_failed record=history written=0')
        if fault == 'cfg':
            if self.fixed:
                return self.emit('RESTORE_ERROR reason=write_failed record=look written=0')
            self.stored, self.shadow = dict(backup), True
            return self.emit('RESTORE_ERROR reason=write_failed written=partial')
        if self.fill and not self.full_restores:
            return self.emit('RESTORE_ERROR reason=write_failed record=motion written=0')
        if fault == 'history' and not self.fixed:
            return self.emit('RESTORE_ERROR reason=write_failed written=partial')
        if fault == 'history' or self.fill:
            if fault == 'history':
                self.emit('HIL_FAULT nvs_fail key=history1 bytes=2668')
            self.emit('RESTORE_NOTICE nvs_copy_failed history=1 best=0 store=log')
        self.stored, self.shadow = dict(backup), False
        line = self.emit('RESTORE_OK restarting=1')
        self.restart()
        return line


class Rig:
    def __init__(self, unit):
        self.dev, self.checks, self.notes, self.scenario, self.boot_id = unit, [], [], {}, None

    def check(self, name, ok, detail='', severity='fail'):
        self.checks.append((name, bool(ok), detail))

    def note(self, text):
        self.notes.append(text)

    def idle_sources(self, *a, **k):
        pass

    def wait_ready(self, timeout=20, fusion=True):
        return {}

    def hil_boot(self):
        start = self.dev.mark()
        self.dev.hil('hil')
        return self.dev.latest('HIL_STATUS', since=start).f

    def pull(self, optical, motion=None):
        u = self.dev
        number, _ = u.ram['newest']
        b = list(u.ram['bests'])
        b[0] = max(b[0], float(optical.rpm))
        u.ram = {'newest': (number + 1, optical.rpm), 'records': u.ram['records'] + 1, 'bests': tuple(b)}
        return optical

    def check_pull(self, r, tolerance=0.03, **kw):
        pass

    def failed(self):
        return [c for c in self.checks if not c[1]]


class RestorePartial(unittest.TestCase):
    def setUp(self):
        self.saved = scenarios.signals, scenarios.time.sleep
        scenarios.signals = types.SimpleNamespace(standard_pull=lambda rpm, label=None: types.SimpleNamespace(rpm=rpm),
                                                  launch_motion=lambda **k: None)
        scenarios.time.sleep = lambda s: None

    def tearDown(self):
        scenarios.signals, scenarios.time.sleep = self.saved

    def run_on(self, unit):
        rig = Rig(unit)
        scenarios.restore_partial(rig)
        return rig

    def test_all_or_nothing_unit_passes(self):
        rig = self.run_on(Unit())
        self.assertEqual(rig.failed(), [])
        self.assertGreater(len(rig.checks), 15)
        # Each history fault met the restore's own write, not the unit's pending save.
        names = [name for name, _, _ in rig.checks]
        self.assertIn('history unread: the readback of the restored history failed', names)
        self.assertIn("history mirror refused: the restore's own NVS history write was refused", names)
        lines = rig.dev.lines
        faults = [l.index for l in lines if l.text.startswith('HIL_FAULT ')]
        self.assertEqual(len(faults), 2)
        self.assertTrue(all(lines[f - 1].text.startswith('RESTORE_VALID') for f in faults))

    def test_a_full_nvs_refused_whole_passes_too(self):
        rig = self.run_on(Unit(full_restores=False))
        self.assertEqual(rig.failed(), [])
        self.assertTrue(any(name.startswith('full NVS: refused') for name, _, _ in rig.checks))

    def test_the_0_20_10_behaviour_fails(self):
        failed = [name for name, _, _ in self.run_on(Unit(fixed=False)).failed()]
        self.assertIn('look refused: RESTORE_ERROR record=look written=0', failed)
        self.assertIn('look refused: after a restart it loads B', failed)
        self.assertIn('history mirror refused: RESTORE_OK', failed)

    def test_a_rollback_it_could_not_read_back_fails(self):
        failed = [name for name, _, _ in self.run_on(Unit(verifies=False)).failed()]
        self.assertEqual(failed, ['history unread: after a restart it loads B'])

    def test_it_leaves_no_fault_filler_or_dry_run(self):
        unit = Unit()
        self.run_on(unit)
        sent = [l.text for l in unit.lines if l.text.startswith('HIL_OK')]
        self.assertEqual((unit.fault, unit.unread, unit.fill, unit.log_open), (None, None, 0, True))
        self.assertIn('HIL_OK sleepdry 0', sent)
        self.assertIn('HIL_OK sleepok 0', sent)

    def test_it_runs_in_hil_all(self):
        import run_hil
        self.assertIn('restore_partial', run_hil.DEFAULT)
        self.assertIs(scenarios.SCENARIOS['restore_partial'], scenarios.restore_partial)


if __name__ == '__main__':
    unittest.main()
