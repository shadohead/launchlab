#!/usr/bin/env python3
"""Proof that the firmware host suite (and optionally HIL) passed on an exact tree.

  python3 scripts/firmware_proof.py host                 # run the host suite, write launchlab-proof.json
  python3 scripts/firmware_proof.py hil --port PORT --backup FILE [SCENARIO ...]   # add a HIL run on your M5
  python3 scripts/firmware_proof.py show                 # print the block to paste into the pull request
  python3 scripts/firmware_proof.py check --repo DIR --event EVENT.json            # CI: check a pull request

`host` needs a committed tree: the proof names the git objects of everything
the suite reads (scripts/firmware_tests.py FINGERPRINT_PATHS) and the suite's
fingerprint of them, so it cannot belong to any other tree. It prints a
fenced JSON block for the pull request's "Test proof" section.

`hil` adds a run on the contributor's own M5StickS3 (firmware/tools/hil/
contributor_hil.py): the application SHA-256 read back from flash, the
scenarios' results and a hash of the serial log. Nobody but the contributor
can check it, so CI reports it as contributor-reported.

`check` is the pull request workflow's verdict, run from the base branch's
copy of this file against the pull request's head checkout. A pull request
that changes what the suite reads needs a proof for its head tree that says
the whole suite passed; CI also reruns the suite itself, and that run is the
one that counts.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import firmware_tests  # noqa: E402

KIND = 'launchlab-firmware-proof'
FORMAT = 1
PROOF = REPO / 'launchlab-proof.json'
M5GFX_DEFAULT = 'firmware/.arduino/m5-libraries/M5GFX'
# Files whose change deserves a maintainer's look before trusting CI: they decide what CI runs and checks.
TRUST_PATHS = ('scripts/firmware_tests.py', 'scripts/firmware_proof.py', 'scripts/host_cc_cache.py',
               'scripts/test-firmware.sh', '.github/')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    return subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True, text=True).stdout.strip()


def tested_objects(root, rev='HEAD'):
    """{path: git object id} of everything the host suite reads, at `rev`."""
    objects = {}
    for path in firmware_tests.FINGERPRINT_PATHS:
        try:
            objects[path] = git(root, 'rev-parse', f'{rev}:{path}')
        except subprocess.CalledProcessError:
            objects[path] = None
    return objects


def dirty_paths(root):
    """Changed or untracked (not ignored) files among the tested paths."""
    return git(root, 'status', '--porcelain', '--', *firmware_tests.FINGERPRINT_PATHS).splitlines()


def suite_names_sha256(names):
    return sha256('\n'.join(sorted(names)).encode())


def version_of(command):
    try:
        out = subprocess.run(command, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    text = (out.stdout or out.stderr).strip()
    return text.splitlines()[0] if text else None


def m5gfx_dir():
    return Path(os.environ.get('LAUNCHLAB_M5GFX') or REPO / M5GFX_DEFAULT)


def tools(compiler):
    m5gfx = m5gfx_dir()
    return {
        'compiler': compiler,
        'python': platform.python_version(),
        'git': version_of(['git', '--version']),
        'os': platform.platform(),
        'machine': platform.machine(),
        'sdl2': version_of(['sdl2-config', '--version']),
        'm5gfx': version_of(['git', '-C', str(m5gfx), 'rev-parse', 'HEAD']) if (m5gfx / '.git').exists() else None,
    }


# ---- the proof file

def load(path):
    try:
        proof = json.loads(Path(path).read_text())
    except FileNotFoundError:
        raise SystemExit(f'{path} not found: run `python3 scripts/firmware_proof.py host` first') from None
    if proof.get('kind') != KIND:
        raise SystemExit(f'{path} is not a LaunchLab firmware proof')
    return proof


def block(proof):
    """The pull request's copy: a fenced JSON block."""
    return '```json\n' + json.dumps(proof, indent=1) + '\n```\n'


def write(path, proof):
    Path(path).write_text(json.dumps(proof, indent=1) + '\n')
    print(f'\nProof written to {path}. Paste this into the "Test proof" section of your pull request:\n')
    print(block(proof))


# ---- host

def host(args):
    dirty = dirty_paths(REPO)
    if dirty:
        raise SystemExit('Commit your changes first: the proof is for a committed tree, and these differ from HEAD:\n  '
                         + '\n  '.join(dirty))
    m5gfx = m5gfx_dir()
    if not (m5gfx / 'src').is_dir():
        if args.no_fetch:
            raise SystemExit(f'M5GFX not found at {m5gfx} (check_ui_render draws the screens with it): '
                             'run firmware/tools/ui_render/fetch_m5gfx.sh or set LAUNCHLAB_M5GFX')
        subprocess.run(['bash', str(REPO / 'firmware/tools/ui_render/fetch_m5gfx.sh'), str(m5gfx)], check=True)
    if shutil.which('sdl2-config') is None:
        raise SystemExit('sdl2-config not found: check_ui_render needs the SDL2 headers '
                         '(macOS: brew install sdl2; Debian/Ubuntu: sudo apt install libsdl2-dev)')
    commit = git(REPO, 'rev-parse', 'HEAD')
    objects = tested_objects(REPO)
    with tempfile.TemporaryDirectory(prefix='launchlab-proof-') as tmp:
        result_path = Path(tmp) / 'host-suite.json'
        command = [sys.executable, str(HERE / 'firmware_tests.py'), '--result-json', str(result_path)]
        command += ['-j', str(args.jobs)] if args.jobs else []
        command += ['--no-cache'] if args.no_cache else []
        env = dict(os.environ, LAUNCHLAB_M5GFX=str(m5gfx))
        returncode = subprocess.run(command, cwd=REPO, env=env).returncode
        if not result_path.exists():
            raise SystemExit(f'The host suite stopped before it finished (exit {returncode}); no proof written.')
        result = json.loads(result_path.read_text())
    if git(REPO, 'rev-parse', 'HEAD') != commit or tested_objects(REPO) != objects:
        raise SystemExit('HEAD moved while the suite ran; run it again.')
    proof = {
        'kind': KIND,
        'format': FORMAT,
        'tree': {'commit': commit, 'fingerprint': result['tree_fingerprint'], 'objects': objects},
        'host_suite': {
            'passed': result['passed'],
            'complete': result['complete'],
            'checkout': result.get('checkout'),
            'suites': len(result['suites']),
            'failed': result['failed'],
            'private_only_not_run': len(result.get('private_only_not_run', [])),
            'suites_sha256': suite_names_sha256(result['suites']),
            'tree_changed_during_run': result['tree_changed_during_run'],
            'compile_cache': result['compile_cache'],
            'started_utc': result['started_utc'],
            'seconds': result['seconds'],
        },
        'tools': tools(result.get('compiler')),
        'hil': None,
    }
    write(args.out, proof)
    if not result['passed']:
        print('The host suite did NOT pass: fix the failures above before opening the pull request.', file=sys.stderr)
        return 1
    return 0


# ---- hil

def hil(args):
    proof = load(args.out)
    if dirty_paths(REPO) or tested_objects(REPO) != proof['tree']['objects']:
        raise SystemExit('The proof is for another tree: commit, run `firmware_proof.py host` again, then HIL.')
    if not proof['host_suite']['passed']:
        raise SystemExit('The host suite has not passed for this tree; fix that before a device run.')
    info_path = Path(args.build_dir) / 'build-info.json'
    info = json.loads(info_path.read_text()) if info_path.is_file() else {}
    if info.get('tree_fingerprint') != proof['tree']['fingerprint']:
        raise SystemExit(f'{args.build_dir} was not built from this tree: '
                         'run `bash scripts/build-firmware.sh --hil` (it writes build-info.json)')
    sys.path.insert(0, str(REPO / 'firmware/tools/hil'))
    import contributor_hil
    out = Path(args.run_dir or Path.home() / 'launchlab-hil' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    try:
        section = contributor_hil.run(port=args.port, backup=args.backup, out=out, build_dir=args.build_dir,
                                      scenarios=args.scenario or ['all'], sensor=args.sensor)
    except contributor_hil.HilError as error:
        raise SystemExit(f'HIL run not made: {error}') from None
    section['tools'] = {'esptool': version_of([*contributor_hil.esptool_command(), 'version']),
                        'pyserial': __import__('serial').VERSION}
    proof['hil'] = section
    write(args.out, proof)
    print(f'Run folder (logs and backups, keep it private): {out}')
    if not section['passed']:
        print('The HIL run did NOT pass (or the device was not put back): see above.', file=sys.stderr)
        return 1
    return 0


# ---- check (CI)

def proofs_in(body):
    """Every fenced JSON block of the pull request body that is a LaunchLab proof.

    Fences are whole lines (``` opens, a line of only ``` closes), so a ``` in
    running text, such as the template's own instructions, opens nothing.
    """
    found, block = [], None
    for line in (body or '').splitlines():
        if block is None:
            if line.strip().startswith('```'):
                block = []
        elif line.strip() == '```':
            try:
                data = json.loads('\n'.join(block))
            except ValueError:
                data = None
            if isinstance(data, dict) and data.get('kind') == KIND:
                found.append(data)
            block = None
        else:
            block.append(line)
    return found


def changed_paths(root, base, head):
    # No rename detection: it would read file contents, and a rename touches both paths anyway.
    return git(root, 'diff', '--name-only', '--no-renames', f'{base}...{head}').splitlines()


def checkout_suite_names(root):
    """The suites the head tree's own runner lists for itself (it is a public copy in CI)."""
    out = subprocess.run([sys.executable, str(Path(root) / 'scripts/firmware_tests.py'), '--list'],
                         cwd=root, capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f'firmware_tests.py --list failed on the head tree:\n{out.stdout}{out.stderr}')
    return out.stdout.split()


def verdict(root, event, suite_names=checkout_suite_names):
    """(ok, markdown lines) for a pull_request event against the head checkout at `root`."""
    pr = event['pull_request']
    head, base = pr['head']['sha'], pr['base']['sha']
    lines, errors, warnings = [], [], []
    if git(root, 'rev-parse', 'HEAD') != head:
        raise SystemExit(f'{root} is not checked out at the pull request head {head}')
    changed = changed_paths(root, base, head)
    tested = [p for p in changed if any(p == t or p.startswith(t + '/') for t in firmware_tests.FINGERPRINT_PATHS)]
    trusted = [p for p in changed if p.startswith(TRUST_PATHS)]
    proofs = proofs_in(pr.get('body') or '')
    lines.append(f'Pull request head `{head[:12]}`: {len(tested)} changed file(s) the firmware host suite reads.')
    if len(proofs) > 1:
        errors.append('The pull request body holds more than one proof block; keep only the newest.')
    proof = proofs[-1] if proofs else None
    if proof is None:
        if tested:
            errors.append('No test proof. Run `python3 scripts/firmware_proof.py host` on your committed branch and paste '
                          'the block it prints into the "Test proof" section (see CONTRIBUTING.md).')
        else:
            lines.append('No firmware change, so no proof is needed.')
    else:
        objects = tested_objects(root, head)
        mismatched = [p for p, oid in objects.items() if proof.get('tree', {}).get('objects', {}).get(p) != oid]
        fingerprint = firmware_tests.fingerprint(Path(root))
        host_suite = proof.get('host_suite') or {}
        rows = [('Tree', 'matches the head' if not mismatched and proof['tree'].get('fingerprint') == fingerprint
                 else f'**another tree** ({", ".join(mismatched) or "fingerprint"} differ)')]
        if mismatched or proof['tree'].get('fingerprint') != fingerprint:
            (errors if tested else warnings).append(
                'The proof is for another tree (pushed since?). Run `python3 scripts/firmware_proof.py host` again on '
                'the pushed head and replace the block.')
        names = suite_names(root)
        suite_ok = (host_suite.get('passed') is True and host_suite.get('complete') is True
                    and not host_suite.get('failed') and host_suite.get('tree_changed_during_run') is False
                    and host_suite.get('suites') == len(names)
                    and host_suite.get('suites_sha256') == suite_names_sha256(names))
        rows.append(('Host suite (contributor)', f'{"passed" if host_suite.get("passed") else "**failed**"}, '
                     f'{host_suite.get("suites")} suites{" (cached compiles)" if host_suite.get("compile_cache") else ""}, '
                     f'{host_suite.get("seconds")} s, failed: {", ".join(host_suite.get("failed") or []) or "none"}'))
        if not suite_ok:
            (errors if tested else warnings).append(
                f'The proof does not show the whole suite of this tree passing ({len(names)} suites, '
                f'{suite_names_sha256(names)[:12]}); it shows {host_suite.get("suites")} suites, '
                f'{str(host_suite.get("suites_sha256"))[:12]}, passed={host_suite.get("passed")}, '
                f'complete={host_suite.get("complete")}, failed={host_suite.get("failed")}.')
        tool = proof.get('tools') or {}
        rows.append(('Tools', f'{tool.get("compiler")}; Python {tool.get("python")}; {tool.get("os")}'))
        hil_section = proof.get('hil')
        if hil_section:
            scenarios = hil_section.get('scenarios') or {}
            image = hil_section.get('image') or {}
            restored = hil_section.get('restored') or {}
            same_tree = image.get('tree_fingerprint') == fingerprint
            rows.append(('HIL (contributor-reported, not checked by CI)',
                         f'{"passed" if hil_section.get("passed") else "**not passed**"}: '
                         f'{scenarios.get("run")} scenarios ({scenarios.get("selection")}), '
                         f'failed: {", ".join(scenarios.get("failed") or []) or "none"}, '
                         f'{scenarios.get("warnings")} warnings; image {str(image.get("readback_sha256"))[:12]} read back '
                         f'({image.get("runtime")}), {"this tree" if same_tree else "**another tree**"}; '
                         f'log {str(scenarios.get("log_sha256"))[:12]}; put back on {restored.get("runtime")}, '
                         f'data restored: {restored.get("data_restored")}'))
            if image.get('readback_sha256') != image.get('sha256'):
                errors.append('The HIL section says the flashed image did not read back as built.')
            if not hil_section.get('passed'):
                errors.append('The contributor reports a HIL run that did not pass.')
            if not same_tree:
                warnings.append('The HIL run was for another tree; it says nothing about this head.')
        else:
            rows.append(('HIL', 'not run (optional)'))
        lines += ['', '| | |', '| --- | --- |'] + [f'| {k} | {v} |' for k, v in rows]
    if trusted:
        warnings.append('This pull request changes what CI runs or checks: '
                        + ', '.join(f'`{p}`' for p in trusted) + '. A maintainer should review those changes before '
                        'trusting a green run.')
    lines.append('')
    lines.append('The CI host suite run in "Verify firmware and publish updater redirect" is the one that counts; '
                 'this check ties the contributor\'s run to the same tree.')
    lines += [''] + [f'> **Warning:** {w}' for w in warnings]
    lines += [f'> **Error:** {e}' for e in errors]
    return not errors, lines


def check(args):
    event = json.loads(Path(args.event).read_text())
    if 'pull_request' not in event:
        raise SystemExit('not a pull_request event')
    ok, lines = verdict(Path(args.repo).resolve(), event)
    text = '## Firmware test proof\n\n' + '\n'.join(lines) + '\n'
    print(text)
    if args.summary:
        with open(args.summary, 'a') as handle:
            handle.write(text)
    return 0 if ok else 1


def show(args):
    print(block(load(args.out)))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('host', help='run the host suite and write the proof')
    p.add_argument('-j', '--jobs', type=int, help='parallel suites (default: one per core)')
    p.add_argument('--no-cache', action='store_true', help='compile everything from scratch')
    p.add_argument('--no-fetch', action='store_true', help='do not fetch M5GFX when it is missing')
    p.add_argument('--out', type=Path, default=PROOF)
    p = sub.add_parser('hil', help='add a HIL run on your own M5StickS3 to the proof')
    p.add_argument('--port', required=True, help='the M5\'s USB serial port, e.g. /dev/cu.usbmodem1101')
    p.add_argument('--backup', required=True, type=Path, help='the backup file you saved with the USB updater first')
    p.add_argument('--build-dir', type=Path, default=REPO / 'firmware/build-hil',
                   help='the HIL build (bash scripts/build-firmware.sh --hil)')
    p.add_argument('--run-dir', type=Path, help='where logs and backups go (default ~/launchlab-hil/<time>)')
    p.add_argument('--sensor', choices=('fitted', 'absent'), help='whether an optical sensor is fitted')
    p.add_argument('--out', type=Path, default=PROOF)
    p.add_argument('scenario', nargs='*', help="run_hil.py scenarios (default: all, about an hour)")
    p = sub.add_parser('show', help='print the proof block to paste')
    p.add_argument('--out', type=Path, default=PROOF)
    p = sub.add_parser('check', help='CI: check a pull request\'s proof against its head checkout')
    p.add_argument('--repo', type=Path, required=True, help='checkout of the pull request head')
    p.add_argument('--event', type=Path, required=True, help='the pull_request event JSON ($GITHUB_EVENT_PATH)')
    p.add_argument('--summary', help='append the report here ($GITHUB_STEP_SUMMARY)')
    args = parser.parse_args()
    return {'host': host, 'hil': hil, 'show': show, 'check': check}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
