#!/usr/bin/env python3
"""Firmware must never call HWCDC::flush() (Serial.flush()).

The core's flush() discards the whole USB TX ring once the host has not read
for the TX timeout, or at once when it already considers the host gone, so
the output it was meant to push out is deleted (a lost HIL reply and status
block on 2026-10-17; RESTORE_OK and WIPE_RESTART were exposed the same way).
Use UsbTx::drain() from usb_tx.h instead.
"""
import re
from pathlib import Path

SOURCES = Path(__file__).resolve().parents[1] / 'LaunchLabMini'
pattern = re.compile(r'\bSerial\s*\.\s*flush\s*\(')
problems = [f'{path.name}:{number}: {line.strip()}'
            for path in sorted(SOURCES.glob('*.[hi]*')) if path.suffix in ('.h', '.ino')
            for number, line in enumerate(path.read_text().splitlines(), 1)
            if pattern.search(line) and not line.lstrip().startswith('//')]
if problems:
    raise SystemExit('Serial.flush() discards queued USB output; use UsbTx::drain():\n' + '\n'.join(problems))
print('PASS: no Serial.flush() in the firmware sources; USB output is drained without discarding')
