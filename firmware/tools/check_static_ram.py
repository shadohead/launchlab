"""Fail a LaunchLab Mini build whose static internal RAM leaves boot short.

Usage: python3 firmware/tools/check_static_ram.py <build-path>

Static internal DRAM (.dram0.data + .dram0.bss + .noinit, what arduino-cli
reports as "Global variables") is taken before the heap the boot needs:
the acquisition task's ADC continuous-mode DMA buffers, the IMU FIFO
transfers, USB and the display. On 2026-10-21 a loaded-effects spike image
with 189,924 bytes boot-looped on unit 83201 (adc_continuous_new_handle
found no DMA memory and aborted); 153,916 bytes booted and ran a full HIL
scenario. The limit sits just above the largest image known to boot: raise
it only after a larger image has booted and run on a device, and record
that image here.
"""
from pathlib import Path
import struct
import sys

LIMIT = 155_000  # bytes; 153,916 booted (loaded-effects spike run 2), 189,924 did not (run 3)
SECTIONS = ('.dram0.data', '.dram0.bss', '.noinit')


def static_dram(elf):
    """Sum of the static internal-RAM sections of a 32-bit little-endian ELF."""
    data = Path(elf).read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise ValueError(f'{elf}: not a 32-bit little-endian ELF')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2E)
    def section(i):
        return struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize)
    names_offset = section(shstrndx)[4]
    total = 0
    for i in range(shnum):
        name_index, _, _, _, _, size, *_ = section(i)
        end = data.index(b'\0', names_offset + name_index)
        if data[names_offset + name_index:end].decode() in SECTIONS:
            total += size
    return total


def main():
    build = Path(sys.argv[1])
    # The build gate (host_suite_gate.sh) parks the image as *.untested until the host suite passes.
    elves = sorted(build.glob('*.ino.elf')) or sorted(build.glob('*.ino.elf.untested'))
    if not elves:
        raise SystemExit(f'No .ino.elf under {build}')
    total = static_dram(elves[0])
    if total > LIMIT:
        raise SystemExit(f'Static internal RAM {total} bytes > {LIMIT}: the boot may not find DMA memory for the ADC '
                         f'(see firmware/tools/check_static_ram.py)')
    print(f'Static internal RAM OK: {total} bytes (limit {LIMIT}, {LIMIT - total} to spare).')


if __name__ == '__main__':
    main()
