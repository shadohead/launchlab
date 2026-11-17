#!/usr/bin/env python3
"""USB `X` on a production image, as a website or a careless host sends it.

Production builds keep the default ~256-byte USB receive buffer (HIL builds
enlarge it), so a host that writes faster than the sketch reads loses bytes.
On a production image this:
 1. installs a real-size effect (Waterleaf, the largest idiomatic file, with
    its packed source) the way the website does: one 96-character line per
    FX_CHUNK; it must install and list as ok;
 2. sends the same request as one burst, every line at once without waiting;
    the unit must refuse it (FX_ERROR) or install it whole, never store a
    damaged effect, and keep answering;
 3. sends one 300-character line of nonsense, which must be refused whole;
 4. clears the store again.

  python firmware/tools/hil/fx_burst_check.py --port /dev/cu.usbmodemXXXX --out DIR
"""
import argparse
import base64
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device  # noqa: E402
from port_guard import require_free  # noqa: E402
import fx_client  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, detail=''):
        checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)})
        print(('PASS ' if ok else 'FAIL ') + name + (f': {detail}' if detail and not ok else ''))

    require_free(a.port)
    d = Device(a.port, a.out / 'serial.log')
    time.sleep(3)
    source_text = (ROOT / 'tools/fx/effects/idiom/waterleaf.fx').read_text()
    file = fx_client.compile_text(source_text)
    packed = source_text.encode()[:4000]  # stands in for the packed source: the length is what matters here
    check('the effect is real-size', len(file) > 3000, len(file))
    reply = fx_client.clear(d)
    check('clear before', reply.text.startswith('FX_OK'), reply.text)

    # 1. Paced, as the website sends it.
    start = time.time()
    reply = fx_client.put(d, file, packed, timeout=60)
    check('paced: installs', reply.text.startswith('FX_OK') and 'slot' in reply.f, f'{reply.text} in {time.time() - start:.1f} s')
    # A slot a deleted effect left keeps its fallback, so the install may go past slot 0.
    slot = int(reply.f.get('slot', 0))
    store, slots = fx_client.listing(d)
    check('paced: listed as ok, whole', slots.get(slot, {}).get('state') == 'ok' and slots[slot].get('bytes') == str(len(file)), slots.get(slot))

    # 2. The same request in one burst.
    frame = fx_client.frame(fx_client.PUT, 0, file, packed)
    text = base64.b64encode(frame).decode()
    lines = ''.join(text[i:i + 96] + '\n' for i in range(0, len(text), 96))
    time.sleep(0.05)
    mark = d.mark()
    d.write('X\n')
    ready = d.wait('FX_READY', 5, since=mark)
    check('burst: FX_READY', ready is not None)
    mark = d.mark()
    d.write(lines + '.\n')
    end = d.wait(lambda l: l.text.startswith(('FX_OK', 'FX_ERROR')), 30, since=mark)
    burst = end.text if end else 'none'
    check('burst: answered (refused or installed)', end is not None, burst)
    time.sleep(1)
    store, slots = fx_client.listing(d)
    states = {s: f.get('state') for s, f in slots.items()}
    check('burst: nothing damaged stored', all(v in ('ok', 'deleted') for v in states.values()) and store.get('installed') in ('1',), (store, states))
    reply = fx_client.put(d, file, packed, timeout=60)
    check('after the burst: a paced install still works', reply.text.startswith('FX_OK'), reply.text)

    # 3. One line longer than the production receive buffer, not a request at all.
    time.sleep(0.05)
    mark = d.mark()
    d.write('X\n')
    d.wait('FX_READY', 5, since=mark)
    mark = d.mark()
    d.write('A' * 300 + '\n')
    long_line = d.wait(lambda l: l.text.startswith(('FX_CHUNK', 'FX_ERROR')), 5, since=mark)
    d.write('.\n')
    end = d.wait(lambda l: l.text.startswith(('FX_OK', 'FX_ERROR')), 10, since=mark)
    check('a 300-character nonsense line is refused', end is not None and end.text.startswith('FX_ERROR'), (long_line.text if long_line else None, end.text if end else None))

    reply = fx_client.clear(d)
    check('clear after', reply.text.startswith('FX_OK') and reply.f.get('installed') == '0', reply.text)
    d.close()
    failed = [c for c in checks if not c['ok']]
    (a.out / 'results.json').write_text(json.dumps({'checks': checks, 'burst_reply': burst}, indent=1) + '\n')
    print(f'{len(checks)} checks, {len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
