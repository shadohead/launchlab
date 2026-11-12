#!/usr/bin/env python3
"""run_hil.py puts the unit back before its first scenario (scenarios.preflight).
The 0.20.8 gate lost three 85-minute HIL all runs to state on the unit, not to
the firmware: the owner's Mounting Sidecar (wear_check, whose main cases assume
Top, failed 54 checks) and nvs_tight's filler left by a run stopped inside it
(themes failed 21 checks on nvs_space)."""
import inspect
from pathlib import Path
import sys
import types
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; the preflight needs none of that.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import hil_device  # noqa: E402
import run_hil  # noqa: E402
import scenarios  # noqa: E402


class Unit:
    """A HIL image's answers to the preflight's commands."""
    def __init__(self, mounting, filler, clears=True, bluetooth=None, restarts=True):
        self.mounting, self.filler, self.clears, self.sent = mounting, filler, clears, []
        self.bluetooth, self.restarts, self.boot = bluetooth, restarts, 7

    def mark(self):
        return len(self.sent)

    def wait(self, prefix, timeout, since=0):
        return hil_device.Line(len(self.sent), 0.0, f'HIL_BOOT boot_id={self.boot}') if prefix == 'HIL_BOOT' else None

    def hil(self, command, expect=None, timeout=5):
        self.sent.append(command)
        if command == 'reset':
            text = 'HIL_OK reset'
        elif command == 'nvsfill 0':
            if self.clears:
                self.filler = 0
            text = f'HIL_OK nvsfill ok=1 entries={self.filler} keys={1 if self.filler else 0}'
        elif command == f'set {scenarios.MOUNT_ROW} 0':
            self.mounting = 'Top'
            text = f'HIL_SET row={scenarios.MOUNT_ROW} value=0 error=0 page=5 ms=1'
        elif command == f'set {scenarios.BLUETOOTH_ROW} 0':
            if self.restarts:
                self.bluetooth, self.boot = '0', self.boot + 1
            text = f'HIL_SET row={scenarios.BLUETOOTH_ROW} value=0 error=0 page=0 ms=1'
        elif command == 'home':
            return None
        else:
            raise AssertionError(command)
        return hil_device.Line(len(self.sent), 0.0, text)

    def status(self):
        status = {'MOUNT_STATUS': {'selected': self.mounting}, 'NVS_STATUS': {'used_entries': str(361 + self.filler)}}
        if self.bluetooth is not None:
            status['BLE_STATUS'] = {'started': self.bluetooth}
        return status


def rig(unit):
    return types.SimpleNamespace(dev=unit, boot_id=7, hil_boot=lambda: {'boot_id': unit.boot})


class Preflight(unittest.TestCase):
    def test_the_owners_unit_after_a_stopped_run(self):
        unit = Unit('Sidecar', 45)
        found = scenarios.preflight(rig(unit))
        self.assertEqual(unit.sent, ['reset', 'nvsfill 0', f'set {scenarios.MOUNT_ROW} 0', 'home'])
        self.assertEqual((unit.mounting, unit.filler), ('Top', 0))
        self.assertEqual(found, {'mounting_was': 'Sidecar', 'nvs_used_before': '406', 'nvs_used_after_filler': '361'})

    def test_a_clean_unit_on_top(self):
        unit = Unit('Top', 0)
        self.assertEqual(scenarios.preflight(rig(unit))['mounting_was'], 'Top')
        self.assertEqual(unit.sent, ['reset', 'nvsfill 0'])

    def test_the_owners_unit_with_bluetooth_on(self):
        # The owner's unit, which is also the rig, came to the 0.20.9 gate with Bluetooth On.
        unit = Unit('Top', 0, bluetooth='1')
        r = rig(unit)
        found = scenarios.preflight(r)
        self.assertEqual(unit.sent, [f'set {scenarios.BLUETOOTH_ROW} 0', 'reset', 'nvsfill 0'])
        self.assertEqual((unit.bluetooth, r.boot_id, found.get('bluetooth_was')), ('0', 8, 'On'))
        off = Unit('Top', 0, bluetooth='0')
        self.assertNotIn('bluetooth_was', scenarios.preflight(rig(off)))
        self.assertEqual(off.sent, ['reset', 'nvsfill 0'])
        with self.assertRaises(RuntimeError):
            scenarios.preflight(rig(Unit('Top', 0, bluetooth='1', restarts=False)))

    def test_filler_that_stays(self):
        with self.assertRaises(RuntimeError):
            scenarios.preflight(rig(Unit('Top', 45, clears=False)))

    def test_run_hil_runs_it_before_the_first_scenario(self):
        source = inspect.getsource(run_hil.main)
        self.assertLess(source.index('scenarios.preflight(rig)'), source.index('for name in names:'))
        self.assertLess(source.index('rig.require_hil_build()'), source.index('scenarios.preflight(rig)'))


if __name__ == '__main__':
    unittest.main()
