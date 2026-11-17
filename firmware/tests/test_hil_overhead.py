#!/usr/bin/env python3
"""hil_overhead.py: the internal RAM a HIL image takes beyond its production
image of the same tree (static DRAM plus the IRAM that moves DRAM up), and the
heap floor a HIL run is held to: production's 32 KB less it. The numbers are
those of 03947c75 (2026-10-23): HIL 139,724 static against 134,364, with
1,280 bytes more IRAM shadow."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'firmware/tools/hil_overhead.py'
spec = importlib.util.spec_from_file_location('test_static_ram', ROOT / 'firmware/tests/test_static_ram.py')
static = importlib.util.module_from_spec(spec)
spec.loader.exec_module(static)


def build(directory, dram, shadow, tree):
    directory.mkdir()
    (directory / 'LaunchLabMini.ino.elf').write_bytes(static.elf([('.dram0.dummy', shadow), ('.dram0.data', 40000),
                                                                  ('.dram0.bss', dram - 40000), ('.noinit', 0)]))
    (directory / 'build-info.json').write_text(json.dumps({'tree_fingerprint': tree, 'source_commit': 'c' * 40}))
    return directory


def run(*builds):
    return subprocess.run([sys.executable, str(TOOL), *map(str, builds)], capture_output=True, text=True)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        hil, production = build(d / 'hil', 139724, 68096 + 1280, 'a' * 32), build(d / 'pm', 134364, 68096, 'a' * 32)
        ok = run(hil, production)
        assert ok.returncode == 0, ok
        result = json.loads((hil / 'hil-overhead.json').read_text())
        assert (result['static_delta'], result['iram_shadow_delta'], result['overhead_bytes']) == (5360, 1280, 6640), result
        assert (result['production_floor'], result['hil_floor']) == (32768, 26128), result
        assert result['tree_fingerprint'] == 'a' * 32 and 'HIL heap floor 26128' in ok.stdout, ok
        # Builds of different trees say nothing about each other.
        other = build(d / 'other', 134364, 68096, 'b' * 32)
        refused = run(hil, other)
        assert refused.returncode != 0 and 'not builds of one tree' in refused.stderr, refused
        # A HIL image smaller than production (it never is) is no overhead, not a credit.
        small = build(d / 'small', 130000, 60000, 'a' * 32)
        assert run(small, production).returncode == 0
        assert json.loads((small / 'hil-overhead.json').read_text())['hil_floor'] == 32768
    print('test_hil_overhead: ok')


if __name__ == '__main__':
    main()
