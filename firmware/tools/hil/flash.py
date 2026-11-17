#!/usr/bin/env python3
"""Install an application image on a unit and prove the flash took.

write-flash already compares an on-chip MD5 of the written region with the
file's ("Hash of data verified"); verify-flash then asks the chip for the MD5
of the same region again, in its own esptool session after the restart, and
compares it with the file ("Verification successful (digest matched)"). Both
are an exact check of every byte, in about two seconds instead of a full
read-flash of the 1.9 MB image. The image header is kept as built
(--flash-mode keep, --flash-size keep), so the region is the file byte for byte.

--readback also reads the region back with read-flash and compares it with the
file byte for byte: the release gate does that for the production image it
leaves on a unit, which is the owner's own.

  python3 firmware/tools/hil/flash.py --esptool ESPTOOL --port /dev/cu.usbmodemNNN [--readback] IMAGE
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from port_guard import require_free  # noqa: E402

APP_OFFSET = '0x10000'
WRITTEN = 'Hash of data verified.'
VERIFIED = 'Verification successful (digest matched).'


class FlashError(RuntimeError):
    pass


def esptool(tool, port, *args, run=subprocess.run):
    require_free(port)
    out = run([tool, '--chip', 'esp32s3', '--port', port, '--after', 'watchdog_reset', *args],
              capture_output=True, text=True)
    return out.returncode, (out.stdout + out.stderr).replace('\r', '\n')


def install(tool, port, image, offset=APP_OFFSET, run=subprocess.run, settle=6.0, readback=False):
    """write-flash then verify-flash `image` at `offset` (and read-flash it back if `readback`);
    the record of each, or FlashError."""
    image = Path(image)
    data = image.read_bytes()
    record = {'image': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
              'md5': hashlib.md5(data).hexdigest(), 'offset': offset, 'port': port}
    start = time.monotonic()
    status, text = esptool(tool, port, 'write-flash', '--flash-mode', 'keep', '--flash-size', 'keep', offset,
                           str(image), run=run)
    if status or WRITTEN not in text:
        raise FlashError(f'write-flash of {image.name} on {port} failed:\n{text[-2000:]}')
    record['write_seconds'] = round(time.monotonic() - start, 1)
    record['write_hash_verified'] = True
    start = time.monotonic()
    status, text = esptool(tool, port, 'verify-flash', '--flash-mode', 'keep', '--flash-size', 'keep', offset,
                           str(image), run=run)
    if status or VERIFIED not in text:
        raise FlashError(f'verify-flash of {image.name} on {port} failed:\n{text[-2000:]}')
    record['verify_seconds'] = round(time.monotonic() - start, 1)
    record['verify_flash'] = 'digest matched'
    if readback:
        start = time.monotonic()
        with tempfile.TemporaryDirectory(prefix='launchlab-readback-') as directory:
            copy = Path(directory) / 'readback.bin'
            status, text = esptool(tool, port, 'read-flash', offset, str(len(data)), str(copy), run=run)
            back = copy.read_bytes() if copy.exists() else b''
        if status:
            raise FlashError(f'read-flash of {image.name} on {port} failed:\n{text[-2000:]}')
        if back != data:
            differ = next((i for i, (x, y) in enumerate(zip(back, data)) if x != y), min(len(back), len(data)))
            raise FlashError(f'read-flash of {image.name} on {port}: {len(back)} bytes read back, '
                             f'the first difference at byte {differ}')
        record['readback_seconds'] = round(time.monotonic() - start, 1)
        record['readback'] = 'exact'
    time.sleep(settle)  # the app boots after the watchdog reset
    return record


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--esptool', default='esptool')
    p.add_argument('--port', required=True)
    p.add_argument('--offset', default=APP_OFFSET)
    p.add_argument('--readback', action='store_true', help='also read the region back and compare every byte')
    p.add_argument('image', type=Path)
    a = p.parse_args(argv)
    try:
        print(json.dumps(install(a.esptool, a.port, a.image, a.offset, readback=a.readback), indent=1))
    except FlashError as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
