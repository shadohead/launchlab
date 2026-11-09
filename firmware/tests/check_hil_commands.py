#!/usr/bin/env python3
"""Every HIL `!` command in hil.h is reachable.

The dispatcher compares commands in order, and a prefix match such as
`!strncmp(cmd,"fx",2)` takes every command that starts with it: an exact
command after it with the same prefix (`fxcut`, the loaded-effect power cut)
would never run. This checks no exact command sits behind a prefix that
takes it, and that every command a scenario sends is one hil.h handles.

It also holds the rig's letter list (hil_device.GUARDED_COMMAND, which waits
out the guard's quiet window before each one) to usb_command_guard.h's.
"""
import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[1]
HIL = FIRMWARE / 'LaunchLabMini/hil.h'
GUARD = FIRMWARE / 'LaunchLabMini/usb_command_guard.h'
TOOLS = FIRMWARE / 'tools/hil'


def main():
    source = HIL.read_text()
    exact, prefixes, problems = {}, [], []
    for number, line in enumerate(source.splitlines(), 1):
        for name in re.findall(r'!strcmp\(cmd,"(\w+)"\)', line):
            for prefix, at in prefixes:
                if name.startswith(prefix):
                    problems.append(f'hil.h:{number}: "{name}" never runs: the prefix "{prefix}" at line {at} takes it first')
            exact.setdefault(name, number)
        for prefix, length in re.findall(r'!strncmp\(cmd,"(\w+)",(\d+)\)', line):
            prefixes.append((prefix[:int(length)], number))
    sent = set()
    for path in TOOLS.glob('*.py'):
        sent |= set(re.findall(r"""\.hil\(\s*f?['"](\w+)""", path.read_text()))
    for name in sorted(sent):
        if name not in exact and not any(name.startswith(p) for p, _ in prefixes):
            problems.append(f'tools/hil sends "!{name}", which hil.h does not handle')
    firmware = re.search(r'strchr\("(\w+)",c\)', GUARD.read_text()).group(1)
    rig = re.search(r"GUARDED_COMMAND = re.compile\(rb'\^\[(\w+)\]", (TOOLS / 'hil_device.py').read_text())
    if not rig or rig.group(1) != firmware:
        problems.append(f'hil_device.GUARDED_COMMAND letters {rig.group(1) if rig else None!r} are not the guard\'s {firmware!r}')
    if problems:
        print('FAIL: ' + '; '.join(problems))
        return 1
    print(f'PASS: {len(exact)} exact HIL commands, none behind a prefix that takes it; '
          f'all {len(sent)} commands the scenarios send are handled')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
