#!/usr/bin/env python3
"""Battery lab host tool for the LAUNCHLAB_POWER_LAB test firmware.

The StickS3 measures battery voltage but not current, so the lab measures
runtime by running the battery down, logging a record every 10 s to flash.
A run can rotate through several power profiles so one discharge compares
them at the same state of charge. See docs/battery/POWER_LAB.md.

  power_lab.py --serial 7C:E8:B1:E1:C1:F8 status
  power_lab.py --serial ... start --segment 600 "b30 a1" "b10 a1" "b0 a1"
  power_lab.py --serial ... start --charge-off "b30 a1"   # USB meter mode: charging off, USB carries the whole load
  power_lab.py --serial ... live                         # LAB_LIVE lines while attached
  power_lab.py --serial ... dump --out run.jsonl         # after the run, back on USB
  power_lab.py --serial ... stop
  power_lab.py analyze run.jsonl [--reference 0] [--settle 120]

Profiles are the firmware's knob text (power_lab.h), each 0 = shipped:
  b<pct>  fixed backlight, never dimmed (b0 panel asleep, b- shipped level and dim)
  a0/1/2  optical ADC shipped (rests when still) / always sampling / always paused
  c0/1/2  CPU shipped (80, 40 MHz when idle) / fixed 80 MHz / fixed 40 MHz, ADC re-clocked, no sounds
  f<n>    at least n*10 ms between ordinary redraws
  i<ms>   IMU FIFO read every 20-40 ms (shipped 20)
  d<s>    dim after s seconds without a button, pull or command, moving or not
  l<raw>  backlight value 1-26 while dimmed (shipped 26, the 10% level)
  r<s>    seconds still before the ADC rests (shipped 30)
  p1      push every frame whole (shipped: only the rows that changed)

Only pyserial is needed. Ports are chosen by USB serial number, never by name.
"""
import argparse
import json
import sys
import time

FLAGS = {'charging': 1, 'charge_known': 2, 'vin': 4, 'usb_host': 8, 'sampling': 16, 'display_on': 32, 'booted': 64, 'end': 128}


def find_port(serial_number):
    from serial.tools import list_ports
    ports = [p.device for p in list_ports.comports() if (p.serial_number or '').upper() == serial_number.upper()]
    if len(ports) != 1:
        sys.exit(f'no single port with USB serial {serial_number}: {ports}')
    return ports[0]


def open_port(args):
    import serial
    port = args.port or find_port(args.serial)
    return serial.Serial(port, 115200, timeout=0.2)


def read_until(s, done, seconds):
    lines, deadline = [], time.time() + seconds
    while time.time() < deadline:
        raw = s.readline()
        if not raw:
            continue
        text = raw.decode('utf-8', 'replace').strip()
        if text.startswith('LAB_'):
            lines.append(text)
            if done(text):
                break
    return lines


def command(s, text, done, seconds=5):
    s.reset_input_buffer()
    s.write(('~' + text + '\n').encode())
    s.flush()
    return read_until(s, done, seconds)


def fields(line):
    out = {'tag': line.split()[0]}
    for part in line.split()[1:]:
        if '=' in part:
            k, v = part.split('=', 1)
            v = v.strip('"')
            try:
                out[k] = int(v)
            except ValueError:
                out[k] = v
    return out


def parse_profile_defs(lines):
    profiles = {}
    for line in lines:
        if line.startswith('LAB_PROFILE_DEF '):
            index = int(line.split('index=')[1].split()[0])
            profiles[index] = line.split('knobs="')[1].rstrip('"')
    return profiles


def cmd_status(args):
    with open_port(args) as s:
        for line in command(s, 'lab status', lambda t: False, 2):
            print(line)


def cmd_start(args):
    if not args.profiles:
        sys.exit('give at least one profile, for example "b30 a1"')
    run_id = int(time.time())
    text = f'lab start {run_id} {args.segment} ' + ('charge0 ' if args.charge_off else '') + ';'.join(args.profiles)
    with open_port(args) as s:
        lines = command(s, text, lambda t: t.startswith(('LAB_STARTED', 'LAB_ERR')), 10)
    for line in lines:
        print(line)
    if not any(l.startswith('LAB_STARTED') for l in lines):
        sys.exit('start failed')
    print(f'run {run_id} started at {time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(run_id))}')


def cmd_stop(args):
    with open_port(args) as s:
        for line in command(s, 'lab stop', lambda t: t.startswith(('LAB_STOPPED', 'LAB_ERR')), 5):
            print(line)


def cmd_charge(args):
    with open_port(args) as s:
        for line in command(s, f'lab charge {args.on}', lambda t: t.startswith(('LAB_CHARGE', 'LAB_ERR')), 5):
            print(line)


def cmd_live(args):
    with open_port(args) as s:
        end = time.time() + args.seconds if args.seconds else None
        out = open(args.out, 'a') if args.out else None
        while end is None or time.time() < end:
            raw = s.readline()
            if not raw:
                continue
            text = raw.decode('utf-8', 'replace').strip()
            if text.startswith(('LAB_LIVE', 'LAB_PROFILE', 'LAB_DISPLAY', 'LAB_NOTICE', 'LAB_BOOT')):
                stamped = f'{time.time():.1f} {text}'
                print(stamped, flush=True)
                if out:
                    out.write(stamped + '\n')
                    out.flush()


def cmd_dump(args):
    with open_port(args) as s:
        lines = command(s, 'lab dump', lambda t: t.startswith('LAB_DUMP_END'), 120)
    status = next((fields(l) for l in lines if l.startswith('LAB_STATUS')), None)
    records = [fields(l) for l in lines if l.startswith('LAB_RECORD')]
    end = next((fields(l) for l in lines if l.startswith('LAB_DUMP_END')), None)
    if not status or not end or end.get('records') != len(records):
        sys.exit(f'incomplete dump: status={bool(status)} end={end} records={len(records)}')
    header = {'tag': 'header', 'dumped_at': int(time.time()), **status, 'profiles': parse_profile_defs(lines)}
    with open(args.out, 'w') as f:
        f.write(json.dumps(header) + '\n')
        for r in records:
            f.write(json.dumps(r) + '\n')
    print(f'{len(records)} records of run {status.get("run_id")} written to {args.out}')


def load(path):
    header, records = None, []
    with open(path) as f:
        for line in f:
            row = json.loads(line)
            if row.get('tag') == 'header':
                header = row
            else:
                records.append(row)
    return header, records


def fit(points):
    """Least-squares slope (mV per hour) and intercept of (seconds, mV)."""
    n = len(points)
    if n < 3:
        return None
    mt = sum(t for t, _ in points) / n
    mv = sum(v for _, v in points) / n
    sxx = sum((t - mt) ** 2 for t, _ in points)
    if sxx <= 0:
        return None
    sxy = sum((t - mt) * (v - mv) for t, v in points)
    slope = sxy / sxx
    resid = sum((v - (mv + slope * (t - mt))) ** 2 for t, v in points)
    se = (resid / max(1, n - 2) / sxx) ** 0.5
    return {'slope_mv_h': slope * 3600, 'se_mv_h': se * 3600, 'mid_mv': mv, 'mid_s': mt, 'n': n}


def battery_only(records):
    """Records from the first battery-only one to the last before power
    returned (USB plugged back in, a charge, or a boot after the battery died)."""
    out, started = [], False
    for r in records:
        f = r['flags']
        on_battery = not (f & FLAGS['vin']) and not (f & FLAGS['charging']) and r['mv'] > 0
        if on_battery:
            started = True
            out.append(r)
        elif started:
            break
    return out


def cmd_analyze(args):
    header, records = load(args.file)
    profiles = {int(k): v for k, v in (header or {}).get('profiles', {}).items()}
    charge_off = bool((header or {}).get('flags', 0) & 1)
    run = records if charge_off else battery_only(records)
    if args.include_usb:
        run = [r for r in records if r['mv'] > 0]
    if len(run) < 3:
        sys.exit('not enough battery-only records')
    t0, t1 = run[0]['t_s'], run[-1]['t_s']
    print(f'run {header.get("run_id") if header else "?"}: {len(run)} battery records, '
          f'{run[0]["mv"]} -> {run[-1]["mv"]} mV over {(t1 - t0) / 3600:.2f} h'
          + (' (charging disabled, USB attached)' if charge_off else ''))
    died = run[-1] is not records[-1] or not (records[-1]['flags'] & FLAGS['end'])
    if died and run[-1]['mv'] < 3500:
        print(f'  the run ended when the battery gave out at about {run[-1]["mv"]} mV: runtime {(t1 - t0) / 3600:.2f} h from {run[0]["mv"]} mV')

    # Segments: contiguous records of one profile, minus a settling time after each switch.
    segments, current = [], []
    for r in run:
        if current and (r['profile'] != current[-1]['profile'] or r['t_s'] - current[-1]['t_s'] > 60):
            segments.append(current)
            current = []
        current.append(r)
    if current:
        segments.append(current)
    fitted = []
    for seg in segments:
        start = seg[0]['t_s'] + args.settle
        pts = [(r['t_s'], r['mv']) for r in seg if r['t_s'] >= start]
        f = fit(pts)
        if f:
            f['profile'] = seg[0]['profile']
            f['cpu_low_pct'] = sum(r['cpu_low_pct'] for r in seg if r['cpu_low_pct'] != 255) / max(1, sum(1 for r in seg if r['cpu_low_pct'] != 255))
            f['frames_per_s'] = sum(r['frames'] for r in seg) / max(1, seg[-1]['t_s'] - seg[0]['t_s'] + 10)
            f['rows_per_frame'] = sum(r.get('rows', 0) for r in seg) / max(1, sum(r['frames'] for r in seg))
            f['temp_c'] = sum(r['temp_c'] for r in seg) / len(seg)
            fitted.append(f)
    print(f'\n{len(fitted)} segments (first {args.settle} s of each dropped):')
    print(f'  {"profile":<36} {"mid mV":>7} {"mV/h":>8} {"+-":>5} {"cpu<80":>7} {"fps":>5} {"rows":>5} {"degC":>5}')
    for f in fitted:
        name = f'{f["profile"]} {profiles.get(f["profile"], "")}'
        print(f'  {name:<36} {f["mid_mv"]:7.0f} {f["slope_mv_h"]:8.1f} {f["se_mv_h"]:5.1f} {f["cpu_low_pct"]:6.0f}% {f["frames_per_s"]:5.1f} {f["rows_per_frame"]:5.0f} {f["temp_c"]:5.1f}')

    # Relative drain: each segment against the reference profile's slope at the
    # same voltage (interpolated between the reference segments around it).
    ref = sorted((f for f in fitted if f['profile'] == args.reference), key=lambda f: f['mid_s'])
    if len(set(f['profile'] for f in fitted)) > 1 and len(ref) >= 2:
        ratios = {}
        for f in fitted:
            if f['profile'] == args.reference:
                continue
            before = [r for r in ref if r['mid_s'] < f['mid_s']]
            after = [r for r in ref if r['mid_s'] > f['mid_s']]
            if not before or not after:
                continue
            a, b = before[-1], after[0]
            w = (f['mid_s'] - a['mid_s']) / (b['mid_s'] - a['mid_s'])
            ref_slope = a['slope_mv_h'] + w * (b['slope_mv_h'] - a['slope_mv_h'])
            if ref_slope < 0:
                ratios.setdefault(f['profile'], []).append(f['slope_mv_h'] / ref_slope)
        print(f'\nDrain relative to profile {args.reference} ({profiles.get(args.reference, "")}) at the same state of charge:')
        share = {}
        for f in fitted:
            share[f['profile']] = share.get(f['profile'], 0) + f['n']
        total = sum(share.values())
        rel = {args.reference: 1.0}
        for p, rs in sorted(ratios.items()):
            rs.sort()
            rel[p] = rs[len(rs) // 2]
            print(f'  profile {p} {profiles.get(p, ""):<32} x{rel[p]:.2f}  (median of {len(rs)}: {", ".join(f"{x:.2f}" for x in rs)})')
        if died and all(p in rel for p in share):
            mix = sum(share[p] / total * rel[p] for p in share)
            hours = (t1 - t0) / 3600
            print(f'\nProjected runtime if each profile ran alone from {run[0]["mv"]} mV (this run: {hours:.2f} h mixed):')
            for p in sorted(rel):
                print(f'  profile {p} {profiles.get(p, ""):<32} {hours * mix / rel[p]:.2f} h')
    elif len(set(f['profile'] for f in fitted)) > 1:
        print('\nNot enough reference segments to compare profiles.')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--serial', help='USB serial number (MAC) of the unit')
    p.add_argument('--port', help='serial port, only when the serial number is not available')
    sub = p.add_subparsers(dest='cmd', required=True)
    sub.add_parser('status')
    st = sub.add_parser('start')
    st.add_argument('--segment', type=int, default=600, help='seconds per profile before the rotation moves on')
    st.add_argument('--charge-off', action='store_true', help='disable charging for the run (USB-attached rundown)')
    st.add_argument('profiles', nargs='*')
    sub.add_parser('stop')
    ch = sub.add_parser('charge')
    ch.add_argument('on', choices=['0', '1'])
    lv = sub.add_parser('live')
    lv.add_argument('--seconds', type=float, default=0)
    lv.add_argument('--out')
    du = sub.add_parser('dump')
    du.add_argument('--out', required=True)
    an = sub.add_parser('analyze')
    an.add_argument('file')
    an.add_argument('--reference', type=int, default=0)
    an.add_argument('--settle', type=int, default=120, help='seconds dropped after each profile switch')
    an.add_argument('--include-usb', action='store_true', help='keep records with USB power (a charge-off run)')
    args = p.parse_args()
    if args.cmd != 'analyze' and not (args.serial or args.port):
        p.error('--serial (or --port) is required')
    {'status': cmd_status, 'start': cmd_start, 'stop': cmd_stop, 'charge': cmd_charge, 'live': cmd_live,
     'dump': cmd_dump, 'analyze': cmd_analyze}[args.cmd](args)


if __name__ == '__main__':
    main()
