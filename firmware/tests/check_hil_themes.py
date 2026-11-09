#!/usr/bin/env python3
"""The HIL rig's custom theme scenario, dry-run against a fake M5.

firmware/tools/hil/themes.py encodes the USB `T` requests the `themes`
scenario sends; its bytes must equal the firmware's own encoder
(fixtures/theme_vectors.json, checked by test_palette). Then the real
`themes` scenario and Rig run against a fake M5 that models the firmware's
`L`/`T` protocol, name validation and color warnings (palette.h's own
tables), the stored record,
`!reboot`, `!themecorrupt`, `!set`, status and `v` frames, so a scenario that
cannot pass on a correct device fails here, before it costs a device run.
"""
import base64
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import threading
import time
import types

ROOT = Path(__file__).resolve().parents[2]
sys.modules.setdefault('serial', types.SimpleNamespace(Serial=object, SerialException=Exception))
sys.path.insert(0, str(ROOT / 'firmware/tools/hil'))
import hil_device  # noqa: E402
import rig as hil_rig  # noqa: E402
import scenarios  # noqa: E402
import themes  # noqa: E402

ACCENTS = [0x07ff, 0x8ff9, 0xff10, 0xc5bf, 0x77e6, 0xfa8a, 0x5c7f, 0xfb9f, 0xffff, 0xfd00]


def vectors_match():
    vectors = json.loads((ROOT / 'firmware/tests/fixtures/theme_vectors.json').read_text())
    assert list(themes.ROLES) == vectors['roles']
    assert themes.BUILT_INS == vectors['rules']['builtIns']
    for color, expected in vectors['luminance']:
        assert themes.luminance(color) == expected, color
    for a, b, expected in vectors['contrast']:
        assert themes.contrast100(a, b) == expected, (a, b)
    rules = vectors['rules']
    assert (themes.TEXT_CONTRAST100, themes.MUTED_CONTRAST100, themes.INVISIBLE_CONTRAST100, themes.LIGHT_BACKGROUND_LUMINANCE) == \
        (rules['text'], rules['muted'], rules['invisible'], rules['lightBackgroundLuminance'])
    for t in vectors['themes']:
        assert themes.problem(t['name']) == t['problem'], t['name']
        assert themes.warnings(t['roles']) == t['warnings'], t['name']
        if len(t['name'].encode()) <= 14:
            assert base64.b64encode(themes.slot_record(t['name'], t['roles'])).decode() == t['slot'], t['name']
    for r in vectors['requests']:
        theme = vectors['themes'][r['theme']] if r['theme'] is not None else None
        line = themes.request_line(r['op'], r['argument'], theme and theme['name'], theme and theme['roles'])
        assert line == r['base64'], r
    header, slots = themes.parse_list([
        'THEME_LIST slots=4 selected=11 showing=0 store=unreadable revision=0 nvs_available=150 nvs_needed=95',
        'THEME_SLOT slot=0 theme=10 state=empty data=AAAA name=',
        'THEME_SLOT slot=1 theme=11 state=valid data=AQ== name=HIL Navy Two',
    ])
    assert header['store'] == 'unreadable' and header['showing'] == '0'
    assert slots[0]['name'] == '' and slots[1]['name'] == 'HIL Navy Two' and slots[1]['state'] == 'valid'
    return len(vectors['requests'])


class FakeM5:
    """The firmware's theme behaviour at the USB boundary."""

    def __init__(self):
        self.lock = threading.Lock()
        self.out, self.frames = [], []
        self.nvs = None             # the stored themes record (bytes) or None
        self.selected, self.boot = 0, 1
        self.load()
        self.pending, self.hil_line, self.rx = b'', None, None

    # ---- store
    def load(self):
        self.slots, self.unreadable = [bytes(32)] * 4, False
        if self.nvs is None:
            return
        b = self.nvs
        if len(b) != 140 or struct.unpack_from('<I', b, 0)[0] != 0x01544c4c or \
                struct.unpack_from('<I', b, 136)[0] != themes.fnv1a(b[:136]):
            self.unreadable = True
            return
        self.slots = [b[8 + 32 * i:40 + 32 * i] for i in range(4)]

    def save(self):
        body = struct.pack('<II', 0x01544c4c, 1) + b''.join(self.slots)
        self.nvs = body + struct.pack('<I', themes.fnv1a(body))
        self.out.append('HIL_NVS key=themes bytes=140')

    def valid(self, slot):
        raw = self.slots[slot]
        if raw[0] != 1:
            return False
        name = raw[1:16].split(b'\0')[0].decode(errors='replace')
        return themes.problem(name) == 'none'

    def available(self, theme):
        return theme < 10 or (theme < 14 and self.valid(theme - 10))

    def showing(self):
        return self.selected if self.available(self.selected) else 0

    def colors(self):
        theme = self.showing()
        if theme < 10:
            return 0x0000, 0xffff if theme == 0 else ACCENTS[theme], ACCENTS[theme]
        roles = struct.unpack_from('<8H', self.slots[theme - 10], 16)
        return roles[0], roles[4], roles[3]

    # ---- bytes
    def feed(self, data):
        with self.lock:
            for byte in data:
                c = bytes([byte])
                if self.rx is not None:
                    self.receive(c)
                elif self.hil_line is not None:
                    if c == b'\n':
                        self.hil(self.hil_line.decode())
                        self.hil_line = None
                    else:
                        self.hil_line += c
                elif c == b'!':
                    self.hil_line = b''
                elif self.pending and c != b'\n':
                    self.pending += c
                elif c == b'p':
                    self.status()
                elif c == b'v':
                    self.frame()
                elif c in b'LTE' and not self.pending:
                    self.pending = c
                elif c == b'\n' and self.pending:
                    command, self.pending = self.pending, b''
                    if command == b'L':
                        self.list()
                    elif command[:1] == b'E':
                        effect = {b'E0': 'Orbit', b'E7': 'Slash'}[command]
                        self.out.append(f'EFFECT_PREVIEW effect={effect} duration_ms=1200 launch=1 sound_render_us=0 bests_unchanged=1')
                    else:
                        self.rx = b''
                        self.out.append('THEME_READY max_chars=60')

    def receive(self, c):
        if c == b'\n':
            if self.rx == b'.':
                text, self.rx = self.text, None
                self.request(text)
            elif self.rx:
                self.text = self.rx
                self.out.append(f'THEME_CHUNK chars={len(self.rx)}')
                self.rx = b''
            return
        self.rx += c

    def request(self, text):
        b = base64.b64decode(text)
        if len(b) != 44 or struct.unpack_from('<I', b, 0)[0] != 0x01514c4c or \
                struct.unpack_from('<I', b, 40)[0] != themes.fnv1a(b[:40]) or b[6] or b[7] or not 1 <= b[4] <= 4:
            self.out.append('THEME_ERROR reason=invalid_request written=0')
            return
        op, arg = {1: 'put', 2: 'delete', 3: 'select', 4: 'clear'}[b[4]], b[5]
        if op == 'select':
            if not self.available(arg):
                self.out.append(f'THEME_ERROR op=select reason=unavailable theme={arg} written=0')
            else:
                self.selected = arg
                self.out.append(f'THEME_OK op=select theme={arg} pending=0')
            return
        if op != 'clear' and arg >= 4:
            self.out.append(f'THEME_ERROR op={op} slot={arg} reason=bad_slot written=0')
            return
        warnings = 'none'
        if op == 'put':
            raw = b[8:40]
            if themes.problem(raw[1:16].split(b'\0')[0].decode(errors='replace')) != 'none':
                self.out.append(f'THEME_ERROR op=put slot={arg} reason=name written=0')
                return
            warnings = themes.warnings(struct.unpack_from('<8H', raw, 16))
        if op != 'clear' and self.unreadable:
            self.out.append(f'THEME_ERROR op={op} slot={arg} reason=store_unreadable written=0')
            return
        if op == 'clear':
            self.nvs, self.slots, self.unreadable = None, [bytes(32)] * 4, False
        else:
            self.slots[arg] = b[8:40] if op == 'put' else bytes(32)
            self.save()
        if not self.available(self.selected):
            self.selected = 0
        state = 'valid' if op == 'put' else 'empty'
        self.out.append(f'THEME_OK op={op} slot={arg} revision=1 state={state} warnings={warnings}')

    def list(self):
        store = 'unreadable' if self.unreadable else 'empty' if self.nvs is None else 'ok'
        revision = 1 if self.nvs else 0
        self.out.append(f'THEME_LIST slots=4 selected={self.selected} showing={self.showing()} store={store} '
                        f'revision={revision} nvs_available=150 nvs_needed=95 colors=any')
        for i, raw in enumerate(self.slots):
            state = 'empty' if not any(raw) else 'valid' if self.valid(i) else 'invalid'
            name = raw[1:16].split(b'\0')[0].decode() if state == 'valid' else ''
            self.out.append(f'THEME_SLOT slot={i} theme={10 + i} state={state} data={base64.b64encode(raw).decode()} name={name}')
        self.out.append('THEME_LIST_END')

    def hil(self, text):
        word, *args = text.split()
        if word == 'hil':
            self.out += [f'HIL_STATUS boot_id={self.boot:08x} reset=poweron commands=1', 'HIL_IMU mode=const', 'HIL_FAULTS none']
        elif word == 'reboot':
            self.out.append('HIL_REBOOT')
            self.boot += 1
            self.load()
            self.out.append(f'HIL_BOOT boot_id={self.boot:08x} reset=sw runtime=0.13.9-sticks3-rpm-guard-hil-pm')
            if self.unreadable:
                self.out.append('THEME_NOTICE invalid_record_retained_using_default_colors')
        elif word == 'themecorrupt':
            self.nvs = bytes([0x5a]) * 140
            self.out += ['HIL_NVS key=themes bytes=140', 'HIL_OK themecorrupt ok=1']
        elif word == 'set':
            row, value = int(args[0]), int(args[1])
            error = int(row == 5 and not self.available(value))
            if not error and row == 5:
                self.selected = value
            self.out.append(f'HIL_SET row={row} value={value} error={error} page={6 if error else 5} ms=3')
        else:
            self.out.append(f'HIL_OK {word}')

    def status(self):
        self.out += ['STATUS v=0.13.9-sticks3-rpm-guard-hil-pm', 'SETTINGS_SAVE_STATUS pending=0x000 failed=0',
                     'RPM_STATUS state=READY', 'MOTION_STATUS pending=0', 'ORIENTATION_STATUS ready=1',
                     'HISTORY_STATUS pending=0', 'MOTION_CLIP ok=1']

    def frame(self):
        background, number, accent = self.colors()
        pixels = [background] * (135 * 240)
        for y in range(60, 85):
            for x in range(30, 100):
                pixels[y * 135 + x] = number
        for y in range(10, 20):  # a highlight
            for x in range(40, 90):
                pixels[y * 135 + x] = accent
        self.frames.append(hil_device.Snapshot(135, 240, struct.pack('>32400H', *pixels)))
        self.out.append('SNAP 135 240 64800')


class Device(hil_device.Device):
    """The real Device's line handling, fed by the fake instead of a port."""

    def __init__(self, fake):
        self.lines, self.snapshots = [], []
        self.cond = threading.Condition()
        self.log, self.echo, self.port, self.disconnects = None, None, None, 0
        self._write_lock = threading.Lock()
        self.fake, self._stop = fake, False
        threading.Thread(target=self._pump, daemon=True).start()

    def write(self, data):
        self.fake.feed(data.encode() if isinstance(data, str) else data)

    def _pump(self):
        while not self._stop:
            with self.fake.lock:
                lines, self.fake.out = self.fake.out, []
                frames, self.fake.frames = self.fake.frames, []
            with self.cond:
                for text in lines:
                    self._record(text)
                self.snapshots += frames
                self.cond.notify_all()
            time.sleep(0.002)


def dry_run():
    scenarios.settle = lambda rig, seconds=0: rig.wait_ready(5)  # no optical/IMU model here
    time_sleep = scenarios.time.sleep
    scenarios.time.sleep = lambda s: time_sleep(min(s, 0.01))
    try:
        with tempfile.TemporaryDirectory(prefix='launchlab-hil-themes-') as out:
            fake = FakeM5()
            dev = Device(fake)
            rig = hil_rig.Rig(dev, Path(out))
            rig.boot_id = rig.hil_boot().get('boot_id')
            rig.run('themes', scenarios.custom_themes)
            dev._stop = True
            result = rig.results[-1]
            failed = [c for c in result['checks'] if not c['ok']]
            assert not failed and result['passed'], failed or result.get('traceback')
            screens = len(result.get('screens', []))
            assert screens == 8, screens
            return len(result['checks'])
    finally:
        scenarios.time.sleep = time_sleep


def main():
    requests = vectors_match()
    checks = dry_run()
    print(f"PASS: HIL theme requests byte-identical to the firmware's {requests} request vectors; "
          f'themes scenario dry run: {checks} checks pass against a fake M5')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
