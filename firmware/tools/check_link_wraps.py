"""Fail a LaunchLab Mini build whose wrapped function does not start at its first instruction.

Usage: python3 firmware/tools/check_link_wraps.py <build-path> [<link flags>...]

The linker's --wrap=NAME sends every call of NAME to __wrap_NAME. With the
sketch's -mtext-section-literals the linker can leave the __wrap_NAME symbol a
few bytes before the function: an object file that defines __wrap_NAME and
also calls NAME gets its symbol moved twice when the constants before the
function are merged away (firmware/LaunchLabMini/src/link_wraps/link_wraps.cpp has the whole
story). That object's calls then jump into data. On 2026-11-18 the HIL image
did: IllegalInstruction at every boot once the wipe code read its marker.

Every ESP32-S3 function starts with the windowed ABI's `entry a1, n`, so the
check is exact: each __wrap_ function in the image, and each NAME the link
flags wrap, must have `entry` at its symbol's address.
"""
from pathlib import Path
import re
import struct
import sys

PREFIX = '__wrap_'
STT_FUNC = 2
SHT_SYMTAB = 2


def is_entry(code):
    """`entry a1, n`: op0 6, n 3, m 0 in the first byte, the stack pointer in the second."""
    return len(code) >= 3 and code[0] == 0x36 and code[1] & 0x0F == 1


def wrappers(elf):
    """{name: (address, its first three bytes)} of the __wrap_ functions in a 32-bit little-endian ELF."""
    data = Path(elf).read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise ValueError(f'{elf}: not a 32-bit little-endian ELF')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2E)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]
    found = {}
    for _, kind, _, _, offset, size, link, _, _, entsize in sections:
        if kind != SHT_SYMTAB or not entsize:
            continue
        names = sections[link][4]
        for at in range(offset, offset + size, entsize):
            name_index, value, _, info, _, index = struct.unpack_from('<IIIBBH', data, at)
            if info & 0x0F != STT_FUNC or not 0 < index < shnum:
                continue
            name = data[names + name_index:data.index(b'\0', names + name_index)].decode()
            if not name.startswith(PREFIX):
                continue
            _, _, _, address, section_offset, section_size, *_ = sections[index]
            inside = value - address
            code = data[section_offset + inside:section_offset + inside + 3] if 0 <= inside < section_size else b''
            found[name] = (value, code)
    return found


def wrapped(flags):
    """The NAMEs of every --wrap=NAME in these link flags."""
    return re.findall(r'--wrap=(\w+)', ' '.join(flags))


def problems(elf, flags=()):
    found = wrappers(elf)
    result = [f'{PREFIX}{name}: the link flags wrap {name}, but the image has no such function'
              for name in wrapped(flags) if PREFIX + name not in found]
    result += [f'{name} at 0x{address:08x} starts with {code.hex() or "nothing"}, not with `entry`'
               for name, (address, code) in sorted(found.items()) if not is_entry(code)]
    return result, found


def main():
    build = Path(sys.argv[1])
    # The build gate (host_suite_gate.sh) parks the image as *.untested until the host suite passes.
    elves = sorted(build.glob('*.ino.elf')) or sorted(build.glob('*.ino.elf.untested'))
    if not elves:
        raise SystemExit(f'No .ino.elf under {build}')
    bad, found = problems(elves[0], sys.argv[2:])
    if bad:
        raise SystemExit('Wrapped functions the linker misplaced, so a call would jump into data:\n  ' + '\n  '.join(bad) +
                         '\nA wrapper must be defined in firmware/LaunchLabMini/src/link_wraps/link_wraps.cpp, which says why.')
    print(f'Wrapped functions OK: {", ".join(sorted(found)) or "none"} start at their first instruction.')


if __name__ == '__main__':
    main()
