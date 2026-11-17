#!/usr/bin/env python3
"""The HIL sleep scale never reaches a production build.

A HIL build's `!sleepscale <n>` divides the limits that end a keep-awake
path through `sleepLimit()` (power_status.h's and the deferred sleep's retry;
the Mic RPM stop's 30 s stays real), so the rig's sleep_blockers waits
seconds where a unit waits minutes. Production must keep the real limits,
compiled as constants. This check:

1. builds test_sleep_scale.cpp as production, where it asserts at compile
   time that `sleepLimit()` is each limit itself and that no `Hil::sleepScale`
   exists, and runs it with the real limits;
2. builds it again with LAUNCHLAB_HIL=1 and runs it: the real limits at
   scale 1 (from boot), and each divided at 10 and 60;
3. lists every `sleepLimit()` in the sketch sources: only the keep-awake
   limits go through it, so no other timing differs on a scaled HIL image,
   and only behind `#if LAUNCHLAB_HIL` does anything name the divisor.

The real limits themselves are pinned by test_motion_activity and
check_keep_awake.py, and check_hil_isolation.py keeps LAUNCHLAB_HIL out of the
production build scripts.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / 'firmware/LaunchLabMini'
TEST = ROOT / 'firmware/tests/test_sleep_scale.cpp'

# Every sleepLimit() call: (file, the argument). A new one changes what a scaled
# HIL image does; list it here only for a limit that ends a keep-awake path.
SCALED = sorted([
    ('power_status.h', 'uint32_t(minutes_)*60*1000'),  # Sleep after
    ('power_status.h', 'UNUSED_SHAKE_MS'),
    ('power_status.h', 'UNUSED_SHAKE_LIMIT_MS'),
    ('power_status.h', 'MOTION_LIMIT_MS'),
    ('power_status.h', 'MEASURE_LIMIT_MS'),
    ('power_status.h', 'LIMIT_MS'),  # HeldButtonUse
    ('LaunchLabMini.ino', '60000'),  # the retry after a deferred sleep
])
CALL = re.compile(r'\bsleepLimit\(((?:[^()]|\([^()]*\))*)\)')


def production_view(text):
    """The source as a production build sees it: `#if LAUNCHLAB_HIL` branches blanked
    (their `#else` kept), line numbers kept."""
    out, stack = [], []
    for line in text.splitlines():
        directive = line.strip()
        if re.match(r'#\s*if\s+LAUNCHLAB_HIL\b', directive):
            stack.append('hil')
        elif re.match(r'#\s*if\s+!\s*LAUNCHLAB_HIL\b', directive):
            stack.append('production')
        elif re.match(r'#\s*if', directive):
            stack.append('other')
        elif re.match(r'#\s*(elif|else)', directive) and stack:
            stack[-1] = {'hil': 'production', 'production': 'hil'}.get(stack[-1], stack[-1])
        elif re.match(r'#\s*endif', directive) and stack:
            stack.pop()
        else:
            out.append('' if 'hil' in stack else line)
            continue
        out.append('')
    return '\n'.join(out)


def calls():
    found, problems = [], []
    for path in sorted(SKETCH.glob('*.h')) + [SKETCH / 'LaunchLabMini.ino']:
        text = path.read_text()
        production = production_view(text)
        for match in CALL.finditer(production):
            # Not the declaration itself, nor a comment naming sleepLimit().
            if match.group(1) not in ('uint32_t ms', ''):
                found.append((path.name, match.group(1)))
        if path.name.startswith('hil'):
            continue
        for number, line in enumerate(production.splitlines(), 1):
            if 'sleepScale' in line:
                problems.append(f'{path.name}:{number}: the divisor outside #if LAUNCHLAB_HIL: {line.strip()}')
    return sorted(found), problems


def main():
    problems = []
    found, leaks = calls()
    problems += leaks
    for item in sorted(set(found) - set(SCALED)):
        problems.append(f'NEW sleepLimit() in {item[0]}: {item[1]}; only a keep-awake limit goes through it (list it in SCALED)')
    for item in sorted(set(SCALED) - set(found)):
        problems.append(f'STALE entry in SCALED: no sleepLimit({item[1]}) in {item[0]}')
    cxx = os.environ.get('CXX', 'c++')
    flags = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', f'-I{SKETCH}']
    with tempfile.TemporaryDirectory(prefix='sleep-scale-') as tmp:
        for name, define in (('production', []), ('hil', ['-DLAUNCHLAB_HIL=1'])):
            exe = Path(tmp) / name
            build = subprocess.run([cxx, *flags, *define, str(TEST), '-o', str(exe)], capture_output=True, text=True)
            if build.returncode:
                problems.append(f'{name} build of test_sleep_scale.cpp failed:\n{build.stdout}{build.stderr}')
                continue
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            print(run.stdout, end='')
            if run.returncode:
                problems.append(f'{name} test_sleep_scale failed (exit {run.returncode}):\n{run.stdout}{run.stderr}')
    if problems:
        print('FAIL: ' + '\n'.join(problems))
        return 1
    print(f'PASS: {len(found)} sleep limits go through sleepLimit(), each a keep-awake limit; production compiles them '
          'as constants with no divisor, a HIL build divides them only on !sleepscale')
    return 0


if __name__ == '__main__':
    sys.exit(main())
