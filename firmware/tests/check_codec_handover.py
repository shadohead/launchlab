#!/usr/bin/env python3
"""The mic powers the ES8311 back up after a sound has powered it down.

From 0.20.6 every sound ends with Es8311Power::down (es8311_power.h), because
M5Unified's speaker stop only switches the amplifier off. The Mic RPM page
(0.20.7) then starts the mic through its own register list in mic_listener.h
(MicListener::enabled), on the same codec. Every register the power-down
writes must either be rewritten by that list with the mic's value, or belong
to the DAC and output side, which the mic never uses. A new power-down write on
the ADC side (system power 0x0E, the PGA 0x14, the ADC 0x15-0x1C) would leave
the mic reading zeros after the first sound until a reboot.
"""
from pathlib import Path
import re
import sys

FIRMWARE = Path(__file__).resolve().parents[1]
POWER = FIRMWARE / 'LaunchLabMini/es8311_power.h'
MIC = FIRMWARE / 'LaunchLabMini/mic_listener.h'

# ES8311 registers only the playback path uses: DAC power (0x12), the
# headphone/line driver (0x13) and the DAC block 0x31-0x37 (volume, EQ, ramp).
DAC_SIDE = {0x12, 0x13} | set(range(0x31, 0x38))


def pairs(text):
    return [(int(r, 16), int(v, 16)) for r, v in re.findall(r'\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}', text)]


def main():
    power = POWER.read_text()
    down = pairs(power[power.index('DOWN[]'):power.index('};', power.index('DOWN[]'))])
    mic = MIC.read_text()
    on_at = mic.index('ON[][2]=')
    on = dict(pairs(mic[on_at:mic.index('};', on_at)]))
    problems = []
    if not down:
        problems.append('no writes found in Es8311Power::DOWN')
    if not on:
        problems.append("no writes found in MicListener::enabled's ON list")
    for reg, value in down:
        if reg in DAC_SIDE:
            continue
        if reg not in on:
            problems.append(f'Es8311Power::DOWN writes 0x{reg:02X}=0x{value:02X}, which the mic\'s ON list never rewrites')
    # The two power registers the power-down leaves off must come back on.
    if on.get(0x00, 0) & 0x80 == 0:
        problems.append('the mic\'s ON list does not start the codec state machine (0x00 bit 7)')
    if on.get(0x0D) != 0x01:
        problems.append('the mic\'s ON list does not power the analog section up (0x0D=0x01)')
    for problem in problems:
        print('FAIL:', problem)
    if problems:
        sys.exit(1)
    print(f'PASS: the mic rewrites every ADC-side register of the codec power-down '
          f'({", ".join(f"0x{r:02X}" for r, _ in down if r not in DAC_SIDE)}); '
          f'{sum(r in DAC_SIDE for r, _ in down)} DAC-side writes need nothing')


if __name__ == '__main__':
    main()
