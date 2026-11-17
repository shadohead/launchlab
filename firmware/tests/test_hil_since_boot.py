#!/usr/bin/env python3
"""check_hil_since_boot.py: a scenario that decides a check on a since-boot figure reboots first.

Synthetic scenarios.py sources and status formats, plus the real ones: the
0.20.7 mic_page reads and 0.20.6's long_save_pull stall check fail it, the
fixed forms pass.
"""
import importlib.util
from pathlib import Path
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('check_hil_since_boot', ROOT / 'firmware/tests/check_hil_since_boot.py')
since_boot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(since_boot)

TAGS = {
    'HEAP_STATUS': {'dma_free', 'dma_min_free', 'internal_min_free'},
    'MIC_STATUS': {'heap_low', 'stop_max_ms', 'behind_max_ms', 'gaps'},
    'MOTION_STATUS': {'max_gap_us', 'gaps'},
    'HIL_IDLE0': {'max_gap_us'},
    'PERF': {'adc_stalls'},
    'MOTION_GAP': {'n', 'at_ms'},
}
HEADER = '''
SINCE_BOOT = {
    'HEAP_STATUS': ('dma_min_free', 'internal_min_free'),
    'MIC_STATUS': ('stop_max_ms', 'behind_max_ms'),
    'MOTION_STATUS': ('max_gap_us',),
    'PERF': ('adc_stalls',),
    'MOTION_GAP': (),
}
SINCE_BOOT_OK = {}


def counter(block, tag, key):
    return 0


def reboot(rig):
    rig.dev.hil('reboot', expect='HIL_REBOOT')


def settle(rig):
    rig.idle_sources()
'''


def check(body, header=HEADER, tags=TAGS):
    return since_boot.check(header + textwrap.dedent(body), tags)


class Reads(unittest.TestCase):
    def assertFlags(self, body, *labels, header=HEADER):
        problems, _, _ = check(body, header)
        for label in labels:
            self.assertTrue(any(label in p for p in problems), (label, problems))
        return problems

    def assertClean(self, body, header=HEADER):
        problems, _, _ = check(body, header)
        self.assertEqual(problems, [])

    def test_mic_page_before_its_fix(self):
        # 0.20.7 gate, 193b569c: settle(), then the heap's since-boot low-water and MIC maxima decide checks.
        self.assertFlags('''
            def mic_page(rig):
                settle(rig)
                on = rig.dev.status()
                mic, heap = on.get('MIC_STATUS', {}), on.get('HEAP_STATUS', {})
                rig.check('heap', min(int(heap.get('dma_free')), int(heap.get('dma_min_free'))) >= 1,
                          f"dma_free={heap.get('dma_free')}")
                rig.check('stops', int(mic.get('stop_max_ms')) < 1000 and int(mic.get('behind_max_ms')) < 500)
            SCENARIOS = {'mic_page': mic_page}
            ''', 'HEAP_STATUS dma_min_free', 'MIC_STATUS stop_max_ms', 'MIC_STATUS behind_max_ms')

    def test_mic_page_after_its_fix(self):
        # efdc61c4: it reboots first; 12bc86b3: the low-water only in the detail.
        self.assertClean('''
            def mic_page(rig):
                reboot(rig)
                on = rig.dev.status()
                mic, heap = on.get('MIC_STATUS', {}), on.get('HEAP_STATUS', {})
                rig.check('heap', int(mic.get('heap_low')) >= 1, f"since boot: dma_min_free={heap.get('dma_min_free')}")
                rig.check('stops', int(mic.get('stop_max_ms')) < 1000 and int(mic.get('behind_max_ms')) < 500)
            SCENARIOS = {'mic_page': mic_page}
            ''')

    def test_reprinted_gap_lines(self):
        self.assertFlags('''
            def gaps(rig, start):
                return [l for l in rig.dev.since(start, 'MOTION_GAP ')]
            def page(rig):
                settle(rig)
                rig.check('no gap', not gaps(rig, 0))
            SCENARIOS = {'page': page}
            ''', 'page: scenarios.py', 'reads MOTION_GAP')

    def test_a_helper_read_counts_where_it_is_called(self):
        body = '''
            def low(rig):
                return rig.dev.status().get('HEAP_STATUS', {}).get('dma_min_free')
            def before(rig):
                rig.check('low', low(rig) > 1)
                reboot(rig)
            def after(rig):
                reboot(rig)
                rig.check('low', low(rig) > 1)
            SCENARIOS = {'before': before, 'after': after}
            '''
        problems = self.assertFlags(body, 'before: ')
        self.assertFalse(any(p.startswith('after:') for p in problems), problems)

    def test_notes_details_and_counter_differences_pass(self):
        self.assertClean('''
            def stall(rig):
                settle(rig)
                before = counter(rig.dev.status(), 'PERF', 'adc_stalls')
                after = counter(rig.dev.status(), 'PERF', 'adc_stalls')
                def delta(key):
                    return counter(rig.dev.status(), 'PERF', key) - before
                rig.check('counted', after == before + 1 and delta('adc_stalls') == 1, f"{rig.dev.status().get('PERF', {}).get('adc_stalls')}")
                rig.note(f"low-water {rig.dev.status().get('HEAP_STATUS', {}).get('dma_min_free')}")
            SCENARIOS = {'stall': stall, 'lambda': lambda rig: stall(rig)}
            ''')

    def test_long_save_pull_before_its_fix(self):
        # 0.20.6's absolute stall check, which adc_stall (later in DEFAULT) would break if it ran first.
        self.assertFlags('''
            def pull_during_long_save(rig):
                settle(rig)
                status = rig.dev.status()
                rig.check('no ADC stall or overflow', status.get('PERF', {}).get('adc_stalls') == '0', str(status.get('PERF', {})))
            SCENARIOS = {'long_save_pull': pull_during_long_save}
            ''', 'long_save_pull: ', 'PERF adc_stalls')

    def test_a_shared_field_name_needs_its_tag(self):
        # max_gap_us is MOTION_STATUS's (since boot) and HIL_IDLE0's (reset by !idle0 reset).
        self.assertClean('''
            def backlog(rig):
                settle(rig)
                rig.dev.hil('idle0 reset')
                idle = rig.dev.hil('idle0', expect='HIL_IDLE0')
                rig.check('idle', int(idle.f.get('max_gap_us')) < 1000000)
                rig.note(str(rig.dev.status().get('MOTION_STATUS')))
            SCENARIOS = {'backlog': backlog}
            ''')
        self.assertFlags('''
            def backlog(rig):
                settle(rig)
                rig.check('gap', int(rig.dev.status().get('MOTION_STATUS', {}).get('max_gap_us')) < 1000000)
            SCENARIOS = {'backlog': backlog}
            ''', 'MOTION_STATUS max_gap_us')

    def test_listed_guards(self):
        body = '''
            def songs(rig):
                kind = 'dma'
                earlier = rig.dev.status().get('HEAP_STATUS', {}).get(f'{kind}_min_free')
                rig.check('low', earlier > 1)
            SCENARIOS = {'songs': songs}
            '''
        self.assertFlags(body, 'HEAP_STATUS dma_min_free', 'HEAP_STATUS internal_min_free')
        guarded = HEADER.replace("SINCE_BOOT_OK = {}", "GUARD = 'compared with the mark before its plays'\n"
                                 "SINCE_BOOT_OK = {'songs': {'HEAP_STATUS dma_min_free': GUARD, 'HEAP_STATUS internal_min_free': GUARD}}")
        self.assertClean(body, guarded)
        stale = HEADER.replace("SINCE_BOOT_OK = {}", "SINCE_BOOT_OK = {'songs': {'HEAP_STATUS dma_min_free': 'x'}, 'gone': {}}")
        self.assertFlags('''
            def songs(rig):
                reboot(rig)
                rig.check('low', rig.dev.status().get('HEAP_STATUS', {}).get('dma_min_free') > 1)
            SCENARIOS = {'songs': songs}
            ''', 'no longer reads HEAP_STATUS dma_min_free', 'SINCE_BOOT_OK names gone', header=stale)

    def test_the_catalogue_names_printed_figures(self):
        bad = HEADER.replace("'PERF': ('adc_stalls',),", "'PERF': ('adc_stall',), 'NOPE': ('x',),")
        self.assertFlags('''
            def a(rig):
                reboot(rig)
            SCENARIOS = {'a': a}
            ''', "PERF has no adc_stall=", 'prints no NOPE line', header=bad)


class Repository(unittest.TestCase):
    def test_the_firmware_formats_are_read(self):
        tags = since_boot.printed()
        self.assertIn('dma_min_free', tags['HEAP_STATUS'])
        self.assertIn('adc_stalls', tags['PERF'])
        self.assertIn('max_gap_us', tags['HIL_IDLE0'])

    def test_scenarios_py_passes(self):
        problems, checked, scenarios = since_boot.check(since_boot.SCENARIOS.read_text(), since_boot.printed())
        self.assertEqual(problems, [])
        self.assertGreater(scenarios, 25)


if __name__ == '__main__':
    unittest.main()
