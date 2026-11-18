#!/usr/bin/env python3
"""The USB restart wedge, recovered without a hand on the cable.

Rarely, after a restart, a unit's USB link goes dead while its application
runs on: nothing answers on the port until the cable is replugged. A release
gate of about two hours rarely finished without one, and an unattended run
ended there. The host can do what the replug does: reset the port and
enumerate the device again (usb_reenumerate.py; found on rig 14:C1 on
2026-11-15, which answered again with its boot and uptime unchanged).

Checked here, with no device: the location a wedged unit is found by, the
device picked at it, and that the rig's tools re-enumerate exactly when the
unit answers nothing at all, send a command again only when that is safe,
and carry on as before when there is no libusb or the unit is truly gone.
"""
from pathlib import Path
import sys
import threading
import types
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools/hil'
sys.path.insert(0, str(TOOLS))
# The rig modules open serial ports; none is opened here.
sys.modules.setdefault('serial', types.ModuleType('serial'))
guard = types.ModuleType('port_guard')
guard.require_free = lambda port: None
sys.modules.setdefault('port_guard', guard)
import flash  # noqa: E402
import hil_device  # noqa: E402
import upgrade_check  # noqa: E402
import usb_reenumerate  # noqa: E402
from usb_reenumerate import ESPRESSIF, Unavailable  # noqa: E402


class Location(unittest.TestCase):
    def test_pyserial_locations(self):
        self.assertEqual(usb_reenumerate.parse_location('1-1'), (1, (1,)))
        self.assertEqual(usb_reenumerate.parse_location('8-3.1'), (8, (3, 1)))
        self.assertEqual(usb_reenumerate.parse_location('8-3.1:1.0'), (8, (3, 1)))  # Linux adds the interface
        self.assertEqual(usb_reenumerate.parse_location('2'), (2, ()))
        for bad in ('', None, 'usb', '1-a', 'x-1'):
            with self.assertRaises(Unavailable):
                usb_reenumerate.parse_location(bad)

    def test_the_device_at_a_location(self):
        devices = [(8, (3, 1), ESPRESSIF, 'rig A'), (1, (1,), ESPRESSIF, 'rig B'), (1, (2,), 0x05ac, 'keyboard'), (8, (3,), 0x2109, 'hub')]
        self.assertEqual(usb_reenumerate.pick(devices, 1, (1,))[3], 'rig B')
        self.assertEqual(usb_reenumerate.pick(devices, 8, (3, 1))[3], 'rig A')
        # Something else is plugged in there now, or nothing: left alone.
        for bus, ports in ((1, (2,)), (8, (3,)), (2, (1,)), (8, (1, 3))):
            with self.assertRaises(Unavailable):
                usb_reenumerate.pick(devices, bus, ports)
        with self.assertRaises(Unavailable):
            usb_reenumerate.pick(devices + [(1, (1,), ESPRESSIF, 'twice')], 1, (1,))

    def test_reenumerate_resets_that_device_and_waits(self):
        calls, sleeps = [], []
        location = usb_reenumerate.reenumerate(location='8-3.1', settle=3.0, reset=lambda bus, ports: calls.append((bus, ports)), sleep=sleeps.append)
        self.assertEqual((location, calls, sleeps), ('8-3.1', [(8, (3, 1))], [3.0]))
        # A reset that cannot be done is not waited for.
        def refuse(bus, ports):
            raise Unavailable('libusb is not installed')
        with self.assertRaises(Unavailable):
            usb_reenumerate.reenumerate(location='1-1', reset=refuse, sleep=sleeps.append)
        self.assertEqual(sleeps, [3.0])


class Stick(hil_device.Device):
    """A HIL unit on a scripted port: it answers `!hil`, state-setting
    commands and `p`, unless it is wedged, when it answers nothing. A
    re-enumeration (when libusb is there) clears the wedge."""
    def __init__(self, wedged=False, libusb=True, mute=()):
        self.lines, self.snapshots, self.cond = [], [], threading.Condition()
        self.log = self.echo = None
        self.port_name, self.usb_location, self.reenumerations = '/dev/cu.usbmodemTEST', '1-1', 0
        self._last_line = 0.0
        self.wedged, self.libusb, self.mute = wedged, libusb, mute
        self.sent, self.resets, self.reopens = [], [], 0

    def write(self, data):
        text = (data if isinstance(data, str) else data.decode()).strip()
        self.sent.append(text)
        if self.wedged or text in self.mute:
            return
        if text == '!hil':
            self._record('HIL_STATUS boot_id=a1500220 commands=7 last=hil tx_short=0')
            self._record('HIL_FAULTS none')
        elif text.startswith('!set '):
            self._record('HIL_SET row=8 value=1 error=0')
        elif text == '!pull 60':
            self._record('HIL_PULL lead_ms=60')
        elif text == 'p':
            self._record('STATUS v=0.20.11-sticks3-songs-pm board=StickS3')
            self._record('MOTION_CLIP accel_range_g=16')

    def wait(self, predicate, timeout, since=None, fail=None):
        test = (lambda l: l.text.startswith(predicate)) if isinstance(predicate, str) else predicate
        return next((l for l in self.lines[since or 0:] if test(l)), None)

    def reopen(self, timeout=30):
        self.reopens += 1
        self._record('HOST_RECONNECT')
        return True

    def said(self, tag):
        return [l.text for l in self.lines if l.text.startswith(tag)]


class Recovery(unittest.TestCase):
    def setUp(self):
        self.real = (usb_reenumerate.reenumerate, hil_device.time.sleep)
        hil_device.time.sleep = lambda seconds: None

        def reenumerate(port=None, location=None, **kw):
            stick = self.stick
            if not stick.libusb:
                raise Unavailable('libusb is not installed (brew install libusb)')
            stick.resets.append(location)
            stick.wedged = False
            return location
        usb_reenumerate.reenumerate = reenumerate

    def tearDown(self):
        usb_reenumerate.reenumerate, hil_device.time.sleep = self.real

    def test_a_wedged_unit_is_reenumerated_and_the_command_answered(self):
        self.stick = Stick(wedged=True)
        line = self.stick.hil('hil', expect='HIL_FAULTS')
        self.assertEqual(line.text, 'HIL_FAULTS none')
        self.assertEqual((self.stick.resets, self.stick.reopens, self.stick.reenumerations), (['1-1'], 1, 1))
        self.assertEqual(self.stick.said('HOST_USB_REENUMERATE'), ['HOST_USB_REENUMERATE ok=1 n=1 location=1-1 why=!hil'])
        # And the status read, which every scenario begins with, goes through.
        self.stick = Stick(wedged=True)
        self.assertEqual(self.stick.status()['HIL_STATUS']['boot_id'], 'a1500220')
        self.assertEqual(self.stick.resets, ['1-1'])

    def test_a_state_setting_command_is_sent_again_and_a_pull_is_not(self):
        self.stick = Stick(wedged=True)
        self.assertEqual(self.stick.hil('set 8 1', expect='HIL_SET').f['error'], '0')
        self.assertEqual(self.stick.sent.count('!set 8 1'), 3)  # sent, sent again, sent after the link came back
        # A pull has a one-shot effect: never sent twice. The link is back for whatever comes next.
        self.stick = Stick(wedged=True)
        with self.assertRaises(hil_device.DeviceError):
            self.stick.hil('pull 60', expect='HIL_PULL')
        self.assertEqual((self.stick.sent.count('!pull 60'), self.stick.resets), (1, ['1-1']))
        self.assertEqual(self.stick.hil('hil', expect='HIL_FAULTS').text, 'HIL_FAULTS none')

    def test_a_unit_that_answers_is_never_reenumerated(self):
        # One command gets no reply (its output was dropped); the unit answers `!hil`.
        self.stick = Stick(mute=('!set 8 1',))
        with self.assertRaises(hil_device.DeviceError):
            self.stick.hil('set 8 1', expect='HIL_SET')
        self.assertEqual((self.stick.resets, self.stick.said('HOST_USB_REENUMERATE')), ([], []))
        self.stick = Stick()
        self.assertEqual(self.stick.hil('hil', expect='HIL_FAULTS').text, 'HIL_FAULTS none')
        self.assertEqual(self.stick.resets, [])

    def test_without_libusb_it_fails_as_before_and_says_why(self):
        self.stick = Stick(wedged=True, libusb=False)
        with self.assertRaises(hil_device.DeviceError):
            self.stick.hil('hil', expect='HIL_FAULTS')
        self.assertEqual(self.stick.said('HOST_USB_REENUMERATE'),
                         ['HOST_USB_REENUMERATE ok=0 why=!hil error=libusb_is_not_installed_(brew_install_libusb)'])
        self.assertEqual(self.stick.reopens, 0)

    def test_a_unit_that_stays_silent_is_given_up_on(self):
        self.stick = Stick(wedged=True)

        def useless(port=None, location=None, **kw):
            self.stick.resets.append(location)
            return location
        usb_reenumerate.reenumerate = useless
        for _ in range(8):
            with self.assertRaises(hil_device.DeviceError):
                self.stick.hil('hil', expect='HIL_FAULTS')
        self.assertEqual(len(self.stick.resets), hil_device.Device.MAX_REENUMERATIONS)
        self.assertIn('error=limit_of_5_reached', self.stick.said('HOST_USB_REENUMERATE')[-1])

    def test_a_production_image_status_read(self):
        self.stick = Stick(wedged=True)
        status = upgrade_check.plain_status(self.stick, timeout=0)
        self.assertEqual(status['STATUS']['v'], '0.20.11-sticks3-songs-pm')
        self.assertEqual((self.stick.resets, self.stick.sent.count('p')), (['1-1'], 2))
        self.stick = Stick()
        self.assertEqual(upgrade_check.plain_status(self.stick, timeout=0)['STATUS']['v'], '0.20.11-sticks3-songs-pm')
        self.assertEqual((self.stick.resets, self.stick.sent.count('p')), ([], 1))


class Esptool(unittest.TestCase):
    """flash.py: esptool on a wedged unit gets no serial data at all."""
    class Result:
        def __init__(self, returncode, stdout):
            self.returncode, self.stdout, self.stderr = returncode, stdout, ''

    def run_with(self, results, reenumerate):
        calls = []

        def run(command, capture_output, text):
            calls.append(command)
            return results[min(len(calls), len(results)) - 1]
        status, text = flash.esptool('esptool', '/dev/cu.usbmodemTEST', 'verify-flash', '0x10000', 'image.bin', run=run, reenumerate=reenumerate)
        return status, text, calls

    def test_no_serial_data_is_reenumerated_and_tried_once_more(self):
        resets = []
        wedged = self.Result(2, 'Connecting......\nA fatal error occurred: Failed to connect to ESP32-S3: No serial data received.\n')
        good = self.Result(0, 'Verification successful (digest matched).\n')
        status, text, calls = self.run_with([wedged, good], lambda port: resets.append(port) or '1-1')
        self.assertEqual((status, resets, len(calls)), (0, ['/dev/cu.usbmodemTEST'], 2))
        self.assertIn('re-enumerated the device at USB location 1-1', text)
        self.assertIn(flash.VERIFIED, text)
        # Still silent afterwards: reported as it is, after one more try and no more.
        status, text, calls = self.run_with([wedged, wedged], lambda port: resets.append(port) or '1-1')
        self.assertEqual((status, len(calls)), (2, 2))
        self.assertIn(flash.NO_DATA, text)

    def test_any_other_failure_and_any_success_are_left_alone(self):
        resets = []
        for result in (self.Result(0, 'Hash of data verified.\n'), self.Result(2, 'A fatal error occurred: MD5 of file does not match data in flash!\n')):
            status, text, calls = self.run_with([result], lambda port: resets.append(port) or '1-1')
            self.assertEqual((status, len(calls), resets), (result.returncode, 1, []))

    def test_without_libusb_the_failure_stands_with_the_reason(self):
        def refuse(port):
            raise Unavailable('libusb is not installed (brew install libusb)')
        wedged = self.Result(2, 'No serial data received.\n')
        status, text, calls = self.run_with([wedged], refuse)
        self.assertEqual((status, len(calls)), (2, 1))
        self.assertIn('USB re-enumeration not possible: libusb is not installed', text)


if __name__ == '__main__':
    unittest.main()
