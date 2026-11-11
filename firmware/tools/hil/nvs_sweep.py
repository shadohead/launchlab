#!/usr/bin/env python3
"""History-write latency and pull capture against free NVS entries, on a real M5.

The free-entry sweep behind NvsBudget::RESERVE_ENTRIES. For each target
level, `!nvsfill` takes the entries above it with filler blobs in a test
namespace; then pulls each end in a natural (unslowed) coalesced history
save whose HISTORY_SAVE duration is recorded, and pulls are started 150 ms
before a history write, natural and slowed by 400 ms (history_save_pull's
worst case). The filler is removed at the end.

  python firmware/tools/hil/nvs_sweep.py --port /dev/cu.usbmodem83201 --out DIR [--levels 120,110,...]
"""
import argparse
import json
from pathlib import Path
import statistics
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device, number  # noqa: E402
from rig import Rig  # noqa: E402
import scenarios  # noqa: E402
import signals  # noqa: E402


def available(dev):
    return int(number(dev.status().get('NVS_STATUS', {}).get('available_entries'), -1))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--levels', default='125,115,108,102,98,95,92,89')
    p.add_argument('--saves', type=int, default=5)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    dev = Device(a.port, a.out / 'serial.log')
    time.sleep(3)
    rig = Rig(dev, a.out)
    rig.begin('nvs_sweep')
    rig.require_hil_build()
    dev.hil('nvsfill 0', expect='HIL_OK nvsfill')
    base = available(dev)
    rig.note(f'available with no filler: {base}')
    scenarios.settle(rig)
    rows = []
    for target in (int(x) for x in a.levels.split(',')):
        fill = base - target
        if fill < 0:
            continue
        line = dev.hil(f'nvsfill {fill}', expect='HIL_OK nvsfill')
        level = available(dev)
        durations = []
        for i in range(a.saves):
            start = dev.mark()
            r = rig.pull(signals.standard_pull(7000 + 500 * i, label=f'{level} free: pull {i + 1}'), signals.launch_motion(seed=90 + i))
            save = dev.wait('HISTORY_SAVE', 40, since=start)
            if save is not None and ' ok=1' in save.text:
                durations.append(int(number(save.f.get('duration_us'), 0)))
            scenarios.drain_history(rig)
        armed = {}
        for slow in (0, 400):
            rig.pull(signals.standard_pull(6000, label=f'{level} free: priming'))
            optical = signals.standard_pull(9000, label=f'{level} free: pull -150 ms into a {slow} ms-slowed history write')
            r, trigger = scenarios.armed_pull(rig, optical, 'history', -150, slow, signals.launch_motion(seed=99))
            save = dev.wait('HISTORY_SAVE', 5, since=trigger.index) if trigger else None
            armed[slow] = {'counted': bool(r and r.valid and r.added), 'rpm': round(r.rpm) if r else 0,
                           'save_us': int(number(save.f.get('duration_us'), 0)) if save else None}
            scenarios.drain_history(rig)
        dev.hil('nvs clear')
        row = {'target': target, 'available': level, 'filler': line.text if line else None, 'save_us': durations,
               'median_us': int(statistics.median(durations)) if durations else None, 'max_us': max(durations) if durations else None,
               'armed': armed}
        rows.append(row)
        rig.note(json.dumps(row))
        (a.out / 'sweep.json').write_text(json.dumps(rows, indent=1) + '\n')
    dev.hil('nvsfill 0', expect='HIL_OK nvsfill')
    rig.note(f'available after: {available(dev)}')
    rig.end()
    lines = ['| Free entries | History save median (ms) | Max (ms) | Pull -150 ms, natural write | Pull -150 ms, +400 ms write |',
             '|---|---|---|---|---|']
    def ms(us):
        return '' if us is None else f'{us / 1000:.0f}'

    def arm(r, k):
        return f"{'counted' if r['armed'][k]['counted'] else 'LOST'} ({ms(r['armed'][k]['save_us'])} ms write)"
    for r in rows:
        lines.append(f"| {r['available']} | {ms(r['median_us'])} | {ms(r['max_us'])} | {arm(r, 0)} | {arm(r, 400)} |")
    (a.out / 'summary.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    dev.close()


if __name__ == '__main__':
    main()
