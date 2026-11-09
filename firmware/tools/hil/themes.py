"""Custom theme requests for the USB `T` transfer and `L` list (theme_library.h).

A request is magic "LLQ\\1", op, argument, two zero bytes, a 32-byte slot record
(Put only) and FNV-1a over the rest, sent as one base64 line and a "." line.
firmware/tests/check_hil_themes.py checks these encoders against the
firmware's own vectors (firmware/tests/fixtures/theme_vectors.json).
"""
import base64
from pathlib import Path
import re
import struct

ROLES = ('background', 'text', 'muted', 'accent', 'number', 'warning', 'ready', 'alert')
OPS = {'put': 1, 'delete': 2, 'select': 3, 'clear': 4}
BUILT_INS = 10
REQUEST_MAGIC = 0x01514c4c

# palette.h's integer sRGB tables, so the verdicts match the device's.
_PALETTE = (Path(__file__).resolve().parents[2] / 'LaunchLabMini/palette.h').read_text()
LINEAR5, LINEAR6 = ([int(v) for v in re.search(name + r'\[\d+\]=\{([^}]*)\}', _PALETTE)[1].split(',')]
                    for name in ('LINEAR5', 'LINEAR6'))
TEXT_CONTRAST100, MUTED_CONTRAST100, INVISIBLE_CONTRAST100, LIGHT_BACKGROUND_LUMINANCE = 450, 300, 150, 50000


def luminance(c):
    return (2126 * LINEAR5[c >> 11] + 7152 * LINEAR6[(c >> 5) & 63] + 722 * LINEAR5[c & 31] + 5000) // 10000


def contrast100(a, b):
    la, lb = luminance(a) + 50000, luminance(b) + 50000
    return la * 100 // lb if la >= lb else lb * 100 // la


def problem(name):
    """palette.h Custom::check(): only the name can be refused."""
    ok = 1 <= len(name) <= 14 and re.fullmatch(r'[\x20-\x7e]+', name) and name[0] != ' ' and name[-1] != ' '
    return 'none' if ok else 'name'


def warnings(roles):
    """palette.h Advice::format(): the warning list a put reply carries."""
    out = ['light_background'] if luminance(roles[0]) > LIGHT_BACKGROUND_LUMINANCE else []
    for r in range(1, 8):
        c = contrast100(roles[r], roles[0])
        if c < INVISIBLE_CONTRAST100:
            out.append(f'invisible:{ROLES[r]}')
        elif c < (MUTED_CONTRAST100 if r == 2 else TEXT_CONTRAST100):
            out.append(f'low_contrast:{ROLES[r]}')
    return ','.join(out) or 'none'


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def slot_record(name, roles):
    """The 32-byte slot: used flag, NUL-padded name (14 chars max), roles LE."""
    raw = name.encode('ascii')
    if len(raw) > 14:
        raise ValueError('name longer than 14 characters')
    return bytes([1]) + raw.ljust(15, b'\0') + struct.pack('<8H', *roles)


def request(op, argument=0, name=None, roles=None):
    body = slot_record(name, roles) if op == 'put' else bytes(32)
    head = struct.pack('<IBBBB', REQUEST_MAGIC, OPS[op], argument, 0, 0) + body
    return head + struct.pack('<I', fnv1a(head))


def request_line(*args, **kwargs):
    return base64.b64encode(request(*args, **kwargs)).decode()


def parse_list(lines):
    """THEME_LIST/THEME_SLOT lines -> (list fields, {slot: {state, name, theme}})."""
    header, slots = {}, {}
    for text in lines:
        if text.startswith('THEME_LIST '):
            header = dict(part.split('=', 1) for part in text.split()[1:] if '=' in part)
        elif text.startswith('THEME_SLOT '):
            head, _, name = text.partition(' name=')
            fields = dict(part.split('=', 1) for part in head.split()[1:] if '=' in part)
            fields['name'] = name
            slots[int(fields['slot'])] = fields
    return header, slots
