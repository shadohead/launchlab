#!/usr/bin/env python3
"""The HIL scenarios for the bounded power paths match the firmware (after 0.20.10).

wipe_blocked drives the blocked Wipe page, where nothing may write after an
erase attempt (docs/FIRMWARE_WIPE_ALL.md), and sleep_blockers case 6 a save no
store takes. Neither runs here (they need the rig); this checks, without a
device, what would make them pass or fail for the wrong reason:

- hil.h refuses every command on a blocked unit before any other dispatch,
  except Hil::BLOCKED_COMMANDS, and none of those writes anything but
  `!wipemarker`, whose writes are only an unresumable marker on an unblocked
  unit and the removal of one boot cannot resume;
- every command wipe_blocked sends is one a blocked unit runs, but `home`,
  which it sends to see the refusal and so must stay outside the set;
- the sketch's blocked page polls the side key and runs its power-off, takes
  HIL input only in a HIL build, and that power-off writes nothing;
- the scenarios' constants are the firmware's (WipeBlockedPower::IDLE_MS,
  ShutdownSaveGuard::LIMIT) and wipe_blocked is a registered scenario.
"""
import ast
from pathlib import Path
import re
import sys

FIRMWARE = Path(__file__).resolve().parents[1]
HIL = FIRMWARE / 'LaunchLabMini/hil.h'
SKETCH = FIRMWARE / 'LaunchLabMini/LaunchLabMini.ino'
POWER = FIRMWARE / 'LaunchLabMini/power_status.h'
SCENARIOS = FIRMWARE / 'tools/hil/scenarios.py'
# What a write looks like in hil.h and the sketch: NVS, the stores, the logs.
WRITES = re.compile(r'\bput\w*\(|\bsave\w*\(|\bclear\w*\(|\bremove\(|nvs_set|commitSetting|flushForShutdown|powerRecord|'
                    r'sleepReceipt|historyLog\.|logHistory|logBests|writeSettings|\.erase')


def body(source, start):
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor]


def handler(source, name):
    return body(source, source.index(f'if(!strcmp(cmd,"{name}"))'))


def sent(tree, names):
    """The `!` commands the named scenario functions send (d.hil, and sleep_scale's sleepscale)."""
    found = set()
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in names:
            for call in ast.walk(node):
                if isinstance(call, ast.Call) and getattr(call.func, 'attr', None) == 'hil' and call.args:
                    arg = call.args[0]
                    text = arg.value if isinstance(arg, ast.Constant) else ''.join(
                        v.value for v in getattr(arg, 'values', []) if isinstance(v, ast.Constant))
                    found.add(text.split()[0])
                if isinstance(call, ast.Call) and getattr(call.func, 'id', None) == 'sleep_scale':
                    found.add('sleepscale')
                if isinstance(call, ast.Call) and getattr(call.func, 'attr', None) == 'hil_boot':
                    found.add('hil')
    return found


def constant(source, pattern):
    return int(re.search(pattern, source).group(1))


def main():
    hil, sketch, power, scenarios = HIL.read_text(), SKETCH.read_text(), POWER.read_text(), SCENARIOS.read_text()
    problems = []
    allowed = re.search(r'BLOCKED_COMMANDS\[\]=\{([^}]*)\}', hil)
    allowed = set(re.findall(r'"(\w+)"', allowed.group(1))) if allowed else set()
    if not allowed:
        problems.append('hil.h has no Hil::BLOCKED_COMMANDS')
    command = body(hil, hil.index('static void hilCommand('))
    guard = command.find('if(wipeBlocked && !Hil::blockedCommand(cmd))')
    first = command.find('if(!strcmp(cmd,')
    if guard < 0 or (first >= 0 and guard > first):
        problems.append('hilCommand must refuse commands outside Hil::BLOCKED_COMMANDS before any dispatch')
    for name in sorted(allowed - {'wipemarker'}):
        try:
            text = handler(hil, name)
        except ValueError:
            problems.append(f'blocked command "{name}" has no handler in hil.h')
            continue
        if WRITES.search(text):
            problems.append(f'blocked command "{name}" may write: {WRITES.search(text).group(0)}')
    marker = handler(hil, 'wipemarker')
    if '!wipeBlocked && before==DeviceWipe::State::Clean' not in marker or 'before==DeviceWipe::State::Invalid && store.clearIntent()' not in marker:
        problems.append('!wipemarker may write only an unresumable marker on an unblocked unit and remove only such a marker')
    tree = ast.parse(scenarios)
    used = sent(tree, {'wipe_blocked', 'blocked_boot'})
    for name in sorted(used - allowed - {'home'}):
        problems.append(f'wipe_blocked sends "!{name}", which a blocked unit refuses')
    if 'home' in allowed or 'home' not in used:
        problems.append('wipe_blocked checks the refusal with "!home": it must be sent and stay outside BLOCKED_COMMANDS')
    if "'wipe_blocked': wipe_blocked," not in scenarios:
        problems.append('wipe_blocked is not in SCENARIOS')
    loop = body(sketch, sketch.index('void loop() {'))
    branch = body(loop, loop.index('if(wipeBlocked) {'))
    for call in ('pollPowerKey(millis());', 'wipeBlockedPowerOff(millis());'):
        if call not in branch:
            problems.append(f'the blocked page in loop() does not call {call}')
    feed = branch.find('hilFeed(')
    if feed >= 0 and branch.rfind('#if LAUNCHLAB_HIL', 0, feed) < 0:
        problems.append('the blocked page takes HIL input outside #if LAUNCHLAB_HIL')
    off = body(sketch, sketch.index('static void wipeBlockedPowerOff('))
    if WRITES.search(off):
        problems.append(f'wipeBlockedPowerOff may write: {WRITES.search(off).group(0)}')
    idle = constant(power, r'IDLE_MS=(\d+),RETRY_MS=')
    if constant(scenarios, r'WIPE_IDLE_S = (\d+)') * 1000 != idle:
        problems.append(f'scenarios.WIPE_IDLE_S is not WipeBlockedPower::IDLE_MS ({idle} ms)')
    limit = constant(power, r'static constexpr uint8_t LIMIT=(\d+);\n  // One more refusal')
    if constant(scenarios, r'SAVE_REFUSALS = (\d+)') != limit:
        problems.append(f'scenarios.SAVE_REFUSALS is not ShutdownSaveGuard::LIMIT ({limit})')
    if problems:
        print('FAIL: ' + '\n  '.join(problems))
        return 1
    print(f'PASS: a blocked unit runs {len(allowed)} HIL commands, none writing but the guarded marker; '
          f'wipe_blocked sends only those (and "!home" to see a refusal); the blocked page polls the key and its power-off writes nothing; '
          f'scenario constants match the firmware')
    return 0


if __name__ == '__main__':
    sys.exit(main())
