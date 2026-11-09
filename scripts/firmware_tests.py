#!/usr/bin/env python3
"""LaunchLab firmware host suite: every regression test that needs no device.

Runs the ASan/UBSan C++ suites, the Python harnesses that compile real sketch
code with fakes, and the replay checks on the retained public evidence, in
parallel (one job per core by default). Each suite's output is captured and
printed as one block; failures always print in full. Exit status is non-zero
when any suite fails.

  scripts/test-firmware.sh                 # parallel, one status line per suite
  scripts/test-firmware.sh --serial        # one at a time, live output, no cache (debugging)
  scripts/test-firmware.sh -k usb -v       # only suites whose name contains "usb"
  scripts/test-firmware.sh --no-cache --result-json OUT.json   # release run
  scripts/test-firmware.sh --list

Compiles go through a cache keyed on preprocessed sources (host_cc_cache.py);
the test binaries always run. The build gate (firmware/tools/host_suite_gate.sh)
uses --gate-dir: the run is tied to a fingerprint of the exact working tree,
refused when the tree changes during the run, and a pass is remembered so an
unchanged tree is not tested twice.

Every test_*.cpp and check_*.py in firmware/tests must be a suite here or be
listed in NOT_IN_HOST_SUITE with a reason, so a new test cannot be left out.

The public launchlab repository holds a copy of this file and of every test
that needs nothing private (scripts/sync_public_firmware.py). Its
firmware/SYNC.json marks that copy: there the PRIVATE_ONLY suites are not
run, and their files must not be present.
"""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
TESTS = 'firmware/tests'
# Everything the suite and the firmware build read. Build outputs are ignored.
FINGERPRINT_PATHS = ['firmware', 'scripts/test-firmware.sh', 'scripts/firmware_tests.py', 'scripts/host_cc_cache.py']
SUITE_TIMEOUT_S = 900
TREE_CHANGED = 3  # exit status: the result does not belong to one tree

SANITIZE = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
INCLUDES = ['-Ifirmware/tests/fakes', '-Ifirmware/LaunchLabRpm', '-Ifirmware/LaunchLabMini']

# C++ suites built with the VQF filter (single precision, as on the device).
VQF_SUITES = '''
accel_range bmi270_fifo i2c_bus_recovery appearance nvs_budget best_effect_sound
device_backup device_wipe wipe_confirmation sticks3 sticks3_level sticks3_power
motion_activity sensor_idle launch_feedback launch_motion motion_replay
motion_store practice_history practice_store orientation_estimator
mount_orientation mount_orientation_setting analog_tachometer swing_reset
edge_start edge_interpolation sound_interference start_mark_reference startup_single_peak paired_rpm_estimators rpm_hardening
rpm_estimator_setting sensor_profile_setting display_flip wake_ui_state controls
controls_ui power_diagnostics weak_slowdown power_signal_recovery store_presence
practice_views history_idle_checkpoint usb_awake usb_tx hil_resync
usb_command_guard pm1_idle power_key palette milestones physical_effects classic_effects
elemental_effects effect_set effect_set_compat milestone_rollback flash_log
eva_effects slayer_effects ninja_effects nier_effects sorcery_effects
aura_effects alchemy_effects phantom_effects armory_effects grimoire_effects ripsaw_effects corsair_effects reaper_effects
fx_effects fx_sound fx_music music_stream fx_fuzz fx_share effect_store loaded_compat loaded_effects fx_transfer
shake_wake wear_check rpm_curve power_lab frame_rows adc_clock mic_rpm mic_view mic_session
'''.split()

# Libraries a VQF suite links besides the filter: share codes are zlib streams.
SUITE_LIBS = {'fx_share': ['-lz']}

# C++ suites for the original LaunchLab RPM board (firmware/LaunchLabRpm).
RPM_BOARD_SUITES = 'launch_detector launch_history optical_calibration touch_filter'.split()

# Python harnesses that extract and compile production sketch code.
PYTHON_HARNESSES = '''
motion_playback motion_pipeline power_log_retry history_retry usb_pipeline
imu_startup mount_pipeline wipe_pipeline controls_pipeline best_sound_pipeline
pm1_idle power_key_pipeline idle_power_pipeline sensor_idle_pipeline save_paths
cpu_power_pipeline hil_isolation mic_isolation usb_flush hil_rig_upload usb_command_pipeline
ui_render hil_themes hil_milestones hil_commands usb_config wear_pipeline usb_standby_pipeline keep_awake status_feeds
codec_handover main_layout
'''.split()

# Python unit tests: the private-corpus runner's synthetic contracts (no
# private data), the release gate, the host suite's compile cache, the
# build gate, the build's static RAM check, the public repo's include layout,
# the CI checkout's frozen releases, the contributor proof and the public sync.
PYTHON_TESTS = '''
captured_replay_runner mic_capture release_gate firmware_channels host_cc_cache build_gate static_ram hil_overhead
hil_settings_walk public_layout ci_checkout hil_status_read hil_mic_stop_wait firmware_proof sync_public_firmware
'''.split()

# Replay checks on firmware/evidence: (check script, replay source or None).
REPLAY_CHECKS = [
    ('check_analog_replays', 'replay_analog_tachometer.cpp'),
    ('check_sticks3_replays', 'replay_sticks3.cpp'),
    ('check_tcrt_sensitivity', 'replay_sticks3.cpp'),
    ('check_paired_rpm_replays', None),
    ('check_start_floor_replays', 'replay_sticks3.cpp'),
]

NOT_IN_HOST_SUITE = {
    'check_captured_replays.py': 'private recordings; run scripts/test-real-captures.sh',
    'check_usb_awake.py': 'compiles test_usb_awake.cpp, already the usb_awake suite',
}

# Suites that need what only this repository has. The public copy leaves
# their files out and runs every other suite.
RECORDINGS = 'replays the pull recordings in firmware/evidence, which stay private'
PRIVATE_ONLY = {
    **{check: RECORDINGS for check, _ in REPLAY_CHECKS},
    'check_wear_pipeline': RECORDINGS,
    'test_sound_interference': 'replays the song-coupling captures in firmware/evidence, which stay private',
    **{f'test_{name}': 'the original RPM board firmware (firmware/LaunchLabRpm) is not published'
       for name in RPM_BOARD_SUITES},
    'test_captured_replay_runner': 'tests the runner for the private capture corpus',
    'test_mic_capture': 'tests the mic recording tool, which stays private',
    'test_release_gate': 'tests the release gate tooling, which stays private',
    'test_firmware_channels': 'tests the release channel tooling, which stays private',
    'test_build_gate': 'tests the private build scripts (the public repo builds with scripts/build-firmware.sh)',
    'test_ci_checkout': "tests this repository's CI checkout",
    'test_sync_public_firmware': 'tests the sync to the public repository, which runs here',
}
# Files in firmware/tests that stay private without a suite of their own.
PRIVATE_FILES = {
    'check_captured_replays.py': 'private recordings',
    'replay_captured_detector.cpp': 'replays private recordings',
    'replay_launch.cpp': 'replays recordings with the original RPM board detector',
    'replay_calibration.cpp': 'replays recordings with the original RPM board calibration',
}
# The marker of the public copy, written by scripts/sync_public_firmware.py.
PUBLIC_MARKER = 'firmware/SYNC.json'


def public_checkout():
    return (ROOT / PUBLIC_MARKER).is_file()


class Suite:
    def __init__(self, name, steps, sources):
        self.name, self.steps, self.sources = name, steps, sources


class SharedStep:
    """A command several suites need, run once by the first suite that reaches it."""
    def __init__(self, name, command):
        self.name, self.command = name, command
        self.lock = threading.Lock()
        self.result = None

    def run(self, execute):
        with self.lock:
            if self.result is None:
                returncode, output = execute(self.command)
                self.result = returncode, output
                return returncode, output
        returncode, _ = self.result
        return returncode, '' if returncode == 0 else f'shared step {self.name} failed; its output is above\n'


def suites(build, cxx):
    """Every suite, longest first so the slow ones do not finish last."""
    result = []
    for check, replay in REPLAY_CHECKS:
        if replay is None:
            # Builds its own replay binaries; its report stays out of the repository.
            steps = [[sys.executable, f'{TESTS}/{check}.py', str(build / f'{check}.json')]]
        else:
            exe = str(build / check)
            steps = [[*cxx, '-std=c++17', '-O2', *INCLUDES, f'{TESTS}/{replay}', '-o', exe],
                     [sys.executable, f'{TESTS}/{check}.py', exe]]
        result.append(Suite(check, steps, [f'{check}.py'] + ([replay] if replay else [])))
    for name in PYTHON_TESTS:
        result.append(Suite(f'test_{name}', [[sys.executable, f'{TESTS}/test_{name}.py']], [f'test_{name}.py']))
    for name in PYTHON_HARNESSES:
        # Some harnesses compile a test_<name>.cpp around the extracted code.
        result.append(Suite(f'check_{name}', [[sys.executable, f'{TESTS}/check_{name}.py']],
                            [f'check_{name}.py', f'test_{name}.cpp']))
    # The filter is compiled once and linked into every suite that uses it.
    vqf_flags = [*SANITIZE, '-DVQF_SINGLE_PRECISION', *INCLUDES]
    vqf = SharedStep('vqf.o', [*cxx, *vqf_flags, '-c', 'firmware/LaunchLabMini/src/vqf/vqf.cpp',
                               '-o', str(build / 'vqf.o')])
    for name in VQF_SUITES:
        exe = str(build / name)
        result.append(Suite(f'test_{name}', [
            [*cxx, *vqf_flags, '-c', f'{TESTS}/test_{name}.cpp', '-o', f'{exe}.o'],
            vqf,
            [*cxx, *SANITIZE, f'{exe}.o', str(build / 'vqf.o'), *SUITE_LIBS.get(name, []), '-o', exe],
            [exe]], [f'test_{name}.cpp']))
    # The production optical owner on the synchronous ESP-IDF stand-ins.
    exe = str(build / 'acquisition_owner')
    result.append(Suite('test_acquisition_owner', [
        [*cxx, *SANITIZE, '-Ifirmware/tests/fakes/acquisition_host', *INCLUDES,
         f'{TESTS}/test_acquisition_owner.cpp', '-o', exe],
        [exe]], ['test_acquisition_owner.cpp']))
    for name in RPM_BOARD_SUITES:
        exe = str(build / name)
        result.append(Suite(f'test_{name}', [
            [*cxx, *SANITIZE, *INCLUDES, f'{TESTS}/test_{name}.cpp', '-o', exe],
            [exe]], [f'test_{name}.cpp']))
    return result


def unlisted_tests(all_suites):
    covered = {source for suite in all_suites for source in suite.sources} | set(NOT_IN_HOST_SUITE)
    present = {path.name for pattern in ('test_*.cpp', 'test_*.py', 'check_*.py')
               for path in (ROOT / TESTS).glob(pattern)}
    return sorted(present - covered)


def private_test_files(all_suites):
    """Files in firmware/tests that never go to the public repository."""
    unknown = sorted(set(PRIVATE_ONLY) - {suite.name for suite in all_suites})
    assert not unknown, f'PRIVATE_ONLY names no suite: {unknown}'
    owned = {source for suite in all_suites if suite.name in PRIVATE_ONLY for source in suite.sources}
    # A source a public suite also uses (replay_sticks3.cpp is not) cannot be left out.
    shared = owned & {source for suite in all_suites if suite.name not in PRIVATE_ONLY for source in suite.sources}
    assert not shared, f'private-only suites share files with public ones: {sorted(shared)}'
    return sorted(owned | set(PRIVATE_FILES))


def checkout_suites(all_suites):
    """The suites this checkout runs: all of them here, the public ones in the public copy."""
    if not public_checkout():
        return all_suites
    return [suite for suite in all_suites if suite.name not in PRIVATE_ONLY]


def leaked_private_files(all_suites):
    """Private-only test files present in the public copy (none may be)."""
    if not public_checkout():
        return []
    return [name for name in private_test_files(all_suites) if (ROOT / TESTS / name).exists()]


def git(*args, root=ROOT):
    return subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True).stdout


def fingerprint(root=ROOT):
    """Content hash of everything the suite and the build read, without writing to git."""
    digest = hashlib.sha256()
    try:
        # HEAD's trees for the paths, the uncommitted diff and untracked files.
        digest.update(git('ls-tree', 'HEAD', '--', *FINGERPRINT_PATHS, root=root))
        digest.update(git('diff', 'HEAD', '--binary', '--no-ext-diff', '--no-textconv', '--', *FINGERPRINT_PATHS, root=root))
        untracked = git('ls-files', '-z', '--others', '--exclude-standard', '--', *FINGERPRINT_PATHS, root=root)
        files = [root / name for name in untracked.decode().split('\0') if name]
    except (OSError, subprocess.CalledProcessError):
        # Not a git checkout: hash every non-build file.
        files = [path for top in FINGERPRINT_PATHS for path in sorted((root / top).rglob('*') if (root / top).is_dir()
                 else [root / top]) if not any(part.startswith(('build', '.')) for part in path.relative_to(root).parts)]
    for path in sorted(files):
        digest.update(str(path.relative_to(root)).encode() + b'\0')
        if path.is_symlink():
            digest.update(b'link:' + os.readlink(path).encode())
        elif path.is_file():
            digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()[:32]


class Runner:
    def __init__(self, verbose, serial, build, env):
        self.verbose, self.serial, self.build, self.env = verbose, serial, build, env
        self.lock = threading.RLock()
        self.processes = set()
        self.stopping = False

    def run(self, suite):
        start = time.monotonic()
        try:
            return self.run_steps(suite, start)
        except Exception as error:  # a broken step is a failed suite, not a crashed run
            return suite, False, time.monotonic() - start, f'{type(error).__name__}: {error}\n'

    def run_steps(self, suite, start):
        output = []
        # Each suite's temporary files stay in its own directory, removed with the run.
        env = dict(self.env, TMPDIR=str(self.build / 'tmp' / suite.name))
        os.makedirs(env['TMPDIR'], exist_ok=True)
        for step in suite.steps:
            if isinstance(step, SharedStep):
                returncode, out = step.run(lambda command: self.execute(command, env))
                command = step.command
            else:
                returncode, out = self.execute(step, env)
                command = step
            output.append(out)
            if returncode != 0:
                output.append(f'FAILED (exit {returncode}): {" ".join(command)}\n')
                return suite, False, time.monotonic() - start, ''.join(output)
        return suite, True, time.monotonic() - start, ''.join(output)

    def execute(self, command, env):
        with self.lock:
            if self.stopping:
                return 1, 'stopped\n'
            process = subprocess.Popen(
                command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, start_new_session=True,
                stdout=None if self.serial else subprocess.PIPE,
                stderr=None if self.serial else subprocess.STDOUT)
            self.processes.add(process)
        timeout = ''
        try:
            out, _ = process.communicate(timeout=SUITE_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            out, _ = process.communicate()
            timeout = f'TIMEOUT after {SUITE_TIMEOUT_S} s\n'
        finally:
            with self.lock:
                self.processes.discard(process)
        return process.returncode, (out or b'').decode(errors='replace') + timeout

    def stop(self):
        with self.lock:
            self.stopping = True
            for process in self.processes:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass

    def report(self, suite, ok, seconds, output):
        status = 'PASS' if ok else 'FAIL'
        with self.lock:
            if self.serial or not (self.verbose or not ok):
                print(f'{status}  {suite.name}  {seconds:.1f} s', flush=True)
                return
            print(f'===== {status}  {suite.name}  {seconds:.1f} s =====', flush=True)
            print(output.rstrip('\n'), flush=True)
            print(f'===== end {suite.name} =====', flush=True)


def compiler_setup(build, cache_dir):
    """(compiler argv for the suites, environment).

    A `c++` shim stands in for both `c++` and $CXX, so harnesses that call
    `c++` directly use $CXX too; with a cache directory it is the cache.
    """
    real = shlex.split(os.environ.get('CXX', 'c++'))
    real[0] = shutil.which(real[0]) or real[0]
    identity = subprocess.run(real + ['--version'], capture_output=True, text=True).stdout
    shim_dir = build / 'bin'
    shim_dir.mkdir()
    shim = shim_dir / 'c++'
    target = [sys.executable, str(ROOT / 'scripts/host_cc_cache.py')] if cache_dir else real
    shim.write_text(f'#!/bin/sh\nexec {shlex.join(target)} "$@"\n')
    shim.chmod(0o755)
    env = dict(os.environ, PATH=f'{shim_dir}{os.pathsep}{os.environ.get("PATH", "")}', CXX=str(shim),
               LAUNCHLAB_REAL_CXX=json.dumps(real), LAUNCHLAB_CXX_ID=identity, LAUNCHLAB_SUITE_TMP=str(build))
    if cache_dir:
        env['LAUNCHLAB_CC_CACHE'] = str(cache_dir)
    # Undefined behaviour fails the suite: by default UBSan only prints a
    # report and the test carries on (a negative shift once went unnoticed).
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    return [str(shim)], env, identity


def prune_cache(cache_dir, keep_bytes):
    """Drop least recently used entries beyond the size budget."""
    entries = sorted(cache_dir.glob('*/*.out'), key=lambda path: path.stat().st_mtime, reverse=True)
    total = 0
    for entry in entries:
        total += entry.stat().st_size
        if total > keep_bytes:
            entry.unlink(missing_ok=True)
            entry.with_suffix('.err').unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--serial', action='store_true',
                        help='one suite at a time with live output and no compile cache (debugging)')
    parser.add_argument('-v', '--verbose', action='store_true', help='print every suite\'s output, not only failures')
    parser.add_argument('-k', dest='only', action='append', default=[], help='run suites whose name contains this')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--no-cache', action='store_true', help='compile everything from scratch')
    parser.add_argument('--cache-dir', type=Path, default=ROOT / 'firmware/build-host-suite/cc-cache')
    parser.add_argument('--cache-mb', type=int, default=2048, help='compile cache size budget')
    parser.add_argument('--result-json', type=Path, help='write the run\'s result here (for the release gate)')
    parser.add_argument('--fingerprint', action='store_true', help='print the working-tree fingerprint and exit')
    parser.add_argument('--gate-dir', type=Path, help='build gate: remember passes per working-tree fingerprint')
    parser.add_argument('--expect-fingerprint', help='build gate: fail unless the tree has this fingerprint')
    parser.add_argument('--build-info', nargs=2, metavar=('BUILD_DIR', 'STATUS'),
                        help='build gate: record the image\'s provenance and host suite status, then exit')
    args = parser.parse_args()

    if args.fingerprint:
        print(fingerprint())
        return 0
    # The build gate starts the suite as a background job, which inherits
    # SIGINT and SIGQUIT ignored, and ignored signals stay ignored across exec.
    # Suites must see the same dispositions as from a terminal.
    if signal.getsignal(signal.SIGINT) is signal.SIG_IGN:
        signal.signal(signal.SIGINT, signal.default_int_handler)
    if signal.getsignal(signal.SIGQUIT) is signal.SIG_IGN:
        signal.signal(signal.SIGQUIT, signal.SIG_DFL)
    if args.build_info:
        write_build_info(Path(args.build_info[0]), args.build_info[1], args.expect_fingerprint or fingerprint())
        return 0
    build = Path(tempfile.mkdtemp(prefix='launchlab-host-suite-'))
    try:
        return run_suite(args, build)
    finally:
        shutil.rmtree(build, ignore_errors=True)


def run_suite(args, build):
    cache_dir = None if args.no_cache or args.serial else args.cache_dir
    if cache_dir:
        cache_dir.mkdir(parents=True, exist_ok=True)
    cxx, env, identity = compiler_setup(build, cache_dir)
    every_suite = suites(build, cxx)
    selected = all_suites = checkout_suites(every_suite)
    if args.list:
        print('\n'.join(suite.name for suite in all_suites))
        return 0
    missing = unlisted_tests(every_suite)
    if missing:
        print('Tests in firmware/tests that the host suite does not run (add them to scripts/firmware_tests.py, '
              'or to NOT_IN_HOST_SUITE with a reason):\n  ' + '\n  '.join(missing), file=sys.stderr)
        return 1
    leaked = leaked_private_files(every_suite)
    if leaked:
        print(f'Private-only tests in the public copy ({PUBLIC_MARKER} is present); they need what only the '
              'private repository has:\n  ' + '\n  '.join(leaked), file=sys.stderr)
        return 1
    if args.only:
        selected = [suite for suite in all_suites if any(key in suite.name for key in args.only)]
        if not selected:
            print(f'No suite matches {args.only}', file=sys.stderr)
            return 1

    tree = fingerprint()
    if args.expect_fingerprint and tree != args.expect_fingerprint:
        print(f'Working tree changed before the host suite started ({tree} != {args.expect_fingerprint})',
              file=sys.stderr)
        return TREE_CHANGED
    stamp = args.gate_dir / f'{tree}.pass' if args.gate_dir and not args.only else None
    if stamp and stamp.exists():
        print(f'HOST SUITE PASSED: already passed for this exact working tree ({tree}); not re-run')
        return 0

    jobs = 1 if args.serial else max(1, args.jobs)
    runner = Runner(args.verbose, args.serial, build, env)
    signal.signal(signal.SIGTERM, lambda *_: (runner.stop(), sys.exit(143)))
    started = datetime.datetime.now(datetime.timezone.utc)
    start = time.monotonic()
    not_run = sorted(set(PRIVATE_ONLY)) if public_checkout() else []
    print(f'Host suite: {len(selected)} suites, {jobs} parallel job{"s" * (jobs != 1)}'
          f'{", compile cache " + str(cache_dir) if cache_dir else ", no compile cache"}'
          f'{f" ({len(not_run)} private-only suites are not in this public copy)" if not_run else ""}', flush=True)
    failed = []
    try:
        with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
            for future in concurrent.futures.as_completed([pool.submit(runner.run, s) for s in selected]):
                suite, ok, seconds, output = future.result()
                runner.report(suite, ok, seconds, output)
                if not ok:
                    failed.append(suite.name)
    except KeyboardInterrupt:
        runner.stop()
        raise
    elapsed = time.monotonic() - start
    changed = fingerprint() != tree
    passed = not failed and not changed
    if failed:
        print(f'HOST SUITE FAILED: {len(failed)} of {len(selected)} suites in {elapsed:.0f} s: {" ".join(sorted(failed))}')
    elif changed:
        print('HOST SUITE RESULT DISCARDED: the working tree changed while it ran; run it again.')
    else:
        print(f'HOST SUITE PASSED: {len(selected)} suites in {elapsed:.0f} s')
    if args.result_json:
        write_result(args.result_json, passed=passed, suites=[suite.name for suite in selected],
                     failed=sorted(failed), complete=not args.only, tree=tree, tree_changed=changed,
                     compile_cache=cache_dir is not None, started=started, seconds=elapsed, compiler=identity,
                     not_run=not_run)
    if passed and stamp:
        args.gate_dir.mkdir(parents=True, exist_ok=True)
        stamp.write_text(f'{len(selected)} suites passed in {elapsed:.0f} s\n')
        for old in sorted(args.gate_dir.glob('*.pass'), key=lambda path: path.stat().st_mtime)[:-100]:
            old.unlink(missing_ok=True)
    if cache_dir:
        prune_cache(cache_dir, args.cache_mb << 20)
    return 0 if passed else 1 if failed else TREE_CHANGED


def source():
    """(HEAD commit, whether anything the suite or build reads differs from it)."""
    try:
        return (git('rev-parse', 'HEAD').decode().strip(),
                bool(git('status', '--porcelain', '--', *FINGERPRINT_PATHS).strip()))
    except (OSError, subprocess.CalledProcessError):
        return None, True


def write_build_info(build_dir, status, tree):
    """Provenance of a gated firmware image, read by run_hil.py and the release gate."""
    commit, dirty = source()
    image = build_dir / 'LaunchLabMini.ino.bin'
    info = {
        'host_suite': status,
        'tree_fingerprint': tree,
        'source_commit': commit,
        'source_dirty': dirty,
        'image': image.name,
        'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest() if image.exists() else None,
        'built_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
    }
    (build_dir / 'build-info.json').write_text(json.dumps(info, indent=1) + '\n')


def write_result(path, *, passed, suites, failed, complete, tree, tree_changed, compile_cache, started, seconds,
                 compiler, not_run=()):
    commit, dirty = source()
    result = {
        'tool': 'scripts/firmware_tests.py',
        'passed': passed,
        'complete': complete,
        'checkout': 'public' if public_checkout() else 'private',
        'suites': suites,
        'private_only_not_run': list(not_run),
        'failed': failed,
        'source_commit': commit,
        'source_dirty': dirty,
        'tree_fingerprint': tree,
        'tree_changed_during_run': tree_changed,
        'compile_cache': compile_cache,
        'started_utc': started.isoformat(timespec='seconds'),
        'seconds': round(seconds, 1),
        'compiler': compiler.splitlines()[0] if compiler else None,
        'platform': platform.platform(),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=1) + '\n')


if __name__ == '__main__':
    sys.exit(main())
