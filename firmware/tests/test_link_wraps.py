#!/usr/bin/env python3
"""The linker's wrappers (--wrap=NAME) are their own object file, and a build
whose wrapper does not start at its first instruction fails.

On 2026-11-18 the HIL image crashed at every boot on the rig once a wipe marker
existed: hil.h defined __wrap_nvs_get_blob in the sketch's object, which also
calls nvs_get_blob (device_wipe_nvs.h), and the linker left the symbol four
bytes before the function (firmware/LaunchLabMini/src/link_wraps/link_wraps.cpp
has the mechanism). The production image wraps nvs_open the same way and was
right only by where its constants happened to fall.

1. check_link_wraps.py on images shaped like the two links: the symbol on the
   constant before the function fails the build, the symbol on `entry` passes.
2. The sources: every wrapped NAME has its __wrap_NAME in link_wraps.cpp and
   nowhere else in the sketch, and that file cannot call NAME itself.
"""
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'firmware'
SKETCH = FIRMWARE / 'LaunchLabMini'
WRAPS = SKETCH / 'src/link_wraps/link_wraps.cpp'
TOOL = FIRMWARE / 'tools/check_link_wraps.py'
spec = importlib.util.spec_from_file_location('check_link_wraps', TOOL)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

TEXT = 0x42000020
ENTRY = bytes.fromhex('364100')    # entry a1, 32
LITERAL = bytes.fromhex('97171c3c')  # the constant the rig's image had where the symbol pointed
RETURN = bytes.fromhex('1df0')     # retw.n


def elf(symbols, code):
    """A 32-bit little-endian ELF: .text at TEXT holding `code`, and (name, offset, type) symbols."""
    names = b'\0' + b''.join(name.encode() + b'\0' for name, _, _ in symbols)
    table, at = bytearray(16), 1
    for name, offset, kind in symbols:
        table += struct.pack('<IIIBBH', at, TEXT + offset, 0, 0x10 | kind, 0, 1)
        at += len(name) + 1
    section_names = b'\0.text\0.symtab\0.strtab\0.shstrtab\0'
    body = bytearray(52)
    offsets = []
    for blob in (code, bytes(table), names, section_names):
        offsets.append(len(body))
        body += blob + b'\0' * (-len(blob) % 4)
    sections = bytearray(40)
    sections += struct.pack('<IIIIIIIIII', 1, 1, 6, TEXT, offsets[0], len(code), 0, 0, 4, 0)
    sections += struct.pack('<IIIIIIIIII', 7, 2, 0, 0, offsets[1], len(table), 3, 1, 4, 16)
    sections += struct.pack('<IIIIIIIIII', 15, 3, 0, 0, offsets[2], len(names), 0, 0, 1, 0)
    sections += struct.pack('<IIIIIIIIII', 23, 3, 0, 0, offsets[3], len(section_names), 0, 0, 1, 0)
    body[:6] = b'\x7fELF\x01\x01'
    struct.pack_into('<I', body, 0x20, len(body))
    struct.pack_into('<HHH', body, 0x2E, 40, 5, 4)
    return bytes(body + sections)


def run(directory, image, *flags, name='LaunchLabMini.ino.elf'):
    for old in directory.glob('*.elf*'):
        old.unlink()
    (directory / name).write_bytes(image)
    return subprocess.run([sys.executable, str(TOOL), str(directory), *flags], capture_output=True, text=True)


def check_images():
    FUNC, OBJECT = 2, 1
    # As linked on the rig's image: one constant left before the function, and the symbol on it.
    code = ENTRY + RETURN + b'\0\0\0' + LITERAL + ENTRY + RETURN + b'\0\0\0' + LITERAL + ENTRY + RETURN
    good = [('__wrap_nvs_open', 0, FUNC), ('__wrap_nvs_set_u8', 12, FUNC), ('loop', 24, FUNC)]
    bad = good + [('__wrap_nvs_get_blob', 20, FUNC)]
    assert check.is_entry(ENTRY) and check.is_entry(bytes.fromhex('360101')) and not check.is_entry(LITERAL)
    assert not check.is_entry(b'\x36\x41') and not check.is_entry(bytes.fromhex('364200'))
    assert check.wrapped(['-Wl,--wrap=nvs_open -Wl,--wrap=nvs_set_u8', '--wrap=nvs_get_blob']) == \
        ['nvs_open', 'nvs_set_u8', 'nvs_get_blob']
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        ok = run(d, elf(good, code), '-Wl,--wrap=nvs_open -Wl,--wrap=nvs_set_u8')
        assert ok.returncode == 0 and '__wrap_nvs_open, __wrap_nvs_set_u8 start at' in ok.stdout, ok
        found = check.wrappers(d / 'LaunchLabMini.ino.elf')
        assert found == {'__wrap_nvs_open': (TEXT, ENTRY), '__wrap_nvs_set_u8': (TEXT + 12, ENTRY)}, found
        # The rig's failure: the symbol four bytes early, on the constant.
        early = run(d, elf(bad, code), '-Wl,--wrap=nvs_open')
        assert early.returncode != 0 and '__wrap_nvs_get_blob at 0x42000034 starts with 97171c, not with `entry`' in early.stderr, early
        assert '__wrap_nvs_open' not in early.stderr.split('\n', 1)[1].split('A wrapper must')[0], early
        # A wrapped name the image has no function for (the wrapper was not compiled in).
        missing = run(d, elf(good, code), '-Wl,--wrap=nvs_open', '-Wl,--wrap=nvs_get_blob')
        assert missing.returncode != 0 and 'the link flags wrap nvs_get_blob, but the image has no such function' in missing.stderr, missing
        # Only functions count: an object named like one is not a wrapper.
        assert run(d, elf(good + [('__wrap_table', 8, OBJECT)], code)).returncode == 0
        # A symbol outside its section has no first instruction.
        outside = run(d, elf(good + [('__wrap_nvs_commit', len(code) + 8, FUNC)], code))
        assert outside.returncode != 0 and '__wrap_nvs_commit' in outside.stderr and 'nothing' in outside.stderr, outside
        # While the host suite runs, the build gate parks the image under an .untested name.
        parked = run(d, elf(bad, code), name='LaunchLabMini.ino.elf.untested')
        assert parked.returncode != 0 and '__wrap_nvs_get_blob' in parked.stderr, parked
        for old in d.glob('*.elf*'):
            old.unlink()
        assert subprocess.run([sys.executable, str(TOOL), str(d)], capture_output=True).returncode != 0


def code_of(path):
    """A C++ source without its comments."""
    text = path.read_text()
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', text, flags=re.S))


def check_sources():
    # The public copy (firmware/SYNC.json) builds with scripts/build-firmware.sh.
    public = (FIRMWARE / 'SYNC.json').exists()
    scripts = [ROOT / 'scripts/build-firmware.sh'] if public else sorted(FIRMWARE.glob('build-m5*.sh'))
    assert scripts, 'no build script to read the wrapped names from'
    wrapped = sorted({name for script in scripts for name in check.wrapped([script.read_text()])})
    assert wrapped, 'the build scripts wrap nothing: this test reads the wrong files'
    own = code_of(WRAPS)
    problems = []
    for name in wrapped:
        if len(re.findall(rf'\b__wrap_{name}\s*\([^;{{]*\)\s*\{{', own)) != 1:
            problems.append(f'--wrap={name}: {WRAPS.name} must define __wrap_{name} once')
        # \b does not split __wrap_NAME or __real_NAME: this is NAME called by its own name.
        if re.search(rf'\b{name}\b', own):
            problems.append(f'{WRAPS.name} names {name} itself: its object would then both define __wrap_{name} and call {name}')
    defined = set(re.findall(r'\b__wrap_(\w+)\s*\(', own))
    for name in sorted(defined - set(wrapped)):
        problems.append(f'{WRAPS.name} defines __wrap_{name}, which no build script wraps')
    includes = re.findall(r'#\s*include\s*(\S+)', own)
    extra = [i for i in includes if not i.startswith('<') and i != '"../../link_wraps.h"']
    if extra:
        problems.append(f'{WRAPS.name} includes {", ".join(extra)}: a sketch header could call a wrapped function in this object')
    header = code_of(SKETCH / 'link_wraps.h')
    if '{' in header.replace('namespace LinkWraps {', ''):
        problems.append('link_wraps.h must only declare: a function body there is compiled into the wrappers\' object')
    for path in sorted(p for p in SKETCH.rglob('*') if p.suffix in ('.ino', '.h', '.hpp', '.c', '.cpp') and p != WRAPS):
        if '__wrap_' in code_of(path):
            problems.append(f'{path.relative_to(FIRMWARE)} names a __wrap_ function: define it in {WRAPS.name}')
    if not public:
        build = (FIRMWARE / 'build-m5.sh').read_text()
        if not re.search(r'^python3 tools/check_link_wraps\.py "\$build_dir" "\$radio_cal_link \$\{LAUNCHLAB_EXTRA_LINK_FLAGS:-\}"$', build, re.M):
            problems.append('build-m5.sh must run tools/check_link_wraps.py on the image with every link flag')
    assert not problems, '\n'.join(problems)


def main():
    check_images()
    check_sources()
    print('test_link_wraps: ok')


if __name__ == '__main__':
    main()
