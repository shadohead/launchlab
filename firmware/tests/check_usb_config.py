#!/usr/bin/env python3
"""The USB serial configuration is the same in every build and holds every request.

HWCDC drops bytes once its RX queue is full, and a tool sends each request line
in one burst. 0.18.0's production build kept the core's 256-byte queue while
its HIL build set 8 KB, so the gate passed while no v3 milestone request (312
characters) reached a production unit (2026-10-23). This check holds:

- setup() sizes the RX and TX queues before Serial.begin(), unconditionally:
  no build (HIL or production) configures USB differently;
- no HIL source and no LAUNCHLAB_HIL block resizes them;
- the RX queue holds the longest request line any tool sends (a v3 milestone
  request, a theme request, a restore line) twice over, from the firmware's
  own request sizes.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / 'LaunchLabMini'
INO = SKETCH / 'LaunchLabMini.ino'
CALLS = ('Serial.setRxBufferSize', 'Serial.setTxBufferSize')


def fail(message):
    print(f'FAIL: {message}')
    sys.exit(1)


def conditional_depths(lines):
    """For each line, the preprocessor conditionals it sits in (their #if lines)."""
    stack, out = [], []
    for line in lines:
        s = line.strip()
        if s.startswith('#if'):
            stack.append(s)
        out.append(list(stack))
        if s.startswith('#endif') and stack:
            stack.pop()
    return out


def main():
    source = INO.read_text()
    lines = source.split('\n')
    depths = conditional_depths(lines)

    # setup(): both sizes set before Serial.begin(), in no conditional.
    start = next(i for i, l in enumerate(lines) if l.startswith('void setup() {'))
    begin = next(i for i in range(start, len(lines)) if 'Serial.begin(' in lines[i])
    for call, constant in (('Serial.setRxBufferSize', 'USB_RX_BUFFER_BYTES'), ('Serial.setTxBufferSize', 'USB_TX_BUFFER_BYTES')):
        at = [i for i in range(start, begin) if call in lines[i]]
        if len(at) != 1 or f'{call}({constant})' not in lines[at[0]]:
            fail(f'setup() must call {call}({constant}) once before Serial.begin()')
        if depths[at[0]]:
            fail(f'{call} sits inside {depths[at[0]]}: every build must configure USB the same way')

    # Nowhere else: not in another sketch file, not in HIL code.
    for path in sorted(SKETCH.glob('*.h')) + [INO]:
        text = path.read_text().split('\n')
        for i, line in enumerate(text):
            if any(call in line for call in CALLS) and not (path == INO and start <= i < begin):
                fail(f'{path.name}:{i + 1} resizes a USB queue outside setup(): {line.strip()}')
    for i, depth in enumerate(depths):
        if any('LAUNCHLAB_HIL' in d for d in depth) and re.search(r'Serial\.set(Rx|Tx)BufferSize|USB_(RX|TX)_BUFFER_BYTES\s*=', lines[i]):
            fail(f'LaunchLabMini.ino:{i + 1} changes the USB configuration in a HIL-only block')

    rx = int(re.search(r'USB_RX_BUFFER_BYTES=(\d+);', source).group(1))
    # The firmware's own request sizes.
    probe = r'''
#include "milestones.h"
#include "theme_library.h"
#include <cstdio>
int main(){std::printf("%u %u\n",unsigned(Milestones::Request::BYTES),unsigned(Themes::Request::BYTES));}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / 'sizes.cpp', Path(tmp) / 'sizes'
        src.write_text(probe)
        subprocess.run(['c++', '-std=c++17', f'-I{ROOT / "tests/fakes"}', f'-I{SKETCH}', str(src), '-o', str(exe)], check=True)
        milestone, theme = map(int, subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.split())
    line = lambda raw: (raw + 2) // 3 * 4 + 2  # base64 plus CR LF
    longest = max(line(milestone), line(theme), 96 + 2)
    if rx < 2 * longest:
        fail(f'USB_RX_BUFFER_BYTES={rx} cannot hold the longest request line ({longest} bytes) twice')
    print(f'PASS: one USB configuration for every build (RX {rx} bytes, set before Serial.begin, never in HIL code); '
          f'it holds the longest request line ({longest} bytes: a v3 milestone request) twice')


if __name__ == '__main__':
    main()
