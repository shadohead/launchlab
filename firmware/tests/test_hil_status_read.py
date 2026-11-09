#!/usr/bin/env python3
"""upgrade_check.plain_status: the status block a `p` asks for, even when the
unit's own 2 s status print is under way as it goes out. In the 0.20.7 gate
songs_rollback sent `p` in the middle of such a block: its MOTION_CLIP ended
the wait before the requested block's STATUS line, and 0.19.0's runtime read
as empty on a unit that was running it."""
from pathlib import Path
import sys
import types
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; the status read needs none of that.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import hil_device  # noqa: E402
import upgrade_check  # noqa: E402

BLOCK = ['STATUS v={v} board=StickS3', 'POWER_STATUS auto_off_s=60', 'MOTION_CLIP accel_range_g=16']


class Unit:
    """Lines arriving as on the rig's port, one at a time while a wait reads
    them: `p` is answered after `pending`, the rest of a block the unit was
    already printing, and only lines that have arrived can be read."""
    def __init__(self, pending, runtime):
        self.lines, self.incoming, self.pending, self.runtime = [], [], pending, runtime

    def add(self, text):
        self.lines.append(hil_device.Line(len(self.lines), 0.0, text))

    def mark(self):
        return len(self.lines)

    def write(self, text):
        assert text == 'p'
        self.incoming += self.pending + [line.format(v=self.runtime) for line in BLOCK]

    def since(self, index, tag=None):
        return [l for l in self.lines[index:] if tag is None or l.text.startswith(tag)]

    def wait(self, predicate, timeout, since=None, fail=None):
        test = (lambda l: l.text.startswith(predicate)) if isinstance(predicate, str) else predicate
        found = next((l for l in self.lines[since or 0:] if test(l)), None)
        while found is None and self.incoming:
            self.add(self.incoming.pop(0))
            found = self.lines[-1] if test(self.lines[-1]) else None
        return found


class PlainStatus(unittest.TestCase):
    def test_a_quiet_unit(self):
        status = upgrade_check.plain_status(Unit([], '0.19.0-sticks3-loadable-pm'), timeout=0)
        self.assertEqual(status['STATUS']['v'], '0.19.0-sticks3-loadable-pm')

    def test_its_own_status_print_under_way(self):
        unit = Unit(['POWER_STATUS auto_off_s=60', 'MOTION_CLIP accel_range_g=16'], '0.19.0-sticks3-loadable-pm')
        status = upgrade_check.plain_status(unit, timeout=0)
        self.assertEqual(status.get('STATUS', {}).get('v'), '0.19.0-sticks3-loadable-pm')

    def test_no_status_at_all(self):
        unit = Unit([], '')
        unit.write = lambda text: unit.incoming.append('MOTION_CLIP accel_range_g=16')
        self.assertNotIn('STATUS', upgrade_check.plain_status(unit, timeout=0))


if __name__ == '__main__':
    unittest.main()
