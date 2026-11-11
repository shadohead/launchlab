"""The song render cost model's calibration on a real M5 (LoadableFx::SONG_KIND_COST,
docs/FIRMWARE_SOUND_0_20.md, "Render budget").

Plays songs over USB previews at Sound Low on a HIL build and measures the music
task's share of core 0 from its own run time (`!tasks`) over a window while the
song plays (1-5 s in), three plays each. Beside each measurement it records the
song's work over the same window from `fxc songwork` (samples, filtered samples
and each voice kind's sounding samples): the regressors of the fit.

The songs: one and four voices of each kind on their own channels, a note every
row (light frames: one glint); seven sine channels (channel scaling); and the
gate's worst mix (fx_client.worst_song_source) with light frames and with
Boundless 0.17's frames, the heaviest the M5 accepts, running on core 1.

  python3 firmware/tools/hil/song_cost.py --port PORT --out DIR [names...]

Writes DIR/song_cost.json and prints one line per song. Leaves Sound Off and no
added effects.
"""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from hil_device import Device, number  # noqa: E402
import fx_client  # noqa: E402

KINDS = {
    'sine': 'tone(wave: sine, attack: .002, decay: 8, gain: .1)',
    'triangle': 'tone(wave: triangle, attack: .002, decay: 8, gain: .1)',
    'sine_low': 'tone(wave: sine, attack: .002, decay: 8, gain: .1, filter: low, k: .3)',
    'noise': 'noise(attack: .002, decay: 8, gain: .05)',
    'stepped': 'noise(step: 3, attack: .002, decay: 8, gain: .05)',
    'pulse': 'pulse(duty: .25, attack: .002, decay: 8, gain: .1)',
    'wave': 'wave(points: [0, .38, .7, .92, 1, .92, .7, .38, 0, -.38, -.7, -.92, -1, -.92, -.7, -.38], attack: .002, decay: 8, gain: .1)',
    'fm': 'fm(ratio: 3.5, index: 1.8, attack: .002, decay: 8, gain: .1)',
    'drum': None,
    # Slow attacks, retriggered every row (100 ms): the gate's worst mix has a pad like this.
    'sine_slow': 'tone(wave: sine, attack: .05, decay: 8, gain: .1)',
    'wave_slow': 'wave(points: [0, .38, .7, .92, 1, .92, .7, .38, 0, -.38, -.7, -.92, -1, -.92, -.7, -.38], attack: .05, decay: 8, gain: .1)',
    'pad': 'wave(points: [0, .38, .7, .92, 1, .92, .7, .38, 0, -.38, -.7, -.92, -1, -.92, -.7, -.38], attack: .05, decay: 3, gain: .1, filter: low, k: .2)',
    'wave_low': 'wave(points: [0, .38, .7, .92, 1, .92, .7, .38, 0, -.38, -.7, -.92, -1, -.92, -.7, -.38], attack: .002, decay: 3, gain: .1, filter: low, k: .2)',
    'base': 'tone(wave: sine, attack: .001, decay: 200, gain: .1)',
}
DRUMS = ['kick', 'snare', 'openhat', 'tomlow', 'tomhigh', 'clap', 'rim']
NOTES = ['c4', 'e4', 'g4', 'b4']
LIGHT = ['duration 6000', 'readout 900', '', 'frame {', '  glint(.1, .4)', '}', '']
WINDOW_MS = (1000, 5000)


def kind_source(kind, n):
    lines = ['effect "Cost"', 'uid 0x4c4c4d55eeee0001', 'revision 1'] + LIGHT
    if kind == 'drum':
        # A drum every row on each channel (a row is 100 ms, shorter than the drums): always sounding.
        lines += [f'voice x{i} = drum({DRUMS[i]}, gain: .2)' for i in range(n)]
        def row(i):
            return ' '.join(f'x{i}' for _ in range(15))
    elif kind == 'base':
        # The player and the task: one short note a pattern, silent the rest of the time.
        lines += [f'voice x0 = {KINDS[kind]}']
        def row(i):
            return 'c4 ' + ' '.join('.' for _ in range(14))
    else:
        lines += [f'voice x{i} = {KINDS[kind]}' for i in range(n)]
        def row(i):
            return ' '.join(NOTES[(r + i) % 4] for r in range(15))
    # 150 bpm, 4 rows a beat: 15 rows are 1.5 s, four times over 6 s.
    lines += ['song(tempo: 150, rows: 4) {', '  pattern p {'] + [f'    x{i}: "{row(i)}"' for i in range(n)] + ['  }', '  order p p p p', '}']
    return '\n'.join(lines) + '\n'


def mix_source(parts):
    """Voices of several kinds at once, each on its own channel with a note (or
    drum) every row; parts are (kind, count). Light frames."""
    lines = ['effect "Mix"', 'uid 0x4c4c4d55eeee0002', 'revision 1'] + LIGHT
    rows, drums = [], iter(DRUMS)
    for kind, n in parts:
        for i in range(n):
            v = f'x{len(rows)}'
            lines.append(f'voice {v} = drum({next(drums)}, gain: .2)' if kind == 'drum' else f'voice {v} = {KINDS[kind]}')
            rows.append((v, ' '.join(v for _ in range(15)) if kind == 'drum' else ' '.join(NOTES[(r + len(rows)) % 4] for r in range(15))))
    lines += ['song(tempo: 150, rows: 4) {', '  pattern p {'] + [f'    {v}: "{r}"' for v, r in rows] + ['  }', '  order p p p p', '}']
    return '\n'.join(lines) + '\n'


# Mixes of two to six kinds: whether kinds sounding together cost more than
# their parts (the gate's worst mix of six kinds measured 2.4 points over them).
MIXES = {
    'mix:pulse+wave': [('pulse', 1), ('wave', 1)],
    'mix:sine+fm': [('sine', 2), ('fm', 2)],
    'mix:sine+pulse+drum': [('sine', 1), ('pulse', 1), ('drum', 1)],
    'mix:sine+pulse+wave+fm': [('sine', 1), ('pulse', 1), ('wave', 1), ('fm', 1)],
    'mix:five': [('sine', 1), ('stepped', 1), ('pulse', 1), ('wave', 1), ('fm', 1)],
    'mix:six': [('sine', 1), ('stepped', 1), ('pulse', 1), ('wave', 1), ('fm', 1), ('drum', 1)],
    'mix:six_x2': [('sine', 1), ('stepped', 1), ('pulse', 1), ('wave', 1), ('fm', 1), ('drum', 1), ('triangle', 1)],
    'mix:pulse4+wave': [('pulse', 4), ('wave', 1)],
}


def light_worst_source():
    """The gate's worst mix with one glint for frames."""
    text = fx_client.worst_song_source()
    song = text[text.index('\nvoice lead'):]
    return '\n'.join(['effect "Worst Light"', 'uid 0x4c4c4d55ffff0004', 'revision 1'] + LIGHT) + song


def songs():
    out = {}
    for kind in KINDS:
        for n in ((1,) if kind == 'base' else (1, 4)):
            out[f'{kind}:{n}'] = kind_source(kind, n)
    out['sine:7'] = kind_source('sine', 7)
    out.update({name: mix_source(parts) for name, parts in MIXES.items()})
    out['worst:light'] = light_worst_source()
    out['worst:boundless'] = fx_client.worst_song_source()
    return out


def songwork(source):
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / 'song.fx'
        path.write_text(source)
        r = subprocess.run([str(fx_client.fxc()), 'songwork', str(path), str(WINDOW_MS[0]), str(WINDOW_MS[1])],
                           capture_output=True, text=True, check=True)
    return json.loads(r.stdout)


def music_us(d):
    start = d.mark()
    d.hil('tasks', expect='HIL_TASKS', timeout=6)
    return next((int(number(l.f.get('run_us'), 0)) for l in d.since(start, 'HIL_TASK ') if l.f.get('name') == 'music'), 0)


def measure(d, source, plays=3):
    reply = fx_client.put(d, fx_client.compile_text(source), timeout=60)
    if not reply.text.startswith('FX_OK'):
        return {'error': reply.text}
    shares, underruns = [], []
    for _ in range(plays):
        start = d.mark()
        d.write(f"E{reply.f['id']}\n")
        preview = d.wait('EFFECT_PREVIEW', 8, since=start)
        if preview is None:
            return {'error': 'no preview'}
        t = time.monotonic()
        time.sleep(WINDOW_MS[0] / 1000)
        u0, w0 = music_us(d), time.monotonic()
        time.sleep(max(0.0, WINDOW_MS[1] / 1000 - (time.monotonic() - t)))
        u1, w1 = music_us(d), time.monotonic()
        shares.append((u1 - u0) / ((w1 - w0) * 1e6))
        time.sleep(max(0.0, 7.0 - (time.monotonic() - t)))  # the song's end and its fade
        mark = d.mark()
        d.hil('heap', expect='HIL_OK heap')
        m = (d.latest('MUSIC_STATUS', since=mark) or None)
        underruns.append(int(number(m.f.get('underruns'), 0)) if m else -1)
    fx_client.clear(d)
    return {'shares': [round(s, 5) for s in shares], 'share': round(sum(shares) / len(shares), 5), 'underruns': underruns}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=pathlib.Path, required=True)
    p.add_argument('names', nargs='*', help='songs to measure (default all): ' + ', '.join(songs()))
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    chosen = {k: v for k, v in songs().items() if not a.names or k in a.names}
    d = Device(a.port, log_path=a.out / 'serial.log')
    results = {}
    try:
        line = d.hil('set 8 1', expect='HIL_SET', timeout=6)
        assert line is not None and line.f.get('error') == '0', line
        d.hil('home')
        fx_client.clear(d)
        for name, source in chosen.items():
            r = measure(d, source) | {'work': songwork(source)}
            results[name] = r
            print(f"{name:16s} device {r.get('share', 0):.2%} model {r['work']['model']:.2%} underruns {r.get('underruns')} {r.get('error', '')}", flush=True)
            (a.out / 'song_cost.json').write_text(json.dumps(results, indent=1) + '\n')
    finally:
        try:
            fx_client.clear(d)
            d.hil('set 8 0', expect='HIL_SET', timeout=6)
            d.hil('home')
        finally:
            d.close()


if __name__ == '__main__':
    main()
