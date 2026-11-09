#!/usr/bin/env python3
"""HIL about_page and battery_icon reach their Settings rows from wherever the
cursor is. Settings keeps the row it was last on, and a `!set` leaves it on
its row: in the 0.20.8 smoke `settings` left it on Brightness and
`battery_icon`'s own `!set 13 1` on Battery icon, so the fixed 13 and 11 B
presses (counted from the top row) landed on Shake to wake and Best effect,
and every check after them failed. The scenarios now walk one full lap of
the rows, checked against Controls::ORDER, and on to their row. Its cleanup
`!set 13 1` also waited for HIL_OK, which `!set` never sends (it replies
HIL_SET)."""
import ast
from pathlib import Path
import re
import sys
import types
import unittest

FIRMWARE = Path(__file__).resolve().parents[1]
TOOLS = FIRMWARE / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; these walks need none of that.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import scenarios  # noqa: E402

CONTROLS = (FIRMWARE / 'LaunchLabMini/controls.h').read_text()


def names(pattern):
    return [n.strip() for n in re.search(pattern, CONTROLS, re.S).group(1).split(',')]


PAGES = names(r'enum class Page:uint8_t \{(.*?)\};')
SETTINGS = names(r'enum class Setting:uint8_t \{(.*?)\};')
ORDER = [SETTINGS.index(n.replace('Setting::', '')) for n in names(r'ORDER\[SETTING_COUNT\]=\{(.*?)\};')]
COUNT = int(re.search(r'SETTING_COUNT=(\d+);', CONTROLS).group(1))
MENU_SETTINGS = 3  # Controls::State::tapA: Menu row 3 opens Settings


class Unit:
    """Controls::State's Main, Menu, Settings, Edit and About pages as the
    USB buttons drive them (tapA, tapB, holdB), with the rows in `order` and
    the Settings cursor left on `setting`."""
    def __init__(self, setting, order=ORDER):
        self.page, self.menu, self.setting, self.draft, self.saves = 'Main', 0, setting, 0, 0
        self.order = order

    def next_row(self):
        rows = self.order + [COUNT]
        return rows[(rows.index(self.setting) + 1) % len(rows)]

    def write(self, text):
        letter = text.strip()
        if letter == 'h':
            if self.page == 'Main':
                self.page, self.menu = 'Menu', 0
            elif self.page == 'Menu':
                self.menu += 1
            elif self.page == 'Settings':
                self.setting = self.next_row()
            elif self.page == 'Edit':
                self.draft = 1 - self.draft
        elif letter == 'a':
            if self.page == 'Menu' and self.menu == MENU_SETTINGS:
                self.page = 'Settings'
            elif self.page == 'Settings':
                name = SETTINGS[self.setting] if self.setting < COUNT else 'Back'
                self.page = {'About': 'About', 'WipeAll': 'Wipe', 'Back': 'Menu'}.get(name, 'Edit')
            elif self.page == 'Edit':
                self.page, self.saves = 'Settings', self.saves + 1
            elif self.page == 'About':
                self.page = 'Settings'
        elif letter == 'b':
            self.page = {'Edit': 'Settings', 'About': 'Settings', 'Settings': 'Menu', 'Menu': 'Main'}.get(self.page, self.page)

    def hil(self, command, expect=None, timeout=5):
        word, *args = command.split()
        if word == 'home':
            self.page = 'Main'
            return types.SimpleNamespace(text='HIL_OK home')
        if word == 'set':
            self.page, self.setting = 'Settings', int(args[0])
            self.saves += 1
            if expect != 'HIL_SET':
                raise scenarios.DeviceError(f'no reply to !{command}')
            return types.SimpleNamespace(text=f'HIL_SET row={args[0]}')
        raise AssertionError(f'unexpected !{command}')

    def status(self):
        return {'UI_STATUS': {'page': str(PAGES.index(self.page)), 'setting': str(self.setting)},
                'SETTINGS_SAVE_STATUS': {'saves': str(self.saves), 'pending': '0x000'}}


class Rig:
    def __init__(self, unit):
        self.dev, self.checks = unit, []

    def check(self, name, ok, detail=''):
        self.checks.append((name, bool(ok), detail))

    def idle_sources(self):
        pass

    def wait_ready(self, timeout):
        return {}

    def screenshot(self, name):
        pass

    def failed(self):
        return [(name, detail) for name, ok, detail in self.checks if not ok]


class SettingsWalk(unittest.TestCase):
    def setUp(self):
        # The scenarios wait on the real unit between presses; this one answers at once.
        self.time = scenarios.time
        scenarios.time = types.SimpleNamespace(sleep=lambda s: None, time=self.time.time, monotonic=self.time.monotonic)

    def tearDown(self):
        scenarios.time = self.time

    def test_rows_and_pages_follow_controls(self):
        self.assertEqual(scenarios.SETTINGS_ORDER, [str(row) for row in ORDER + [COUNT]])
        self.assertEqual(scenarios.ABOUT_ROW, str(SETTINGS.index('About')))
        self.assertEqual(scenarios.BATTERY_ICON_ROW, str(SETTINGS.index('BatteryIcon')))
        for constant, page in (('ABOUT_PAGE', 'About'), ('SETTINGS_PAGE', 'Settings'), ('EDIT_PAGE', 'Edit'), ('BATTERY_PAGE', 'Battery'), ('MENU_PAGE', 'Menu')):
            self.assertEqual(getattr(scenarios, constant), str(PAGES.index(page)), constant)

    def test_about_page_from_any_row(self):
        # The smoke's cases: Brightness (after `settings`) and Battery icon (after `battery_icon`).
        for start in ORDER + [COUNT]:
            rig = Rig(Unit(start))
            scenarios.about_page(rig)
            self.assertEqual(rig.failed(), [], f'cursor left on row {start}')
            self.assertEqual(rig.dev.saves, 0, f'about_page applied a setting from row {start}')

    def test_walk_to_battery_icon_from_any_row(self):
        target = scenarios.BATTERY_ICON_ROW
        for start in ORDER + [COUNT]:
            unit = Unit(start)
            unit.page = 'Settings'
            ordered, rows = scenarios.settings_walk(Rig(unit), target)
            self.assertTrue(ordered, f'from row {start}: {rows}')
            self.assertEqual(unit.setting, int(target))
            self.assertGreaterEqual(len(rows), len(ORDER) + 2, 'one full lap at least')

    def test_walk_reports_a_row_out_of_place(self):
        # Battery icon swapped with Wipe all: the walk still stops on Battery icon, but out of order.
        swapped = list(ORDER)
        i, j = swapped.index(SETTINGS.index('BatteryIcon')), swapped.index(SETTINGS.index('WipeAll'))
        swapped[i], swapped[j] = swapped[j], swapped[i]
        unit = Unit(SETTINGS.index('Brightness'), swapped)
        unit.page = 'Settings'
        ordered, rows = scenarios.settings_walk(Rig(unit), scenarios.BATTERY_ICON_ROW)
        self.assertFalse(ordered, rows)

    def test_every_set_waits_for_its_reply(self):
        # `!set` answers HIL_SET, never HIL_OK set: without expect= the call times out.
        def command(node):
            if isinstance(node, ast.Constant):
                return node.value
            if isinstance(node, ast.JoinedStr) and node.values and isinstance(node.values[0], ast.Constant):
                return node.values[0].value
            return ''
        sets = [call for call in ast.walk(ast.parse((TOOLS / 'scenarios.py').read_text()))
                if isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute) and call.func.attr == 'hil'
                and call.args and str(command(call.args[0])).startswith('set ')]
        self.assertGreater(len(sets), 10)
        unanswered = [f'line {call.lineno}' for call in sets
                      if not any(k.arg == 'expect' and isinstance(k.value, ast.Constant) and k.value.value == 'HIL_SET' for k in call.keywords)]
        self.assertEqual(unanswered, [])


if __name__ == '__main__':
    unittest.main()
