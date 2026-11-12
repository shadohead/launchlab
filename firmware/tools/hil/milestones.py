"""RPM milestone requests for the USB `U` transfer and `M` list (milestones.h).

A request is magic "LLU\\3" (v3, 0.18), "LLU\\2" (v2, 0.16 and 0.17) or "LLU\\1"
(v1, 0.15), op (1 set, 2 reset), three zero bytes, the body (mode, row count,
two zero bytes, rows of RPM (LE) and then, in v3, a 128-bit effect pool (LE,
low word first), in v2 a 64-bit pool (LE) or, in v1, one effect id; unused
rows zero; all zero for reset) and FNV-1a over the rest, sent as one base64
line and a "." line. v3 and v2 have twelve rows, v1 eight.
firmware/tests/check_hil_milestones.py checks these encoders and the
celebration rule against the firmware's own vectors
(firmware/tests/fixtures/milestone_vectors.json).
"""
import base64
import struct

MODES = ('best', 'milestones', 'both')
# A unit with no saved mode (Milestones::DEFAULT_MODE).
DEFAULT_MODE = 'both'
EFFECTS = ('Orbit', 'Flames', 'Sparkles', 'Shockwave', 'Lightning', 'Off', 'Random', 'Slash',
           'Tidal', 'Frost', 'Gale', 'Eclipse', 'Radiance', 'Spin', 'Storm', 'Quickdraw', 'Leaves', 'Bloom',
           'Crimson', 'Bulwark', 'Crux', 'Klaxon', 'Prism', 'Sunwheel', 'Torrent', 'Blitz', 'Tigerclaw', 'Butterfly', 'Birdsong', 'Spiral', 'Pinwheel', 'Overdrive', 'Summon', 'Breach', 'Bladework', 'Moonpetal', 'Barrage', 'Glitch', 'Rupture', 'Boundless', 'Precision', 'Umbra', 'Ravage',
           'Haymaker', 'Palms', 'Elastic', 'Chains', 'Waterleaf', 'Transmute', 'Ignition', 'Spires', 'Gate', 'Fracture',
           'Timestop', 'Zipper', 'Rush', 'Menace', 'Sequel', 'Swordland', 'Treasury', 'Sunblade', 'Aegis', 'Trace',
           'Volley', 'Starfall', 'Mimic', 'Scales', 'Meadow', 'Rev', 'Hammer', 'Foxbite', 'Detonate', 'Sharkfin',
           'Slingshot', 'Presence', 'Threefold', 'Banner', 'Vapor', 'Crescent', 'Release', 'Petalfall', 'Mask', 'Blink')
DRAWN = tuple(i for i in range(len(EFFECTS)) if EFFECTS[i] not in ('Off', 'Random'))
MAX_ROWS, V1_ROWS = 12, 8
REQUEST_MAGIC, V2_REQUEST_MAGIC, V1_REQUEST_MAGIC = 0x03554c4c, 0x02554c4c, 0x01554c4c
ROW_BYTES = {1: 3, 2: 10, 3: 18}
DEFAULTS = [(9000, 'Sparkles'), (10000, 'Sparkles'), (11000, 'Orbit'), (12000, 'Flames'),
            (13000, 'Shockwave'), (14000, 'Lightning'), (15000, 'Slash')]


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def effect_id(effect):
    return EFFECTS.index(effect) if isinstance(effect, str) else effect


def pool(effect):
    """A row's effect: one name or id, or a list/tuple of them (a pool)."""
    effects = effect if isinstance(effect, (list, tuple)) else [effect]
    mask = 0
    for e in effects:
        mask |= 1 << effect_id(e)
    return mask


def pool_names(effect):
    """The pool as a tuple of names, sorted by id."""
    mask = pool(effect)
    return tuple(EFFECTS[i] if i < len(EFFECTS) else str(i) for i in range(128) if mask >> i & 1)


def body_bytes(version):
    return 4 + ROW_BYTES[version] * (V1_ROWS if version == 1 else MAX_ROWS)


def body(mode, rows, version=3):
    """mode: a MODES name or id; rows: [(rpm, effect or pool)], up to twelve (v1: eight single effects)."""
    mode = MODES.index(mode) if isinstance(mode, str) else mode
    out = bytearray(body_bytes(version))
    out[0], out[1] = mode, len(rows)
    # A table longer than the frame (a refusal test) sends its count and the rows that fit.
    for i, (rpm, effect) in enumerate(rows[:V1_ROWS if version == 1 else MAX_ROWS]):
        at = 4 + ROW_BYTES[version] * i
        if version == 1:
            struct.pack_into('<HB', out, at, rpm, effect_id(effect if not isinstance(effect, (list, tuple)) else effect[0]))
        elif version == 2:
            struct.pack_into('<HQ', out, at, rpm, pool(effect))
        else:
            value = pool(effect)
            struct.pack_into('<HQQ', out, at, rpm, value & (1 << 64) - 1, value >> 64)
    return bytes(out)


def request(op, mode='best', rows=(), version=3):
    magic = {1: V1_REQUEST_MAGIC, 2: V2_REQUEST_MAGIC, 3: REQUEST_MAGIC}[version]
    head = struct.pack('<IBBBB', magic, 1 if op == 'set' else 2, 0, 0, 0)
    head += body(mode, rows, version) if op == 'set' else bytes(body_bytes(version))
    return head + struct.pack('<I', fnv1a(head))


def request_line(*args, **kwargs):
    return base64.b64encode(request(*args, **kwargs)).decode()


def parse_list(lines):
    """MILESTONE_LIST/MILESTONE_ROW lines -> (list fields, [(rpm, effect name or pool tuple)]);
    each row's `next` pick is in header['next'] by index."""
    header, rows, picks = {}, [], []
    for text in lines:
        if text.startswith('MILESTONE_LIST '):
            header = dict(part.split('=', 1) for part in text.split()[1:] if '=' in part)
        elif text.startswith('MILESTONE_ROW '):
            fields = dict(part.split('=', 1) for part in text.split()[1:] if '=' in part)
            effects = tuple(fields['effects'].split('+')) if 'effects' in fields else (fields['effect'],)
            rows.append((int(fields['rpm']), effects[0] if len(effects) == 1 else effects))
            picks.append(fields.get('next'))
    header['next'] = picks
    return header, rows


def shown(rpm):
    """The RPM the recap prints ("%.0f": round half to even, like Python's round)."""
    return round(rpm)


def decide(mode, rows, valid, rpm, best, best_effect):
    """milestones.h decide(): (show, milestone, effect name, or the row's pool tuple when it has several)."""
    mode = MODES.index(mode) if isinstance(mode, str) else mode
    hit = None
    if valid and mode in (1, 2):
        for threshold, effect in rows:
            if shown(rpm) >= threshold:
                hit = (threshold, effect)
    if hit:
        names = pool_names(hit[1])
        return True, hit[0], names[0] if len(names) == 1 else names
    if best:
        return True, 0, 'Off' if mode == 1 else best_effect
    return False, 0, 'Off'


# ---- The effect set (effect_set.h): USB `U` op 3 and the `M` fields.
# A set_effects request is 0.15's 40-byte v1 frame with op 3, the 128-bit mask
# of offered effect IDs (bit n = stored effect value n, LE, low word first) in
# body bytes 0-15 and the other 12 body bytes zero. check_hil_milestones checks these against
# fixtures/effect_set_vectors.json.
EFFECT_SET_OP = 3
DEFAULT_EFFECT_SET = frozenset(i for i in DRAWN if i < 18)  # the 16 effects of 0.16


def effect_mask(effects):
    """Effect names or IDs -> the 128-bit mask."""
    mask = 0
    for effect in effects:
        mask |= 1 << effect_id(effect)
    return mask


def effect_ids(mask):
    """A mask (int or 32 hex digits, as `M` prints it) -> the set of IDs."""
    mask = int(mask, 16) if isinstance(mask, str) else mask
    return frozenset(i for i in range(128) if mask >> i & 1)


def effect_set_request(effects=None, mask=None):
    head = struct.pack('<IBBBB', V1_REQUEST_MAGIC, EFFECT_SET_OP, 0, 0, 0)
    value = effect_mask(effects) if mask is None else mask
    head += struct.pack('<QQ', value & (1 << 64) - 1, value >> 64) + bytes(4 + 3 * V1_ROWS - 16)
    return head + struct.pack('<I', fnv1a(head))


def effect_set_line(effects=None, mask=None):
    return base64.b64encode(effect_set_request(effects, mask)).decode()


def next_choice(effect, offered):
    """effect_set.h next(): Settings › Best effect's next choice (IDs)."""
    effect, random, off = effect_id(effect), EFFECTS.index('Random'), EFFECTS.index('Off')
    if effect != random and effect not in DRAWN:
        return random
    start = 0 if effect == random else DRAWN.index(effect) + 1
    return next((e for e in DRAWN[start:] if e in offered), off)
