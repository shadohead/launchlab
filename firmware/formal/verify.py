#!/usr/bin/env python3
"""Fail-closed LaunchLab wipe model, Lean proof and production C++ host checks."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TLC_SHA256 = 'fdffef247f563ef6edbfd44958d2a82db79e7564e07cb673262d896353fa6713'
THEOREMS = {
    'wrapped_difference_eq_gap_mod', 'wrapped_difference_recovers_gap', 'after_recovers_delta',
    'true_times_recover_gap', 'no_recovery_at_or_past_full_wrap', 'full_wrap_is_invisible',
    'expired_iff_true_gap', 'holdShort_iff_true_gap', 'pollGapTooLong_iff_true_gap',
    'deadline_hidden_after_full_wrap', 'tick_cancel_first', 'b_wins_on_completing_tick',
    'no_event_at_or_after_true_deadline', 'tick_commits_of_true_gaps',
}
AXIOMS = {'propext', 'Classical.choice', 'Quot.sound'}
WITNESSES = (
    'W_NeverValidMarker', 'W_NeverTorn', 'W_NeverComplete', 'W_NeverResumeErases',
    'W_NeverBlockedInvalid', 'W_NeverFreshNormal',
)
HOST_SUITES = {'test_device_wipe', 'test_wipe_confirmation', 'check_wipe_pipeline'}


class Failure(Exception):
    pass


def step(args, cwd=ROOT, timeout=180):
    r = subprocess.run(args, cwd=cwd, timeout=timeout, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.returncode, r.stdout


def require(ok, reason, output=''):
    if not ok:
        if output:
            print(output, file=sys.stderr)
        raise Failure(reason)


def successful(args, cwd=ROOT, timeout=180):
    status, out = step(args, cwd, timeout)
    require(status == 0, f'{Path(str(args[0])).name} exited {status}', out)
    return out


def config_text(path):
    return '\n'.join(
        (s for line in path.read_text().splitlines()
         if (s := line.split(r'\*', 1)[0].strip()))
    )


def expect_violation(status, output, name):
    errors = re.findall(r'^Error: (.*)$', output, re.M)
    # A trace is required in addition to the named invariant; parse, infrastructure,
    # deadlock, liveness and other invariant failures are not expected mutations.
    require(status == 12 and errors == [f'Invariant {name} is violated.', 'The behavior up to this point is:'] and
            'State 1: <Initial predicate>' in output,
            f'TLC should report only {name} with an actual counterexample (exit {status})', output)


def check_tlc(java, jar, tmp):
    baseline, mutation = (config_text(HERE / name)
                          for name in ('WipeContract.cfg', 'WipeMutation.cfg'))
    require(baseline.count('PersistIntentFirst = TRUE') == 1 and
            baseline.replace('PersistIntentFirst = TRUE', 'PersistIntentFirst = FALSE') == mutation,
            'The expected-failure configuration must differ only by PersistIntentFirst')
    match = re.search(r'^NumOwned\s*=\s*([1-9][0-9]*)$', baseline, re.M)
    require(match is not None, 'TLC baseline does not define the modeled storage-scope size')
    shutil.copyfile(HERE / 'WipeContract.tla', tmp / 'WipeContract.tla')

    def tlc(cfg, name):
        (tmp / f'{name}.cfg').write_text(cfg + '\n')
        # Leave deadlock detection enabled. TL part of the temporal properties runs too.
        return step([java, '-Xmx768m', '-XX:+UseParallelGC', '-jar', str(jar), '-noGenerateSpecTE', '-workers', '1',
                     '-metadir', str(tmp / name), '-config', f'{name}.cfg', 'WipeContract'], tmp)

    status, out = tlc(baseline, 'Baseline')
    require(status == 0 and 'Model checking completed. No error has been found.' in out and
            re.search(r'\b[1-9][0-9]* distinct states found, 0 states left on queue\.', out) and
            not re.search(r'^Error:', out, re.M),
            'TLC baseline did not complete successfully', out)
    print(next(line for line in reversed(out.splitlines()) if 'states found, 0 states left on queue.' in line))
    expect_violation(*tlc(mutation, 'Mutation'), 'IntentDurableBeforeErase')
    print('TLC mutation: IntentDurableBeforeErase caught the persist-first bypass')
    for witness in WITNESSES:
        expect_violation(*tlc(baseline + f'\nINVARIANT {witness}', witness), witness)
    print(f'TLC reachability: {len(WITNESSES)} expected witnesses reached')
    return int(match[1])


def check_lean(lean):
    version = successful([lean, '--version'])
    require(re.search(r'\bversion 4\.30\.0(?:,|\s)', version), 'Expected Lean 4.30.0', version)
    output = successful([lean, '-DwarningAsError=true', str(HERE / 'WipeClock.lean')])
    rows = re.findall(r"^'LaunchLab\.WipeClock\.(\w+)' depends on axioms: \[(.*?)\]$", output, re.M)
    require(len(rows) == len(THEOREMS) and {name for name, _ in rows} == THEOREMS and
            all(set(raw.split(', ')) - {''} <= AXIOMS for _, raw in rows),
            'Expected all printed Lean theorems and only standard logical axioms', output)
    print(f'Lean: {len(rows)} theorems checked with warnings as errors and axioms audited')


def check_cpp(owned):
    cxx = shlex.split(os.environ.get('CXX', 'c++'))
    require(cxx, 'CXX is empty')
    successful([*cxx, '-std=c++17', '-Werror', '-pedantic', '-Ifirmware/LaunchLabMini',
                f'-DLAUNCHLAB_MODEL_OWNED={owned}', '-fsyntax-only',
                str(HERE / 'firmware_contract.cpp')])
    print('C++ bridge: modeled timing constants, namespace scope, and marker match')
    host = [sys.executable, 'scripts/firmware_tests.py', '--no-cache', '-k', 'wipe']
    available = set(successful([*host, '--list']).splitlines())
    require(HOST_SUITES <= available and all(
        (ROOT / 'firmware' / 'tests' / (name + ('.py' if name.startswith('check_') else '.cpp'))).is_file()
        for name in HOST_SUITES), f'Missing required production wipe suites: {sorted(HOST_SUITES - available)}')
    output = successful([*host, '--serial'], timeout=300)
    for name in HOST_SUITES:
        require(re.search(rf'\bPASS\s+{re.escape(name)}\b', output),
                f'Host suite did not report PASS for {name}', output)
    print(output, end='')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tlc-jar', default=os.environ.get('LAUNCHLAB_TLC_JAR'), type=Path)
    p.add_argument('--lean', default=os.environ.get('LAUNCHLAB_LEAN', 'lean'))
    p.add_argument('--java', default=os.environ.get('LAUNCHLAB_JAVA', 'java'))
    a = p.parse_args()
    try:
        require(a.tlc_jar is not None and a.tlc_jar.is_file(),
                'Supply --tlc-jar or LAUNCHLAB_TLC_JAR; see firmware/formal/README.md')
        jar = a.tlc_jar.resolve()
        require(hashlib.sha256(jar.read_bytes()).hexdigest() == TLC_SHA256, 'Unexpected TLC jar SHA-256')
        java, lean = (shutil.which(x) for x in (a.java, a.lean))
        require(java is not None and lean is not None,
                'Java and Lean are required; see firmware/formal/README.md')
        with tempfile.TemporaryDirectory(prefix='launchlab-formal-') as d:
            tmp = Path(d)
            owned = check_tlc(java, jar, tmp)
            check_lean(lean)
            check_cpp(owned)
        print('FIRMWARE FORMAL VERIFICATION PASSED')
    except (Failure, OSError, subprocess.TimeoutExpired) as exc:
        print(f'Firmware formal verification failed: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
