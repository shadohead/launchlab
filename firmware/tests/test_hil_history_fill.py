#!/usr/bin/env python3
"""HIL nvs_tight, "a full history (128 records)": the scenario counted on the
scenarios before it to leave the history full. A resumed gate
(release_gate.py --resume) starts after them, and the 0.20.9 gate's resume
ran nvs_tight on 107 records and failed on that alone. nvs_tight now fills
the history itself, with pulls, and a full one costs no pull."""
from pathlib import Path
import sys
import types
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; the fill needs none of that.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import scenarios  # noqa: E402


class Dev:
    def __init__(self, rig):
        self.rig = rig

    def status(self):
        return {'HISTORY_STATUS': {'records': str(self.rig.records), 'pending': '0'}, 'MOTION_STATUS': {'pending': '0'}}


class Rig:
    """A unit whose history holds `records`; a pull adds `adds` up to 128."""

    def __init__(self, records, adds=1):
        self.records, self.adds, self.pulls, self.checked = records, adds, [], 0
        self.dev = Dev(self)

    def pull(self, optical, motion=None):
        self.pulls.append(optical)
        self.records = min(128, self.records + self.adds)
        return optical

    def check_pull(self, r, tolerance=0.03):
        self.checked += 1


class HistoryFill(unittest.TestCase):
    def test_the_resumed_gates_107_records_are_filled_to_128(self):
        rig = Rig(107)
        self.assertEqual(scenarios.fill_history(rig), 128)
        self.assertEqual((len(rig.pulls), rig.checked), (21, 21))

    def test_a_full_history_takes_no_pull(self):
        rig = Rig(128)
        self.assertEqual(scenarios.fill_history(rig), 128)
        self.assertEqual(rig.pulls, [])

    def test_an_empty_history_is_filled(self):
        rig = Rig(0)
        self.assertEqual(scenarios.fill_history(rig), 128)
        self.assertEqual(len(rig.pulls), 128)

    def test_pulls_that_are_not_recorded_stop_it_short(self):
        rig = Rig(107, adds=0)
        self.assertEqual(scenarios.fill_history(rig), 107)
        self.assertEqual(len(rig.pulls), 1)

    def test_nvs_tight_fills_before_it_checks(self):
        import inspect
        body = inspect.getsource(scenarios.nvs_tight)
        self.assertIn('fill_history(rig)', body)
        self.assertLess(body.index('fill_history(rig)'), body.index("'a full history (128 records)'"))


if __name__ == '__main__':
    unittest.main()
