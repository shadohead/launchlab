"""The internal RAM a HIL image takes that its production image does not.

Usage: python3 firmware/tools/hil_overhead.py <hil-build> <production-build>

Both builds must be of the same source tree (their build-info.json
tree_fingerprint). The overhead is the HIL image's extra static internal DRAM
(check_static_ram's sections) plus its extra IRAM, which on the ESP32-S3
shares the same SRAM and moves the DRAM start up (.dram0.dummy). It is written
to <hil-build>/hil-overhead.json with the floor a HIL run holds the DMA-capable
heap to: production's 32 KB less this overhead, so a HIL check means what the
production check means (docs/FIRMWARE_SOUND_0_20.md). A production image is
held to the full 32 KB (production_songs.py, the release gate).
"""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_static_ram import static_dram  # noqa: E402

FLOOR = 32768  # bytes of DMA-capable heap a production image keeps (0.18.1)


def section_size(elf, wanted):
    data = Path(elf).read_bytes()
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2E)
    def section(i):
        return struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize)
    names = section(shstrndx)[4]
    for i in range(shnum):
        name_index, _, _, _, _, size, *_ = section(i)
        end = data.index(b'\0', names + name_index)
        if data[names + name_index:end].decode() == wanted:
            return size
    return 0


def build(directory):
    directory = Path(directory)
    elves = sorted(directory.glob('*.ino.elf')) or sorted(directory.glob('*.ino.elf.untested'))
    if not elves:
        raise SystemExit(f'No .ino.elf under {directory}')
    info = directory / 'build-info.json'
    return elves[0], json.loads(info.read_text()) if info.exists() else {}


def overhead(hil_dir, production_dir):
    hil_elf, hil_info = build(hil_dir)
    production_elf, production_info = build(production_dir)
    tree = hil_info.get('tree_fingerprint')
    if not tree or tree != production_info.get('tree_fingerprint'):
        raise SystemExit(f'{hil_dir} and {production_dir} are not builds of one tree '
                         f'({tree} vs {production_info.get("tree_fingerprint")})')
    static = static_dram(hil_elf) - static_dram(production_elf)
    shadow = section_size(hil_elf, '.dram0.dummy') - section_size(production_elf, '.dram0.dummy')
    total = max(0, static) + max(0, shadow)
    return {'tool': 'firmware/tools/hil_overhead.py', 'tree_fingerprint': tree, 'source_commit': hil_info.get('source_commit'),
            'hil_static': static_dram(hil_elf), 'production_static': static_dram(production_elf),
            'static_delta': static, 'iram_shadow_delta': shadow, 'overhead_bytes': total,
            'production_floor': FLOOR, 'hil_floor': FLOOR - total}


def main():
    result = overhead(sys.argv[1], sys.argv[2])
    (Path(sys.argv[1]) / 'hil-overhead.json').write_text(json.dumps(result, indent=1) + '\n')
    print(f"HIL overhead: {result['overhead_bytes']} bytes internal (static {result['static_delta']:+d}, "
          f"IRAM shadow {result['iram_shadow_delta']:+d}); HIL heap floor {result['hil_floor']} "
          f"(production {result['production_floor']}).")


if __name__ == '__main__':
    main()
