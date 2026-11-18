#!/usr/bin/env python3
"""The HIL rig's stalled-transfer check, dry-run against three fake M5s.

scenarios.stalled_transfers (part of `stray_bytes`) opens a restore, a theme
and a milestone request, waits out each one's timeout and sends what a
stalled host would. It runs here with the real Rig and Device line handling
against a fake M5 that models the USB boundary: letters that run at once
(p, P, C, K, R) anywhere in a burst, newline-ended L/T/U, line transfers that
acknowledge each line and time out (scaled from 15 s to 0.3 s after the
last byte; the clock starts at the first byte, so a slow host suite cannot
time a transfer out before the scenario writes to it), and the
transfer that ended unfinished. With the fixed guard (the rest of that line,
then body lines until "." or a one-letter command) the scenario must
pass; with 0.20.10's (the timeout hands the next bytes to the parser) and
with the first fix's (drop only up to the first newline) it must fail, so it
can catch either on a device. Each write is one burst; a
write of a single letter stands alone.

The fake also prints the status block by itself, as the firmware does every
2 s while a host is attached (here every STATUS_PERIOD_S, and never less
apart). On a rig that block fell inside a "runs nothing" window and failed a
correct firmware, and it let "then p runs" pass whether or not p ran: the
scenario must tell a command's block from the periodic one.
"""
from pathlib import Path
import sys
import tempfile
import threading
import time
import types

ROOT = Path(__file__).resolve().parents[2]
sys.modules.setdefault('serial', types.SimpleNamespace(Serial=object, SerialException=Exception))
sys.path.insert(0, str(ROOT / 'firmware/tools/hil'))
import hil_device  # noqa: E402
import rig as hil_rig  # noqa: E402
import scenarios  # noqa: E402

TIMEOUT_S = 0.3
STATUS_PERIOD_S = 0.05
STATUS_BLOCK = ['STATUS v=0.20.10-sticks3-hil-pm', 'RPM_STATUS state=READY', 'CPU_PM requested=1 enabled=1', 'MOTION_CLIP ok=1']
IMMEDIATE = b'pPCKR'
LETTERS = IMMEDIATE + b'OSxgiaodhceQEqbnutyzvwjsLTMUFXmAYWV'


class FakeM5:
    def __init__(self, fixed):
        self.fixed, self.lock, self.out = fixed, threading.Lock(), []
        self.rx = self.line = None  # the open transfer's tag and the line it is reading
        self.generation, self.stale, self.hil_line = 0, False, None
        self.status_at = time.monotonic()

    def tick(self):
        """The loop's own status block, a period or more after the last one (lock held)."""
        now = time.monotonic()
        if now - self.status_at >= STATUS_PERIOD_S:
            self.status_at = now
            self.out += STATUS_BLOCK

    def feed(self, data):
        with self.lock:
            self.tick()
            if self.stale and not self.rx and self.fixed == 'prev':
                # The first version of the fix: drop up to the first newline.
                if len(data) == 1 and data in LETTERS:
                    self.stale = False
                else:
                    end = data.find(b'\n')
                    if end < 0:
                        return
                    self.stale, data = False, data[end + 1:]
            elif self.stale and not self.rx and self.fixed:
                data = self.orphan(data)
            elif self.stale and not self.rx:
                self.stale = False  # 0.20.10: the parser reads it all
            self.commands(data)

    def orphan(self, data, quiet_first=True):
        """usb_command_guard.h after an abandoned transfer; returns what the
        normal parser reads. The first byte of a write came after quiet; a
        write ending right after a held letter ends with quiet."""
        held = None
        for i in range(len(data)):
            c, quiet = data[i:i + 1], i == 0 and quiet_first
            newline = c in b'\r\n'
            if held:
                letter, held_quiet = held
                held = None
                if self.stale == 'start':
                    if newline and held_quiet:
                        self.stale = False
                        return letter + data[i:]  # a one-letter line begun after quiet
                    if not newline:
                        self.stale = 'body'
            if self.stale == 'start':
                if newline:
                    continue
                if c == b'.':
                    self.stale = 'dot'
                elif c in LETTERS:
                    held = (c, quiet)
                elif c.isalnum() or c in b'+/=':
                    self.stale = 'body'
                else:
                    self.stale = False
                    return data[i:]  # not a body line
                continue
            if self.stale == 'dot':
                if c == b'\r':
                    continue
                if c == b'\n':
                    self.stale = False
                    return data[i + 1:]
                self.stale = 'body'
            if newline:
                self.stale = 'start'
            elif quiet and c in LETTERS:
                held = (c, True)
        if held and held[1]:
            self.stale = False
            return held[0]  # a letter alone: quiet before and after
        if held:
            self.stale = 'body'
        return b''

    def commands(self, data):
        i = 0
        while i < len(data):
            if self.rx:
                i = self.receive(data, i)
                if self.stale:  # refused: the rest of this write is the rest of its line
                    if self.fixed is True:
                        data, i = self.orphan(data[i:], quiet_first=False), 0
                    else:
                        end = data.find(b'\n', i)
                        data, i, self.stale = (b'' if end < 0 else data[end + 1:]), 0, False
                continue
            c = data[i:i + 1]
            i += 1
            if self.hil_line is not None:
                if c == b'\n':
                    self.hil(self.hil_line.decode())
                    self.hil_line = None
                else:
                    self.hil_line += c
            elif c == b'!':
                self.hil_line = b''
            elif c == b'p':
                self.out += STATUS_BLOCK
            elif c == b'P':
                self.out.append('POWER_LOG schema=1 boots=1')
            elif c == b'C':
                self.out.append('CPU_PM_TRACE error=disabled')
            elif c == b'K':
                self.out += ['BACKUP_BEGIN format=7 bytes=96', 'BACKUP_DATA QUJD', 'BACKUP_END']
            elif c == b'R':
                self.open('RESTORE')
            elif c in b'LTU' and data[i:i + 1] == b'\n' and (i == 1 or data[i - 2:i - 1] == b'\n'):
                i += 1
                if c == b'L':
                    self.out += ['THEME_LIST slots=4', 'THEME_LIST_END']
                else:
                    self.open('THEME' if c == b'T' else 'MILESTONE')

    def open(self, tag):
        self.rx, self.line = tag, b''
        self.out.append(f'{tag}_READY max_chars=60')

    def arm(self):
        self.generation += 1
        generation = self.generation
        threading.Timer(TIMEOUT_S, self.timeout, (generation,)).start()

    def timeout(self, generation):
        with self.lock:
            if self.rx and generation == self.generation:
                self.out.append(f'{self.rx}_ERROR reason=timeout written=0')
                self.rx, self.stale = None, 'rest' if self.line else 'start'

    def receive(self, data, i):
        c = data[i:i + 1]
        if c == b'\n':
            if self.line == b'.':
                self.out.append(f'{self.rx}_ERROR reason=invalid_request written=0')
                self.rx = None
            elif self.line:
                self.out.append(f'{self.rx}_CHUNK chars={len(self.line)}')
            self.line = b''
        elif c.isalnum() or c in b'+/=.':
            self.line += c
        else:
            self.out.append(f'{self.rx}_ERROR reason=bad_character written=0')
            self.rx, self.stale = None, 'rest'
            return i + 1
        self.arm()
        return i + 1

    def hil(self, text):
        word = text.split()[0]
        if word == 'hil':
            self.out += ['HIL_STATUS boot_id=00000001 reset=poweron commands=1 sleep_scale=1', 'HIL_IMU mode=const',
                         'HIL_FAULTS none']
        else:
            self.out.append(f'HIL_OK {word}')


class Device(hil_device.Device):
    """The real Device's line handling, fed by the fake instead of a port."""

    def __init__(self, fake):
        self.lines, self.snapshots = [], []
        self.cond = threading.Condition()
        self.log, self.echo, self.port, self.disconnects = None, None, None, 0
        self._write_lock = threading.Lock()
        self.fake, self._stop = fake, False
        threading.Thread(target=self._pump, daemon=True).start()

    def write(self, data):
        self.fake.feed(data.encode() if isinstance(data, str) else data)

    def wait(self, predicate, timeout, since=None, fail=None):
        # The fake answers in milliseconds and times out in 0.3 s: a check
        # that is meant to fail need not wait the device's seconds.
        return super().wait(predicate, min(timeout, 1.0), since, fail)

    def _pump(self):
        while not self._stop:
            with self.fake.lock:
                self.fake.tick()
                lines, self.fake.out = self.fake.out, []
            with self.cond:
                for text in lines:
                    self._record(text)
                self.cond.notify_all()
            time.sleep(0.002)


def dry_run(fixed):
    with tempfile.TemporaryDirectory(prefix='launchlab-hil-stalled-') as out:
        dev = Device(FakeM5(fixed))
        rig = hil_rig.Rig(dev, Path(out))
        rig.boot_id = rig.hil_boot().get('boot_id')
        rig.run('stalled_transfers', scenarios.stalled_transfers)
        dev._stop = True
        return rig.results[-1]


def main():
    time_sleep, period = scenarios.time.sleep, scenarios.STATUS_PERIOD_S
    scenarios.time.sleep = lambda s: time_sleep(min(s, 0.01))
    scenarios.STATUS_PERIOD_S = STATUS_PERIOD_S
    try:
        fixed, prev, old = dry_run(True), dry_run('prev'), dry_run(False)
    finally:
        scenarios.time.sleep, scenarios.STATUS_PERIOD_S = time_sleep, period
    failed = [c for c in fixed['checks'] if not c['ok']]
    assert not failed and fixed['passed'], failed or fixed.get('traceback')
    caught = [c['name'] for c in old['checks'] if not c['ok']]
    resumed = [f'stalled {case}: what the host sent after the timeout runs nothing' for case in
               ('RESTORE (between lines, then line)', 'RESTORE (between lines, then paced)',
                'MILESTONE (between lines, then lines)', 'THEME (inside a line, then tail)')]
    assert all(name in caught for name in resumed) and not old['passed'], caught
    # The first fix dropped only up to the first newline: a command right
    # after a between-lines timeout was lost, and later lines ran.
    missed = [c['name'] for c in prev['checks'] if not c['ok']]
    assert {'stalled THEME (between lines, then nothing): then T runs',
            'stalled MILESTONE (between lines, then lines): what the host sent after the timeout runs nothing'} <= set(missed), missed
    print(f"PASS: stalled_transfers dry run: {len(fixed['checks'])} checks pass against the fixed guard's model; "
          f"0.20.10's fails {len(caught)}, the first drop-to-newline fix {len(missed)} ({'; '.join(missed)})")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
