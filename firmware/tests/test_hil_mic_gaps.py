#!/usr/bin/env python3
"""HIL mic_page, "no IMU gap with the mic on, beyond a history save's": a
history log save may stretch one IMU poll into a gap, and the check passes
only a gap that falls on a save. A gap carries the unit's uptime and a save
the host's read time; the check tied the two clocks with LAUNCH_RESULT, whose
t_us is the launch but which prints when the pull has ended, a second later.
In a 0.20.9 gate run a save's gap (0.03 s from its save) was placed 1.04 s
after it and failed the gate. The clocks are now tied by HIL_STATUS, which
prints the uptime as it is sent."""
import inspect
from pathlib import Path
import sys
import types
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; the timing needs none of that.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import hil_device  # noqa: E402
import scenarios  # noqa: E402


def lines(*timed):
    return [hil_device.Line(i, t, text) for i, (t, text) in enumerate(timed)]


GAP = ('MOTION_GAP n=1 at_ms=42843 gap_us=39243 polls=2 empty_polls=1 max_period_us=21989 max_lock_us=33 '
       'max_poll_us=14813 max_ring_us=7665')
STATUS = 'HIL_STATUS boot_id=85d46509 reset=software uptime_ms=72638 optical=generator idle=1800/3.0 buffer=0'
# The 0.20.9 gate's mic_page (host read times, serial.log): the pull's result,
# the save 10 s later, the gap in the unit's next status prints, and the status
# the scenario reads after the spin.
GATE = lines((163703.469, 'LAUNCH_RESULT valid=1 peak_rpm=7501.9 revs=83 amplitude=1003.1 start_noise=8.0 launches=1 end=slowdown t_us=31784960'),
             (163713.490, 'HISTORY_SAVE ok=1 generation=7830 duration_us=22488 store=log'),
             (163714.823, GAP),
             (163716.804, GAP),
             (163743.319, STATUS))


def with_gap(text, gate=GATE):
    return [hil_device.Line(l.index, l.t, text if l.text == GAP else l.text) for l in gate]


class MicGaps(unittest.TestCase):
    def test_the_gate_gap_is_its_saves(self):
        self.assertEqual(scenarios.gaps_beyond_saves(GATE), (1, []))
        # Where the old tie put it: over a second after the save.
        launch = GATE[0]
        old = 42.843 + launch.t - int(launch.f['t_us']) / 1e6
        self.assertGreater(old - GATE[1].t, 1.0)

    def test_a_late_status_read_does_not_move_the_tie(self):
        late = GATE + lines((163746.0, STATUS.replace('uptime_ms=72638', 'uptime_ms=74000')))
        self.assertEqual(scenarios.gaps_beyond_saves(late), (1, []))

    def test_a_gap_away_from_any_save(self):
        count, unexplained = scenarios.gaps_beyond_saves(with_gap(GAP.replace('at_ms=42843', 'at_ms=52843')))
        self.assertEqual((count, [f['at_ms'] for f in unexplained]), (1, ['52843']))
        count, unexplained = scenarios.gaps_beyond_saves([l for l in GATE if not l.text.startswith('HISTORY_SAVE ')])
        self.assertEqual((count, len(unexplained)), (1, 1))

    def test_a_gap_of_another_shape_on_a_save(self):
        for change in (('gap_us=39243', 'gap_us=80000'), ('polls=2', 'polls=9'), ('max_poll_us=14813', 'max_poll_us=4000')):
            count, unexplained = scenarios.gaps_beyond_saves(with_gap(GAP.replace(*change)))
            self.assertEqual((count, len(unexplained)), (1, 1), change)

    def test_no_status_explains_nothing(self):
        count, unexplained = scenarios.gaps_beyond_saves(GATE[:4])
        self.assertEqual((count, len(unexplained)), (1, 1))

    def test_gaps_from_before_the_window(self):
        self.assertEqual(scenarios.gaps_beyond_saves(GATE, before=1), (0, []))

    def test_the_scenario_reads_a_status_in_the_window(self):
        source = inspect.getsource(scenarios.mic_page)
        window = source[source.index('start = d.mark()'):source.index('mic_gaps_beyond_saves(rig, start')]
        self.assertIn('block = d.status()', window)
        self.assertNotIn('launch_line', source)


class MissedPolls(unittest.TestCase):
    """The 0.20.9 gate's resumed mic_page on the 14:C1 rig: the window's one
    save woke the IMU task a period late, +2 late polls and +1 missed, no gap,
    and failed the gate."""
    BASE = {'gaps': 0, 'gate_misses': 0, 'late_polls': 0, 'missed_polls': 0}
    SAVE = (184633.916, 'HISTORY_SAVE ok=1 generation=173 duration_us=24204 store=log')

    def after(self, missed):
        return dict(self.BASE, late_polls=missed + 1, missed_polls=missed)

    def test_a_poll_missed_on_a_save_passes(self):
        self.assertEqual(scenarios.missed_polls_beyond_saves(lines(self.SAVE), self.BASE, self.after(1)), 0)

    def test_a_poll_missed_with_no_save_fails(self):
        self.assertEqual(scenarios.missed_polls_beyond_saves(lines((1.0, STATUS)), self.BASE, self.after(1)), 1)

    def test_a_save_covers_only_a_stall_under_60_ms(self):
        self.assertEqual(scenarios.missed_polls_beyond_saves(lines(self.SAVE), self.BASE, self.after(3)), 0)
        self.assertEqual(scenarios.missed_polls_beyond_saves(lines(self.SAVE), self.BASE, self.after(4)), 1)

    def test_no_missed_poll_is_none_beyond(self):
        self.assertEqual(scenarios.missed_polls_beyond_saves(lines(self.SAVE), self.BASE, self.BASE), 0)

    def test_mic_page_judges_missed_polls_by_the_saves(self):
        body = inspect.getsource(scenarios.mic_page)
        self.assertIn('missed_polls_beyond_saves(d.since(start), base, after)', body)
        self.assertNotIn("after['missed_polls'] == base['missed_polls']", body)


if __name__ == '__main__':
    unittest.main()
