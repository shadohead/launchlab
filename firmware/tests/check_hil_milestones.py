#!/usr/bin/env python3
"""The HIL rig's milestone encoder and celebration rule against the firmware.

firmware/tools/hil/milestones.py encodes the USB `U` requests the
`milestones` scenario sends and re-implements the rule it checks each pull
against. Its request bytes, list parsing and decisions must equal the
firmware's own (fixtures/milestone_vectors.json, checked by test_milestones),
so a scenario that cannot pass on a correct device fails here first.
"""
import base64
import json
from pathlib import Path
import sys
import types

ROOT = Path(__file__).resolve().parents[2]
sys.modules.setdefault('serial', types.SimpleNamespace(Serial=object, SerialException=Exception))
sys.path.insert(0, str(ROOT / 'firmware/tools/hil'))
import milestones  # noqa: E402
import scenarios  # noqa: E402,F401  (the scenario module imports cleanly)


def main():
    v = json.loads((ROOT / 'firmware/tests/fixtures/milestone_vectors.json').read_text())
    assert v['maxRows'] == milestones.MAX_ROWS and v['requestBytes'] == len(milestones.request('reset'))
    assert [m['name'] for m in v['modes']] == list(milestones.MODES)
    assert [e['label'] for e in v['effects']] == list(milestones.EFFECTS)
    assert [e['id'] for e in v['effects'] if e['drawn']] == list(milestones.DRAWN)
    assert v['schema'] == 3 and v['v1MaxRows'] == milestones.V1_ROWS
    assert v['v2RequestBytes'] == len(milestones.request('reset', version=2)) and v['v1RequestBytes'] == len(milestones.request('reset', version=1))
    defaults = [(r['rpm'], milestones.EFFECTS[r['effects'][0]]) for r in v['defaults']['rows']]
    assert defaults == milestones.DEFAULTS and milestones.MODES[v['defaults']['mode']] == milestones.DEFAULT_MODE
    requests = 0
    for t in v['tables']:
        if t['request'] is None:
            continue
        rows = [(r['rpm'], r['effects']) for r in t['rows']]
        assert milestones.request_line('set', t['mode'], rows) == t['request'], t
        if t['requestV2'] is not None:
            assert milestones.request_line('set', t['mode'], rows, version=2) == t['requestV2'], t
        if t['requestV1'] is not None:
            assert milestones.request_line('set', t['mode'], rows, version=1) == t['requestV1'], t
        requests += 1
    assert milestones.request_line('reset') == v['resetRequest'] and milestones.request_line('reset', version=1) == v['resetRequestV1']
    assert milestones.request_line('reset', version=2) == v['resetRequestV2']
    effects = {e['id']: e['label'] for e in v['effects']}
    for d in v['decisions']:
        show, milestone, effect = milestones.decide(d['mode'], milestones.DEFAULTS, d['valid'], milestones.shown(d['rpm']),
                                                    d['best'], 'Orbit')
        assert (show, milestone, effect if show else 'Off') == (d['show'], d['milestone'], effects[d['effect']] if d['show'] else 'Off'), d
    header, rows = milestones.parse_list([
        'MILESTONE_LIST version=3 mode=both rows=3 max_rows=12 min_rpm=1000 max_rpm=30000 effects=83 revision=3 store=damaged default=0 '
        'nvs_available=150 nvs_needed=65 data=TExNAw==',
        'MILESTONE_ROW index=0 rpm=9000 effect=Sparkles effect_id=2 pool=00000000000000000000000000000004 effects=Sparkles next=Sparkles',
        'MILESTONE_ROW index=1 rpm=12000 effect=Tidal effect_id=8 pool=00000000000000000000000000020300 effects=Tidal+Frost+Bloom next=Frost',
        'MILESTONE_ROW index=2 rpm=15000 effect=Slash effect_id=7 pool=00000000000000000000000000000080 effects=Slash next=Slash',
        'MILESTONE_LIST_END'])
    assert header['mode'] == 'both' and header['store'] == 'damaged' and rows == [(9000, 'Sparkles'), (12000, ('Tidal', 'Frost', 'Bloom')), (15000, 'Slash')]
    assert header['next'] == ['Sparkles', 'Frost', 'Slash'] and milestones.pool(rows[1][1]) == 0x20300
    assert milestones.decide('milestones', rows, True, 12400, False, 'Orbit') == (True, 12000, ('Tidal', 'Frost', 'Bloom'))
    # A pool past value 63 (0.18) names its effects by label.
    _, wide = milestones.parse_list(['MILESTONE_ROW index=0 rpm=9000 effect=Haymaker effect_id=43 pool=00000000000001100000080000000000 effects=Haymaker+Rev+Sharkfin next=Rev'])
    assert wide == [(9000, ('Haymaker', 'Rev', 'Sharkfin'))] and milestones.pool(wide[0][1]) == 1 << 43 | 1 << 68 | 1 << 72
    # 0.15's list has no effects= or next=: one effect per row.
    _, old = milestones.parse_list(['MILESTONE_LIST mode=best rows=1', 'MILESTONE_ROW index=0 rpm=9000 effect=Orbit effect_id=0'])
    assert old == [(9000, 'Orbit')]
    assert base64.b64decode(milestones.request_line('reset'))[4] == 2
    # The scenario's thirteenth-row refusal: a full frame whose count says 13.
    ladder = [(9000 + 500 * i, 'Slash') for i in range(milestones.MAX_ROWS + 1)]
    over = milestones.request('set', 'milestones', ladder)
    assert len(over) == len(milestones.request('reset')) and over[9] == milestones.MAX_ROWS + 1
    # The listing check of a pool row compares the RPM too.
    assert rows[1] == (12000, ('Tidal', 'Frost', 'Bloom'))
    # The effect set (effect_set.h): requests, the default and the editor's cycle.
    e = json.loads((ROOT / 'firmware/tests/fixtures/effect_set_vectors.json').read_text())
    assert [x['label'] for x in e['effects']] == list(milestones.EFFECTS)
    assert milestones.effect_ids(e['default']) == milestones.DEFAULT_EFFECT_SET
    assert {x['id'] for x in e['effects'] if x['default']} == milestones.DEFAULT_EFFECT_SET
    assert milestones.effect_ids(e['all']) == frozenset(milestones.DRAWN) and e['requestOp'] == milestones.EFFECT_SET_OP
    sets = 0
    for x in e['sets']:
        assert milestones.effect_ids(x['mask']) == frozenset(x['ids']) and milestones.effect_mask(x['ids']) == int(x['mask'], 16)
        assert milestones.effect_set_line(mask=int(x['mask'], 16)) == (x['request'] or milestones.effect_set_line(x['ids']))
        if x['request'] is None:
            continue
        sets += 1
        offered = frozenset(x['ids'])
        cycle, effect = [], milestones.EFFECTS.index('Random')
        while True:
            effect = milestones.next_choice(effect, offered)
            cycle.append(effect)
            if effect == milestones.EFFECTS.index('Random'):
                break
        assert cycle == x['cycle'], x
        assert scenarios.expected_cycle(milestones.EFFECTS.index('Random'), offered, len(cycle)) == cycle
    for x in e['next']:
        assert milestones.next_choice(x['from'], milestones.effect_ids(x['mask'])) == x['next'], x
    header, _ = milestones.parse_list([
        'MILESTONE_LIST mode=best rows=7 max_rows=8 min_rpm=1000 max_rpm=30000 effects=43 revision=0 store=ok default=1 '
        'nvs_available=150 nvs_needed=91 data=TExNAQ== effect_set=00000000000000010000040000040180 effect_set_store=damaged effect_set_default=0'])
    assert milestones.effect_ids(header['effect_set']) == {7, 8, 18, 42, 64} and header['effect_set_store'] == 'damaged'
    assert header['data'] == 'TExNAQ=='
    print(f"PASS: HIL milestone requests byte-identical to the firmware's {requests} table vectors and reset; "
          f"{len(v['decisions'])} celebration decisions match the firmware's rule; {sets} effect set requests and cycles match")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
