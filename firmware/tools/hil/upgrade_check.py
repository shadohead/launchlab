#!/usr/bin/env python3
"""Upgrade from 0.15 with a full history, and roll back, on a real M5 (milestone storage).

Customers on 0.15 stable have up to 128 history records and a 0.15 milestone
table. A unit with a full history has about 100 free NVS entries, and a
milestone save needs one 8-entry slot plus room for a history rewrite (87
entries), so 0.18's table must never hold more NVS than 0.17's does: a unit
rolled back with Repair / rollback still has to save its pulls and tables.

Starting from the HIL build on the unit, this:
 1. removes the records 0.16 and later add (`!newerforget`), so the store
    looks as a 0.15 unit's does;
 2. installs frozen 0.15 (application only), restores a full-history backup
    and uploads a 0.15 milestone table with 0.15's own request (v1);
 3. installs the HIL build again (application only): the table carries over
    (MILESTONE_NOTICE imported_v1_table);
 4. saves, in turn, a table with effects past value 63, a narrow one, a wide
    one, a reset and a wide one, an effect set reaching past value 63, then
    reboots: every save must succeed and the last table stay;
 5. for each --rollback VERSION FROZEN HIL (0.15.0, then 0.17.0): installs
    the frozen application over 0.18's wide table, which must show the table
    the version reads (0.15 its own, 0.16 and 0.17 0.18's view of it), have
    room for a history save and save a table with its own request; installs
    that version's HIL build (the same storage code), which must save
    scripted pulls on the full history (128 records), a table, and a pull
    after it; then installs the 0.18 HIL build again, which loads the table
    (0.17's last one; after 0.15, its own, as 0.17 does) and saves a wide one.
It leaves the HIL build and test data on the unit; restore the unit's own
backup and firmware afterwards as the README says.

  python firmware/tools/hil/upgrade_check.py --port /dev/cu.usbmodem83201 --out DIR \\
      --frozen firmware/releases/0.15.0-sticks3-milestone-pm/LaunchLabMini.ino.bin \\
      --hil-image firmware/build-m5-hil/LaunchLabMini.ino.bin --backup FULL_HISTORY_BACKUP.bin \\
      --rollback 0.15.0 FROZEN_0_15.bin HIL_0_15.bin --rollback 0.17.0 FROZEN_0_17.bin HIL_0_17.bin
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device  # noqa: E402
from port_guard import require_free  # noqa: E402
import milestones  # noqa: E402
from rig import Rig as PullRig  # noqa: E402
import signals  # noqa: E402

V015_TABLE = [(9000, 'Sparkles'), (11000, 'Orbit'), (13000, 'Lightning'), (15000, 'Slash')]
WIDE_A = [(9000, 'Sparkles'), (12000, ['Tidal', 'Starfall', 'Blink']), (15000, 'Meadow')]
NARROW = [(9500, ['Tidal', 'Ravage']), (14000, 'Slash')]
WIDE_B = [(8000 + 1000 * i, ['Orbit', 'Haymaker', 'Volley', 'Starfall', 'Sharkfin', 'Blink'][i % 6]) for i in range(12)]
WIDE_C = [(10000, ['Rev', 'Crescent']), (13000, ['Volley', 'Starfall', 'Mimic', 'Scales', 'Meadow'])]
EFFECT_SET = ['Slash', 'Crimson', 'Haymaker', 'Starfall', 'Blink']
# What each older build saves with its own request after the rollback.
OLDER_TABLES = {1: [(9500, 'Orbit'), (12500, 'Lightning'), (14500, 'Slash')],
                2: [(9500, ['Tidal', 'Ravage']), (13500, ['Slash', 'Crimson'])],
                3: [(9500, ['Haymaker', 'Blink']), (13500, ['Mask', 'Slash'])]}
OLDER_AFTER_PULLS = {1: [(10000, 'Flames'), (13000, 'Slash')],
                     2: [(10000, ['Orbit', 'Crimson']), (13000, 'Slash')],
                     3: [(10000, ['Sunblade', 'Orbit']), (13000, ['Crescent', 'Rev'])]}
# 0.16's sixteen effects (below Crimson, not Off or Random).
V016_EFFECTS = set(milestones.EFFECTS[:18]) - {'Off', 'Random'}


def for_v016(rows):
    """0.18's view of a table for 0.16 and 0.17 (Config::forV016): pools cut
    to 0.16's effects, else the default table's effect at that RPM."""
    out = []
    for rpm, effect in rows:
        kept = tuple(e for e in milestones.pool_names(effect) if e in V016_EFFECTS)
        if not kept:
            kept = (next((e for r, e in reversed(milestones.DEFAULTS) if r <= rpm), milestones.DEFAULTS[0][1]),)
        out.append((rpm, kept[0] if len(kept) == 1 else kept))
    return out


def listed(rows_in):
    return [(rpm, e if isinstance(e, str) else tuple(milestones.pool_names(e))) for rpm, e in rows_in]


class Rig:
    """What scenarios.milestone_list / milestone_request need: a device."""
    def __init__(self, dev):
        self.dev = dev


IMAGES = []  # (image, sha256) in the order flashed


def flash(esptool, port, image):
    """Application only, the image header as built (keep), so a readback is the file byte for byte."""
    IMAGES.append((str(image), hashlib.sha256(Path(image).read_bytes()).hexdigest()))
    require_free(port)
    out = subprocess.run([esptool, '--chip', 'esp32s3', '--port', port, '--after', 'watchdog_reset',
                          'write-flash', '--flash-mode', 'keep', '--flash-size', 'keep', '0x10000', str(image)],
                         capture_output=True, text=True)
    text = out.stdout.replace('\r', '\n')
    if out.returncode or 'Hash of data verified' not in text:
        raise SystemExit(f'flash failed:\n{text[-2000:]}\n{out.stderr[-2000:]}')


def backup_lines(path):
    data = base64.b64encode(Path(path).read_bytes()).decode()
    return '\n'.join('BACKUP_DATA ' + data[i:i + 96] for i in range(0, len(data), 96)) + '\n'


def plain_status(dev, timeout=6):
    """`p` alone (production builds read `!` as other commands): {tag: fields}.

    A unit with a host attached also prints its status by itself every 2 s, so a
    block already under way when `p` goes out can end (MOTION_CLIP) before any
    STATUS line after the request: the 0.20.7 gate's songs_rollback read 0.19.0's
    runtime as empty that way, on a unit that was running it. The block read is
    the first one that starts (STATUS) after the request."""
    time.sleep(0.05)
    start = dev.mark()
    dev.write('p')
    first = dev.wait(lambda l: l.tag == 'STATUS', timeout, since=start)
    if first is not None:
        start = first.index
        dev.wait('MOTION_CLIP', timeout, since=start)
    return {l.tag: l.f for l in dev.since(start)}


def newest_launch(dev, timeout=8):
    """The newest history record (`u` export, oldest first): (number, rpm), or None."""
    time.sleep(0.05)
    start = dev.mark()
    dev.write('u')
    end = time.time() + timeout
    while time.time() < end:
        lines = [l.text for l in dev.since(start)]
        export = next((t for t in lines if t.startswith('HISTORY_EXPORT ')), None)
        launches = [t for t in lines if t.startswith('HISTORY_LAUNCH ')]
        if export:
            want = int(export.split('records=')[1].split()[0])
            if len(launches) >= want:
                if not launches:
                    return None
                f = dict(part.split('=', 1) for part in launches[-1].split()[1:])
                return int(f['number']), round(float(f['rpm']))
        time.sleep(0.2)
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frozen', type=Path, required=True, help='frozen 0.15.0 application image')
    p.add_argument('--hil-image', type=Path, required=True)
    p.add_argument('--backup', type=Path, required=True, help='a full-history backup (format 1) to restore under 0.15')
    p.add_argument('--esptool', default='esptool')
    p.add_argument('--rollback', nargs=3, action='append', default=[], metavar=('VERSION', 'FROZEN', 'HIL'),
                   help='an older release (0.15.0, 0.17.0) to roll back to: its frozen image and a HIL build of its source')
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

    def reboot(dev):
        """A HIL-build restart; returns the boot's lines."""
        start = dev.mark()
        dev.hil('reboot', expect='HIL_REBOOT')
        boot = dev.wait('HIL_BOOT', 30, since=start)
        time.sleep(1.5)
        return [l.text for l in dev.since(start)] if boot else []

    # 1. On the HIL build: forget 0.16+ records.
    dev = connect()
    line = dev.hil('newerforget', expect='HIL_OK newerforget')
    check('0.16+ records removed', line is not None and line.f.get('ok') == '1', line.text if line else 'none')
    dev.close()

    # 2. Frozen 0.15, a full history and a 0.15 table.
    flash(a.esptool, a.port, a.frozen)
    dev = connect()
    reply = dev.restore(backup_lines(a.backup))
    check('0.15 restores the full-history backup', reply is not None, reply.text if reply else '')
    time.sleep(2)
    text = dev.backup()
    begin = next((l for l in text.splitlines() if l.startswith('BACKUP_BEGIN')), '')
    check('0.15 holds a full history', 'records=128' in begin and 'runtime=0.15.0' in begin, begin)
    rig = Rig(dev)
    f, text = milestone_request(rig, milestones.request_line('set', 'milestones', V015_TABLE, version=1), paced=True)
    check('0.15 takes its own (v1) milestone request', text.startswith('MILESTONE_OK'), text)
    header, rows = milestone_list(rig)
    check('0.15 lists the table', rows == V015_TABLE, rows)
    dev.close()

    # 3. 0.18 (HIL build) over it: the table carries over.
    flash(a.esptool, a.port, a.hil_image)
    dev = connect()
    rig = Rig(dev)
    # Until a table is saved, every boot carries 0.15's table over; a reboot shows the notice.
    notices = [t for t in reboot(dev) if 'MILESTONE_NOTICE' in t]
    check('0.18 carries 0.15\'s table over', any('imported_v1_table' in n for n in notices), notices)
    header, rows = milestone_list(rig)
    check('0.18 lists 0.15\'s table', rows == V015_TABLE and header.get('version') == '3', (header.get('version'), rows))
    nvs = [('0.18 first boot', header.get('nvs_available'), header.get('nvs_needed'), None)]

    # 4. Every save succeeds on the full history.
    def save(label, rows_in, version=3, reset=False):
        f, text = milestone_request(rig, milestones.request_line('reset') if reset else
                                    milestones.request_line('set', 'milestones', rows_in, version=version))
        header, rows = milestone_list(rig)
        nvs.append((label, header.get('nvs_available'), header.get('nvs_needed'), None))
        want = milestones.DEFAULTS if reset else [(rpm, e if isinstance(e, str) else tuple(e)) for rpm, e in rows_in]
        check(f'save: {label}', text.startswith('MILESTONE_OK') and rows == want, f'{text} nvs={nvs[-1][1:3]}')
        return header

    save('a table with effects past value 63', WIDE_A)
    save('a narrow table (0.17\'s record again)', NARROW)
    save('a wide twelve-row ladder', WIDE_B)
    save('a reset to the defaults', None, reset=True)
    save('a wide table again', WIDE_C)
    save('a narrow table sent as 0.17\'s v2 request', NARROW, version=2)
    save('a wide table again', WIDE_A)
    time.sleep(0.05)
    start = dev.mark()
    dev.write('U\n')
    dev.wait('MILESTONE_READY', 5, since=start)
    mark = dev.mark()
    dev.write(milestones.effect_set_line(EFFECT_SET) + '\n')
    chunk = dev.wait(lambda l: l.text.startswith(('MILESTONE_CHUNK', 'MILESTONE_ERROR')), 5, since=mark)
    reply = None
    if chunk and chunk.text.startswith('MILESTONE_CHUNK'):
        mark = dev.mark()
        dev.write('.\n')
        reply = dev.wait(lambda l: l.text.startswith(('MILESTONE_OK', 'MILESTONE_ERROR')), 8, since=mark)
    check('an effect set reaching past value 63 saves', reply is not None and reply.text.startswith('MILESTONE_OK'), reply.text if reply else chunk)
    boot = reboot(dev)
    check('a clean reboot', any(t.startswith('HIL_BOOT') for t in boot) and not any('MILESTONE_NOTICE' in t for t in boot), boot[-5:])
    header, rows = milestone_list(rig)
    check('after a reboot the last table stays', rows == [(rpm, e if isinstance(e, str) else tuple(e)) for rpm, e in WIDE_A], rows)
    text = dev.backup()
    begin = next((l for l in text.splitlines() if l.startswith('BACKUP_BEGIN')), '')
    check('the history is intact (128 records), and the backup is format 6', 'records=128' in begin and 'format=6' in begin, begin)
    dev.close()

    # 5. Roll back to each older release over 0.18's wide table, and up again.
    last = WIDE_A
    for version, frozen, hil_image in a.rollback:
        # 0.15 sends v1 requests, 0.16 and 0.17 v2, 0.18.x (a rollback from a
        # later 0.18) and 0.19 v3 and read the wide table themselves.
        request_version = 1 if version.startswith('0.15') else 2 if version.startswith(('0.16', '0.17')) else 3
        flash(a.esptool, a.port, frozen)
        dev = connect()
        status = plain_status(dev)
        runtime = status.get('STATUS', {}).get('v', '')
        check(f'{version}: the frozen application runs', runtime.startswith(version) and '-hil' not in runtime, runtime)
        rig = Rig(dev)
        header, rows = milestone_list(rig)
        want = V015_TABLE if request_version == 1 else last if request_version == 3 else for_v016(last)
        shows = 'its own table' if request_version == 1 else 'the table' if request_version == 3 else "0.18's view of the table"
        check(f'{version}: shows {shows}', rows == listed(want),
              (header.get('store'), rows))
        check(f'{version}: the table store is not damaged', header.get('store') == 'ok', header.get('store'))
        nvs_status = status.get('NVS_STATUS', {})
        headroom = int(nvs_status.get('history_headroom', '-1'))
        check(f'{version}: room for a history save on the full history', headroom >= 0, nvs_status)
        nvs.append((f'{version} frozen', header.get('nvs_available'), header.get('nvs_needed'), headroom))
        older = OLDER_TABLES[request_version]
        f, text = milestone_request(rig, milestones.request_line('set', 'milestones', older, version=request_version), paced=True)
        header, rows = milestone_list(rig)
        check(f'{version}: saves a table with its own request', text.startswith('MILESTONE_OK') and rows == listed(older), f'{text} rows={rows}')
        text = dev.backup()
        begin = next((l for l in text.splitlines() if l.startswith('BACKUP_BEGIN')), '')
        check(f'{version}: the history is full (128 records)', 'records=128' in begin, begin)
        dev.close()

        # The same storage code, with scripted pulls.
        flash(a.esptool, a.port, hil_image)
        dev = connect()
        pulls = PullRig(dev, a.out)
        pulls.begin(f'{version} pulls')
        runtime = pulls.require_hil_build()
        check(f'{version}: its HIL build runs', runtime.startswith(version), runtime)

        def history():
            return pulls.dev.status().get('HISTORY_STATUS', {})

        def pull_and_save(label, rpm, seed):
            before = history()
            r = pulls.pull(signals.standard_pull(rpm), signals.launch_motion(seed=seed))
            pulls.check_pull(r, tolerance=0.03, saturated=False)
            time.sleep(1.5)
            drained = drain(pulls)
            after = history()
            ok = (bool(r.added) and drained and after.get('records') == '128' and
                  int(after.get('saves', 0)) > int(before.get('saves', 0)) and after.get('save_errors') == before.get('save_errors'))
            check(f'{version}: {label} saves on the full history', ok,
                  f"added={bool(r.added)} drained={drained} records={after.get('records')} saves={before.get('saves')}->{after.get('saves')} "
                  f"errors={before.get('save_errors')}->{after.get('save_errors')}")

        from scenarios import settle, drain_history as drain  # noqa: E402
        settle(pulls)
        for i, rpm in enumerate((6000, 9000, 12000)):
            pull_and_save(f'pull {i + 1}', rpm, 30 + i)
        rig = Rig(dev)
        older = OLDER_AFTER_PULLS[request_version]
        f, text = milestone_request(rig, milestones.request_line('set', 'both', older, version=request_version), paced=True)
        header, rows = milestone_list(rig)
        check(f'{version}: saves a table after the pulls', text.startswith('MILESTONE_OK') and rows == listed(older), f'{text} rows={rows}')
        nvs.append((f'{version} HIL', header.get('nvs_available'), header.get('nvs_needed'), None))
        pull_and_save('a pull after the table', 10500, 40)
        older_newest = newest_launch(dev)
        check(f'{version}: its newest record is its own last pull', older_newest is not None and abs(older_newest[1] - 10500) <= 315,
              older_newest)
        pulls.end()
        dev.close()
        if request_version >= 2:
            last = older

        # Up to 0.18 again: the older build saved pulls into NVS after 0.18's
        # last mirror, so newest wins takes NVS, with those pulls.
        flash(a.esptool, a.port, a.hil_image)
        dev = connect()
        rig = Rig(dev)
        # (A stale log copy would show 0.18's own last pull instead.)
        shown = newest_launch(dev)
        # (0.18.0 saved them to the shared history log instead.)
        via = 'the log' if request_version == 3 else 'NVS'
        check(f'0.18 after {version}: the older build\'s pulls are there (newest wins took {via})', shown == older_newest, (shown, older_newest))
        header, rows = milestone_list(rig)
        check(f'0.18 after {version}: loads the table', rows == listed(last) and header.get('store') == 'ok', (header.get('store'), rows))
        save(f'a wide table after {version}', WIDE_A)
        last = WIDE_A
        dev.close()

    failed = [c for c in checks if not c['ok']]
    (a.out / 'results.json').write_text(json.dumps({'checks': checks, 'nvs': nvs, 'images': IMAGES}, indent=1) + '\n')
    lines = ['# Upgrade from 0.15 with a full history, and rollback', '', f'{len(checks)} checks, {len(failed)} failed.', '',
             '| Step | NVS available | Table save needs | History headroom |', '|---|---|---|---|']
    lines += [f'| {step} | {x} | {y} | {"" if h is None else h} |' for step, x, y, h in nvs]
    lines += ['', 'Images, in the order flashed (frozen production images for the release steps, HIL builds only for scripted pulls):', '']
    lines += [f'- `{image}` sha256 `{sha}`' for image, sha in IMAGES]
    lines += ['']
    lines += [('- PASS ' if c['ok'] else '- FAIL ') + c['name'] + ('' if c['ok'] else f': {c["detail"]}') for c in checks]
    (a.out / 'summary.md').write_text('\n'.join(lines) + '\n')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
