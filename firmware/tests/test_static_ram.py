#!/usr/bin/env python3
"""check_static_ram.py: sums the static internal-RAM sections of the image's
ELF and fails a build above the limit calibrated on the device (the
loaded-effects spike image of 189,924 bytes boot-looped; 153,916 booted)."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'firmware/tools/check_static_ram.py'
spec = importlib.util.spec_from_file_location('check_static_ram', TOOL)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


def elf(sections):
    """A minimal 32-bit little-endian ELF with these (name, size) sections."""
    names = b'\0' + b''.join(n.encode() + b'\0' for n, _ in sections) + b'.shstrtab\0'
    header = bytearray(52)
    header[:6] = b'\x7fELF\x01\x01'
    shoff = 52 + len(names)
    count = len(sections) + 2  # a null section first, the name table last
    struct.pack_into('<I', header, 0x20, shoff)
    struct.pack_into('<HHH', header, 0x2E, 40, count, count - 1)
    table = bytearray(40)  # SHT_NULL
    at = 1
    for name, size in sections:
        table += struct.pack('<IIIIIIIIII', at, 8, 0, 0, 0, size, 0, 0, 4, 0)
        at += len(name) + 1
    table += struct.pack('<IIIIIIIIII', at, 3, 0, 0, 52, len(names), 0, 0, 1, 0)
    return bytes(header) + names + bytes(table)


def run(directory, total):
    data = (directory / 'LaunchLabMini.ino.elf')
    data.write_bytes(elf([('.iram0.text', 90000), ('.dram0.data', 48000), ('.dram0.bss', total - 48000), ('.noinit', 0),
                          ('.flash.rodata', 400000)]))
    return subprocess.run([sys.executable, str(TOOL), str(directory)], capture_output=True, text=True)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        assert check.static_dram(_write(d, 135820)) == 135820
        ok = run(d, 153916)
        assert ok.returncode == 0 and '153916 bytes' in ok.stdout, ok
        bad = run(d, 189924)
        assert bad.returncode != 0 and '189924 bytes > 155000' in bad.stderr, bad
        assert run(d, check.LIMIT).returncode == 0 and run(d, check.LIMIT + 1).returncode != 0
    # While the host suite runs, the build gate parks the image under an .untested name.
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        run(d, 189924)
        (d / 'LaunchLabMini.ino.elf').rename(d / 'LaunchLabMini.ino.elf.untested')
        parked = subprocess.run([sys.executable, str(TOOL), str(d)], capture_output=True, text=True)
        assert parked.returncode != 0 and '189924 bytes > 155000' in parked.stderr, parked
    print('test_static_ram: ok')


def _write(directory, total):
    path = directory / 'probe.elf'
    path.write_bytes(elf([('.dram0.data', 46556), ('.dram0.bss', total - 46556), ('.noinit', 0), ('.text', 5)]))
    return path


if __name__ == '__main__':
    main()
