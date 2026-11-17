#!/usr/bin/env python3
"""Refuse to open a device port another process holds.

Several sessions share the HIL units. Every tool that opens a port checks
`lsof` immediately before, and refuses if any other process has it open,
whatever it was told about the port being free. Run as a command it guards
one esptool call:

  python firmware/tools/hil/port_guard.py ESPTOOL --port /dev/cu.usbmodem83201 ... (esptool arguments)
"""
import os
import subprocess
import sys


class PortBusy(RuntimeError):
    pass


def holders(port):
    """PIDs other than this process with `port` open, from lsof."""
    out = subprocess.run(['lsof', '-t', port], capture_output=True, text=True)
    return [int(p) for p in out.stdout.split() if p.strip().isdigit() and int(p) != os.getpid()]


def require_free(port):
    pids = holders(port)
    if pids:
        detail = subprocess.run(['ps', '-o', 'pid,command', '-p', ','.join(map(str, pids))], capture_output=True, text=True).stdout
        raise PortBusy(f'{port} is held by another process; refusing to open it:\n{detail}')


def main():
    if len(sys.argv) < 2 or '--port' not in sys.argv:
        raise SystemExit(__doc__)
    port = sys.argv[sys.argv.index('--port') + 1]
    try:
        require_free(port)
    except PortBusy as e:
        raise SystemExit(str(e))
    os.execvp(sys.argv[1], sys.argv[1:])


if __name__ == '__main__':
    main()
