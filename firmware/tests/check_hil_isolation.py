#!/usr/bin/env python3
"""Hardware-in-the-loop hooks must compile only into LAUNCHLAB_HIL=1 test builds.

Every reference to the HIL rig in the production sketch sources must sit in
the true branch of `#if LAUNCHLAB_HIL`, and the production PM build must not
define the flag. See firmware/tools/hil/README.md.
"""
import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[1]
SOURCES = FIRMWARE / 'LaunchLabMini'
REFERENCE = re.compile(r'\bHil::|\bhil[A-Z_]\w*|"hil(_\w+)?\.h"')


def check(path):
    stack, problems = [], []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        directive = line.strip()
        if re.match(r'#\s*if\s+LAUNCHLAB_HIL\b', directive):
            stack.append('hil')
            continue
        if re.match(r'#\s*if', directive):
            stack.append('other')
        elif re.match(r'#\s*(elif|else)', directive) and stack and stack[-1] == 'hil':
            stack[-1] = 'not-hil'
        elif re.match(r'#\s*endif', directive):
            stack.pop()
        elif REFERENCE.search(line) and 'hil' not in stack:
            problems.append(f'{path.name}:{number}: HIL reference outside #if LAUNCHLAB_HIL: {directive}')
    if stack:
        problems.append(f'{path.name}: unbalanced preprocessor blocks')
    return problems


def main():
    problems = []
    sources = [p for p in sorted(SOURCES.glob('*.h')) + [SOURCES / 'LaunchLabMini.ino'] if not p.name.startswith('hil')]
    for path in sources:
        problems += check(path)
    hooks = sum(p.read_text().count('#if LAUNCHLAB_HIL') for p in sources)
    if hooks < 8:
        problems.append(f'expected the HIL hook blocks in acquisition/IMU/sketch sources, found {hooks}')
    if (FIRMWARE / 'SYNC.json').exists():
        # The public copy builds with scripts/build-firmware.sh: production with no
        # extra flags, and --hil adds the flag to its build-hil image only.
        lines = [line.strip() for line in (FIRMWARE.parent / 'scripts/build-firmware.sh').read_text().splitlines()]
        defines = [line for line in lines if 'LAUNCHLAB_HIL=1' in line and not line.startswith('#')]
        if "build build ''" not in lines or not defines or any(not d.startswith('build build-hil ') for d in defines):
            problems.append('scripts/build-firmware.sh may define LAUNCHLAB_HIL only for its build-hil image')
    else:
        pm = (FIRMWARE / 'build-m5-pm.sh').read_text()
        if 'LAUNCHLAB_HIL' in pm or 'LAUNCHLAB_HIL=1' not in (FIRMWARE / 'build-m5-hil.sh').read_text():
            problems.append('only build-m5-hil.sh may define LAUNCHLAB_HIL')
    if problems:
        raise SystemExit('\n'.join(problems))
    print(f'PASS: {hooks} HIL hook blocks, all behind #if LAUNCHLAB_HIL; production PM build leaves it undefined')


if __name__ == '__main__':
    main()
