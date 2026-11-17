#!/usr/bin/env python3
"""HIL sleep_blockers case 4, a mic stop that never ends: the sleep must run
MicSession::STOP_LIMIT_MS after it was asked. The check timed the wait from
the host's read of MIC_STATE stopping, but the host reads a line when it
arrives, and in the 0.20.8 gate that line arrived 1.3 s late with the unit's
2 s status print: the sleep, 30.0 s after the request, measured 28.7 s and
failed the 29-35 s bound. The device starts its clock only after the request,
so the wait is now timed from the host's own send time."""
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

LIMIT = scenarios.MIC_STOP_LIMIT_S


def lines(*timed):
    return [hil_device.Line(i, t, text) for i, (t, text) in enumerate(timed)]


# The 0.20.8 gate's second HIL all (host read times, serial.log): the request
# went out at 2285532.62 and was answered at once; MIC_STATE stopping came
# with the status print 1.3 s later; the sleep ran 30.0 s after the request.
GATE = lines((2285532.627, 'POWER_SLEEP_TEST requested=1 normal_save_and_sleep_path=1'),
             (2285533.953, 'MIC_STATE state=stopping why=none'),
             (2285533.953, 'STATUS v=0.20.8-sticks3-songs-hil-pm board=StickS3'),
             (2285562.645, 'POWER_OFF_NOTICE mic_stop_timeout'),
             (2285562.645, 'POWER_OFF reason=inactivity idle_ms=90054 since_use_ms=90054 records=128 saved=1 forced=0'),
             (2285563.300, 'POWER_OFF_DEFERRED reason=sleep_dry_run restored=1 retry_s=0'))
ASKED = 2285532.620


class MicStopWait(unittest.TestCase):
    def test_a_late_stopping_line(self):
        after = scenarios.mic_stop_wait(GATE, ASKED)
        self.assertAlmostEqual(after, 30.025, places=3)
        self.assertTrue(LIMIT - 1 <= after <= LIMIT + 5, after)
        # What the old check measured from the stopping line's read.
        self.assertLess(GATE[4].t - GATE[1].t, LIMIT - 1)

    def test_lines_read_as_they_are_printed(self):
        prompt = lines((10.01, 'MIC_STATE state=stopping why=none'), (40.02, 'POWER_OFF_NOTICE mic_stop_timeout'),
                       (40.02, 'POWER_OFF reason=inactivity idle_ms=90000'))
        self.assertAlmostEqual(scenarios.mic_stop_wait(prompt, 10.0), 30.02, places=3)

    def test_no_sleep(self):
        self.assertIsNone(scenarios.mic_stop_wait(GATE[:3], ASKED))

    def test_the_scenario_times_from_its_request(self):
        source = inspect.getsource(scenarios.sleep_blockers)
        case = source[source.index("d.hil('micstall 1'"):source.index("d.hil('micstall 0'")]
        self.assertLess(case.index('asked = time.monotonic()'), case.index('standby_dry_run(rig, MIC_STOP_LIMIT_S, request=True'))
        self.assertIn('after = mic_stop_wait(lines, asked)', source)
        self.assertNotIn('off.t - stopping.t', source)


if __name__ == '__main__':
    unittest.main()
