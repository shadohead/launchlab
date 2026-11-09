#!/usr/bin/env python3
"""Every screen as the device draws it, pinned frame by frame.

Builds firmware/tools/ui_render (the firmware's drawing code on a real M5GFX
canvas) and compares one hash per frame with fixtures/ui_render.txt: 52
screens in each built-in theme and both Flip choices, plus one custom theme.
A light custom theme (white background, allowed since 0.16) is pinned in
fixtures/ui_render_light.txt, every screen and celebration frame in both
flips, and every celebration frame of the default, the dark and the light
theme must keep the UI legible: no UI ink below the header changes (except
while Boundless covers those rows, until Renderer::covers gives them back),
and the header (label and RPM readout) keeps at least 90% of the pixels it
had at 3:1 contrast on the background without the effect.
From the end of every effect through the next second, the recap must equal
the same recap without an effect: nothing is left behind when it is over.
The host cost model (Renderer::modelledUs()) is held to the device: for every
effect in fixtures/effect_device_frames.txt whose frames are still those it
was recorded with, the model at the recorded times must not hide device
time: the device's median frame and its slowest frame at most 1 ms above the
model's (with the speaker's share while a sound plays, for a recording made
with Sound on), and the model's slowest frame at most max(3.5 ms, 30%) above
the device's. An effect whose frames changed is
listed as stale until re-recorded from a HIL effect_times serial log:
check_ui_render.py --record-device SERIAL_LOG[,SERIAL_LOG...] [--sound low] [EFFECT...]
(every preview in the logs is kept, a line per round; the device's slowest
frame is the median of its rounds').
The effects library's engine (ui_render --fx, fx_preview.h) takes each
idiomatic effect file to a share code and back to the identical file, keeps
the UI rules on every real recap the validator checks, and never passes the
frame budget for a file the device ran over its 9 ms guard (run 5).
The built-in frames were byte-identical to the pre-palette firmware when the
palette landed, so a change here is a visible change to a built-in theme.
After an intended UI change, regenerate with --update and review the frames
(firmware/tools/ui_render/build.sh, then ui_render --out DIR).
Needs M5GFX (firmware/setup-m5.sh, or $LAUNCHLAB_M5GFX) and SDL2 headers.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'firmware/tests/fixtures/ui_render.txt'
CELEBRATIONS = ROOT / 'firmware/tests/fixtures/ui_celebrations.txt'
# Dragoon Storm from fixtures/theme_vectors.json: a dark navy custom theme.
CUSTOM = ['--custom', '0006ffff5aeb2d7f2d7ffda007e0f800', 'Dragoon Storm']
LIGHT_FIXTURE = ROOT / 'firmware/tests/fixtures/ui_render_light.txt'
# Paper from fixtures/theme_vectors.json: white background, every role readable.
LIGHT = ['--custom', 'ffff0000738e001f00009a000400c000', 'Paper']
MIC_FIXTURE = ROOT / 'firmware/tests/fixtures/ui_render_mic.txt'
MIN_READABLE_HEADER = 0.90
DEVICE_FRAMES = ROOT / 'firmware/tests/fixtures/effect_device_frames.txt'
# How far the model may sit from the device. Below it, where it would hide a
# slow effect: the frames' median at most 1 ms, the slowest frame at most 1 ms
# (the device's is the worst of about 60 noisy frames, about 0.6 ms high on
# its own). Above it: the slowest frame by at most max(3.5 ms, 30%).
MEDIAN_SLACK_US, PEAK_UNDER_US, COST_SLACK_US, COST_SLACK = 1000, 1000, 3500, 0.30
# Effects the model is known to miss, listed in the fixture as
# "# diverges EFFECT: cause" until the model counts what they do. The list
# may only shrink: lower this as entries go, never raise it.
KNOWN_DIVERGENCES_MAX = 0
IDIOM = ROOT / 'firmware/tools/fx/effects/idiom'
# Run 5 (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Device frame times"): the
# device's slowest unguarded pass of each idiomatic file on the recap, in us.
RUN5_DEVICE_US = {'orbit': 6800, 'sparkles': 7300, 'slash': 7900, 'rupture': 7400, 'tidal': 8700,
                  'boundless017': 7700, 'summon': 9400, 'crimson': 10600, 'bladework': 10800,
                  'overdrive': 11000, 'waterleaf': 11000, 'sunblade': 11300}
DEVICE_GUARD_US = 9000


def legibility_failures(lines):
    """Frames whose effect changed UI ink below the header or hid the header's readable pixels."""
    failures = []
    for line in lines.splitlines():
        name, changed, readable = line.split()
        shown, base = (int(v) for v in readable.split('=')[1].split('/'))
        if int(changed.split('=')[1]) or shown < MIN_READABLE_HEADER * base:
            failures.append(f'{name} {changed} {readable}')
    return failures


def device_frames(log_text):
    """Effect -> ['ms:us ...' per preview] from a HIL effect_times serial log (each EFFECT_PREVIEW, then its
    HIL_EFFECT_FRAMES); a soak previews every effect several times."""
    frames, current = {}, None
    for line in log_text.splitlines():
        preview = re.search(r'EFFECT_PREVIEW effect=(\w+)', line)
        if preview:
            current = preview.group(1)
        elif 'HIL_EFFECT_FRAMES' in line and current:
            frames.setdefault(current, []).append(' '.join(p for p in line.split()[3:] if ':' in p))
            current = None
    return frames


def cost_rows(build, fixture):
    """Per fixture line: (effect, current hash, recorded hash, device peak us, model peak us, median gap us)."""
    rows = []
    output = subprocess.run([str(build / 'ui_render'), '--costs', str(fixture)], check=True, capture_output=True, text=True).stdout
    for line in output.splitlines():
        name, now, recorded, device, model, gap = line.split()
        rows.append((name, now, recorded, int(device.split('=')[1].split('@')[0]), float(model.split('=')[1].split('@')[0]),
                     float(gap.split('=')[1])))
    return rows


def record_device(build, logs, effects, sound=None):
    """Writes every preview of `effects` (default: all) in the logs into the fixture, a line per round, with their
    current frame hashes; `sound` (the runs' Sound level, if not Off) is noted for the model's speaker share."""
    frames = {}
    for log in logs:
        for effect, rounds in device_frames(Path(log).read_text(errors='replace')).items():
            frames.setdefault(effect, []).extend(rounds)
    chosen = effects or list(frames)
    missing = [e for e in chosen if e not in frames]
    if missing:
        sys.exit(f'not in {", ".join(logs)}: {", ".join(missing)}')
    kept = {}
    header = []
    if DEVICE_FRAMES.exists():
        for line in DEVICE_FRAMES.read_text().splitlines():
            if line.startswith('#'):
                header.append(line)
            elif line:
                kept.setdefault(line.split()[0], []).append(line)
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as draft:
        draft.write(''.join(f'{e} 0 {frames[e][0]}\n' for e in chosen))
    hashes = {row[0]: row[1] for row in cost_rows(build, Path(draft.name))}
    os.unlink(draft.name)
    everything = set(kept) <= set(chosen)
    for e in chosen:
        kept[e] = [f'{e} {hashes[e]} {round_}' for round_ in frames[e]]
    log = ', '.join(logs)
    diverges = [h for h in header if h.startswith('# diverges ')]
    if not header or everything:
        header = ['# Device frame times from HIL effect_times (!effectframes): effect, the hash of its frames on',
                  '# ui_render\'s recap when recorded, then ms:us per frame. check_ui_render.py holds the cost model to them;',
                  '# re-record an effect after changing it: check_ui_render.py --record-device SERIAL_LOG[,SERIAL_LOG...] [--sound low] EFFECT...',
                  '# A line per round (preview); the check takes the median of the rounds\' slowest frames.']
    # A recording of every recorded effect replaces the earlier runs' notes;
    # the known divergences stay until the model stops missing them.
    header = [h for h in header if not h.startswith(('# sound: ', '# diverges '))]
    if sound:
        header.append(f'# sound: {sound}')
    header.append(f'# {len(chosen)} effects recorded from {log}' + (f': {", ".join(chosen)}' if len(chosen) < 6 else ''))
    header += diverges
    DEVICE_FRAMES.write_text('\n'.join(header + [line for lines in kept.values() for line in lines]) + '\n')
    print(f'Recorded {sum(len(frames[e]) for e in chosen)} previews of {len(chosen)} effects into {DEVICE_FRAMES.relative_to(ROOT)}')


def known_divergences(text):
    """Effect -> cause, from the fixture's '# diverges EFFECT: cause' lines."""
    found = (re.match(r'# diverges (\w+): (.+)', line) for line in text.splitlines())
    return {m.group(1): m.group(2) for m in found if m}


def by_effect(rows):
    """cost_rows (a row per recorded round) per effect: the device's slowest frame is the median of its rounds'
    (one round's worst frame swings about 0.6 ms), the model's the slowest of all, the gap the rounds' median."""
    rounds = {}
    for row in rows:
        rounds.setdefault(row[0], []).append(row)
    def median(values):
        values = sorted(values)
        middle = len(values) // 2
        return values[middle] if len(values) % 2 else (values[middle - 1] + values[middle]) / 2
    return [(name, rs[0][1], rs[0][2] if all(r[2] == rs[0][2] for r in rs) else '?', median([r[3] for r in rs]),
             max(r[4] for r in rs), median([r[5] for r in rs])) for name, rs in rounds.items()]


def cost_failures(rows, known=None):
    """(divergent effects, stale effects, known effects that diverge, known effects that no longer do) of cost_rows."""
    known = known or {}
    divergent, stale, still, cured = [], [], [], []
    for name, now, recorded, device, model, gap in by_effect(rows):
        if now != recorded:
            stale.append(name)
            continue
        off = gap > MEDIAN_SLACK_US or device - model > PEAK_UNDER_US or model - device > max(COST_SLACK_US, COST_SLACK * device)
        detail = f'{name} device {device:.0f} us, model {model:.0f} us, median gap {gap:.0f} us'
        if name in known:
            (still if off else cured).append(detail)
        elif off:
            divergent.append(detail)
    return divergent, stale, still, cured


def loaded_failures(lines):
    """The effects library's results that break a rule: a share code that does not give back the
    same file, a verdict other than a pass or the frame budget, or a pass the device disproved."""
    fields = {}
    for line in lines.splitlines():
        key, value = line.split(' ', 1)
        for what in ('file', 'code', 'reopened', 'check'):
            if key.endswith('-' + what):
                fields.setdefault(key[3:-len(what) - 1], {})[what] = value
    failures = []
    for name, f in sorted(fields.items()):
        if f.get('reopened') != f.get('file'):
            failures.append(f'{name}: its share code gives a different file')
        verdict = f.get('check', '')
        if not (verdict.startswith('ok ') or verdict.startswith('over the frame budget')):
            failures.append(f'{name}: {verdict}')
        if verdict.startswith('ok ') and RUN5_DEVICE_US.get(name, 0) > DEVICE_GUARD_US:
            failures.append(f'{name}: passes, but took {RUN5_DEVICE_US[name]} us on the device')
    if sorted(fields) != sorted(path.stem for path in IDIOM.glob('*.fx')):
        failures.append(f'results for {sorted(fields)}')
    return failures


def main():
    if '--record-device' in sys.argv:
        at = sys.argv.index('--record-device')
        with tempfile.TemporaryDirectory(prefix='launchlab-ui-render-') as directory:
            build = Path(os.environ.get('LAUNCHLAB_UI_RENDER_BUILD', directory))
            subprocess.run(['bash', str(ROOT / 'firmware/tools/ui_render/build.sh'), str(build)], check=True)
            logs = sys.argv[at + 1].split(',')
            rest = sys.argv[at + 2:]
            sound = None
            if '--sound' in rest:
                s = rest.index('--sound')
                sound = rest[s + 1]
                rest = rest[:s] + rest[s + 2:]
            record_device(build, logs, rest, sound)
        return 0
    update = '--update' in sys.argv
    with tempfile.TemporaryDirectory(prefix='launchlab-ui-render-') as directory:
        build = Path(os.environ.get('LAUNCHLAB_UI_RENDER_BUILD', directory))
        subprocess.run(['bash', str(ROOT / 'firmware/tools/ui_render/build.sh'), str(build)], check=True)
        frames = subprocess.run([str(build / 'ui_render')], check=True, capture_output=True, text=True).stdout
        frames += subprocess.run([str(build / 'ui_render'), *CUSTOM], check=True, capture_output=True, text=True).stdout
        effects = subprocess.run([str(build / 'ui_render'), '--celebrations'], check=True, capture_output=True, text=True).stdout
        effects += subprocess.run([str(build / 'ui_render'), '--celebrations', *CUSTOM], check=True, capture_output=True, text=True).stdout
        light = subprocess.run([str(build / 'ui_render'), *LIGHT], check=True, capture_output=True, text=True).stdout
        light += subprocess.run([str(build / 'ui_render'), '--celebrations', *LIGHT], check=True, capture_output=True, text=True).stdout
        legibility = ''.join(subprocess.run([str(build / 'ui_render'), '--legibility', *theme], check=True, capture_output=True,
                                            text=True).stdout for theme in ([], CUSTOM, LIGHT))
        settled = ''.join(subprocess.run([str(build / 'ui_render'), '--settled', *theme], check=True, capture_output=True,
                                         text=True).stdout for theme in ([], CUSTOM, LIGHT))
        mic = ''.join(subprocess.run([str(build / 'ui_render'), '--mic', *theme], check=True, capture_output=True,
                                     text=True).stdout for theme in ([], CUSTOM, LIGHT))
        costs = cost_rows(build, DEVICE_FRAMES)
        loaded = subprocess.run([str(build / 'ui_render'), '--fx', *map(str, sorted(IDIOM.glob('*.fx')))], check=True,
                                capture_output=True, text=True).stdout
    failed = 0
    broken = loaded_failures(loaded)
    if broken:
        print(f'FAIL: {len(broken)} effects library results break a rule: ' + '; '.join(broken))
        failed += 1
    unsettled = [line.split()[0] for line in settled.splitlines() if not line.endswith('settled=1')]
    if unsettled or not settled:
        print(f'FAIL: {len(unsettled)} frames after a celebration ends differ from the recap without it: ' + ', '.join(unsettled[:8]))
        failed += 1
    known = known_divergences(DEVICE_FRAMES.read_text())
    divergent, stale, still, cured = cost_failures(costs, known)
    if len(known) > KNOWN_DIVERGENCES_MAX:
        print(f'FAIL: {len(known)} known cost-model divergences listed in {DEVICE_FRAMES.relative_to(ROOT)}, more than the '
              f'{KNOWN_DIVERGENCES_MAX} allowed: the list may only shrink')
        failed += 1
    if cured:
        print(f'FAIL: listed as known divergences but now within bounds (remove their "# diverges" lines and lower '
              f'KNOWN_DIVERGENCES_MAX): ' + ', '.join(cured))
        failed += 1
    if still:
        print(f'NOTE: {len(still)} known cost-model divergences (fixture "# diverges" lines): ' + ', '.join(still))
    if divergent:
        print(f'FAIL: {len(divergent)} effects whose cost model is off their recorded device frames (median gap over '
              f'{MEDIAN_SLACK_US / 1000:g} ms, or the slowest frame over {PEAK_UNDER_US / 1000:g} ms above the model): ' + ', '.join(divergent))
        failed += 1
    if stale:
        print(f'NOTE: device frames are stale (the effect changed since recording) for: {", ".join(stale)}; '
              're-record with --record-device after a HIL effect_times run')
    illegible = legibility_failures(legibility)
    if illegible:
        print(f'FAIL: {len(illegible)} celebration frames change UI ink or hide the header: ' + ', '.join(illegible[:8]))
        failed += 1
    for fixture, rendered in ((FIXTURE, frames), (CELEBRATIONS, effects), (LIGHT_FIXTURE, light), (MIC_FIXTURE, mic)):
        if update:
            fixture.write_text(rendered)
            print(f'Wrote {len(rendered.splitlines())} frame hashes to {fixture.relative_to(ROOT)}')
            continue
        expected = dict(line.split() for line in fixture.read_text().splitlines())
        actual = dict(line.split() for line in rendered.splitlines())
        changed = sorted(name for name in expected if actual.get(name) != expected[name])
        extra = sorted(set(actual) - set(expected))
        if changed or extra:
            print(f'FAIL: {len(changed)} frames differ from {fixture.relative_to(ROOT)}, {len(extra)} new: '
                  + ', '.join((changed + extra)[:12]))
            failed += 1
    if update or failed:
        return int(bool(failed))
    print(f'PASS: {len(frames.splitlines())} screen frames (every screen x 10 built-in themes x both flips, and a custom theme), '
          f'{len(effects.splitlines())} celebration frames, {len(light.splitlines())} light-theme frames and '
          f'{len(mic.splitlines())} Mic RPM frames match the pinned renders; '
          f'{len(legibility.splitlines())} celebration frames (default, dark and light theme) keep the UI legible, and '
          f'{len(settled.splitlines())} frames from each effect\'s end through the next second equal the recap without it; '
          f'the cost model is at most {MEDIAN_SLACK_US / 1000:g} ms (median frame) and {PEAK_UNDER_US / 1000:g} ms (slowest frame) '
          f'below the device for {len(by_effect(costs)) - len(stale) - len(still)} recorded effects; '
          f'{len(list(IDIOM.glob("*.fx")))} idiomatic effects round-trip through share codes, keep the UI rules on the real recaps '
          f'and pass the frame budget only where the device kept it')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
