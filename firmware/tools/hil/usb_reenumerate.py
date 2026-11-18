#!/usr/bin/env python3
"""Re-enumerate one USB device from the host: a cable replug, without the hand.

The USB restart wedge: rarely, after a software restart, a StickS3's
USB-Serial-JTAG link goes dead while its application keeps running. The serial
port stays listed, but nothing answers on it, esptool reports "No serial data
received", and the device no longer answers even a string-descriptor request
on its control endpoint. Unplugging and replugging the cable has always cleared
it, with no restart of the stick. So does asking the host to reset the port and
enumerate the device again (libusb_reset_device, which on macOS is
USBDeviceReEnumerate): found on 2026-11-15 on rig 14:C1, which answered again
with the same boot and 22 minutes of uptime.

A wedged device cannot be asked for its serial number, so it is found by where
it is plugged in: pyserial's location for its port ("1-1", "8-3.1": the bus,
then the hub ports), which the system keeps from when the device enumerated.

  python3 firmware/tools/hil/usb_reenumerate.py /dev/cu.usbmodem1101

Needs libusb (brew install libusb / apt install libusb-1.0-0). Without it the
tools say so and carry on as before: a person replugs the cable.
"""
import ctypes
import ctypes.util
import sys
import time

ESPRESSIF = 0x303a
LIBRARIES = ('/opt/homebrew/lib/libusb-1.0.dylib', '/usr/local/lib/libusb-1.0.dylib', 'libusb-1.0.so.0', 'libusb-1.0.dylib')


class Unavailable(RuntimeError):
    """No libusb, or no such device: nothing was reset."""


def parse_location(text):
    """pyserial's location ("8-3.1", also "8-3.1:1.0" on Linux) as (bus, (port, ...))."""
    if not text:
        raise Unavailable('the port has no USB location')
    bus, _, path = text.split(':')[0].partition('-')
    try:
        return int(bus), tuple(int(p) for p in path.split('.')) if path else ()
    except ValueError:
        raise Unavailable(f'cannot read the USB location {text!r}') from None


def location_of(port):
    """The USB location of a serial port, from pyserial."""
    from serial.tools import list_ports
    for p in list_ports.comports():
        if p.device == port:
            return p.location
    raise Unavailable(f'{port} is not listed')


def pick(devices, bus, ports):
    """The one device at (bus, ports) among (bus, ports, vendor, handle) entries.
    Espressif's only: a location that now holds something else is left alone."""
    found = [d for d in devices if d[0] == bus and tuple(d[1]) == tuple(ports) and d[2] == ESPRESSIF]
    if len(found) != 1:
        raise Unavailable(f'{len(found)} Espressif devices at USB location {bus}-{".".join(map(str, ports))}')
    return found[0]


def load():
    names = [ctypes.util.find_library('usb-1.0'), *LIBRARIES]
    for name in names:
        if not name:
            continue
        try:
            return ctypes.CDLL(name)
        except OSError:
            continue
    raise Unavailable('libusb is not installed (brew install libusb)')


class Descriptor(ctypes.Structure):
    _fields_ = [('bLength', ctypes.c_uint8), ('bDescriptorType', ctypes.c_uint8), ('bcdUSB', ctypes.c_uint16),
                ('bDeviceClass', ctypes.c_uint8), ('bDeviceSubClass', ctypes.c_uint8), ('bDeviceProtocol', ctypes.c_uint8),
                ('bMaxPacketSize0', ctypes.c_uint8), ('idVendor', ctypes.c_uint16), ('idProduct', ctypes.c_uint16),
                ('bcdDevice', ctypes.c_uint16), ('iManufacturer', ctypes.c_uint8), ('iProduct', ctypes.c_uint8),
                ('iSerialNumber', ctypes.c_uint8), ('bNumConfigurations', ctypes.c_uint8)]


def reset_at(bus, ports):
    """Reset and re-enumerate the Espressif device at that location. Raises Unavailable."""
    usb = load()
    usb.libusb_get_device_list.restype = ctypes.c_ssize_t
    usb.libusb_error_name.restype = ctypes.c_char_p
    context = ctypes.c_void_p()
    if usb.libusb_init(ctypes.byref(context)):
        raise Unavailable('libusb would not start')
    listing = ctypes.POINTER(ctypes.c_void_p)()
    try:
        count = usb.libusb_get_device_list(context, ctypes.byref(listing))
        if count < 0:
            raise Unavailable('libusb could not list the devices')
        try:
            devices = []
            for i in range(count):
                device = ctypes.c_void_p(listing[i])
                descriptor = Descriptor()
                if usb.libusb_get_device_descriptor(device, ctypes.byref(descriptor)):
                    continue
                path = (ctypes.c_uint8 * 8)()
                depth = usb.libusb_get_port_numbers(device, path, 8)
                devices.append((usb.libusb_get_bus_number(device), tuple(path[:max(depth, 0)]), descriptor.idVendor, device))
            device = pick(devices, bus, ports)[3]
            handle = ctypes.c_void_p()
            rc = usb.libusb_open(device, ctypes.byref(handle))
            if rc:
                raise Unavailable(f'could not open the device: {usb.libusb_error_name(rc).decode()}')
            try:
                rc = usb.libusb_reset_device(handle)
            finally:
                usb.libusb_close(handle)
            # NOT_FOUND is the device having gone and come back under a new handle: the reset happened.
            if rc and usb.libusb_error_name(rc) != b'LIBUSB_ERROR_NOT_FOUND':
                raise Unavailable(f'the reset failed: {usb.libusb_error_name(rc).decode()}')
        finally:
            usb.libusb_free_device_list(listing, 1)
    finally:
        usb.libusb_exit(context)


def reenumerate(port=None, location=None, settle=3.0, reset=reset_at, sleep=time.sleep):
    """Re-enumerate the device behind `port` (or at `location`); its location as text.
    The serial port goes away and comes back: wait `settle` before using it."""
    location = location or location_of(port)
    bus, ports = parse_location(location)
    reset(bus, ports)
    sleep(settle)
    return location


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__)
        return 2
    try:
        print(f're-enumerated the device at USB location {reenumerate(argv[0])}')
    except Unavailable as e:
        print(f'not reset: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
