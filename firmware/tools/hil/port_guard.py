#!/usr/bin/env python3
"""Refuse to open a device port that belongs to the owner or is held by another process.

Every tool that flashes or drives a unit calls require_free() immediately before
it opens the port, and refuses when either check fails, whatever it was told:

- the unit: the port's USB serial (an M5's MAC), read from pyserial's list_ports
  without opening the port, must not be one of the owner's units (PROTECTED)
  unless the owner allowed that unit for this run (--owner-ok MAC). A port whose
  serial cannot be read is refused too, unless --unknown-unit-ok: port names are
  reused when units are swapped, so a port is never an identity. The check runs
  at every open, so a unit swapped during a run is caught when the port reopens;
- the port: `lsof` must show no other process holding it, since several sessions
  share the HIL units.

A tool takes the overrides with add_arguments() and apply(); without them nothing
is allowed. Run as a command it guards one esptool call (the guard's own options
first):

  python firmware/tools/hil/port_guard.py [--owner-ok MAC] [--unknown-unit-ok] ESPTOOL --port /dev/cu.usbmodem83201 ...
"""
import os
import re
import subprocess
import sys
import time

# The owner's units: owner_units.py, the one list (gate_plan.PROTECTED is it too). It stays
# private, so the public copy, which has no owner's units, protects none.
try:
    from owner_units import PROTECTED
except ImportError:
    PROTECTED = {}
# How long a port list_ports does not list (a unit re-enumerating after a reset) is waited for.
APPEAR_SECONDS = 10.0


class PortBusy(RuntimeError):
    """The port may not be opened: another process holds it, or the unit on it is refused."""


class UnitRefused(PortBusy):
    """The unit on the port is one of the owner's, or cannot be identified."""


class UnitUnknown(UnitRefused):
    """The port's USB serial cannot be read."""


ALLOWED = {'owner_ok': frozenset(), 'unknown_ok': False}


def serial_key(serial):
    """A USB serial reduced to its hex digits in upper case: 7c:e8:... and 7CE8... are one unit."""
    return re.sub(r'[^0-9A-F]', '', (serial or '').upper())


PROTECTED_KEYS = {serial_key(mac): mac for mac in PROTECTED}


def comports():
    """pyserial's list_ports entries, or None without pyserial. Never opens a port."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    return list_ports.comports()


def device_name(port):
    """A port as list_ports and the tools name it: a symlink resolved, a tty as its callout device."""
    return os.path.realpath(port).replace('/dev/tty.', '/dev/cu.')


def listed(port, ports):
    return any(device_name(p.device) == device_name(port) for p in ports or ())


def usb_serial(port, ports=None):
    """The USB serial list_ports gives for `port`, or None when it lists none, several or not the port."""
    ports = comports() if ports is None else ports
    found = {p.serial_number.strip() for p in ports or ()
             if device_name(p.device) == device_name(port) and (p.serial_number or '').strip()}
    return found.pop() if len(found) == 1 else None


def allow(owner_ok=(), unknown_ok=False):
    """What this run may open besides other units: these protected units, and unidentified ones."""
    ALLOWED['owner_ok'] = frozenset(serial_key(mac) for mac in owner_ok)
    ALLOWED['unknown_ok'] = bool(unknown_ok)


def forward():
    """This run's guard options, for a tool it starts on the same unit."""
    return [arg for key in sorted(ALLOWED['owner_ok']) for arg in ('--owner-ok', PROTECTED_KEYS.get(key, key))] + \
        (['--unknown-unit-ok'] if ALLOWED['unknown_ok'] else [])


def require_unit(port, wait=None, sleep=time.sleep, clock=time.monotonic):
    """The USB serial of the unit on `port`. UnitRefused for one of the owner's units without their
    OK; UnitUnknown when the serial cannot be read (no pyserial, the port still not listed after
    `wait` seconds, no serial or several) unless unidentified units are allowed."""
    deadline = clock() + (APPEAR_SECONDS if wait is None else wait)
    while True:
        ports = comports()
        serial = usb_serial(port, ports) if ports is not None else None
        if serial or ports is None or listed(port, ports) or clock() >= deadline:
            break
        sleep(0.2)
    if serial is None:
        if ALLOWED['unknown_ok']:
            return None
        why = ('pyserial is not installed' if ports is None else
               'list_ports does not list it' if not listed(port, ports) else 'list_ports gives it no single USB serial')
        raise UnitUnknown(f'{port}: cannot tell which unit it is ({why}), and it could be one of the owner\'s '
                          f'({", ".join(PROTECTED)}); refusing to open it. --unknown-unit-ok opens it anyway, '
                          'for a unit known not to be the owner\'s.')
    mac = PROTECTED_KEYS.get(serial_key(serial))
    if mac and serial_key(mac) not in ALLOWED['owner_ok']:
        raise UnitRefused(f'{port} is {mac}, {PROTECTED[mac]}: it never gets a test image or a tool run without the '
                          f'owner\'s OK (--owner-ok {mac}); refusing to open it.')
    return serial


def holders(port):
    """PIDs other than this process with `port` open, from lsof."""
    out = subprocess.run(['lsof', '-t', port], capture_output=True, text=True)
    return [int(p) for p in out.stdout.split() if p.strip().isdigit() and int(p) != os.getpid()]


def require_free(port):
    """Refuse (PortBusy) the owner's units and unidentified ones (require_unit), then a port another process holds."""
    require_unit(port)
    pids = holders(port)
    if pids:
        detail = subprocess.run(['ps', '-o', 'pid,command', '-p', ','.join(map(str, pids))], capture_output=True, text=True).stdout
        raise PortBusy(f'{port} is held by another process; refusing to open it:\n{detail}')


def add_arguments(parser):
    """The guard's options on a tool's command line."""
    parser.add_argument('--owner-ok', action='append', default=[], metavar='MAC',
                        help=f'the owner allowed this protected unit for this run ({", ".join(PROTECTED)})')
    parser.add_argument('--unknown-unit-ok', action='store_true',
                        help="open a port whose USB serial cannot be read (only a unit known not to be the owner's)")


def apply(args, port=None):
    """Take a tool's parsed guard options and, given its port, check it now: SystemExit on a refusal."""
    allow(args.owner_ok, args.unknown_unit_ok)
    if port is not None:
        try:
            require_free(port)
        except PortBusy as e:
            raise SystemExit(f'refused: {e}')


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    owner_ok, unknown_ok = [], False
    while argv and argv[0] in ('--owner-ok', '--unknown-unit-ok'):
        if argv.pop(0) == '--unknown-unit-ok':
            unknown_ok = True
        elif argv:
            owner_ok.append(argv.pop(0))
    if not argv or '--port' not in argv or argv.index('--port') + 1 >= len(argv):
        raise SystemExit(__doc__)
    port = argv[argv.index('--port') + 1]
    allow(owner_ok, unknown_ok)
    try:
        require_free(port)
    except PortBusy as e:
        raise SystemExit(f'refused: {e}')
    os.execvp(argv[0], argv)


if __name__ == '__main__':
    main()
