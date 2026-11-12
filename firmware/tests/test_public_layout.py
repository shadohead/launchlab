"""The public launchlab repo's sketch folder keeps its own copy of the RPM
header (firmware/LaunchLabMini/analog_tachometer.h, the compiled snapshot's
form), where this repo has only firmware/LaunchLabRpm/analog_tachometer.h.
A test that includes the RPM header directly and also a sketch header that
includes "analog_tachometer.h" compiles here (one file, #pragma once) but
defines AnalogTachometer twice there (0.20.1's test_edge_interpolation).
Each test must include the detector one way only."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / 'firmware/LaunchLabMini'
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
RPM_HEADER = '../LaunchLabRpm/analog_tachometer.h'


def sketch_includes_detector(name, seen=None):
    """Whether sketch header `name` reaches "analog_tachometer.h" through its own quoted includes."""
    seen = set() if seen is None else seen
    if name in seen:
        return False
    seen.add(name)
    path = SKETCH / name
    if name == 'analog_tachometer.h':
        return True
    if not path.is_file():
        return False
    return any(sketch_includes_detector(Path(inc).name, seen) for inc in INCLUDE.findall(path.read_text()) if not inc.startswith('..'))


def double_includes(text):
    includes = INCLUDE.findall(text)
    if RPM_HEADER not in includes:
        return []
    return [inc for inc in includes if inc.startswith('../LaunchLabMini/') and sketch_includes_detector(inc.split('/')[-1])]


# The pattern is caught: the 0.20.1 test as first written.
assert double_includes(f'#include "{RPM_HEADER}"\n#include "../LaunchLabMini/sensor_profile.h"\n') == ['../LaunchLabMini/sensor_profile.h']
assert double_includes('#include "../LaunchLabMini/sensor_profile.h"\n') == []

bad = {p.name: d for p in sorted((ROOT / 'firmware/tests').glob('*.cpp')) if (d := double_includes(p.read_text()))}
if bad:
    for name, headers in bad.items():
        print(f'{name}: includes {RPM_HEADER} and {", ".join(headers)}, which includes "analog_tachometer.h": '
              'in the public repo that is a second copy of AnalogTachometer. Include the detector one way only.')
    sys.exit(1)
print('PASS: no test includes the RPM header both directly and through a sketch header (the public layout compiles)')
