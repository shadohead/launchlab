"""Serial connection to a LaunchLab HIL test build (see README.md).

One background thread owns the port. It splits the device's output into
timestamped lines, survives USB re-enumeration after a reset, and handles the
binary SNAP payload of the `v` screen command. Callers wait for lines with
predicates; every line is also written to a log file.
"""
import re
import struct
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import serial

from port_guard import require_free

FIELD = re.compile(r'(\w+)=(\S*)')


def fnv1a(data, value=2166136261):
    for b in data:
        value = ((value ^ b) * 16777619) & 0xffffffff
    return value


def fields(text):
    """KEY a=1 b=x -> {'a': '1', 'b': 'x'} (values stay strings)."""
    return dict(FIELD.findall(text))


def number(value, default=None):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


@dataclass
class Line:
    index: int
    t: float  # host monotonic seconds
    text: str

    @property
    def tag(self):
        return self.text.split(' ', 1)[0]

    @property
    def f(self):
        return fields(self.text)


@dataclass
class Snapshot:
    """Raw RGB565 frame from the `v` command."""
    width: int
    height: int
    data: bytes = field(repr=False)

    def write_png(self, path):
        import zlib
        rows = bytearray()
        for y in range(self.height):
            rows.append(0)
            for x in range(self.width):
                p = self.data[(y * self.width + x) * 2] << 8 | self.data[(y * self.width + x) * 2 + 1]
                rows += bytes(((p >> 11) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))

        def chunk(kind, body):
            return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', self.width, self.height, 8, 2, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b'')
        Path(path).write_bytes(png)


class DeviceError(RuntimeError):
    pass


# A guarded USB command alone on its line, a letter or `E`/`g` with its digits
# (usb_command_guard.h, which also lists the letters).
GUARDED_COMMAND = re.compile(rb'^[OSxgiaodhceQEqbnutyzvwjsLTMUFXmAYWV][0-9]*\n$')
# The guard runs one only after 20 ms of input quiet on the device's clock: the
# 0.20.7 smoke lost a tap A and a song preview sent straight after a status
# read. The device takes in a line only when the loop gets to it (after the
# whole `p` reply, say), so the wait runs from its last line, by which it had
# read everything sent before, as well as from the host's last write.
COMMAND_QUIET_S = 0.06


class Device:
    def __init__(self, port, log_path=None, echo=None):
        self.port_name = port
        self.lines = []
        self.snapshots = []
        self.cond = threading.Condition()
        self.log = open(log_path, 'a', buffering=1) if log_path else None
        self.echo = echo  # optional callable(line) for live output
        self.port = None
        self.disconnects = 0
        self._stop = False
        self._write_lock = threading.Lock()
        self._last_write = self._last_line = 0.0
        self._open()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    # ---- connection
    def _open(self):
        deadline = time.monotonic() + 30
        while True:
            # Another session's tool on the same unit: refuse rather than share the port.
            require_free(self.port_name)
            try:
                self.port = serial.Serial(self.port_name, 115200, timeout=0.05)
                return
            except serial.SerialException:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.2)

    def reopen(self, timeout=30):
        """Close and reopen the port, as a person would after a reset left the
        link silent: the reader sees the closed port and reconnects."""
        start = self.mark()
        try:
            self.port.close()
        except Exception:
            pass
        return self.wait('HOST_RECONNECT', timeout, since=start) is not None

    def close(self):
        self._stop = True
        self.thread.join(timeout=2)
        if self.port:
            self.port.close()
        if self.log:
            self.log.close()

    def _record(self, text):
        with self.cond:
            line = Line(len(self.lines), time.monotonic(), text)
            self.lines.append(line)
            self._last_line = line.t
            self.cond.notify_all()
        if self.log:
            self.log.write(f'{line.t:.3f} {text}\n')
        if self.echo:
            self.echo(line)

    def _reader(self):
        pending = bytearray()
        binary = None  # (width, height, remaining bytes, collected)
        while not self._stop:
            try:
                chunk = self.port.read(65536)
            except (serial.SerialException, OSError, TypeError, AttributeError):
                self.disconnects += 1
                self._record(f'HOST_DISCONNECT n={self.disconnects}')
                try:
                    self.port.close()
                except Exception:
                    pass
                time.sleep(0.5)
                try:
                    self._open()
                    self._record('HOST_RECONNECT')
                except serial.SerialException:
                    pass
                continue
            if not chunk:
                continue
            pending += chunk
            while pending:
                if binary:
                    width, height, need, got = binary
                    take = pending[:need - len(got)]
                    got += take
                    del pending[:len(take)]
                    if len(got) == need:
                        with self.cond:
                            self.snapshots.append(Snapshot(width, height, bytes(got)))
                            self.cond.notify_all()
                        binary = None
                    continue
                end = pending.find(b'\n')
                if end < 0:
                    break
                text = pending[:end].decode(errors='replace').strip()
                del pending[:end + 1]
                if not text:
                    continue
                self._record(text)
                if text.startswith('SNAP '):
                    parts = text.split()
                    if len(parts) == 4:
                        binary = (int(parts[1]), int(parts[2]), int(parts[3]), bytearray())

    # ---- I/O
    def write(self, data):
        if isinstance(data, str):
            data = data.encode()
        with self._write_lock:
            if GUARDED_COMMAND.match(data):
                time.sleep(max(0.0, max(self._last_write, self._last_line) + COMMAND_QUIET_S - time.monotonic()))
            for attempt in range(40):
                try:
                    self.port.write(data)
                    self.port.flush()
                    self._last_write = time.monotonic()
                    return
                except (serial.SerialException, OSError, TypeError, AttributeError):
                    time.sleep(0.25)
            raise DeviceError('write failed: device not reconnecting')

    def mark(self):
        """Index to wait from: only lines received after this call match."""
        with self.cond:
            return len(self.lines)

    def wait(self, predicate, timeout, since=None, fail=None):
        """First line after `since` for which predicate(line) is true.

        predicate may be a tag string ('LAUNCH_RESULT') or a callable.
        fail: optional predicate that aborts the wait (returns None).
        """
        if isinstance(predicate, str):
            tag = predicate
            predicate = lambda line: line.text.startswith(tag)
        index = self.mark() if since is None else since
        deadline = time.monotonic() + timeout
        with self.cond:
            while True:
                while index < len(self.lines):
                    line = self.lines[index]
                    index += 1
                    if predicate(line):
                        return line
                    if fail and fail(line):
                        return None
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.cond.wait(remaining)

    def since(self, index, tag=None):
        with self.cond:
            found = self.lines[index:]
        return [l for l in found if tag is None or l.text.startswith(tag)]

    def latest(self, tag, since=0):
        with self.cond:
            for line in reversed(self.lines[since:]):
                if line.text.startswith(tag + ' '):
                    return line
        return None

    def sleep(self, seconds):
        time.sleep(seconds)

    # ---- HIL protocol
    def hil(self, command, expect=None, timeout=5):
        """Send `!command` and return its reply line (HIL_OK/HIL_ERR or expect)."""
        start = self.mark()
        self.write('!' + command + '\n')
        word = command.split()[0]
        def done(line):
            if expect and line.text.startswith(expect):
                return True
            return line.text.startswith('HIL_ERR') or (expect is None and line.text.startswith('HIL_OK ' + word))
        line = self.wait(done, timeout, since=start)
        if line is None and self._idempotent(command):
            # The device drops USB output it cannot drain within its 10 ms TX
            # timeout; re-sending a command that only sets state is safe.
            # HIL_STATUS says whether the command arrived (its reply was lost)
            # or not, and how many replies went out short.
            self._record(f'HOST_RETRY !{command} {self._lost(word)}')
            start = self.mark()
            self.write('!' + command + '\n')
            line = self.wait(done, timeout, since=start)
        if line is None:
            raise DeviceError(f'no reply to !{command}')
        if line.text.startswith('HIL_ERR'):
            raise DeviceError(f'!{command}: {line.text}')
        return line

    def _lost(self, word):
        """Classify a missing reply from HIL_STATUS (commands/last/tx_short)."""
        start = self.mark()
        self.write('!hil\n')
        status = self.wait('HIL_STATUS', 3, since=start)
        if status is None or 'last' not in status.f:
            return 'reason=unknown'
        reason = 'reply_lost' if status.f['last'] == word else 'command_lost'
        return f"reason={reason} commands={status.f.get('commands')} last={status.f['last']} tx_short={status.f.get('tx_short')}"

    @staticmethod
    def _idempotent(command):
        # Everything except commands with one-shot side effects (a pull, an
        # armed or injected fault, an upload) only sets state, so sending it
        # twice is harmless. `set` applies one settings value: applying the
        # same value again changes and saves nothing. `reset` returns sources
        # and faults to their defaults.
        words = command.split()
        if len(words) > 1 and words[1] == 'clear':
            return True
        return words[0] not in ('pull', 'arm', 'gate', 'powerlog', 'nvs', 'up', 'upload', 'fault')

    def upload(self, kind, payload, count):
        """Send a binary upload: one chunk per HIL_ACK.

        Binary bytes the device is not receiving reach its command parsers
        (on 2026-10-17 they powered two M5s off), so streaming stops at the
        first error, stale ready or missing acknowledgement, and the device is
        resynchronized before anything else is sent.
        """
        start = self.mark()
        self.write(f'!up {kind} {count} {fnv1a(payload)}\n')
        ready = self.wait(lambda l: l.text.startswith(('HIL_UP_READY', 'HIL_ERR')), 5, since=start)
        if ready is None or ready.text.startswith('HIL_ERR'):
            self._resync('refused')
            raise DeviceError(f'upload refused: {ready.text if ready else "timeout"}')
        # A ready line held in the device's TX buffer can arrive together with
        # the timeout that followed it: never stream into that.
        time.sleep(0.05)
        late = self.latest('HIL_ERR', since=start)
        if late:
            self._resync('stale_ready')
            raise DeviceError(f'upload refused: {late.text}')
        chunk = int(ready.f['chunk'])
        sent = 0
        while sent < len(payload):
            block = payload[sent:sent + chunk]
            mark = self.mark()
            self.write(block)
            sent += len(block)
            ack = self.wait(lambda l: l.text.startswith(('HIL_ACK', 'HIL_ERR')), 10, since=mark)
            if ack is None or ack.text.startswith('HIL_ERR') or int(ack.text.split()[1]) != sent:
                self._resync('ack')
                raise DeviceError(f'upload failed at {sent}: {ack.text if ack else "timeout"}')
        done = self.wait(lambda l: l.text.startswith(('HIL_OK up', 'HIL_ERR')), 10, since=start)
        if done is None or done.text.startswith('HIL_ERR'):
            self._resync('done')
            raise DeviceError(f'upload failed: {done.text if done else "timeout"}')
        return done

    def _resync(self, why):
        """After a failed upload the device swallows input until "!sync"."""
        start = self.mark()
        self.write('\n!sync\n')
        line = self.wait('HIL_SYNC', 3, since=start)
        self._record(f'HOST_RESYNC after={why} ' + (line.text if line else 'no_reply'))

    def upload_optical(self, samples):
        payload = struct.pack(f'<{len(samples)}H', *samples)
        return self.upload('opt', payload, len(samples))

    def upload_imu(self, frames):
        """frames: iterable of (ax, ay, az, gx, gy, gz) in g / dps (board axes)."""
        def clamp(v):
            return max(-32768, min(32767, int(round(v))))
        payload = bytearray()
        count = 0
        for ax, ay, az, gx, gy, gz in frames:
            payload += struct.pack('<6h', clamp(ax * 1000), clamp(ay * 1000), clamp(az * 1000),
                                   clamp(gx * 10), clamp(gy * 10), clamp(gz * 10))
            count += 1
        return self.upload('imu', bytes(payload), count)

    def status(self, timeout=4, attempts=3):
        """Request `p` + `!hil`; return {tag: fields} of the fresh status block.

        The device drops USB output that cannot drain within its 10 ms TX
        timeout (two status blocks back to back after a flash write), so a
        missing reply to this read-only query is retried.
        """
        for attempt in range(attempts):
            start = self.mark()
            self.write('p')
            self.wait('MOTION_CLIP', timeout, since=start)
            try:
                self.hil('hil', expect='HIL_FAULTS')
            except DeviceError:
                if attempt == attempts - 1:
                    raise
                self._record(f'HOST_RETRY status attempt={attempt + 1}')
                continue
            block = {}
            for line in self.since(start):
                block[line.tag] = line.f
            return block

    def snapshot(self, timeout=10):
        with self.cond:
            before = len(self.snapshots)
        self.write('v')
        deadline = time.monotonic() + timeout
        with self.cond:
            while len(self.snapshots) == before:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.cond.wait(remaining)
            return self.snapshots[-1]

    # ---- portable backup / restore (user data)
    def backup(self, timeout=30):
        start = self.mark()
        self.write('K')
        end = self.wait(lambda l: l.text.startswith(('BACKUP_END', 'BACKUP_ERROR')), timeout, since=start)
        if end is None or end.text.startswith('BACKUP_ERROR'):
            raise DeviceError(f'backup failed: {end.text if end else "timeout"}')
        lines = [l.text for l in self.since(start) if l.text.startswith(('BACKUP_BEGIN', 'BACKUP_DATA', 'BACKUP_END'))]
        return '\n'.join(lines) + '\n'

    def restore(self, backup_text, timeout=60):
        data = [l.split(' ', 1)[1] for l in backup_text.splitlines() if l.startswith('BACKUP_DATA ')]
        if not data:
            raise DeviceError('no BACKUP_DATA lines')
        start = self.mark()
        self.write('R')
        if not self.wait('RESTORE_READY', 5, since=start):
            raise DeviceError('restore not ready')
        for chunk in data:
            mark = self.mark()
            self.write(chunk + '\n')
            reply = self.wait(lambda l: l.text.startswith(('RESTORE_CHUNK', 'RESTORE_ERROR')), 5, since=mark)
            if reply is None or reply.text.startswith('RESTORE_ERROR'):
                raise DeviceError(f'restore failed: {reply.text if reply else "timeout"}')
        mark = self.mark()
        self.write('.\n')
        reply = self.wait(lambda l: l.text.startswith(('RESTORE_OK', 'RESTORE_ERROR')), timeout, since=mark)
        if reply is None or reply.text.startswith('RESTORE_ERROR'):
            raise DeviceError(f'restore failed: {reply.text if reply else "timeout"}')
        return reply
