#!/usr/bin/env python3
"""Compare two ESP32-S3 application images segment by segment.

Exits 0 when every loaded byte (code, read-only data, initialised data) is
identical. Only three fields may differ, and each is reported:
- app_elf_sha256 in the app descriptor: a hash of the whole ELF file, whose
  DWARF line tables change when lines move in a source file;
- the image checksum byte and the appended image SHA-256, both derived from it.
Build both images with the same SOURCE_DATE_EPOCH so __DATE__/__TIME__ match.
"""
import hashlib
import struct
import sys
from pathlib import Path

HEADER = 24
DESC_SHA = slice(HEADER + 8 + 144, HEADER + 8 + 176)


def parse(data):
    if data[0] != 0xE9:
        raise ValueError('not an ESP application image')
    count, hashed = data[1], data[23]
    offset, segments = HEADER, []
    for _ in range(count):
        address, length = struct.unpack_from('<II', data, offset)
        segments.append((address, offset + 8, length))
        offset += 8 + length
    end = offset + (15 - offset % 16) + 1  # padding up to the checksum byte
    return segments, end, hashed


def main(a, b):
    left, right = Path(a).read_bytes(), Path(b).read_bytes()
    sa, ea, ha = parse(left)
    sb, eb, hb = parse(right)
    problems = []
    if len(left) != len(right) or [(s[0], s[2]) for s in sa] != [(s[0], s[2]) for s in sb]:
        problems.append('segment layout differs')
    masked = bytearray(right)
    masked[DESC_SHA] = left[DESC_SHA]
    for address, start, length in sa:
        if left[start:start + length] != bytes(masked[start:start + length]):
            problems.append(f'segment 0x{address:08x} ({length} bytes) differs')
    if left[:HEADER] != right[:HEADER]:
        problems.append('image header differs')
    print(f'segments={len(sa)} bytes={len(left)} elf_sha_differs={left[DESC_SHA] != right[DESC_SHA]} '
          f'checksum_differs={left[ea - 1] != right[eb - 1]} '
          f'digest_differs={ha and left[ea:ea + 32] != right[eb:eb + 32]}')
    for p in problems:
        print('DIFFERENT', p)
    if not problems:
        print('IDENTICAL loaded code and data;',
              'whole file identical' if left == right else 'only the ELF hash and its digests differ')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:3]))
