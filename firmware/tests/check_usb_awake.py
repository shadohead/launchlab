#!/usr/bin/env python3
"""Compile the real USB policy header with sanitizers; no hardware access."""
import argparse
import pathlib
import subprocess
import tempfile

parser=argparse.ArgumentParser()
location=pathlib.Path(__file__).resolve()
default_firmware=location.parents[1] if (location.parents[1]/'LaunchLabMini/usb_awake.h').exists() else location.parents[2]/'qa-integration/firmware'
parser.add_argument('firmware',nargs='?',type=pathlib.Path,
                    default=default_firmware)
args=parser.parse_args()
with tempfile.TemporaryDirectory() as tmp:
    exe=pathlib.Path(tmp)/'usb_awake'
    subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(args.firmware/'LaunchLabMini'),str(pathlib.Path(__file__).with_name('test_usb_awake.cpp')),
                    '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
