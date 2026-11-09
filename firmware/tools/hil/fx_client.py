"""Loaded effects over USB for the HIL rig (docs/FIRMWARE_LOADABLE_EFFECTS.md):
compile an effect with fxc, frame an `X` request as fx_transfer.h does, send
it the way the website will, and read the `F` list."""
import re
import base64
import pathlib
import struct
import subprocess
import tempfile
import time

from hil_device import DeviceError

ROOT = pathlib.Path(__file__).resolve().parents[2]  # firmware/
MAGIC = 0x01584C4C  # "LLX\1"
PUT, DELETE, CLEAR, SOURCE = 1, 2, 3, 4
_FXC = None


def fxc():
    """The effect compiler, built once into a temporary directory."""
    global _FXC
    if _FXC is None:
        out = pathlib.Path(tempfile.mkdtemp(prefix='launchlab-fxc-')) / 'fxc'
        subprocess.run(['c++', '-std=c++17', '-O2', '-ffp-contract=off', f'-I{ROOT / "LaunchLabMini"}', f'-I{ROOT / "tools/fx"}',
                        str(ROOT / 'tools/fx/fxc.cpp'), '-lz', '-o', str(out)], check=True)
        _FXC = out
    return _FXC


def compile_text(source):
    """The compiled LLFX file of an effect's source text."""
    with tempfile.TemporaryDirectory(prefix='launchlab-fx-') as d:
        src, dst = pathlib.Path(d) / 'effect.fx', pathlib.Path(d) / 'effect.llfx'
        src.write_text(source)
        subprocess.run([str(fxc()), 'compile', str(src), str(dst)], check=True, capture_output=True)
        return dst.read_bytes()


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def frame(op, slot=0, file=b'', source=b''):
    body = struct.pack('<IBBBBII', MAGIC, op, slot, 0, 0, len(file), len(source)) + file + source
    return body + struct.pack('<I', fnv1a(body))


def request(dev, data, timeout=30):
    """Sends one framed `X` request; returns the FX_OK / FX_ERROR line."""
    time.sleep(0.05)  # a guarded command starts after input quiet
    start = dev.mark()
    dev.write('X\n')
    if not dev.wait('FX_READY', 5, since=start):
        raise DeviceError('no FX_READY')
    text = base64.b64encode(data).decode()
    for i in range(0, len(text), 96):
        mark = dev.mark()
        dev.write(text[i:i + 96] + '\n')
        reply = dev.wait(lambda l: l.text.startswith(('FX_CHUNK', 'FX_ERROR')), 5, since=mark)
        if reply is None:
            raise DeviceError('no FX_CHUNK')
        if reply.text.startswith('FX_ERROR'):
            return reply
    mark = dev.mark()
    dev.write('.\n')
    reply = dev.wait(lambda l: l.text.startswith(('FX_OK', 'FX_ERROR')), timeout, since=mark)
    if reply is None:
        raise DeviceError('no FX_OK/FX_ERROR')
    return reply


def put(dev, file, source=b'', timeout=30):
    return request(dev, frame(PUT, 0, file, source), timeout)


def delete(dev, slot):
    return request(dev, frame(DELETE, slot))


def clear(dev):
    return request(dev, frame(CLEAR))


def source(dev, slot):
    """A slot's packed source (X op 4), or None with the FX_ERROR line."""
    start = dev.mark()
    reply = request(dev, frame(SOURCE, slot))
    if not reply.text.startswith('FX_OK'):
        return None, reply
    lines = [l.text for l in dev.since(start)]
    at = next(i for i, text in enumerate(lines) if text.startswith('FX_SOURCE '))
    end = next(i for i, text in enumerate(lines) if text.startswith('FX_OK op=source'))
    return base64.b64decode(''.join(lines[at + 1:end])), reply


def listing(dev):
    """`F`: the store's fields and one dict per slot (keyed by slot number)."""
    time.sleep(0.05)
    start = dev.mark()
    dev.write('F\n')
    if dev.wait('FX_LIST_END', 5, since=start) is None:
        raise DeviceError('no FX_LIST_END')
    store, slots = {}, {}
    for line in dev.since(start):
        if line.text.startswith('FX_LIST '):
            store = line.f
        elif line.text.startswith('FX_SLOT '):
            head, _, name = line.text.partition(' name=')
            fields = dict(part.split('=', 1) for part in head.split()[1:])
            fields['name'] = name
            slots[int(fields['slot'])] = fields
    return store, slots


def effect_source(uid, name, fallback='Orbit', revision=1, size=1):
    """A small effect with its own uid; `size` grows its body (more frames, a bigger file)."""
    text = f'effect "{name}"\nuid 0x{uid:x}\nrevision {revision}\nduration 3000\nfallback "{fallback}"\n'
    for i in range(size):
        text += ('repeat 8 {\n  a = t*3 + i*.78 + ' + str(i) + '\n'
                 '  dot(acx + 40*cos(a), acy + 30 + 16*sin(a), accent, 1 - t/3)\n}\n')
    return text


def worst_song_source(short_frames=False):
    """The heaviest song effect the M5 accepts: Boundless 0.17's frames (the
    heaviest passing idiom effect, 8.7 ms modelled, the most code) and a
    6 s song (as long as a celebration, the most a song may be from 0.20.1)
    at the render budget (19.9% of core 0 by the cost model,
    LoadableFx::SONG_BUDGET 20%): every voice type sounding at once on seven
    channels (pulse, wavetable with a filter, FM, tone, stepped noise) and the
    drum bank one drum at a time. A song that passes the validator must play
    with no underrun, and this is the gate's worst case of one."""
    text = (ROOT / 'tools/fx/effects/idiom/boundless017.fx').read_text()
    text = text.replace('effect "Boundless 0.17"', 'effect "Worst Song"').replace('uid 0x4c4c465800010127', 'uid 0x4c4c4d55ffff0002')
    if short_frames:
        # The same song after 1 s of frames: a history save waits while a
        # celebration is shown, so it can land only in the 5 s the song plays on.
        text = text.replace('effect "Worst Song"', 'effect "Worst Song 1s"').replace('uid 0x4c4c4d55ffff0002', 'uid 0x4c4c4d55ffff0003')
        text = re.sub(r'^duration \d+$', 'duration 1000', text, flags=re.M)
        text = re.sub(r'^readout \d+$', 'readout 600', text, flags=re.M)
    text = '\n'.join(l for l in text.split('\n') if not l.startswith('sound '))
    voices = """
voice lead = pulse(duty: .25, attack: .003, decay: 4, gain: .14)
voice harm = tone(wave: sine, attack: .003, decay: 6, gain: .1)
voice pad = wave(points: [0, .38, .7, .92, 1, .92, .7, .38, 0, -.38, -.7, -.92, -1, -.92, -.7, -.38], attack: .05, decay: 3, gain: .1, filter: low, k: .2)
voice bell = fm(ratio: 3.5, index: 1.8, attack: .002, decay: 2, gain: .1)
voice bass = tone(wave: triangle, attack: .002, decay: 1.6, gain: .25)
voice hiss = noise(step: 3, attack: .001, decay: 2, gain: .04)
voice kick = drum(kick, gain: .5)
voice snare = drum(snare, gain: .3)
voice openhat = drum(openhat, gain: .15)
voice tomlow = drum(tomlow, gain: .2)
"""
    lead = ['c5', 'e5', 'g5', 'b5', 'a5', 'g5', 'e5', 'd5']
    def row(notes, k):
        return ' '.join(notes[(i + k) % len(notes)] for i in range(15))
    # A drum every 4th row (400 ms, past the longest drum, 320 ms): one sounds at a time.
    drums = ['kick', 'snare', 'openhat', 'tomlow']
    patterns = []
    for k in range(4):
        lines = {
            'lead': row(lead, k), 'harm': row(['e4', 'g4', 'b4', 'd5'], k), 'pad': row(['c3', 'e3', 'g3', 'a3'], k),
            'bell': row(['c6', 'g6', 'e6', 'b6'], k), 'bass': row(['c2', 'c3', 'g1', 'g2'], k), 'hiss': row(['hiss'], k),
            # Every 4th row of the whole song, across the 15-row patterns' ends.
            'beat': ' '.join(drums[((15 * k + i) // 4) % 4] if (15 * k + i) % 4 == 0 else '.' for i in range(15)),
        }
        patterns.append(f'  pattern p{k} {{\n' + ''.join(f'    {c}: "{r}"\n' for c, r in lines.items()) + '  }')
    # 150 bpm, 4 rows a beat: a row is 1,600 samples, four 15-row patterns are 6 s.
    song = 'song(tempo: 150, rows: 4) {\n' + '\n'.join(patterns) + '\n  order p0 p1 p2 p3\n}\n'
    return text.rstrip('\n') + '\n' + voices + '\n' + song


WORST_SONG_SECONDS = 6.0
# A song's channel can be empty between its requests (MUSIC_STATUS
# max_dry_ms, 0.20.8), but M5's speaker writes silence once it has been
# empty for 48 ms (music_stream.h): a play keeps under this.
MUSIC_DRY_LIMIT_MS = 40
