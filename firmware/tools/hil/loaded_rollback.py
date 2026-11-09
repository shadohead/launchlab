#!/usr/bin/env python3
"""Added effects across a rollback, on a real M5 (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Rollout").

0.19 keeps added effects in the spiffs partition, which older firmware never
opens, and saves every record that names one with an older view (the effect's
stock fallback in its place) in the other NVS slot. Starting from the 0.19 HIL
build on the unit, this:
 1. installs the 0.19 HIL build, clears the effect store and adds two effects (Comet, standing in as Tidal;
    Spark, as Sparkles); saves a milestone table and an effect set that name
    them; takes a backup, which must be format 7 with both effects;
 2. for each --rollback VERSION FROZEN (0.18.0, then 0.15.0): installs the
    frozen application, which must run, show its view of the table and set
    (0.18: each added effect replaced by its fallback; 0.15: its own table),
    keep its stores undamaged, refuse the format 7 backup whole and change
    nothing, and save a table with its own request; then installs the 0.19
    HIL build again, which must list both effects as before (the store
    untouched), load the older build's table (newest wins), and save a table
    naming an added effect again;
 3. restores the format 7 backup on 0.19: both effects and the records come back.
It leaves the HIL build and test data on the unit; restore the unit's own
backup and firmware afterwards as the README says.

  python firmware/tools/hil/loaded_rollback.py --port /dev/cu.usbmodem1101 --out DIR \\
      --hil-image firmware/build-m5-hil/LaunchLabMini.ino.bin \\
      --rollback 0.18.0 firmware/releases/0.18.0-sticks3-anime-pm/LaunchLabMini.ino.bin \\
      --rollback 0.15.0 firmware/releases/0.15.0-sticks3-milestone-pm/LaunchLabMini.ino.bin
"""
import argparse
import base64
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device, DeviceError  # noqa: E402
import fx_client  # noqa: E402
import milestones  # noqa: E402
from upgrade_check import IMAGES, Rig, flash, plain_status  # noqa: E402

# The records name each added effect by the ID its install reply gives (96 +
# slot): a slot a deleted effect left keeps its fallback, so installs may go
# past slot 0.
def table(comet, spark):
    return [(9000, 'Sparkles'), (12000, (comet, 'Frost')), (15000, spark)]


# 0.18's view of the table (Milestones::Config::forOlder): each added effect
# becomes its fallback, so every milestone still plays. Its view of the set
# (EffectSet::olderView) leaves added effects out, never offering a stand-in
# nobody chose.
TABLE_018 = [(9000, 'Sparkles'), (12000, ('Tidal', 'Frost')), (15000, 'Sparkles')]
EFFECT_SET_018 = {'Slash', 'Tidal'}
OLDER_TABLES = {'0.18': [(9500, ('Tidal', 'Crimson')), (13500, 'Slash')], '0.15': [(9500, 'Orbit'), (12500, 'Lightning')]}


def row_ids(lines):
    """Each MILESTONE_ROW's pool as a set of IDs (names stop at the built-ins); 0.15 lists one effect_id."""
    rows = [dict(p.split('=', 1) for p in t.split()[1:] if '=' in p) for t in lines if t.startswith('MILESTONE_ROW ')]
    return [milestones.effect_ids(f['pool']) if 'pool' in f else frozenset({int(f['effect_id'])}) for f in rows]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--hil-image', type=Path, required=True)
    p.add_argument('--esptool', default='esptool')
    p.add_argument('--rollback', nargs=2, action='append', default=[], metavar=('VERSION', 'FROZEN'))
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    log = a.out / 'serial.log'
    checks = []

    def check(name, ok, detail=''):
        checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)})
        print(('PASS ' if ok else 'FAIL ') + name + (f': {detail}' if detail and not ok else ''))

    def connect():
        dev = Device(a.port, log)
        time.sleep(4)  # boot
        return dev

    from scenarios import milestone_list, milestone_request  # noqa: E402

    def table_lines(dev):
        time.sleep(0.05)
        start = dev.mark()
        dev.write('M\n')
        dev.wait('MILESTONE_LIST_END', 5, since=start)
        return [l.text for l in dev.since(start)]

    def set_effects(dev, effects):
        time.sleep(0.05)
        start = dev.mark()
        dev.write('U\n')
        dev.wait('MILESTONE_READY', 5, since=start)
        mark = dev.mark()
        dev.write(milestones.effect_set_line(effects) + '\n')
        chunk = dev.wait(lambda l: l.text.startswith(('MILESTONE_CHUNK', 'MILESTONE_ERROR')), 5, since=mark)
        if not chunk or not chunk.text.startswith('MILESTONE_CHUNK'):
            return chunk
        mark = dev.mark()
        dev.write('.\n')
        return dev.wait(lambda l: l.text.startswith(('MILESTONE_OK', 'MILESTONE_ERROR')), 8, since=mark)

    def installed(slots):
        return {s: (f['uid'], f['state']) for s, f in slots.items() if f['state'] != 'deleted'}

    def ids(rows):
        return [milestones.effect_ids(milestones.pool(e)) for _, e in rows]

    def save_table(dev, label, rows, version=3):
        # Paced: the frozen production builds read USB into a 256-byte queue.
        f, text = milestone_request(Rig(dev), milestones.request_line('set', 'milestones', rows, version=version), paced=True)
        lines = table_lines(dev)
        check(f'{label}: saves a table', text.startswith('MILESTONE_OK') and row_ids(lines) == ids(rows), f'{text} {row_ids(lines)}')

    # 1. 0.19: two added effects, records naming them, a format 7 backup.
    flash(a.esptool, a.port, a.hil_image)
    dev = connect()
    fx_client.clear(dev)
    comet = fx_client.compile_text(fx_client.effect_source(0xC0FFEE01, 'Comet', fallback='Tidal'))
    spark = fx_client.compile_text(fx_client.effect_source(0xC0FFEE02, 'Spark', fallback='Sparkles'))
    at = {}
    for name, file in (('Comet', comet), ('Spark', spark)):
        reply = fx_client.put(dev, file, name.encode(), timeout=60)
        check(f'0.19: installs {name}', reply.text.startswith('FX_OK') and 'slot' in reply.f, reply.text)
        at[name] = int(reply.f.get('slot', 0))
    store, slots = fx_client.listing(dev)
    before = installed(slots)
    check('0.19: both listed', [slots[at[n]]['name'] for n in ('Comet', 'Spark')] == ['Comet', 'Spark'] and len(before) == 2, slots)
    records = table(96 + at['Comet'], 96 + at['Spark'])
    save_table(dev, '0.19 (naming added effects)', records)
    reply = set_effects(dev, ['Slash', 'Tidal', 96 + at['Spark']])
    check('0.19: an effect set naming an added effect saves', reply is not None and reply.text.startswith('MILESTONE_OK'), reply.text if reply else 'none')
    text = dev.backup()
    begin = next((l for l in text.splitlines() if l.startswith('BACKUP_BEGIN')), '')
    check('0.19: the backup is format 7 with both effects', 'format=7' in begin and 'effects=2' in begin, begin)
    data = ''.join(l[len('BACKUP_DATA '):] for l in text.splitlines() if l.startswith('BACKUP_DATA '))
    backup_lines = '\n'.join('BACKUP_DATA ' + data[i:i + 96] for i in range(0, len(data), 96)) + '\n'
    dev.close()

    for version, frozen in a.rollback:
        older = version.rsplit('.', 1)[0]
        flash(a.esptool, a.port, frozen)
        dev = connect()
        runtime = plain_status(dev).get('STATUS', {}).get('v', '')
        check(f'{version}: the frozen application runs', runtime.startswith(version), runtime)
        header, rows = milestone_list(Rig(dev))
        check(f'{version}: the table store is not damaged', header.get('store') == 'ok', header.get('store'))
        if older == '0.18':
            check('0.18: shows the table with each added effect replaced by its fallback',
                  rows == [(r, e if isinstance(e, str) else tuple(e)) for r, e in TABLE_018], rows)
            check('0.18: offers the effect set without the added effect',
                  {milestones.EFFECTS[i] for i in milestones.effect_ids(header.get('effect_set', '0'))} == EFFECT_SET_018, header.get('effect_set'))
        restore_at = dev.mark()
        try:
            reply = dev.restore(backup_lines)
            refused = f'accepted: {reply.text if reply else None}'
        except DeviceError as error:
            refused = str(error)
        check(f'{version}: refuses the format 7 backup whole, writing nothing', 'RESTORE_ERROR' in refused and 'written=0' in refused, refused)
        # Older firmware hands the rest of the refused line to the command
        # parser, where an R in the base64 starts another restore that would
        # take the next commands; end it (an empty restore writes nothing).
        time.sleep(0.5)
        after_error = [l.text for l in dev.since(restore_at)]
        after_error = after_error[next((i for i, t in enumerate(after_error) if t.startswith('RESTORE_ERROR')), len(after_error)):]
        if any(t.startswith('RESTORE_READY') for t in after_error):
            mark = dev.mark()
            dev.write('.\n')
            dev.wait(lambda l: l.text.startswith(('RESTORE_OK', 'RESTORE_ERROR')), 8, since=mark)
        after, _ = milestone_list(Rig(dev))
        check(f'{version}: nothing changed', after.get('revision') == header.get('revision'), (header.get('revision'), after.get('revision')))
        save_table(dev, version, OLDER_TABLES[older], version=1 if older == '0.15' else 3)
        dev.close()

        flash(a.esptool, a.port, a.hil_image)
        dev = connect()
        store, slots = fx_client.listing(dev)
        check(f'0.19 after {version}: both effects as before', installed(slots) == before, slots)
        lines = table_lines(dev)
        if older == '0.18':
            check('0.19 after 0.18: loads 0.18\'s table (newest wins)', row_ids(lines) == ids(OLDER_TABLES[older]), row_ids(lines))
        save_table(dev, f'0.19 after {version} (naming added effects)', records)
        dev.close()

    # 3. The format 7 backup on 0.19.
    dev = connect()
    fx_client.clear(dev)
    reply = dev.restore(backup_lines)
    check('0.19: restores the format 7 backup', reply is not None, reply.text if reply else 'none')
    time.sleep(6)  # it restarts
    store, slots = fx_client.listing(dev)
    check('0.19: the restore puts both effects back', installed(slots) == before, slots)
    packed, reply = fx_client.source(dev, at['Comet'])
    check('0.19: with its source', packed == b'Comet', reply.text)
    check('0.19: and the table', row_ids(table_lines(dev)) == ids(records))
    dev.close()

    failed = [c for c in checks if not c['ok']]
    (a.out / 'results.json').write_text(json.dumps({'checks': checks, 'images': IMAGES}, indent=1) + '\n')
    lines = ['# Added effects across a rollback', '', f'{len(checks)} checks, {len(failed)} failed.', '',
             'Images, in the order flashed:', ''] + [f'- `{image}` sha256 `{sha}`' for image, sha in IMAGES] + ['']
    lines += [('- PASS ' if c['ok'] else '- FAIL ') + c['name'] + ('' if c['ok'] else f': {c["detail"]}') for c in checks]
    (a.out / 'summary.md').write_text('\n'.join(lines) + '\n')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
