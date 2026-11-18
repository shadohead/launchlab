#!/usr/bin/env python3
"""Two sticks battling each other over the air.

Both run the HIL image with Bluetooth on for real (no tap): their pulls are
injected, as in every HIL scenario, and everything between the two sticks is
the radio. What only two sticks can show is checked here: that they hear each
other, pair up on pulls begun together and not otherwise, agree on when the
other's pull began across two clocks, carry a result, a correction and a
result entered on both at once across, and make up a result one of them missed
at the next battle. Each match also reports how strongly the other stick was
heard (docs/FIRMWARE_BATTLES.md).

  python3 firmware/tools/hil/battle_pair.py --port PORT_A --port PORT_B --out DIR
  python3 firmware/tools/hil/battle_pair.py --port PORT_A --port PORT_B --out DIR --battles 12

--battles N runs only N battles in a row, at pull offsets across the 2 s
window, and reports how long each took to pair.
Leaves both sticks on the HIL image with Bluetooth off and no rivals.
A stick that stops answering right after its Bluetooth restart has the known
USB restart wedge: replug its cable and run again.
"""
import argparse
import json
from pathlib import Path
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device  # noqa: E402
from rig import Rig  # noqa: E402
import ble_scenarios as ble  # noqa: E402
import scenarios  # noqa: E402
import signals  # noqa: E402
import port_guard  # noqa: E402


class Pair:
    def __init__(self, rigs):
        self.rigs = rigs
        self.a, self.b = rigs
        self.ids = [None, None]
        self.repeat = 0

    def check(self, name, ok, detail=''):
        return self.a.check(name, ok, detail)

    def note(self, text):
        self.a.note(text)

    def status(self, tag='BATTLE_STATUS'):
        return [rig.dev.status().get(tag, {}) for rig in self.rigs]

    def pulls(self, rpms, later_s, seed):
        """A pull on each stick, B's begun `later_s` after A's (negative: before).
        Returns (marks, results, the host-clock time each pull began or None)."""
        optical = [signals.standard_pull(rpm) for rpm in rpms]
        motion = [signals.launch_motion(tilt_rate_dps=300 + 10 * i, pull_g=4, seed=seed + i) for i in range(2)]
        for rig, o, m in zip(self.rigs, optical, motion):
            signals.program_optical(rig.dev, o)
            signals.program_imu(rig.dev, m, 0)
        marks = [rig.dev.mark() for rig in self.rigs]
        results = [None, None]

        def go(i, delay):
            time.sleep(delay)
            results[i] = self.rigs[i].pull(optical[i], motion[i], program=False)
        threads = [threading.Thread(target=go, args=(0, max(0.0, -later_s))), threading.Thread(target=go, args=(1, max(0.0, later_s)))]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        began = []
        for rig, mark in zip(self.rigs, marks):
            line = rig.dev.wait('BATTLE_LAUNCH', 2, since=mark)
            began.append(line.t - float(line.f['pull_began_ms_ago']) / 1000 if line else None)
        return marks, results, began

    def matched(self, marks, timeout=9):
        return [rig.dev.wait('BATTLE_MATCHED', timeout, since=mark) for rig, mark in zip(self.rigs, marks)]

    def mirror(self, what):
        """Each stick's totals are the other's, the other way round."""
        end = time.monotonic() + 4
        while True:
            a, b = self.status()
            ok = a.get('won') == b.get('lost') and a.get('lost') == b.get('won')
            if ok or time.monotonic() >= end:
                break
            time.sleep(0.4)
        self.check(f'{what}: each stick\'s record is the other\'s, mirrored', ok,
                   f"A won {a.get('won')} lost {a.get('lost')} points {a.get('points')}; B won {b.get('won')} lost {b.get('lost')} points {b.get('points')}")
        return a, b


def battle(pair, rpms, later_s, seed, label):
    """One battle begun together: both pull, both must match each other. Returns marks or None."""
    for rig in pair.rigs:
        scenarios.settle(rig)
    marks, results, began = pair.pulls(rpms, later_s, seed)
    for rig, r in zip(pair.rigs, results):
        rig.check_pull(r, tolerance=0.03)
    lines = pair.matched(marks)
    for i, rig in enumerate(pair.rigs):
        frames = ble.told(rig.dev, marks[i])
        if frames:
            pair.ids[i] = frames[0][1]['id']
    ok = all(lines) and all(pair.ids)
    pair.check(f'{label}: both sticks hear each other and pair up', ok, ' / '.join(l.text if l else 'no BATTLE_MATCHED' for l in lines))
    if not ok:
        return None
    for i, line in enumerate(lines):
        other = 1 - i
        pair.check(f'{label}: stick {"AB"[i]} names stick {"AB"[other]}, with its launch',
                   line.f.get('rival') == f'{pair.ids[other]:08x}' and abs(float(line.f.get('their_rpm', 0)) - float(results[other].launch['peak_rpm'])) <= 1 and
                   abs(float(line.f.get('my_rpm', 0)) - float(results[i].launch['peak_rpm'])) <= 1, line.text)
    if all(began):
        truth = (began[1] - began[0]) * 1000  # B's pull after A's, on the host's clock
        seen = [float(lines[0].f['pulled_ms_after']), -float(lines[1].f['pulled_ms_after'])]
        pair.check(f'{label}: each stick places the other\'s pull within 200 ms across the two clocks',
                   all(abs(s - truth) <= 200 for s in seen), f'B began {truth:.0f} ms after A; A saw {seen[0]:.0f}, B saw {seen[1]:.0f}')
        latest = max(began)
        # By when each line reached the host: a line can arrive late over USB (one
        # came 1.7 s after its match, 2026-11-15), so a long time here is checked
        # against when that stick stopped telling, 2 s or more after it paired.
        waits = [line.t - latest for line in lines]
        pair.note(f'{label}: matched {waits[0]:.1f} s (A) and {waits[1]:.1f} s (B) after the later pull began; '
                  f"heard at {lines[0].f.get('rssi_dbm')} dBm (A) and {lines[1].f.get('rssi_dbm')} dBm (B)")
        pair.check(f'{label}: matched within 6 s of the later pull', max(waits) <= 6.0, f'{max(waits):.1f} s')
    return marks


def enter(rig, letters, since, pause=0.7):
    """USB button letters on one stick (a: A, h: B, b: hold B); its BATTLE_RESULT after `since`, if any."""
    for letter in letters:
        rig.dev.write(letter + '\n')
        time.sleep(pause)
    return rig.dev.wait('BATTLE_RESULT', 3, since=since)


def last_result(rig, since):
    lines = rig.dev.since(since, 'BATTLE_RESULT')
    return lines[-1] if lines else None


def run(pair):
    a, b = pair.rigs
    # ---- Bluetooth on, for real.
    threads = [threading.Thread(target=ble.set_bluetooth, args=(rig, True)) for rig in pair.rigs]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    for rig in pair.rigs:
        state = {}
        for _ in range(20):
            state = rig.dev.status().get('BLE_STATUS', {})
            if state.get('started') == '1':
                break
            time.sleep(1)
        rig.check('Bluetooth is on and advertising', state.get('started') == '1' and state.get('adv_active') == '1', str(state))
        rig.dev.hil('battleclear', expect='HIL_OK battleclear')

    if pair.repeat:
        # Battles only, one after another, at pull offsets across the window: how long each takes to pair.
        offsets = [0.0, 0.3, -0.3, 0.8, -0.8, 1.5, -1.5, 0.1, -0.1, 0.5]
        for n in range(pair.repeat):
            battle(pair, (6000 + 37 * n, 6400 - 29 * n), offsets[n % len(offsets)], 100 + 2 * n, f'battle {n + 1}')
        return
    # ---- Not battles: a pull alone, and pulls three seconds apart.
    scenarios.settle(a)
    mark_b = b.dev.mark()
    r = a.pull(signals.standard_pull(6000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=80))
    a.check_pull(r, tolerance=0.03)
    time.sleep(9)
    st = pair.status()
    pair.check('a pull on one stick alone is no battle on either', st[0].get('rivals') == '0' and st[1].get('rivals') == '0' and
               not b.dev.since(mark_b, 'BATTLE_MATCHED'), str(st))
    for rig in pair.rigs:
        scenarios.settle(rig)
    marks, results, began = pair.pulls((6000, 6200), 3.0, 81)
    time.sleep(9)
    st = pair.status()
    apart = (began[1] - began[0]) if all(began) else None
    pair.check('pulls 3 s apart are no battle', st[0].get('rivals') == '0' and st[1].get('rivals') == '0' and apart is not None and apart > 2.3,
               f'{apart:.2f} s apart; {st}' if apart is not None else str(st))
    heard = [s.get('heard') for s in pair.status('BATTLE_RADIO_STATUS')]
    pair.check('though each stick heard the other telling', all(int(h or 0) > 0 for h in heard), f'frames heard: A {heard[0]}, B {heard[1]}')

    # ---- Battle 1: B pulls 0.3 s after A. A enters the win; B corrects the finish.
    marks = battle(pair, (6400, 5900), 0.3, 82, 'battle 1')
    if marks is None:
        return
    ble.card_up(a, marks[0])
    entered = [rig.dev.mark() for rig in pair.rigs]
    mine = enter(a, 'aa', entered[0])
    theirs = b.dev.wait('BATTLE_RESULT', 4, since=entered[1])
    pair.check('A, A on stick A: won by a Spin finish', mine is not None and mine.f.get('outcome') == 'won' and mine.f.get('finish') == 'Spin', mine.text if mine else 'no BATTLE_RESULT')
    pair.check('stick B shows the loss by itself, over the air', theirs is not None and theirs.f.get('outcome') == 'lost' and theirs.f.get('finish') == 'Spin' and
               theirs.f.get('points') == '0-1', theirs.text if theirs else 'no BATTLE_RESULT on B')
    if mine and theirs:
        pair.note(f'the result crossed in {theirs.t - mine.t:.2f} s')
    synced = a.dev.wait('BATTLE_SYNCED', 4, since=entered[0])
    pair.check('and stick A hears it told back: on both sticks', synced is not None)
    quiet = [rig.dev.wait('BATTLE_RADIO beacon=0', 5, since=entered[i]) for i, rig in enumerate(pair.rigs)]
    pair.check('both then stop telling', all(quiet))
    pair.mirror('after the result')
    # B: Change, Won, Over, Send.
    ble.card_up(b, marks[1], timeout=5)
    fix = [rig.dev.mark() for rig in pair.rigs]
    mine = enter(b, 'haha', fix[1])
    theirs = a.dev.wait('BATTLE_RESULT', 4, since=fix[0])
    pair.check('B corrects it (Change, Won, Over, Send): both follow the newer entry',
               mine is not None and mine.f.get('outcome') == 'won' and mine.f.get('finish') == 'Over' and mine.f.get('rev') == '2' and
               theirs is not None and theirs.f.get('outcome') == 'lost' and theirs.f.get('finish') == 'Over' and theirs.f.get('rev') == '2',
               f"B: {mine.text if mine else None} / A: {theirs.text if theirs else None}")
    pair.check('and stick B hears it told back', b.dev.wait('BATTLE_SYNCED', 4, since=fix[1]) is not None)
    pair.mirror('after the correction')
    for rig in pair.rigs:
        rig.dev.write('a\n')  # OK
    time.sleep(1)

    # ---- Battle 2: B pulls 0.2 s before A. Both enter a win at the same moment.
    marks = battle(pair, (6100, 6600), -0.2, 84, 'battle 2')
    if marks is None:
        return
    for rig, mark in zip(pair.rigs, marks):
        ble.card_up(rig, mark)
    both = [rig.dev.mark() for rig in pair.rigs]
    for rig in pair.rigs:
        rig.dev.write('a\n')  # Won: the picker
    time.sleep(1.0)
    threads = [threading.Thread(target=rig.dev.write, args=('a\n',)) for rig in pair.rigs]  # Send, together
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    time.sleep(3.5)
    last = [last_result(rig, both[i]) for i, rig in enumerate(pair.rigs)]
    outcomes = [l.f.get('outcome') if l else None for l in last]
    pair.note(f'both entered a win at once: A ends on {outcomes[0]}, B on {outcomes[1]}')
    pair.check('both sticks end on the same result: void on both, or the earlier press on both',
               outcomes in (['void', 'void'], ['won', 'lost'], ['lost', 'won']), ' / '.join(l.text if l else 'no BATTLE_RESULT' for l in last))
    pair.mirror('after both entered a win')
    if outcomes == ['void', 'void']:
        # B gives in: Change, Lost, Send.
        again = [rig.dev.mark() for rig in pair.rigs]
        mine = enter(b, 'hha', again[1])
        theirs = a.dev.wait('BATTLE_RESULT', 4, since=again[0])
        pair.check('B gives in (Change, Lost, Send): A has its win', mine is not None and mine.f.get('outcome') == 'lost' and theirs is not None and
                   theirs.f.get('outcome') == 'won' and theirs.f.get('rev') == '2', f"B: {mine.text if mine else None} / A: {theirs.text if theirs else None}")
        pair.mirror('after B gave in')
    for rig in pair.rigs:
        rig.dev.write('a\n')
    time.sleep(1)

    # ---- Battle 3: B stops listening (tournament mode) before A enters the result.
    marks = battle(pair, (6300, 6250), 0.1, 86, 'battle 3')
    if marks is None:
        return
    ble.card_up(b, marks[1])
    b.dev.write('b\n')  # hold B: the card away
    time.sleep(0.8)
    b.dev.write('o\n')  # hold A: tournament mode, which tells and hears nothing
    time.sleep(1.0)
    pair.check('stick B is out of the battle (tournament mode)', pair.status()[1].get('allowed') == '0', str(pair.status()[1]))
    ble.card_up(a, marks[0])
    missed = [rig.dev.mark() for rig in pair.rigs]
    mine = enter(a, 'ahhha', missed[0])
    pair.check('A enters its win by Xtreme finish with nobody listening', mine is not None and mine.f.get('outcome') == 'won' and mine.f.get('finish') == 'Xtreme',
               mine.text if mine else 'no BATTLE_RESULT')
    pair.check('and is not told back', a.dev.wait('BATTLE_SYNCED', 3, since=missed[0]) is None)
    before = pair.status()
    pair.check('so the two records differ for now', before[0].get('won') != before[1].get('lost'),
               f"A won {before[0].get('won')}, B lost {before[1].get('lost')}")
    stopped = a.dev.wait('BATTLE_RADIO beacon=0', 18, since=missed[0])
    pair.check('A gives up telling after 15 s', stopped is not None and mine is not None and 13 <= stopped.t - mine.t <= 18,
               f'{stopped.t - mine.t:.1f} s' if stopped and mine else 'still telling')
    b.dev.write('o\n')  # back out of tournament mode
    time.sleep(1.0)
    a.dev.write('a\n')
    time.sleep(0.5)

    # ---- Battle 4: the missed result arrives with it.
    marks = battle(pair, (6500, 6450), 0.0, 88, 'battle 4')
    if marks is None:
        return
    time.sleep(1.0)
    after = pair.mirror('battle 4 brought the result B missed')
    pair.check('and it is the Xtreme win: three points', int(after[0].get('points', 0)) - int(before[0].get('points', 0)) == 0 and
               int(after[1].get('lost', 0)) == int(before[1].get('lost', 0)) + 1, f'A {after[0]} / B {after[1]}')

    # ---- What the radio's windows cost the measurement.
    for name, rig in zip('AB', pair.rigs):
        block = rig.dev.status()
        perf, radio = block.get('PERF', {}), block.get('BATTLE_RADIO_STATUS', {})
        rig.note(f"stick {name}: max_read_gap_us={perf.get('max_read_gap_us')} lost_events={perf.get('lost_events')} adc_stalls={perf.get('adc_stalls')} "
                 f"frames heard={radio.get('heard')} dropped={radio.get('dropped')} radio failures={radio.get('failures')}")
        rig.check(f'stick {name}: the stack refused nothing', radio.get('failures') == '0', str(radio))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', action='append', required=True, help='give it twice: stick A, stick B')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--battles', type=int, default=0, help='only this many battles in a row, with their pairing times')
    port_guard.add_arguments(p)
    a = p.parse_args()
    for port in a.port:
        port_guard.apply(a, port)
    if len(a.port) != 2:
        p.error('two --port')
    rigs = []
    for name, port in zip('ab', a.port):
        out = a.out / name
        out.mkdir(parents=True, exist_ok=True)
        rigs.append(Rig(Device(port, log_path=out / 'serial.log'), out))
    pair = Pair(rigs)
    pair.repeat = a.battles
    failed = 1
    try:
        for rig in rigs:
            version = rig.require_hil_build()
            print(f'{rig.dev.port_name}: {version}, boot_id {rig.boot_id}')
            scenarios.preflight(rig)
            rig.begin('battle_pair')
        try:
            run(pair)
        except Exception as e:  # a scenario error is a finding
            import traceback
            pair.check('the test completed', False, f'{type(e).__name__}: {e}')
            traceback.print_exc()
    finally:
        for rig in rigs:
            try:
                rig.dev.hil('home')
                if rig.dev.status().get('BATTLE_STATUS', {}).get('allowed') == '0' and rig.dev.status().get('BLE_STATUS', {}).get('started') == '1':
                    rig.dev.write('o\n')  # left in tournament mode by a stopped run
                    time.sleep(1)
                rig.dev.hil('battleclear', expect='HIL_OK battleclear')
                if rig.dev.status().get('BLE_STATUS', {}).get('started') == '1':
                    ble.set_bluetooth(rig, False)
                scenarios.drain_history(rig)
            except Exception as e:  # noqa: BLE001 - report, and still close the other stick
                print(f'!! {rig.dev.port_name}: cleanup failed: {type(e).__name__}: {e}')
            if rig.scenario:
                rig.end()
        checks = [c for rig in rigs for c in (rig.scenario or {}).get('checks', [])]
        failed = sum(1 for c in checks if not c['ok'] and c['severity'] == 'fail')
        (a.out / 'results.json').write_text(json.dumps([rig.results for rig in rigs], indent=1))
        for rig in rigs:
            rig.dev.close()
    print(f'\n{failed} failed checks of {len(checks)}; see {a.out}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
