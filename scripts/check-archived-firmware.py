#!/usr/bin/env python3
"""Exercise archived entry points offline, without compiling or using hardware.

The real shell scripts run against recording Arduino/compiler adapters. This
checks cwd-independent routing, frozen input selection and temporary cleanup;
the real toolchain and sanitizer suites are separate validation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
relative = 'firmware/releases/0.13.8-sticks3-fifo-timing-pm'
parser = argparse.ArgumentParser()
parser.add_argument('--baseline-ref', help='Run the same checks against old scripts')
args = parser.parse_args()


def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if executable:
        path.chmod(0o755)


with tempfile.TemporaryDirectory(prefix='launchlab archive paths ') as temp:
    base = Path(temp).resolve()
    repo = base / 'checkout with spaces'
    archive = repo / relative
    for name in ['build-firmware.sh', 'test-firmware.sh']:
        text = (subprocess.check_output(['git', 'show', f'{args.baseline_ref}:{relative}/{name}'], cwd=root, text=True)
                if args.baseline_ref else (root / relative / name).read_text())
        write(archive / name, text)
    write(archive / 'source/LaunchLabMini.ino', 'FROZEN_SOURCE\n')
    write(archive / 'source/header.h', 'FROZEN_HEADER\n')
    write(archive / 'source/src/vqf/vqf.cpp', 'FROZEN_LIBRARY\n')
    write(repo / 'firmware/LaunchLabMini/LaunchLabMini.ino', 'WRONG_WORKING_SOURCE\n')
    write(repo / 'firmware/LaunchLabRpm/analog_tachometer.h', 'LEGACY_FIXTURE\n')
    write(repo / 'firmware/tests/test_accel_range.cpp', 'NATIVE_FIXTURE\n')
    write(repo / 'firmware/releases/0.6.2-sticks3-qre1113-rotation/source/analog_tachometer.h', 'HISTORICAL_FIXTURE\n')
    write(repo / 'firmware/tools/check_stack_usage.py', '''from pathlib import Path
import os,sys
assert Path.cwd()==Path(os.environ['ROUTING_REPO'])/'firmware'
assert Path(sys.argv[1])==Path.cwd()/'build'
assert Path(sys.argv[1]).is_dir()
''')
    sdk = base / 'sdk with spaces'
    header = sdk / 'esp32s3/qio_opi/include/sdkconfig.h'
    write(header, '#define CONFIG_PM_ENABLE 1\n#define CONFIG_PM_PROFILING 1\n')
    write(sdk / 'sha256-manifest.json', json.dumps({str(header.relative_to(sdk)): hashlib.sha256(header.read_bytes()).hexdigest()}))
    cli = base / 'arduino adapter'
    write(cli, '''#!/usr/bin/env python3
from pathlib import Path
import json,os,sys
repo=Path(os.environ['ROUTING_REPO']);assert Path.cwd()==repo/'firmware'
args=sys.argv[1:]
if args[0]=='compile':
 assert Path(args[-1]).resolve()==repo/'firmware/build-source/LaunchLabMini'
 assert Path(args[-1],'LaunchLabMini.ino').read_text()=='FROZEN_SOURCE\\n'
 assert Path(args[-1],'header.h').read_text()=='FROZEN_HEADER\\n'
 assert Path(args[-1],'src/vqf/vqf.cpp').read_text()=='FROZEN_LIBRARY\\n'
 build=Path(args[args.index('--build-path')+1]);assert build==repo/'firmware/build';build.mkdir()
 assert '-DLAUNCHLAB_CPU_PM=1' in ' '.join(args)
with open(os.environ['ROUTING_LOG'],'a') as log:log.write(json.dumps(args)+'\\n')
''', True)
    compiler = base / 'compiler adapter'
    write(compiler, '''#!/usr/bin/env python3
from pathlib import Path
import os
assert Path('firmware/tests/test_accel_range.cpp').read_text()=='NATIVE_FIXTURE\\n'
assert Path('firmware/LaunchLabMini/LaunchLabMini.ino').read_text()=='FROZEN_SOURCE\\n'
assert Path('firmware/LaunchLabMini/header.h').read_text()=='FROZEN_HEADER\\n'
assert Path('firmware/LaunchLabRpm/analog_tachometer.h').read_text()=='LEGACY_FIXTURE\\n'
assert Path('firmware/releases/0.6.2-sticks3-qre1113-rotation/source/analog_tachometer.h').read_text()=='HISTORICAL_FIXTURE\\n'
Path(os.environ['ROUTING_TEST_CWD']).write_text(str(Path.cwd()))
raise SystemExit(23) # Stop before native compilation; routing is proven.
''', True)
    env = {**os.environ, 'ARDUINO_CLI': str(cli), 'CXX': str(compiler),
           'LAUNCHLAB_ARDUINO_CACHE': str(base / 'cache with spaces'),
           'LAUNCHLAB_PM_SDK': str(sdk), 'ROUTING_REPO': str(repo),
           'ROUTING_LOG': str(base / 'cli.jsonl'),
           'ROUTING_TEST_CWD': str(base / 'test-cwd')}
    unrelated = base / 'unrelated directory'
    unrelated.mkdir()
    build = subprocess.run(['bash', str(archive / 'build-firmware.sh')], cwd=unrelated, env=env, capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    calls = [json.loads(line) for line in (base / 'cli.jsonl').read_text().splitlines()]
    assert [c[0] for c in calls] == ['core', 'core', 'lib', 'compile'], calls
    test = subprocess.run(['bash', str(archive / 'test-firmware.sh')], cwd=unrelated, env=env, capture_output=True, text=True)
    assert test.returncode == 23, test.stderr
    staged = Path((base / 'test-cwd').read_text())
    assert staged != repo and not staged.exists(), 'Temporary test tree must be removed'
    assert (repo / 'firmware/LaunchLabMini/LaunchLabMini.ino').read_text() == 'WRONG_WORKING_SOURCE\n'

print('PASS: archived build/test entry points resolve checkout from unrelated cwd with spaces; frozen source is selected; working source preserved; test staging cleaned; no compilation/network/device')
