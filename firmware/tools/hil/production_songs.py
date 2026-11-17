#!/usr/bin/env python3
"""Songs on a production image (0.20): installed and played through only what
the effects page uses, with the production USB receive buffer.

0.18.0's production image kept the core's 256-byte receive buffer while HIL
images had 8 KB, so a request written in one piece past 256 bytes was lost
only on a unit; 0.18.1 gives every build the same 8 KB, and this still checks
the production image end to end. The page and fx_client write an `X` request as 96-character
lines, each after the last one's FX_CHUNK, so a song, the largest kind of
effect, must arrive whole however long it is. On the device this:

- refuses a HIL image (its runtime version names `hil`);
- installs the three example songs with their packed source, as the page
  sends them, and reads each back: `F` lists it with its sizes and `X source`
  returns its packed source byte for byte;
- previews each (`E<id>`) and reads MUSIC_STATUS: played to its end, every
  block, with no underrun. Playback needs Sound not Off and a pull on the unit
  to preview on; without them it is listed as NOT COVERED, never as a pass;
- notes HEAP_STATUS before, in the middle of each song and after (DMA-capable
  heap from 0.18.1, internal before it): above the 32 KB floor in the middle
  of each song and after, and back within 1 KB after the songs besides 0.19's hot block (taken on the
  first added-effect play, 4,492 bytes measured, and kept);
- then the worst case over a whole song: the heaviest frames the M5 accepts
  with a 6 s song of the five dearest voice kinds at the render budget (a song is as
  long as a celebration from 0.20.1), the heap sampled every second;
- removes the songs (`X clear`). It starts only on a unit with no added
  effects (or with --replace-added), so nobody's effects are lost.

usage: python3 firmware/tools/hil/production_songs.py --port /dev/cu.usbmodemNNN --out DIR
Writes DIR/production_songs.json and DIR/summary.md; exits 1 if a check fails.
"""
import argparse
import base64
import json
import subprocess
import sys
import time
import zlib
from pathlib import Path

import fx_client
from hil_device import Device, number

MUSIC = fx_client.ROOT / 'tools/fx/effects/music'
# The example songs and their lengths in seconds (test_fx_music pins them).
SONGS = (('victory_lap', 'Victory Lap', 5.143), ('night_spin', 'Night Spin', 6.0), ('ripcord', 'Ripcord', 2.0))
LINE = 96


def packed_source(path):
    """The packed source a share code holds (what the page sends with the file)."""
    code = subprocess.run([str(fx_client.fxc()), 'share', str(path)], check=True, capture_output=True, text=True).stdout.strip()
    payload = code.split(':', 1)[1]
    return zlib.decompress(base64.urlsafe_b64decode(payload + '=' * (-len(payload) % 4)))


STATUS_TAGS = ('STATUS', 'SOUND_STATUS', 'MUSIC_STATUS', 'HEAP_STATUS')


def status(dev, timeout=4):
    """`p`: {tag: fields} of the status block, once every line read here has arrived."""
    for _ in range(3):
        time.sleep(0.1)
        start = dev.mark()
        dev.write('p\n')
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            tags = {l.text.split()[0]: l.f for l in dev.since(start) if l.text[:1].isupper()}
            if all(t in tags for t in STATUS_TAGS):
                return tags
            time.sleep(0.05)
    return {}


class Report:
    def __init__(self):
        self.checks, self.notes, self.runtime = [], [], None

    def check(self, name, ok, detail='', severity='fail'):
        self.checks.append({'name': name, 'ok': bool(ok), 'detail': detail, 'severity': severity})
        print(f"{'PASS' if ok else {'fail': 'FAIL', 'info': 'NOT COVERED'}[severity]} {name}{': ' + detail if detail else ''}")

    def note(self, text):
        self.notes.append(text)
        print(f'  - {text}')

    def failed(self):
        return [c for c in self.checks if not c['ok'] and c['severity'] == 'fail']


def dry_check(report, name, music):
    """0.20.8 on: the longest the song's channel was empty with more to come
    (MUSIC_STATUS max_dry_ms), under the 48 ms after which M5's speaker
    writes silence. Earlier images do not report it."""
    label = f'{name}: its channel never empty long enough for the speaker to write silence'
    if 'max_dry_ms' not in music:
        report.check(label, False, 'not reported before 0.20.8', severity='info')
        return
    dry = int(number(music.get('max_dry_ms'), -1))
    report.check(label, 0 <= dry < fx_client.MUSIC_DRY_LIMIT_MS,
                 f'max_dry_ms={dry} (limit {fx_client.MUSIC_DRY_LIMIT_MS}), begin_ms={music.get("begin_ms")}')


def worst_song(dev, report, heap):
    """The heaviest song effect the M5 accepts (fx_client.worst_song_source:
    Boundless 0.17's frames and a 6 s song of the five dearest voice kinds) through the
    page's path, previewed to its end with the heap sampled every second."""
    source = fx_client.worst_song_source()
    path = Path(fx_client.fxc()).parent / 'worst_song.fx'
    path.write_text(source)
    file, packed = fx_client.compile_text(source), packed_source(path)
    reply = fx_client.put(dev, file, packed, timeout=60)
    report.check(f'worst song: installed whole ({len(file)} + {len(packed)} bytes)', reply.text.startswith('FX_OK'), reply.text)
    if not reply.text.startswith('FX_OK'):
        return
    seconds = fx_client.WORST_SONG_SECONDS
    before = status(dev).get('MUSIC_STATUS', {})
    start = dev.mark()
    dev.write(f"E{reply.f['id']}\n")
    if not dev.wait(lambda l: l.text.startswith('EFFECT_PREVIEW'), 8, since=start):
        report.check('worst song: playback', False, 'no preview', severity='info')
        return
    t0, samples = time.monotonic(), []
    while time.monotonic() - t0 < seconds + 2:
        time.sleep(1)
        st = status(dev)
        samples.append((round(time.monotonic() - t0), heap(st.get('HEAP_STATUS', {}), 'free'), heap(st.get('HEAP_STATUS', {}), 'min_free'),
                        st.get('MUSIC_STATUS', {}).get('streaming')))
    after = status(dev).get('MUSIC_STATUS', {})
    plays = int(number(after.get('plays'), 0)) - int(number(before.get('plays'), 0))
    underruns = int(number(after.get('underruns'), 0))  # start() resets the count: this play's own
    blocks, expected = int(number(after.get('played_blocks'), 0)), -(-round(seconds * 16000) // 512)
    report.check('worst song: streamed its 6 s without an underrun', plays == 1 and underruns == 0 and after.get('streaming') == '0' and blocks == expected,
                 f'plays +{plays} underruns {underruns} streaming={after.get("streaming")} blocks {blocks}/{expected}')
    dry_check(report, 'worst song', after)
    playing = [free for _, free, _, streaming in samples if streaming == '1']
    low = min(playing + [samples[-1][2]]) if samples else 0
    report.check('worst song: heap above the 32 KB floor for the whole song', bool(playing) and low >= 32768,
                 f'lowest {low}; ' + ', '.join(f'{s} s {free}' for s, free, _, _ in samples))


def source():
    """(commit, dirty) of the checkout running this."""
    try:
        def git(*args):
            return subprocess.run(['git', '-C', str(fx_client.ROOT), *args], check=True, capture_output=True, text=True).stdout
        return git('rev-parse', 'HEAD').strip(), bool(git('status', '--porcelain', '--', '.').strip())
    except (OSError, subprocess.CalledProcessError):
        return None, True


def run(dev, report, replace_added):
    head = status(dev)
    version = head.get('STATUS', {}).get('v', '')
    report.note(f'runtime {version or "unknown"}')
    report.runtime = version
    if not version or 'hil' in version.split('-'):
        report.check('a production image (no HIL receive buffer)', False, f'runtime {version or "no STATUS"}: flash a production build (build-m5-pm.sh)')
        return
    report.check('a production image (no HIL receive buffer)', True, version)
    store, _ = fx_client.listing(dev)
    installed = int(number(store.get('installed'), 0))
    if installed and not replace_added:
        report.check('the unit holds no added effects', False, f'{installed} installed: run on a spare unit, or pass --replace-added to remove them')
        return
    if installed:
        fx_client.clear(dev)
    files = {}
    for key, name, _ in SONGS:
        file, packed = fx_client.compile_text((MUSIC / f'{key}.fx').read_text()), packed_source(MUSIC / f'{key}.fx')
        request = fx_client.frame(fx_client.PUT, 0, file, packed)
        lines = -(-len(base64.b64encode(request)) // LINE)
        started = time.monotonic()
        reply = fx_client.put(dev, file, packed, timeout=60)
        report.check(f'{name}: installed whole ({len(request)} bytes in {lines} paced lines of {LINE} characters)', reply.text.startswith('FX_OK'),
                     f'{reply.text} in {time.monotonic() - started:.1f} s')
        if reply.text.startswith('FX_OK'):
            files[key] = (int(reply.f['slot']), int(reply.f['id']), file, packed)
    _, slots = fx_client.listing(dev)
    for key, name, _ in SONGS:
        if key not in files:
            continue
        slot, _, file, packed = files[key]
        s = slots.get(slot, {})
        report.check(f'{name}: listed with its sizes', s.get('state') == 'ok' and s.get('bytes') == str(len(file)) and s.get('source_bytes') == str(len(packed)),
                     str(s))
        back, reply = fx_client.source(dev, slot)
        report.check(f'{name}: its source reads back byte for byte', back == packed, reply.text)
    first = status(dev)
    heap0, level = first.get('HEAP_STATUS', {}), first.get('SOUND_STATUS', {}).get('level')
    heaps = []
    if level in (None, 'Off'):
        report.check('playback of the installed songs', False, f'Sound is {level or "unknown"}: set Sound to Low on the unit to cover it', severity='info')
    else:
        for key, name, seconds in SONGS:
            if key not in files:
                continue
            before = status(dev).get('MUSIC_STATUS', {})
            start = dev.mark()
            dev.write(f'E{files[key][1]}\n')
            preview = dev.wait(lambda l: l.text.startswith('EFFECT_PREVIEW'), 8, since=start)
            if preview is None or preview.text.startswith('EFFECT_PREVIEW_ERROR'):
                report.check(f'{name}: playback', False, f'{preview.text if preview else "no preview"}: pull once on the unit to give it a recap',
                             severity='info')
                continue
            time.sleep(min(1.0, seconds / 2))
            heaps.append((name, status(dev).get('HEAP_STATUS', {})))
            time.sleep(seconds + 0.5)
            after = status(dev).get('MUSIC_STATUS', {})
            plays = int(number(after.get('plays'), 0)) - int(number(before.get('plays'), 0))
            underruns = int(number(after.get('underruns'), 0))  # start() resets the count: this play's own
            blocks, expected = int(number(after.get('played_blocks'), 0)), -(-round(seconds * 16000) // 512)
            report.check(f'{name}: played to its end at Sound {level} without an underrun',
                         plays == 1 and underruns == 0 and after.get('streaming') == '0' and blocks == expected,
                         f'plays +{plays} underruns {underruns} streaming={after.get("streaming")} blocks {blocks}/{expected}')
            dry_check(report, f'{name} at Sound {level}', after)
    time.sleep(1.0)
    heap1 = status(dev).get('HEAP_STATUS', {})
    kind = 'dma' if 'dma_free' in heap0 else 'internal'
    def heap(h, field):
        return int(number(h.get(f'{kind}_{field}'), 0))
    report.note(f"HEAP_STATUS {kind} free/largest/min: before {heap(heap0, 'free')}/{heap(heap0, 'largest')}/{heap(heap0, 'min_free')}"
                + ''.join(f", {n} playing {heap(h, 'free')}/{heap(h, 'largest')}/{heap(h, 'min_free')}" for n, h in heaps)
                + f", after {heap(heap1, 'free')}/{heap(heap1, 'largest')}/{heap(heap1, 'min_free')}")
    for n, h in heaps:
        used = heap(heap0, 'free') - heap(h, 'free')
        # The speaker's own: 8,364 bytes measured on this image, 2026-10-23.
        report.check(f'{n}: while it plays, only the speaker\'s own heap is taken (<= 9 KB, plus 0.19\'s 4.5 KB hot block)', used <= 9216 + 4608, f'{used:+d} bytes')
        report.check(f'{n}: {kind} heap above the 32 KB floor while it plays', heap(h, 'free') >= 32768 and heap(h, 'min_free') >= 32768,
                     f"{kind}_free={heap(h, 'free')} {kind}_min_free={heap(h, 'min_free')}")
    kept = heap(heap0, 'free') - heap(heap1, 'free')
    report.check(f'after the songs the {kind} heap is back (within 1 KB, besides 0.19\'s 4.5 KB hot block)', kept <= 1024 + 4608, f"{kind}_free {heap(heap0, 'free')} -> {heap(heap1, 'free')} ({kept:+d} bytes)")
    low = heap(heap1, 'min_free')
    report.check(f'{kind} heap low-water mark above the 32 KB floor', low >= 32768, f'{kind}_min_free={low}')
    if level not in (None, 'Off') and heaps:
        worst_song(dev, report, heap)
    reply = fx_client.clear(dev)
    report.check('the songs are removed', reply.text.startswith('FX_OK') and reply.f.get('installed') == '0', reply.text)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--replace-added', action='store_true', help='remove the added effects the unit already holds')
    p.add_argument('--build-dir', type=Path, default=fx_client.ROOT / 'build-m5-pm',
                   help='build of the flashed production image; its build-info.json is recorded for the release gate')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    report = Report()
    dev = Device(a.port, log_path=a.out / 'serial.log')
    try:
        run(dev, report, a.replace_added)
    finally:
        dev.close()
    failed = report.failed()
    uncovered = [c for c in report.checks if not c['ok'] and c['severity'] == 'info']
    info = a.build_dir / 'build-info.json'
    commit, dirty = source()
    # What scripts/check-release-gate.py reads: the production image and the checkout that tested it.
    record = {'tool': 'firmware/tools/hil/production_songs.py', 'runtime': report.runtime, 'source_commit': commit, 'source_dirty': dirty,
              'build_info': json.loads(info.read_text()) if info.exists() else None, 'floor': 32768,
              'failed_checks': len(failed), 'not_covered': len(uncovered), 'checks': report.checks, 'notes': report.notes}
    (a.out / 'production_songs.json').write_text(json.dumps(record, indent=1) + '\n')
    lines = ['# Songs on a production image', '', f'{len(report.checks)} checks, {len(failed)} failed, {len(uncovered)} not covered.', '']
    lines += [f"- {'PASS' if c['ok'] else 'FAIL' if c['severity'] == 'fail' else 'NOT COVERED'} {c['name']}: {c['detail']}" for c in report.checks]
    lines += [f'- note: {n}' for n in report.notes]
    (a.out / 'summary.md').write_text('\n'.join(lines) + '\n')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
