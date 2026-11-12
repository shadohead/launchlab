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
    0x75 and 0x61 doing nothing), and after a pull "top on", "top off" and the
    launch's set, with nothing sent while the pull is measured; the latest
    shoot power is the stick's RPM and the curve is the recap's graph.
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
        for byte in ('75', '61', '00', 'ff'):
            d.hil(f'blepass {byte}', expect='HIL_OK blepass')
        time.sleep(1.0)
        rig.check('0x75, 0x61 and unknown bytes do nothing', not tap_lines(d, mark), str([t[2].hex() for t in tap_lines(d, mark)]))

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
        want = [min(65535, max(1, (7500000 + rpm_ // 2) // rpm_)) for _, rpm_ in curve[:32]]
        rig.check('the curve is the recap\'s graph, turn by turn as periods', after is not None and len(curve) >= 7 and after['curve'] == want,
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
        rig.check('the second launch has its own whole set', len(second) == 14 and abs((parse_set(second[2:]) or {}).get('latest', 0) - r2.rpm) <= 0.6,
                  str([s[:1].hex() for s in second]))
        payload, error = ask(rig, '0c00')
        time.sleep(0.5)
        mark4 = d.mark()
        time.sleep(1.5)
        rig.check('Set live orientation off: no more poses', payload == b'\x00' and not [t for t in tap_lines(d, mark4, 'event') if t[2][0] == 2],
                  f'{payload}')

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
        set_bluetooth(rig, True)
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
        rig.check('On: no NVS entries taken for the radio', abs(used_on - used) <= 3, f'{used} -> {used_on}')
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

