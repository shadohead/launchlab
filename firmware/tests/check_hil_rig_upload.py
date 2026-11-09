#!/usr/bin/env python3
"""HIL rig uploads against a fake M5 that models the firmware's USB protocol.

On 2026-10-17 a HIL_UP_READY held in the device's TX buffer reached the host
together with the device's upload timeout; the rig streamed chunk data into
the command parsers, which ran backup, restore, a setting change, the sleep
test and power-off on both M5s. The rig must stop streaming at the first
error, stale ready or missing acknowledgement and resynchronize, and the
device swallows input after a failed upload until "!sync" (hil_resync.h).
"""
import math
import sys
import threading
import time
import types
from pathlib import Path

sys.modules.setdefault('serial', types.SimpleNamespace(Serial=object, SerialException=Exception))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'hil'))
import hil_device  # noqa: E402

DANGEROUS = set(b'KRSOxgaoij')  # production single-letter commands that act
TIMEOUT_S = 0.3
HANG_SCALE = 20


class FakeM5:
    """Byte-level model: HIL lines, one upload, the resync swallow, letters."""

    def __init__(self, hold_ready=False, stop_acking_after=None):
        self.hold_ready = hold_ready
        self.stop_acking_after = stop_acking_after
        self.out = []          # lines for the host
        self.executed = []     # production letters that reached the parser
        self.line = None       # HIL line being collected
        self.upload = None     # [remaining, received, deadline]
        self.resync = False
        self.sync_buffer = b''
        self.lock = threading.Lock()

    def tick(self):
        with self.lock:
            if self.upload and time.monotonic() > self.upload[2]:
                held = ['HIL_UP_READY bytes=3120 chunk=4096'] if self.hold_ready else []
                self.out += held + [f'HIL_ERR up timeout received={self.upload[1]}']
                self.upload = None
                self.resync = True

    def feed(self, data):
        with self.lock:
            for byte in data:
                self._byte(byte)

    def _byte(self, byte):
        if self.upload:
            if self.stop_acking_after is not None and self.upload[1] >= self.stop_acking_after:
                if self.upload[2] == math.inf:
                    self.upload[2] = time.monotonic() + TIMEOUT_S
                return  # bytes lost: the device times out
            self.upload[0] -= 1
            self.upload[1] += 1
            if self.upload[0] == 0:
                self.out += [f'HIL_ACK {self.upload[1]}', 'HIL_OK up imu frames=260']
                self.upload = None
            return
        if self.resync:
            self.sync_buffer = (self.sync_buffer + bytes([byte]))[-6:]
            if self.sync_buffer == b'!sync\n':
                self.resync = False
                self.out.append('HIL_SYNC reason=sync discarded=1')
            return
        if self.line is not None:
            if byte == ord('\n'):
                self._command(self.line.decode(errors='replace'))
                self.line = None
            else:
                self.line += bytes([byte])
            return
        if byte == ord('!'):
            self.line = b''
        elif byte in DANGEROUS:
            self.executed.append(chr(byte))

    def _command(self, text):
        word = text.split()[0] if text.split() else ''
        if word == 'up':
            size = int(text.split()[2]) * 12
            # The device times out a chunk that stops arriving (3 s on the M5,
            # shortened here). It is modelled only where a test needs it: after
            # bytes were lost, or with the ready line held, so a slow host
            # scheduler cannot time out an upload that is meant to succeed.
            self.upload = [size, 0, time.monotonic() + TIMEOUT_S if self.hold_ready else math.inf]
            if not self.hold_ready:
                self.out.append(f'HIL_UP_READY bytes={size} chunk=4096')
        elif word == 'sync':
            self.out.append('HIL_SYNC reason=idle discarded=0')
        else:
            self.out.append(f'HIL_OK {word}')


class Rig(hil_device.Device):
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
        # Every awaited line does arrive here, so the rig's timeouts are only
        # hang guards: stretch them so a loaded host cannot trip one.
        return super().wait(predicate, timeout * HANG_SCALE, since, fail)

    def _pump(self):
        while not self._stop:
            self.fake.tick()
            with self.fake.lock:
                lines, self.fake.out = self.fake.out, []
            # Lines the device sent together (a held ready and its timeout) are
            # seen together, as from one USB packet, however late this runs.
            with self.cond:
                for text in lines:
                    self._record(text)
            time.sleep(0.005)


def run(fake, expect_ok):
    rig = Rig(fake)
    payload = bytes((i * 37 + 11) & 0xff for i in range(3120))  # contains letters
    try:
        rig.upload('imu', payload, 260)
        ok = True
    except hil_device.DeviceError:
        ok = False
    rig.hil('home')
    rig._stop = True
    assert ok == expect_ok, (ok, [l.text for l in rig.lines])
    assert not fake.executed, f'binary reached the command parser: {fake.executed[:10]}'
    return rig


rig = run(FakeM5(), True)
print('normal upload: acknowledged, nothing parsed')
rig = run(FakeM5(hold_ready=True), False)
resync = [l.text for l in rig.lines if l.text.startswith('HOST_RESYNC')]
assert resync and 'stale_ready' in resync[0] and 'HIL_SYNC' in resync[0], resync
print('ready held until the device timeout: no chunk streamed, resynchronized')
rig = run(FakeM5(stop_acking_after=1000), False)
resync = [l.text for l in rig.lines if l.text.startswith('HOST_RESYNC')]
assert resync and 'after=ack' in resync[0] and 'HIL_SYNC' in resync[0], resync
print('missing acknowledgement: streaming stopped, device swallowed the rest, resynchronized')
print('PASS: HIL rig uploads never stream into the command parsers')
