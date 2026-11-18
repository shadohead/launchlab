#!/usr/bin/env python3
"""Restart a HIL unit over and over and count the USB restart wedges.

Each restart is the scenarios' own (`!reboot`, then the new boot's status).
A unit that answers nothing afterwards is wedged: the rig's tools re-enumerate
it from the host (hil_device.py, usb_reenumerate.py) and the run goes on. This
says how often that happened, whether every one was recovered, and that the
application had kept running (the boot id from before the recovery).

  python3 firmware/tools/hil/usb_wedge_soak.py --port PORT --out DIR --restarts 60
  python3 firmware/tools/hil/usb_wedge_soak.py --port PORT --out DIR --restarts 30 --bluetooth

--bluetooth restarts by turning Bluetooth on and off (Apply's restart, the one
the wedge hit on 2026-11-15) instead of `!reboot`. Before the restarts the
unit is re-enumerated once on purpose, healthy, to show the tools reconnect
after a re-enumeration with the unit's boot unchanged.

Exit status 0 when every restart ended with the unit answering.
"""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device, DeviceError  # noqa: E402
from rig import Rig  # noqa: E402
import scenarios  # noqa: E402
import ble_scenarios  # noqa: E402
import port_guard  # noqa: E402


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--restarts', type=int, default=60)
    p.add_argument('--bluetooth', action='store_true', help="restart by turning Bluetooth on and off (Apply's restart)")
    port_guard.add_arguments(p)
    a = p.parse_args(argv)
    port_guard.apply(a, a.port)
    a.out.mkdir(parents=True, exist_ok=True)
    dev = Device(a.port, log_path=a.out / 'serial.log')
    rig = Rig(dev, a.out)
    record = {'port': a.port, 'restart': 'bluetooth' if a.bluetooth else 'reboot', 'restarts': 0, 'wedges': [], 'unrecovered': None, 'forced': None}
    try:
        print(f'{a.port}: {rig.require_hil_build()}, USB location {dev.usb_location}')
        rig.begin('usb_wedge_soak')
        # A re-enumeration of a healthy unit: the port goes and comes back, the reader
        # reconnects, and the unit answers with the same boot (the app never restarted).
        before = rig.hil_boot().get('boot_id')
        forced = dev.reenumerate('forced')
        after = rig.hil_boot().get('boot_id') if forced else None
        record['forced'] = {'ok': forced, 'boot_before': before, 'boot_after': after}
        rig.check('a forced re-enumeration of a healthy unit: the tools reconnect, and its boot is unchanged',
                  forced and before is not None and after == before, str(record['forced']))
        print(f"forced re-enumeration: ok={forced} boot {before} -> {after}")
        for n in range(1, a.restarts + 1):
            before, started = dev.reenumerations, time.monotonic()
            try:
                if a.bluetooth:
                    ble_scenarios.set_bluetooth(rig, n % 2 == 1)
                else:
                    scenarios.reboot(rig)
                status = rig.hil_boot()
            except DeviceError as e:
                record['unrecovered'] = {'restart': n, 'error': str(e)}
                print(f'restart {n}: NOT RECOVERED: {e}')
                break
            record['restarts'] = n
            if dev.reenumerations > before:
                wedge = {'restart': n, 'boot_id': status.get('boot_id'), 'uptime_ms': status.get('uptime_ms'),
                         'seconds': round(time.monotonic() - started, 1)}
                record['wedges'].append(wedge)
                print(f'restart {n}: wedged, re-enumerated, answering again after {wedge["seconds"]} s '
                      f'(boot {wedge["boot_id"]}, up {wedge["uptime_ms"]} ms: it had kept running)')
            elif n % 10 == 0:
                print(f'restart {n}: {len(record["wedges"])} wedges so far')
    finally:
        if a.bluetooth:
            try:
                if dev.status().get('BLE_STATUS', {}).get('started') == '1':
                    ble_scenarios.set_bluetooth(rig, False)
            except DeviceError as e:
                print(f'!! could not turn Bluetooth off again: {e}')
        (a.out / 'usb-wedge-soak.json').write_text(json.dumps(record, indent=1) + '\n')
        dev.close()
    print(f'{record["restarts"]} restarts, {len(record["wedges"])} wedges, '
          + ('all recovered' if not record['unrecovered'] else f'one NOT recovered at restart {record["unrecovered"]["restart"]}'))
    forced_ok = bool(record['forced'] and record['forced']['ok'] and record['forced']['boot_after'] == record['forced']['boot_before'])
    return 1 if record['unrecovered'] or record['restarts'] < a.restarts or not forced_ok else 0


if __name__ == '__main__':
    sys.exit(main())
