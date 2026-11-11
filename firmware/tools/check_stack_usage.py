"""Fail a LaunchLab Mini build whose stack frames could overflow the loop task.

Usage: python3 firmware/tools/check_stack_usage.py <build-path>

The build must be compiled with -fstack-usage (add it to
compiler.cpp.extra_flags). loop() runs on the Arduino loop task's 8 KB stack;
0.11.0 shipped with a 6.9 KB loop() frame (inlined helpers plus a 5 KB
temporary) and crash-looped on the first status print. Budgets leave room for
the deepest helper chain plus printf and the M5 display stack.
"""
from pathlib import Path
import re
import sys

LOOP_BUDGET = 1536      # bytes for loop() itself
FUNCTION_BUDGET = 2560  # bytes for any other sketch function's own frame

build = Path(sys.argv[1])
reports = sorted((build / 'sketch').glob('*.su'))
if not reports:
    raise SystemExit(f'No .su files under {build}/sketch: build with -fstack-usage')
problems, loop = [], None
for report in reports:
    for line in report.read_text().splitlines():
        location, size, kind = line.rsplit('\t', 2)
        match = re.match(r'^(.*):\d+:\d+:(.*)$', location)
        if not match:
            continue
        source, name = match.groups()
        # Library templates instantiated here (M5GFX, newlib) are budgeted by their owners.
        if '/LaunchLabMini/' not in source.replace('\\', '/'):
            continue
        size = int(size)
        if kind.strip() != 'static':
            problems.append(f'{name}: {kind.strip()} stack usage ({size} bytes) cannot be bounded')
        elif name == 'void loop()':
            loop = size
            if size > LOOP_BUDGET:
                problems.append(f'loop(): {size} bytes > {LOOP_BUDGET}')
        elif size > FUNCTION_BUDGET:
            problems.append(f'{name}: {size} bytes > {FUNCTION_BUDGET}')
if loop is None:
    problems.append('loop() not found in the stack report')
if problems:
    raise SystemExit('Stack budget exceeded:\n  ' + '\n  '.join(problems))
print(f'Stack budgets OK: loop() {loop} bytes (budget {LOOP_BUDGET}); every other sketch frame <= {FUNCTION_BUDGET} bytes.')
