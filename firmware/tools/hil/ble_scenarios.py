"""Bluetooth through the rig's tap (ble_link.h, `!bletap 1`).

The tap is a link without the radio: both profiles run as for a subscribed,
paired phone, and every packet they would notify is printed over USB as
`BLE_TAP pass|event|reply HEX`. The rig injects pulls and poses as ever and
reads here exactly what an app would get, against the stick's own results.
The stack's transport, advertising and pairing are not in it: those are
tools/ble_check (a Mac as the phone) and the owner's hand test.
"""
import math
import struct
import time

import signals
from hil_device import DeviceError

PASS_SET = [0xb0 + i for i in range(8)] + [0x70 + i for i in range(4)]
PHONE_HOLD_S = 600  # PhoneLinkAwake::LIMIT_MS
HOLD_SCALE = 20


NO_ID = ':'.join(['00'] * 6)  # a status that carries no unit ID


def number(text, fallback=None):
    try:
        return float(text)
    except (TypeError, ValueError):
        return fallback


def tap_lines(d, since, kind=None):
    """[(line, bytes)] of the BLE_TAP lines after `since`, of one kind or all."""
    found = []
    for line in d.since(since, 'BLE_TAP '):
        parts = line.text.split()
        if len(parts) == 3 and (kind is None or parts[1] == kind):
            try:
                found.append((line, parts[1], bytes.fromhex(parts[2])))
            except ValueError:
                pass
    return found


def wait_tap(d, since, kind, want, timeout):
    """Wait until `want(list of bytes)` is true for the tap lines of `kind` after `since`."""
    end = time.monotonic() + timeout
    while True:
        got = [data for _, _, data in tap_lines(d, since, kind)]
        if want(got) or time.monotonic() >= end:
            return got
        time.sleep(0.05)


def u16(data, at):
    return data[at] | data[at + 1] << 8


def parse_status(p):
    return {'state': p[3], 'battery': p[4] * 100 / 250, 'best': u16(p, 7), 'total': u16(p, 9), 'id': p[11:17].hex(':')}


def parse_set(packets):
    """A launch's set as an app reads it: None unless B0-B7 and 70-73 are all
    there, in order, with a matching checksum and a latest slot."""
    if [p[0] for p in packets] != PASS_SET or any(len(p) != 17 for p in packets):
        return None
    by = {p[0]: p for p in packets}
    if sum(sum(by[t][1:]) for t in range(0xb0, 0xb7)) & 0xff != by[0xb7][16]:
        return None
    n = by[0xb6][11]
    if not 1 <= n <= 50:
        return None
    shots = [u16(by[0xb0 + i // 8], 1 + (i % 8) * 2) for i in range(n)]
    curve = []
    for i in range(32):
        value = u16(by[0x70 + i // 8], 1 + (i % 8) * 2)
        if not value:
            break
        curve.append(value)
    return {'shots': shots, 'latest': shots[-1], 'n': n, 'best': u16(by[0xb6], 7), 'total': u16(by[0xb6], 9), 'curve': curve}


def pass_curve(points):
    """The 32 curve values the Battle Pass profile sends for a pull's curve points
    [(t_ms, rpm)] (ble_battle_pass.h, curve()): the pull from its start to its
    last turn in 32 equal spans, each the RPM averaged over its span by time, as
    periods in 8 us steps; cut at the first 0 as parse_set reads them."""
    if not points:
        return []
    ms, rpm, span = [t for t, _ in points], [r for _, r in points], points[-1][0]
    values = []
    for k in range(32):
        r = rpm[-1]
        if span:
            low, high, total, weight, lo = k * span, (k + 1) * span, 0, 0, 0
            for t, v in zip(ms, rpm):
                hi = t * 32
                a, b = max(lo, low), min(hi, high)
                if b > a:
                    total += v * (b - a)
                    weight += b - a
                lo = max(lo, hi)
            if weight:
                r = (total + weight // 2) // weight
        values.append(min(65535, max(1, (7500000 + r // 2) // r)) if r else 0)
    return values[:values.index(0)] if 0 in values else values


def ask(rig, op_hex, timeout=6):
    """One LaunchLab command through the tap: (payload, error flag) of its framed
    reply, or (None, None) when the frames are not a whole reply."""
    d = rig.dev
    start = d.mark()
    d.hil(f'blell {op_hex}', expect='HIL_OK blell')
    op = int(op_hex[:2], 16)
    frames = wait_tap(d, start, 'reply', lambda got: bool(got) and got[-1][2] & 1, timeout)
    if not frames or not frames[-1][2] & 1 or [f[1] for f in frames] != list(range(len(frames))) or any(f[0] != op for f in frames):
        return None, None
    return b''.join(f[3:] for f in frames), bool(frames[-1][2] & 2)


def export(rig):
    """The stick's own export of the latest pull (USB `e`): its curve points and
    the poses the recap draws."""
    d = rig.dev
    start = d.mark()
    d.write('e\n')
    end = time.monotonic() + 6
    while len(d.since(start, 'REPLAY_POINT role=latest')) < 48 and time.monotonic() < end:
        time.sleep(0.1)
    time.sleep(0.3)
    curve = [(int(l.f['t_ms']), int(l.f['rpm'])) for l in d.since(start, 'RPM_CURVE_POINT')]
    poses = []
    for l in d.since(start, 'REPLAY_POINT role=latest'):
        poses.append((int(l.f['t_ms']), [float(v) for v in l.f['gravity_aligned_q'].split(',')]))
    return curve, poses


def up_in_stick(q):
    """Where up points in the stick's axes for pose q (w, x, y, z as sent, scaled by 16384): the inverse rotation of (0, 0, 1)."""
    w, x, y, z = (v / 16384 for v in q)
    return (2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y))


def angle(a, b):
    dot = sum(i * j for i, j in zip(a, b)) / (math.sqrt(sum(i * i for i in a)) * math.sqrt(sum(i * i for i in b)))
    return math.degrees(math.acos(max(-1.0, min(1.0, dot))))


def sent_in_pull(d, since):
    """(the MEASURING line, the LAUNCH_RESULT line, the tap packets sent while the
    pull was measured). The detector ends the pull before the loop's pass that
    prints its result, so a packet in that same pass, right before the result
    line, is after the measurement and not counted."""
    launch = d.wait('LAUNCH_RESULT', 1, since=since)
    begun = next((l for l in d.since(since, 'RPM_STATE') if 'state=MEASURING' in l.text), None)
    if not begun or not launch:
        return begun, launch, []
    return begun, launch, [t for t in tap_lines(d, since) if begun.index < t[0].index < launch.index - 2]


def poses_after(rig, since, seconds):
    time.sleep(seconds)
    return [struct.unpack('<4h', data[1:9]) for _, _, data in tap_lines(rig.dev, since, 'event') if len(data) == 9 and data[0] == 2]


def ble_tap(rig):
    """Both Bluetooth profiles as an app gets them, through the tap.

    Battle Pass profile: the greeting status, the commands (0x51, 0x74, and
    0x61 doing nothing), and after a pull "top on", "top off" and the
    launch's set, with nothing sent while the pull is measured; the latest
    shoot power is the stick's RPM and the curve is the recap's graph over the
    whole pull, in 32 equal time spans. The clear (0x75, 0.20.11): A1, a status
    counting 0 and an empty list; the launch number it confirmed is saved, so
    after a restart the count is still 0 and the next pull lists alone. The
    power-off (0x63) ends a radio link, which the tap does not have: here it
    must send nothing (the Beyblade X app and tools/ble_check test the link).
    LaunchLab profile: the launch event, Get info, Get curve and Get status,
    the tilt replay equal to the poses the recap draws, an op this firmware
    does not have answering "unknown", and live orientation: about ten poses a
    second that follow an injected tilt, none during a pull, none once off.
    Sleep: the tap counts as a connected phone, which holds the sleep off for
    10 minutes after the last pull and no longer (limits divided by 20 here).
    """
    import scenarios  # late: scenarios.py registers this module's scenario
    scenarios.settle(rig)
    d = rig.dev
    d.hil('home')
    still = 'const {:.4f} {:.4f} {:.4f} 0 0 0 0.004 0.08'
    start = d.mark()
    d.hil('bletap 1', expect='HIL_OK bletap')
    try:
        # ---- Battle Pass profile: the greeting and the commands.
        greeting = wait_tap(d, start, 'pass', lambda got: len(got) >= 1, 3)
        rig.check('a new subscription is greeted with one status', len(greeting) == 1 and greeting[0][0] == 0xa0 and len(greeting[0]) == 17,
                  str([g.hex() for g in greeting]))
        first = parse_status(greeting[0]) if greeting and len(greeting[0]) == 17 else {}
        block = d.status()
        battery = number(block.get('POWER_STATUS', {}).get('battery_pct'))
        rig.check('status: no top, the battery as the stick reads it, the unit ID',
                  first.get('state') == 0 and battery is not None and abs(first.get('battery', -99) - battery) <= 2 and first.get('id', NO_ID) != NO_ID,
                  f"{first} battery_pct={battery}")
        mark = d.mark()
        d.hil('blepass 51', expect='HIL_OK blepass')
        got = wait_tap(d, mark, 'pass', lambda got: len(got) >= 1, 3)
        time.sleep(0.5)
        got = [data for _, _, data in tap_lines(d, mark, 'pass')]
        rig.check('0x51 answers with one status', len(got) == 1 and got[0][0] == 0xa0, str([g.hex() for g in got]))
        mark = d.mark()
        d.hil('blepass 74', expect='HIL_OK blepass')
        got = wait_tap(d, mark, 'pass', lambda got: len(got) >= 12, 4)
        before = parse_set(got)
        rig.check('0x74 answers with the twelve packets in order, checksum and latest slot good', before is not None, str([g.hex() for g in got]))
        mark = d.mark()
        for byte in ('61', '63', '00', 'ff'):
            d.hil(f'blepass {byte}', expect='HIL_OK blepass')
        time.sleep(1.0)
        rig.check('0x61, 0x63 (no radio link under the tap) and unknown bytes send nothing', not tap_lines(d, mark), str([t[2].hex() for t in tap_lines(d, mark)]))

        # ---- A pull: nothing while it is measured, then the launch on both profiles.
        mark = d.mark()
        r = rig.pull(signals.standard_pull(6400), signals.launch_motion(tilt_rate_dps=320, pull_g=4, seed=91))
        rig.check_pull(r, tolerance=0.03)
        begun, launch, during = sent_in_pull(d, mark)
        rig.check('nothing is sent while the pull is measured', begun is not None and launch is not None and not during,
                  f"begun={begun.text if begun else None} during={[t[2].hex() for t in during]}")
        got = wait_tap(d, mark, 'pass', lambda got: len(got) >= 14, 5)
        time.sleep(0.4)
        got = [data for _, _, data in tap_lines(d, mark, 'pass')]
        rig.check('after the pull: top on, top off, then the twelve packets, and nothing else',
                  len(got) == 14 and [g[0] for g in got[:2]] == [0xa0, 0xa0] and got[0][3] == 0x04 and got[1][3] == 0x00 and [g[0] for g in got[2:]] == PASS_SET,
                  str([g.hex() for g in got]))
        after = parse_set(got[2:]) if len(got) == 14 else None
        rpm = r.rpm if r.launch else 0
        rig.check('the latest shoot power is the stick\'s RPM', after is not None and abs(after['latest'] - rpm) <= 0.6,
                  f"{after['latest'] if after else None} vs {rpm}")
        if before and after:
            rig.check('the count rises by one and the list gains the launch',
                      after['total'] == before['total'] + 1 and after['shots'][:-1] == (before['shots'] if before['n'] < 50 else before['shots'][1:]) and after['best'] >= after['latest'],
                      f"total {before['total']} -> {after['total']}, n {before['n']} -> {after['n']}, best {after['best']}")
        status = parse_status(got[1]) if len(got) == 14 else {}
        rig.check('the status with it carries the same count and best', after is not None and status.get('total') == after['total'] and status.get('best') == after['best'], str(status))
        curve, poses = export(rig)
        want = pass_curve(curve)
        rig.check('the curve is the recap\'s graph over the whole pull: 32 time spans, each its average RPM as a period',
                  after is not None and len(curve) >= 7 and len(want) == 32 and after['curve'] == want,
                  f"{after['curve'][:6] if after else None}... vs {want[:6]}... ({len(curve)} points)")
        events = [data for _, _, data in tap_lines(d, mark, 'event')]
        launches = [e for e in events if e[0] == 1 and len(e) == 13]
        number_, session, event_rpm = struct.unpack('<IIf', launches[0][1:]) if launches else (0, 0, 0)
        rig.check('LaunchLab: one launch event with the stick\'s RPM', len(launches) == 1 and abs(event_rpm - rpm) < 0.06,
                  f'{len(launches)} events, rpm {event_rpm} vs {rpm}, number {number_}')

        # ---- LaunchLab profile: commands.
        info, error = ask(rig, '01')
        version = block.get('STATUS', {}).get('v', '')
        ok = bool(info) and not error and info[0] == 1 and struct.unpack_from('<I', info, 1)[0] == number_
        if ok:
            n = info[14]
            ok = info[15:15 + n].decode(errors='replace') == version and info[-1] == 0
        rig.check('Get info: the protocol, the latest launch, this firmware, no level reference', ok, f'{info.hex() if info else None} v={version}')
        payload, error = ask(rig, '08')
        ok = bool(payload) and not error and len(payload) >= 10
        if ok:
            launch_number, peak, count, lead = struct.unpack_from('<IHHH', payload)
            points = [struct.unpack_from('<HH', payload, 10 + 4 * i) for i in range(count)] if len(payload) == 10 + 4 * count else None
            ok = points == curve and lead == 0 and peak == max(p[1] for p in curve) and launch_number == number_
        rig.check('Get curve: every point of the recap\'s graph', ok, f'{payload[:14].hex() if payload else None} ({len(curve)} points)')
        payload, error = ask(rig, '0b')
        ok = bool(payload) and not error and len(payload) == 11 + 48 * 12
        worst = None
        if ok:
            replay_number, duration, count, flags = struct.unpack_from('<IIHB', payload)
            sent_poses = [struct.unpack_from('<i4h', payload, 11 + 12 * i) for i in range(48)]
            worst = max(max(abs(sent[0] - ms), *(abs(sent[1 + j] / 16384 - q[j]) * 16384 for j in range(4))) for sent, (ms, q) in zip(sent_poses, poses)) if len(poses) == 48 else None
            ok = replay_number == number_ and count == 48 and worst is not None and worst <= 1.0 and 0 < duration <= 2500
        rig.check('Get replay: the 48 poses the recap draws, with their times', ok,
                  f'{len(payload) if payload else None} bytes, worst difference {worst} (1/16384 steps or ms), {len(poses)} exported')
        payload, error = ask(rig, '04')
        rig.check('Get status: 13 bytes, ready or resting, the level in degrees', bool(payload) and not error and len(payload) == 13 and payload[4] in (0, 2),
                  payload.hex() if payload else None)
        payload, error = ask(rig, '0e01')
        rig.check('Set level reference is not in this firmware: unknown command', payload == b'\x04' and error is True, f'{payload} error={error}')

        # ---- Live orientation.
        flat = scenarios.settle_level(rig, (0.0, 0.0, 1.0))
        mark = d.mark()
        payload, error = ask(rig, '0c01')
        rig.check('Set live orientation on: ok', payload == b'\x00' and not error, f'{payload} error={error}')
        level = poses_after(rig, mark, 3.0)
        norms = [math.sqrt(sum(v * v for v in q)) / 16384 for q in level]
        rig.check('about ten unit poses a second', 22 <= len(level) <= 36 and all(abs(n - 1) < 0.01 for n in norms),
                  f'{len(level)} in 3 s, norms {min(norms, default=0):.3f}-{max(norms, default=0):.3f}')
        tilt = math.radians(30)
        leaning = scenarios.settle_level(rig, (0.0, math.sin(tilt), math.cos(tilt)))
        mark2 = d.mark()
        tilted = poses_after(rig, mark2, 1.5)
        turned = angle(up_in_stick(level[-1]), up_in_stick(tilted[-1])) if level and tilted else None
        rig.check('the pose follows the stick: a 30 degree tilt reads as 30 degrees, as its level does',
                  turned is not None and abs(turned - 30) <= 3 and leaning is not None and abs(leaning - 30) <= 3,
                  f'pose turned {turned} degrees, level {flat} -> {leaning}; q {level[-1] if level else None} -> {tilted[-1] if tilted else None}')
        scenarios.settle_level(rig, (0.0, 0.0, 1.0))
        mark3 = d.mark()
        r2 = rig.pull(signals.standard_pull(7100), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=92))
        rig.check_pull(r2, tolerance=0.03)
        begun, launch, during = sent_in_pull(d, mark3)
        rig.check('live orientation stops for the pull: nothing is sent while it is measured', begun is not None and launch is not None and not during,
                  f'{len(during)} packets')
        streamed = wait_tap(d, launch.index if launch else mark3, 'event', lambda got: sum(1 for e in got if e[0] == 2) >= 5, 4)
        rig.check('and goes on after it', sum(1 for e in streamed if e[0] == 2) >= 5, f'{len(streamed)} events')
        second = [data for _, _, data in tap_lines(d, mark3, 'pass')]
        second_set = parse_set(second[2:]) if len(second) == 14 else None
        rig.check('the second launch has its own whole set', second_set is not None and abs(second_set['latest'] - r2.rpm) <= 0.6,
                  str([s[:1].hex() for s in second]))
        launches2 = [data for _, _, data in tap_lines(d, mark3, 'event') if data[0] == 1 and len(data) == 13]
        number2 = struct.unpack('<I', launches2[0][1:5])[0] if launches2 else None
        payload, error = ask(rig, '0c00')
        time.sleep(0.5)
        mark4 = d.mark()
        time.sleep(1.5)
        rig.check('Set live orientation off: no more poses', payload == b'\x00' and not [t for t in tap_lines(d, mark4, 'event') if t[2][0] == 2],
                  f'{payload}')

        # ---- Clear (0x75): the app has every launch so far. A1, then a status
        # counting 0 and the list, empty; the launch number is saved.
        scenarios.drain_history(rig)
        mark = d.mark()
        d.hil('blepass 75', expect='HIL_OK blepass')
        wait_tap(d, mark, 'pass', lambda got: len(got) >= 10, 4)
        time.sleep(0.4)
        got = [data for _, _, data in tap_lines(d, mark, 'pass')]
        cleared = parse_status(got[1]) if len(got) >= 2 and len(got[1]) == 17 and got[1][0] == 0xa0 else {}
        rig.check('0x75: A1, a status counting 0, and the list with no launch',
                  len(got) == 10 and got[0] == bytes([0xa1]) + bytes(16) and cleared.get('total') == 0 and cleared.get('state') == 0
                  and [g[0] for g in got[2:]] == [0xb0 + i for i in range(8)] and got[8][11] == 0 and not any(sum(g[1:]) for g in got[2:8]),
                  str([g.hex() for g in got]))
        line = d.wait('BATTLE_PASS_CLEAR', 1, since=mark)
        waiting = second_set['total'] if second_set else None
        rig.check('the clear confirmed the latest launch and took the launches that waited',
                  line is not None and number2 is not None and waiting is not None
                  and int(line.f.get('confirmed', -1)) == number2 and int(line.f.get('listed', -1)) == min(50, waiting),
                  f"{line.text if line else None}, latest {number2}, waiting {waiting}")
        saved = d.wait(lambda l: l.text.startswith('SETTINGS_SAVE ') and int(l.f.get('saved', '0'), 16) & 0x8000, 15, since=mark)
        rig.check('the confirmed launch number is saved in the next settings checkpoint', saved is not None, saved.text if saved else 'no save')
        mark = d.mark()
        d.hil('blepass 74', expect='HIL_OK blepass')
        d.hil('blepass 51', expect='HIL_OK blepass')
        wait_tap(d, mark, 'pass', lambda got: len(got) >= 1, 3)
        time.sleep(0.6)
        got = [data for _, _, data in tap_lines(d, mark, 'pass')]
        rig.check('then 0x74 sends no set (no launch waits) and 0x51 counts 0',
                  len(got) == 1 and got[0][0] == 0xa0 and parse_status(got[0])['total'] == 0, str([g.hex() for g in got]))
        # A restart keeps it: the greeting still counts 0, and the next pull is the list's only launch.
        scenarios.reboot(rig)
        mark = d.mark()
        d.hil('bletap 1', expect='HIL_OK bletap')
        greeting = wait_tap(d, mark, 'pass', lambda got: len(got) >= 1, 3)
        rig.check('after a restart the greeting still counts 0', len(greeting) == 1 and len(greeting[0]) == 17 and parse_status(greeting[0])['total'] == 0,
                  str([g.hex() for g in greeting]))
        mark = d.mark()
        r5 = rig.pull(signals.standard_pull(6800), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=95))
        rig.check_pull(r5, tolerance=0.03)
        wait_tap(d, mark, 'pass', lambda got: len(got) >= 14, 5)
        time.sleep(0.4)
        got = [data for _, _, data in tap_lines(d, mark, 'pass')]
        fresh = parse_set(got[2:]) if len(got) == 14 else None
        rig.check('the next pull is listed alone and counted as 1',
                  fresh is not None and fresh['n'] == 1 and fresh['total'] == 1 and abs(fresh['latest'] - r5.rpm) <= 0.6 and parse_status(got[1])['total'] == 1,
                  f"{fresh} rpm {r5.rpm}")

        # ---- A connected phone holds the sleep off for 10 minutes after the last use, and no longer.
        for row, value in ((scenarios.SLEEP_ROW, 1), (scenarios.SHAKE_ROW, 1)):
            d.hil(f'set {row} {value}', expect='HIL_SET', timeout=6)
        d.hil('home')
        scenarios.drain_history(rig)
        d.hil('sleepdry 1', expect='HIL_OK sleepdry')
        d.hil('sleepok 1', expect='HIL_OK sleepok')
        try:
            r3 = rig.pull(signals.standard_pull(8000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=93))
            rig.check_pull(r3, tolerance=0.03)
            pulled = time.monotonic()
            d.hil(still.format(0.0, 0.0, 1.0))
            d.hil('imu const')
            with scenarios.sleep_scale(rig, HOLD_SCALE) as scale:
                hold, timeout = PHONE_HOLD_S / scale, 60 / scale
                lines, end = scenarios.standby_dry_run(rig, hold - (time.monotonic() - pulled), timeout=30)
            off = next((l for l in lines if l.text.startswith('POWER_OFF reason=inactivity')), None)
            after_s = (off.t if off else time.monotonic()) - pulled
            rig.note(f'connected phone: the sleep {"ran" if off else "never ran"} {after_s:.0f} s after the pull (limits / {scale}: {hold:.0f} s hold, {timeout:.0f} s Sleep after)')
            rig.check(f'a connected phone holds the sleep past the {timeout:.0f} s timeout', off is None or after_s >= 0.8 * hold, f'{after_s:.1f} s')
            rig.check(f'and no longer than {hold:.0f} s after the last pull', off is not None and end is not None and after_s <= hold + 6,
                      f'{after_s:.1f} s, {end.text if end else "no sleep"}')
        finally:
            d.hil('sleepok 0', expect='HIL_OK sleepok')
            d.hil('sleepdry 0', expect='HIL_OK sleepdry')
            d.hil('sleepscale 1', expect='HIL_OK sleepscale')
    finally:
        try:
            d.hil('bletap 0', expect='HIL_OK bletap')
        except DeviceError:
            pass
    # Off: nothing more is sent for a pull.
    scenarios.settle(rig)
    mark = d.mark()
    r4 = rig.pull(signals.standard_pull(6000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=94))
    rig.check_pull(r4, tolerance=0.03)
    time.sleep(1.0)
    rig.check('with the tap off a pull sends nothing', not tap_lines(d, mark), str(len(tap_lines(d, mark))))
    scenarios.drain_history(rig)


def nvs_entries(size):
    """At most the NVS entries a write of `size` bytes takes: one for an integer,
    a blob's 32-byte data entries with its header and index entries."""
    return 1 if size <= 8 else (size + 31) // 32 + 2


def set_bluetooth(rig, on):
    """Bluetooth On or Off as Apply does it: saved, then the restart that applies
    it. Returns the lines from the command to the new boot's settled state."""
    import scenarios
    d = rig.dev
    start = d.mark()
    before = rig.boot_id
    d.hil(f'set 14 {int(on)}', expect='HIL_SET')
    boot = d.wait('HIL_BOOT', 15, since=start)
    if boot is None:
        reopened = d.reopen()
        status = rig.hil_boot() if reopened else {}
        rig.note(f'Bluetooth {"On" if on else "Off"}: no HIL_BOOT in 15 s, port reopened={reopened} boot_id={status.get("boot_id")}')
        boot = status if status.get('boot_id') not in (None, before) else None
    rig.check(f'Bluetooth {"On" if on else "Off"} restarts the stick', boot is not None)
    rig.idle_sources()
    time.sleep(1.5)
    rig.boot_id = rig.hil_boot().get('boot_id')
    rig.scenario['start_line'] = d.mark()
    scenarios.settle(rig)
    return [l.text for l in d.since(start)]


def bluetooth_storage(rig):
    """Bluetooth takes none of the stick's storage (radio_calibration.h).

    The SDK saves the radio's calibration in NVS the first time the radio
    starts, 67 entries kept for good: on a unit with a full history, the room a
    custom theme or a milestone table needs. This turns Bluetooth on with the
    real radio, then off again, and holds the stick to the same NVS entries
    throughout, with nothing left for the next boot to remove.
    """
    import scenarios
    scenarios.settle(rig)
    d = rig.dev
    first = d.status()
    rig.check('Bluetooth is Off to begin with', first.get('BLE_STATUS', {}).get('started') == '0', str(first.get('BLE_STATUS')))
    rig.check('this build refuses the radio its NVS namespace', first.get('RADIO_CAL', {}).get('wrapped') == '1', str(first.get('RADIO_CAL')))
    used = number(first.get('NVS_STATUS', {}).get('used_entries'), -1)
    try:
        lines_on = set_bluetooth(rig, True)
        on = {}
        for _ in range(20):
            on = d.status()
            if on.get('BLE_STATUS', {}).get('started') == '1' or on.get('BLE_STATUS', {}).get('init_failed') == '1':
                break
            time.sleep(1)
        ble = on.get('BLE_STATUS', {})
        rig.check('On: the stack started and advertises', ble.get('started') == '1' and ble.get('init_failed') == '0' and ble.get('adv_active') == '1', str(ble))
        cal = on.get('RADIO_CAL', {})
        rig.check('On: the radio asked for its NVS namespace and was refused', number(cal.get('refused'), 0) >= 1, str(cal))
        used_on = number(on.get('NVS_STATUS', {}).get('used_entries'), -2)
        # The restart's shutdown flush may mirror the stick's own records first
        # (history, the log, start marks: whatever the scenarios before left
        # unmirrored), each HIL_NVS-logged. Those entries are allowed; the
        # radio's would be unlogged, and in a namespace of their own.
        own = sum(nvs_entries(int(number(l.split('bytes=')[1].split()[0], 0)))
                  for l in lines_on if 'HIL_NVS key=' in l and 'bytes=' in l)
        spaces, spaces_on = first.get('NVS_STATUS', {}).get('namespaces'), on.get('NVS_STATUS', {}).get('namespaces')
        rig.check('On: no NVS entries taken for the radio', used_on - used <= 3 + own and spaces_on == spaces,
                  f'{used} -> {used_on} entries ({own} for the stick\'s own logged writes), namespaces {spaces} -> {spaces_on}')
    finally:
        lines = set_bluetooth(rig, False)
    off = d.status()
    rig.check('Off again: the stack never started', off.get('BLE_STATUS', {}).get('started') == '0', str(off.get('BLE_STATUS')))
    rig.check('Off again: the boot found nothing of the radio\'s to remove',
              off.get('RADIO_CAL', {}).get('reclaimed') == '0' and not [l for l in lines if l.startswith('NVS_RECLAIM key=phy')],
              str(off.get('RADIO_CAL')))
    used_off = number(off.get('NVS_STATUS', {}).get('used_entries'), -3)
    # The stick's own records differ by an entry between boots (357, 358, 357 on
    # the first run); the radio's data would be 67.
    rig.check('Off again: the NVS entries it had with Bluetooth On, give or take the stick\'s own', abs(used_off - used_on) <= 3, f'{used_on} -> {used_off}')



# ---- Battles (docs/FIRMWARE_BATTLES.md): the rig's host as the rival stick.

LAUNCH_FRAME = struct.Struct('<BBIHHHIHBHBB')  # magic, kind, id, tag, age, rpm, peer, peer tag, result, earlier tag, earlier result, match length
RIG_RIVAL, RIG_TAG = 0x0badcafe, 0x7a11
WON, LOST, VOID = 1, 2, 3
SPIN, OVER, BURST, XTREME = range(4)
NAME_AFTER_S, TOGETHER_S = 3.5, 2.0  # Battle::Session::NAME_AFTER_MS, TOGETHER_MS
# How strongly the rival is heard, in dB below 0 dBm: across a stadium (two
# real sticks 28 cm apart, 2026-11-16), a stick too far away to be across one
# (Battle::Session::MIN_RSSI is -50), and a single frame faded by a hand.
NEAR_DB, FAR_DB, FADED_DB = 43, 56, 80


def claim(outcome, finish, rev):
    """A result register's byte."""
    return outcome | finish << 2 | rev << 4


def told(d, since):
    """[(line, fields)] of the launch frames the stick told after `since`."""
    frames = []
    for line in d.since(since, 'BATTLE_TELL '):
        try:
            data = bytes.fromhex(line.text.split()[1])
        except (IndexError, ValueError):
            continue
        if len(data) != LAUNCH_FRAME.size or data[0] != 0x4c or data[1] != 0xb1:
            continue
        keys = ('id', 'tag', 'age', 'rpm', 'peer', 'peer_tag', 'claim', 'prev_tag', 'prev_claim', 'match_to')
        frames.append((line, dict(zip(keys, LAUNCH_FRAME.unpack(data)[2:]))))
    return frames


class Rival:
    """The other stick: its launch began at `began` (host clock)."""

    def __init__(self, d, rpm, began, tag=RIG_TAG):
        self.d, self.rpm, self.began, self.tag = d, rpm, began, tag
        self.peer = self.peer_tag = self.claim = self.prev_tag = self.prev_claim = 0
        self.match_to = 4

    def tell(self, strength=NEAR_DB):
        age = min(0xffff, max(0, int((time.monotonic() - self.began) * 100)))
        frame = LAUNCH_FRAME.pack(0x4c, 0xb1, RIG_RIVAL, self.tag, age, self.rpm, self.peer, self.peer_tag, self.claim, self.prev_tag, self.prev_claim,
                                  self.match_to)
        self.d.hil(f'battlerx {frame.hex()} {strength}', expect='HIL_OK battlerx')

    def name(self, text):
        frame = bytes([0x4c, 0xb2]) + struct.pack('<IB', RIG_RIVAL, len(text)) + text.encode()
        self.d.hil(f'battlerx {frame.hex()}', expect='HIL_OK battlerx')

    def tell_until(self, want, timeout, every=0.15, strength=NEAR_DB, fading=False):
        """Tell the frame until want() is true; whether it became true.
        `fading`: every other frame is heard faded far below the floor."""
        end = time.monotonic() + timeout
        n = 0
        while time.monotonic() < end:
            self.tell(FADED_DB if fading and n % 2 else strength)
            n += 1
            if want():
                return True
            time.sleep(every)
        return bool(want())


def battle_status(d):
    return d.status().get('BATTLE_STATUS', {})


def card_up(rig, since, timeout=25):
    """Wait until the recap of the pull after `since` has left the main screen:
    the battle card is then on it, and A and B are the battle's."""
    d = rig.dev
    gone = d.wait(lambda l: l.text.startswith('MAIN_FEEDBACK shown=0'), timeout, since=since)
    if gone is None:
        rig.note('the recap did not close by itself: closing it with A')
        d.write('a\n')
        gone = d.wait(lambda l: l.text.startswith('MAIN_FEEDBACK shown=0'), 3, since=since)
    time.sleep(0.3)
    return gone is not None


def press(rig, letter, want, timeout=3.0):
    """One USB button letter (a: A, h: B, b: hold B), then wait for the card's view."""
    d = rig.dev
    d.write(letter + '\n')
    end = time.monotonic() + timeout
    while True:
        view = battle_status(d).get('view')
        if view == want or time.monotonic() >= end:
            return view == want
        time.sleep(0.2)


def rivals_reply(rig):
    """Get rivals (op 16) through the tap: (count, wins, losses, points, [rival dict])."""
    payload, error = ask(rig, '10')
    if payload is None or error or len(payload) < 13:
        return None
    count, wins, losses, points = struct.unpack_from('<BIII', payload, 0)
    at, rivals = 13, []
    for _ in range(count):
        if at + 5 > len(payload):
            return None
        ident, n = struct.unpack_from('<IB', payload, at)
        name = payload[at + 5:at + 5 + n].decode('ascii', 'replace')
        fields = struct.unpack_from('<10H2B4HBB', payload, at + 5 + n)
        keys = ('wins', 'losses', 'points_for', 'points_against', 'spin', 'over', 'burst', 'xtreme', 'match_wins', 'match_losses',
                'match_me', 'match_them', 'together', 'faster', 'rpm', 'rival_rpm', 'last', 'match_to')
        rivals.append(dict(zip(keys, fields), id=ident, name=name))
        at += 5 + n + 34
    return count, wins, losses, points, rivals


def battle(rig):
    """A battle against the rig's host, which plays the other stick.

    The tap stands in for the radio: what the stick tells is on its
    BATTLE_TELL lines, and the host's frames arrive through `!battlerx` as if
    heard. The stick's own pull is a real injected one. Checked: a pull is told
    and names nobody until every stick that pulled with it could have been
    heard; then it names the rival, and the battle is on once the rival names
    it back; A, B, A enters "won by Over" and tells it until the rival shows
    it back; the rival's correction is adopted and shown; the record is saved
    to the log without stopping the ADC and is there after a reboot, also as a
    phone reads it; a result the stick missed arrives with the next battle; a
    pull alone, a pull in tournament mode and a pull with Auto battles off are
    not battles; and nothing of a battle keeps the stick from sleeping.
    The radio itself (the advertisement and the scan) is not in this: that is
    scenario `battle_air` and two sticks side by side.
    """
    import scenarios
    scenarios.settle(rig)
    d = rig.dev
    d.hil('home')
    d.hil('bletap 1', expect='HIL_OK bletap')
    try:
        d.hil('battleclear', expect='HIL_OK battleclear')
        status = battle_status(d)
        rig.check('Auto battles is on, with the tap for its radio, and no rival yet',
                  status.get('on') == '1' and status.get('allowed') == '1' and status.get('rivals') == '0', str(status))

        # ---- A pull alone: told for 8 s, then quiet, and no battle.
        mark = d.mark()
        r = rig.pull(signals.standard_pull(6000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=70))
        rig.check_pull(r, tolerance=0.03)
        on = d.wait('BATTLE_RADIO beacon=1', 2, since=mark)
        off = d.wait('BATTLE_RADIO beacon=0', 11, since=mark)
        rig.check('a pull alone is told for about 8 s and then not', on is not None and off is not None and 7.0 <= off.t - on.t <= 9.5,
                  f'{off.t - on.t:.1f} s' if on and off else 'no beacon lines')
        frames = told(d, mark)
        rig.check('its frame names nobody', len(frames) == 1 and frames[0][1]['peer'] == 0 and frames[0][1]['claim'] == 0, str([f for _, f in frames]))
        status = battle_status(d)
        rig.check('and no battle came of it', status.get('active') == '0' and status.get('rivals') == '0' and status.get('view') == '0', str(status))

        # ---- A battle: the rival pulls 0.3 s before the stick.
        scenarios.settle(rig)
        mark = d.mark()
        r = rig.pull(signals.standard_pull(6400), signals.launch_motion(tilt_rate_dps=320, pull_g=4, seed=71))
        rig.check_pull(r, tolerance=0.03)
        launched = d.wait('BATTLE_LAUNCH', 2, since=mark)
        rig.check('the pull is told', launched is not None and d.wait('BATTLE_RADIO scan=detect', 2, since=mark) is not None)
        if launched is None:
            return
        began = launched.t - float(launched.f['pull_began_ms_ago']) / 1000  # the stick's pull, on the host's clock
        mine = told(d, mark)
        rig.check('with its own RPM, a tag and its age', bool(mine) and mine[0][1]['tag'] != 0 and mine[0][1]['id'] != 0 and
                  abs(mine[0][1]['rpm'] - float(r.launch['peak_rpm'])) <= 1 and abs(mine[0][1]['age'] * 10 - float(launched.f['pull_began_ms_ago'])) <= 150,
                  f"{mine[0][1] if mine else None} launch={r.launch.get('peak_rpm')} began_ms_ago={launched.f.get('pull_began_ms_ago')}")
        if not mine:
            return
        stick, tag = mine[0][1]['id'], mine[0][1]['tag']
        rival = Rival(d, 5980, began - 0.3)

        def named():
            return any(f['peer'] == RIG_RIVAL and f['peer_tag'] == RIG_TAG for _, f in told(d, mark))
        heard = rival.tell_until(named, 6)
        naming = next((line for line, f in told(d, mark) if f['peer'] == RIG_RIVAL), None)
        rig.check('the stick names the rival', heard and naming is not None)
        if naming is not None:
            after = naming.t - began
            rig.check(f'only once every stick that pulled with it could have been heard ({NAME_AFTER_S} s after its pull began)',
                      NAME_AFTER_S - 0.25 <= after <= NAME_AFTER_S + 1.0, f'{after:.2f} s')
        rig.check('and that alone is no battle yet', battle_status(d).get('in_battle') == '0')
        rival.peer, rival.peer_tag = stick, tag
        rival.tell()
        matched = d.wait('BATTLE_MATCHED', 2, since=mark)
        rig.check('named back, the battle is on: both launches side by side',
                  matched is not None and matched.f.get('rival') == f'{RIG_RIVAL:08x}' and matched.f.get('their_rpm') == '5980' and
                  abs(float(matched.f.get('my_rpm', 0)) - float(r.launch['peak_rpm'])) <= 1 and matched.f.get('launches_together') == '1',
                  matched.text if matched else 'no BATTLE_MATCHED')
        rival.name('Rig Rival')
        quiet = d.wait('BATTLE_RADIO scan=listen', 6, since=mark)
        rig.check('the stick then only listens, a tenth of the time', quiet is not None and d.wait('BATTLE_RADIO beacon=0', 6, since=mark) is not None)
        # The recap has the screen for a few seconds; then the card is the main screen's.
        rig.check('the battle card is up once the recap has gone', card_up(rig, mark) and battle_status(d).get('view') == '1')
        rig.screenshot('battle_versus')

        # ---- A, B, A: won by an Over finish.
        entered = d.mark()
        rig.check('A opens the picker', press(rig, 'a', '2'))
        rig.screenshot('battle_picker')
        d.write('h\n')
        time.sleep(0.6)
        d.write('a\n')
        result = d.wait('BATTLE_RESULT', 4, since=entered)
        rig.check('A, B, A: won by an Over finish, two points',
                  result is not None and result.f.get('outcome') == 'won' and result.f.get('finish') == 'Over' and result.f.get('rev') == '1' and
                  result.f.get('won') == '1' and result.f.get('points') == '2-0' and result.f.get('match') == '2-0',
                  result.text if result else 'no BATTLE_RESULT')
        # The frame goes out with the loop's next pass, a moment after the result line.
        end = time.monotonic() + 2
        while not any(f['claim'] == claim(WON, OVER, 1) for _, f in told(d, entered)) and time.monotonic() < end:
            time.sleep(0.05)
        telling = [f for _, f in told(d, entered)]
        rig.check('and tells it', bool(telling) and telling[-1]['claim'] == claim(WON, OVER, 1) and telling[-1]['peer'] == RIG_RIVAL, str(telling[-1:]))
        rig.screenshot('battle_result_win')
        rig.check('still telling while the rival has not shown it back', d.wait('BATTLE_SYNCED', 1.0, since=entered) is None)
        rival.claim = claim(LOST, OVER, 1)
        rival.tell()
        synced = d.wait('BATTLE_SYNCED', 2, since=entered)
        rig.check('the rival shows it back: both agree', synced is not None)
        stopped = d.wait('BATTLE_RADIO beacon=0', 4, since=entered)
        rig.check('and the stick stops telling within 2 s', synced is not None and stopped is not None and stopped.t - synced.t <= 2.5)

        # ---- The rival corrects it: a Spin finish, and its own win.
        fix = d.mark()
        rival.claim = claim(WON, SPIN, 2)
        rival.tell()
        result = d.wait('BATTLE_RESULT', 2, since=fix)
        rig.check('the rival\'s correction is adopted: lost by a Spin finish',
                  result is not None and result.f.get('outcome') == 'lost' and result.f.get('rev') == '2' and result.f.get('won') == '0' and
                  result.f.get('lost') == '1' and result.f.get('points') == '0-1' and result.f.get('match') == '0-1',
                  result.text if result else 'no BATTLE_RESULT')
        echo = [f for _, f in told(d, fix)]
        rig.check('and shown back to it', bool(echo) and echo[-1]['claim'] == claim(LOST, SPIN, 2), str(echo[-1:]))
        rig.check('the card shows it', battle_status(d).get('view') == '3')
        rig.screenshot('battle_result_loss')

        # ---- Saved to the log, with the ADC sampling throughout, and kept across a reboot.
        saved = d.wait('BATTLE_SAVE', 20, since=mark)
        late = d.wait('BATTLE_SAVE', 12, since=fix) if saved is None or saved.index < fix else saved
        rig.check('the record is saved to the log', late is not None and late.f.get('ok') == '1' and late.f.get('rivals') == '1',
                  late.text if late else 'no BATTLE_SAVE')
        if late is not None:
            rig.check('in a few milliseconds', float(late.f.get('duration_us', 1e9)) <= 40000, late.f.get('duration_us'))
        seen = rivals_reply(rig)
        rig.check('a phone reads the same record (Get rivals), under the rival\'s name',
                  seen is not None and seen[0] == 1 and seen[1:4] == (0, 1, 0) and seen[4][0]['id'] == RIG_RIVAL and seen[4][0]['name'] == 'Rig Rival' and
                  seen[4][0]['losses'] == 1 and seen[4][0]['points_against'] == 1 and seen[4][0]['together'] == 1 and seen[4][0]['faster'] == 1 and
                  seen[4][0]['rival_rpm'] == 5980 and seen[4][0]['last'] == (LOST | SPIN << 2), str(seen))
    finally:
        try:
            d.hil('bletap 0', expect='HIL_OK bletap')
        except DeviceError:
            pass
    boot = d.mark()
    scenarios.reboot(rig)
    loaded = d.wait('BATTLE_LOAD', 3, since=boot)
    if loaded is None:
        # Printed before the port was back: the status has the same.
        status = battle_status(d)
        rig.check('after a reboot the record is there', status.get('rivals') == '1' and status.get('lost') == '1' and status.get('unsaved') == '0', str(status))
    else:
        rig.check('after a reboot the record is there', loaded.f.get('found') == '1' and loaded.f.get('rivals') == '1', loaded.text)
    scenarios.settle(rig)
    d.hil('bletap 1', expect='HIL_OK bletap')
    try:
        # ---- A result the stick missed arrives with the next battle.
        # The rival says: that last battle was void in the end (revision 3).
        mark = d.mark()
        r = rig.pull(signals.standard_pull(6100), signals.launch_motion(tilt_rate_dps=310, pull_g=4, seed=72))
        rig.check_pull(r, tolerance=0.03)
        launched = d.wait('BATTLE_LAUNCH', 2, since=mark)
        mine = told(d, mark)
        rig.check('the next pull is told, with the battle before it', launched is not None and bool(mine) and mine[0][1]['prev_tag'] != 0 and
                  mine[0][1]['prev_claim'] == claim(LOST, SPIN, 2), str(mine[:1]))
        if launched is None or not mine:
            return
        began = launched.t - float(launched.f['pull_began_ms_ago']) / 1000
        again = Rival(d, 6300, began + 0.2, tag=RIG_TAG + 1)
        again.prev_tag, again.prev_claim = RIG_TAG, claim(VOID, SPIN, 3)
        # The rival's player has chosen first to 7 since. The first battle turns
        # out void, so this one starts a match: the longer of the two lengths.
        again.match_to = 7
        stick, tag = mine[0][1]['id'], mine[0][1]['tag']

        def named_again():
            return any(f['peer'] == RIG_RIVAL and f['peer_tag'] == RIG_TAG + 1 for _, f in told(d, mark))
        # Heard too faintly to be across the same stadium: past the moment it
        # would have been named, it is not.
        far = again.tell_until(named_again, max(0.5, began + NAME_AFTER_S + 0.7 - time.monotonic()), strength=FAR_DB)
        rig.check(f'a stick heard at -{FAR_DB} dBm, too far to be across the stadium, is not named', not far)
        # Then near, with every other frame faded: the faded ones undo nothing.
        named = again.tell_until(named_again, 2.5, fading=True)
        rig.check(f'heard near (-{NEAR_DB} dBm), it is named, though every other frame fades to -{FADED_DB} dBm', named)
        again.peer, again.peer_tag = stick, tag
        again.tell()
        matched = d.wait('BATTLE_MATCHED', 2, since=mark)
        rig.check('the second battle is on', named and matched is not None and matched.f.get('launches_together') == '2',
                  matched.text if matched else 'no BATTLE_MATCHED')
        rig.check('a new match, first to 7 as the rival asked, while this stick still tells its own first to 4',
                  matched is not None and matched.f.get('match_to') == '7' and matched.f.get('match') == '0-0' and
                  all(f['match_to'] == 4 for _, f in told(d, mark)), matched.text if matched else '')
        seen = rivals_reply(rig)
        rig.check('and the missed result of the first is in the record: void, nothing counted',
                  seen is not None and seen[1:4] == (0, 0, 0) and seen[4][0]['losses'] == 0 and seen[4][0]['points_against'] == 0 and
                  seen[4][0]['match_them'] == 0 and seen[4][0]['together'] == 2 and seen[4][0]['faster'] == 1 and seen[4][0]['last'] == 0 and
                  seen[4][0]['match_to'] == 7, str(seen))

        # ---- Hold B puts the card away; the rival's result still arrives.
        card_up(rig, mark)
        rig.check('hold B puts the card away', press(rig, 'b', '0') and battle_status(d).get('in_battle') == '1')
        mark2 = d.mark()
        again.claim = claim(LOST, XTREME, 1)
        again.tell()
        result = d.wait('BATTLE_RESULT', 2, since=mark2)
        rig.check('the rival enters its own loss by Xtreme finish: three points here, and the card is back',
                  result is not None and result.f.get('outcome') == 'won' and result.f.get('finish') == 'Xtreme' and result.f.get('points') == '3-0' and
                  result.f.get('match') == '3-0' and battle_status(d).get('view') == '3', result.text if result else 'no BATTLE_RESULT')
        rig.check('A puts the result away', press(rig, 'a', '0'))

        # ---- Not battles: tournament mode and Auto battles off.
        scenarios.settle(rig)
        d.write('o\n')  # hold A: tournament mode
        time.sleep(0.5)
        mark = d.mark()
        r = rig.pull(signals.standard_pull(6200), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=73), wait_motion=False)
        time.sleep(1.5)
        said = [l.text for l in d.since(mark, 'BATTLE_') if l.text.startswith(('BATTLE_LAUNCH', 'BATTLE_RADIO ', 'BATTLE_TELL', 'BATTLE_MATCHED'))]
        rig.check('a pull in tournament mode is not told', r.launch is not None and not said and battle_status(d).get('active') == '0', str(said))
        d.write('o\n')
        time.sleep(0.5)
        d.hil('home')
    finally:
        try:
            d.hil('battleclear', expect='HIL_OK battleclear')
            d.hil('bletap 0', expect='HIL_OK bletap')
        except DeviceError:
            pass
    # With the tap off and Bluetooth off there is no radio: a pull tells nothing.
    scenarios.settle(rig)
    mark = d.mark()
    r = rig.pull(signals.standard_pull(6000), signals.launch_motion(tilt_rate_dps=300, pull_g=4, seed=74))
    rig.check_pull(r, tolerance=0.03)
    time.sleep(1.0)
    said = [l.text for l in d.since(mark, 'BATTLE_') if l.text.startswith(('BATTLE_LAUNCH', 'BATTLE_RADIO ', 'BATTLE_TELL', 'BATTLE_MATCHED'))]
    rig.check('without a radio a pull tells nothing', not said, str(said))
    scenarios.drain_history(rig)


def battle_air(rig):
    """The radio's side of a battle, on the air, with one stick.

    Bluetooth is turned on for real (no tap). A pull must then be told as an
    advertisement and listened for with a scan, the stack refusing neither;
    meanwhile the stick's own advertisement, the one phones and Battle Pass
    apps find it by, takes every other turn with the frame; both end after 8 s,
    and the stick's own advertisement stays. A second pull inside the first one's window is measured
    as well as any other: nothing listens while it is measured. Nothing hears
    the frames here: that takes a second stick, or a phone with a scanner app,
    and is a hand test (docs/FIRMWARE_BATTLES.md).
    """
    import scenarios
    scenarios.settle(rig)
    d = rig.dev
    try:
        set_bluetooth(rig, True)
        ble = {}
        for _ in range(20):
            ble = d.status().get('BLE_STATUS', {})
            if ble.get('started') == '1' or ble.get('init_failed') == '1':
                break
            time.sleep(1)
        rig.check('Bluetooth is on and the stick advertises as itself', ble.get('started') == '1' and ble.get('adv_active') == '1', str(ble))
        d.hil('battleclear', expect='HIL_OK battleclear')
        before = d.status()
        failures = before.get('BATTLE_RADIO_STATUS', {}).get('failures')
        rig.check('battles have the radio', before.get('BATTLE_STATUS', {}).get('allowed') == '1' and
                  before.get('BATTLE_RADIO_STATUS', {}).get('on_air') == '1', str(before.get('BATTLE_RADIO_STATUS')))
        mark = d.mark()
        r = rig.pull(signals.standard_pull(6300), signals.launch_motion(tilt_rate_dps=310, pull_g=4, seed=75))
        rig.check_pull(r, tolerance=0.03)
        on = d.wait('BATTLE_RADIO beacon=1', 3, since=mark)
        listening = d.wait('BATTLE_RADIO scan=detect', 3, since=mark)
        rig.check('the pull is told and listened for', on is not None and listening is not None and bool(told(d, mark)))
        time.sleep(1.5)
        during = d.status()
        radio = during.get('BATTLE_RADIO_STATUS', {})
        rig.check('the stack took the frame and the scan', radio.get('beacon') == '1' and radio.get('scan') == '1' and radio.get('failures') == failures and
                  during.get('BLE_STATUS', {}).get('adv_active') == '1', f"{radio} {during.get('BLE_STATUS')}")
        # A turn each 200 ms: in about 2 s of telling, the stick's own advertisement has had several.
        own_before = int(before.get('BATTLE_RADIO_STATUS', {}).get('own_turns', 0))
        rig.check("and the stick's own advertisement takes turns with the frame, so a phone can still find it",
                  int(radio.get('own_turns', 0)) >= own_before + 3, str(radio))
        # A second pull inside the window: its own window starts, and it is measured as well as the first.
        second = d.mark()
        r2 = rig.pull(signals.standard_pull(7100), signals.launch_motion(tilt_rate_dps=330, pull_g=4, seed=76))
        rig.check_pull(r2, tolerance=0.03)
        relaunch = d.wait('BATTLE_LAUNCH', 2, since=second)
        time.sleep(0.5)
        first, frames = told(d, mark), told(d, second)
        rig.check('a second pull inside the window is told in its place',
                  r2.launch is not None and relaunch is not None and bool(first) and bool(frames) and frames[-1][1]['tag'] != first[0][1]['tag'] and
                  abs(frames[-1][1]['rpm'] - float(r2.launch['peak_rpm'])) <= 1, str([f for _, f in frames][-1:]))
        off = d.wait('BATTLE_RADIO beacon=0', 12, since=second)
        quiet = d.wait('BATTLE_RADIO scan=off', 12, since=second)
        rig.check('8 s after it the stick stops telling and listening',
                  off is not None and quiet is not None and relaunch is not None and 7.0 <= off.t - relaunch.t <= 10.0,
                  f'{off.t - relaunch.t:.1f} s' if off and relaunch else 'no beacon=0')
        ble = {}
        end = time.monotonic() + 8
        while time.monotonic() < end:
            ble = d.status().get('BLE_STATUS', {})
            if ble.get('adv_active') == '1':
                break
            time.sleep(0.5)
        after = d.status()
        radio = after.get('BATTLE_RADIO_STATUS', {})
        rig.check('and advertises as itself again, for phones', ble.get('adv_active') == '1', str(ble))
        rig.check('with nothing refused on the way', radio.get('failures') == failures and radio.get('beacon') == '0' and radio.get('scan') == '0', str(radio))
        perf = after.get('PERF', {})
        rig.note(f"with the radio's windows: max_read_gap_us={perf.get('max_read_gap_us')} lost_events={perf.get('lost_events')} adc_stalls={perf.get('adc_stalls')} "
                 f"frames heard from other sticks={radio.get('heard')} dropped={radio.get('dropped')}")
        rig.check('no battle came of launches alone', after.get('BATTLE_STATUS', {}).get('rivals') == '0', str(after.get('BATTLE_STATUS')))
    finally:
        set_bluetooth(rig, False)
    scenarios.drain_history(rig)
