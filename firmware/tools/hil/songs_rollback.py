#!/usr/bin/env python3
"""A song effect across a rollback to 0.19, on a real M5 (docs/FIRMWARE_SOUND_0_20.md).

A song makes an effect file engine level 2; 0.19 (engine level 1) must keep
it stored untouched, list it as needing an update rather than damaged, play
its fallback in its place, and play everything else as before. Starting from
the 0.20 HIL build on the unit, this:
 1. installs the 0.20 HIL build, sets Sound Low, clears the effect store and
    adds a song (Victory Lap) and a level-1 effect (Comet, standing in as
    Tidal), each with its packed source, and makes the song the Best effect;
 2. installs frozen 0.19 (application only), which must run, list the song as
    needs_engine and Comet as ok with their sizes, and on a preview of the
    song play its fallback or refuse it, without restarting;
 3. installs the 0.20 HIL build again, which must list both as before and
    stream the song to its end.
It leaves the HIL build, Sound Off, Best effect Orbit and no added effects.

  python firmware/tools/hil/songs_rollback.py --port /dev/cu.usbmodemNNN --out DIR \\
      --hil-image firmware/build-m5-hil/LaunchLabMini.ino.bin \\
      --rollback 0.19.0 firmware/releases/0.19.0-sticks3-loadable-pm/LaunchLabMini.ino.bin
"""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device, number  # noqa: E402
import fx_client  # noqa: E402
from production_songs import packed_source  # noqa: E402
from upgrade_check import IMAGES, flash, plain_status  # noqa: E402

MUSIC = fx_client.ROOT / 'tools/fx/effects/music'
SONG_SECONDS = 5.143  # Victory Lap, 0.20.1's 5.1 s phrase (test_fx_music pins it)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--hil-image', type=Path, required=True)
    p.add_argument('--esptool', default='esptool')
    p.add_argument('--rollback', nargs=2, required=True, metavar=('VERSION', 'FROZEN'))
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

    # By uid: 0.19 lists a slot that needs a newer engine without its name.
    def listed(dev):
        store, slots = fx_client.listing(dev)
        return store, {f['uid']: f for f in slots.values() if f['state'] != 'deleted'}

    # 1. 0.20: a song and a level-1 effect, the song as Best effect.
    flash(a.esptool, a.port, a.hil_image)
    dev = connect()
    dev.hil('set 8 1', expect='HIL_SET', timeout=6)
    dev.hil('home')
    fx_client.clear(dev)
    song_source = (MUSIC / 'victory_lap.fx').read_text()
    files = {'Victory Lap': (fx_client.compile_text(song_source), packed_source(MUSIC / 'victory_lap.fx')),
             'Comet': (fx_client.compile_text(fx_client.effect_source(0xC0FFEE01, 'Comet', fallback='Tidal')), b'Comet')}
    ids = {}
    for name, (file, packed) in files.items():
        reply = fx_client.put(dev, file, packed, timeout=60)
        check(f'0.20: installs {name}', reply.text.startswith('FX_OK'), reply.text)
        ids[name] = int(reply.f.get('id', 0))
    store, before = listed(dev)
    uid = {f['name']: u for u, f in before.items()}
    check('0.20: both listed, engine 2', store.get('engine') == '2' and all(n in uid and before[uid[n]].get('state') == 'ok' for n in files),
          (store, before))
    line = dev.hil(f"set 6 {ids['Victory Lap']}", expect='HIL_SET', timeout=6)
    check('0.20: the song is the Best effect', line is not None and line.f.get('error') == '0', line.text if line else 'none')
    dev.hil('home')
    dev.close()

    # 2. The frozen 0.19 application over the same storage.
    version, frozen = a.rollback
    flash(a.esptool, a.port, Path(frozen))
    dev = connect()
    runtime = plain_status(dev).get('STATUS', {}).get('v', '')
    check(f'{version}: the frozen application runs', runtime.startswith(version), runtime)
    store, older = listed(dev)
    song, comet = older.get(uid.get('Victory Lap'), {}), older.get(uid.get('Comet'), {})
    check(f'{version}: engine 1, the song listed as needing an update (not damaged), stored whole',
          store.get('engine') == '1' and song.get('state') == 'needs_engine' and song.get('bytes') == before[uid['Victory Lap']].get('bytes'), (store, song))
    check(f'{version}: the level-1 effect plays as before', comet.get('state') == 'ok' and comet.get('name') == 'Comet', comet)
    start = dev.mark()
    dev.write(f"E{ids['Victory Lap']}\n")
    preview = dev.wait(lambda l: l.text.startswith('EFFECT_PREVIEW'), 8, since=start)
    text = preview.text if preview else 'none'
    # Its fallback (Orbit, the default) plays in its place, or the preview is refused; either way nothing restarts.
    check(f'{version}: a preview of the song plays its fallback or is refused', preview is not None and
          (text.startswith('EFFECT_PREVIEW_ERROR') or ' effect=Orbit ' in text + ' '), text)
    time.sleep(SONG_SECONDS + 1)
    restarts = [l.text for l in dev.since(start, 'BOOT ')]
    check(f'{version}: and keeps running', not restarts and plain_status(dev).get('STATUS', {}).get('v', '') == runtime, restarts)
    dev.close()

    # 3. 0.20 again: both as before, and the song streams.
    flash(a.esptool, a.port, a.hil_image)
    dev = connect()
    store, after = listed(dev)
    check(f'0.20 after {version}: both effects as before',
          all(after.get(uid[n], {}).get('state') == 'ok' and after[uid[n]].get('name') == n for n in files), after)
    music0 = dev.status().get('MUSIC_STATUS', {})
    start = dev.mark()
    dev.write(f"E{ids['Victory Lap']}\n")
    preview = dev.wait('EFFECT_PREVIEW', 8, since=start)
    time.sleep(SONG_SECONDS + 1.5)
    music1 = dev.status().get('MUSIC_STATUS', {})
    plays = int(number(music1.get('plays'), 0)) - int(number(music0.get('plays'), 0))
    underruns = int(number(music1.get('underruns'), 0))  # start() resets the count: this play's own
    check(f'0.20 after {version}: the song streams to its end', preview is not None and plays == 1 and underruns == 0 and music1.get('streaming') == '0',
          f'{preview.text if preview else None} plays +{plays} underruns {underruns}')
    dev.hil('set 6 0', expect='HIL_SET', timeout=6)
    dev.hil('set 8 0', expect='HIL_SET', timeout=6)
    fx_client.clear(dev)
    dev.close()

    failed = [c for c in checks if not c['ok']]
    (a.out / 'songs_rollback.json').write_text(json.dumps({'tool': 'firmware/tools/hil/songs_rollback.py', 'rollback': version,
                                                           'images': IMAGES, 'checks': checks, 'failed': len(failed)}, indent=1) + '\n')
    print(f'{len(checks)} checks, {len(failed)} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
