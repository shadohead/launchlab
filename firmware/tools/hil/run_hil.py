#!/usr/bin/env python3
"""Run HIL scenarios against a LaunchLab HIL test build. See README.md.

  run_hil.py --port /dev/cu.usbmodem1101 --out ~/hil-runs/run1 normal hard
  run_hil.py --port ... --out ... all            # every scenario except soak/corpus
  run_hil.py --port ... --out ... soak --soak-minutes 30

Results: <out>/results.json, <out>/summary.md, <out>/serial.log, screenshots,
and <out>/run.json (device runtime, source commit, the flashed build's
build-info.json) for the release gate, scripts/check-release-gate.py.
Keep --out outside the repository: logs contain the device's history.
"""
import argparse
import datetime
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hil_device import Device  # noqa: E402
from rig import Rig  # noqa: E402
import scenarios  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
DEFAULT = ['normal', 'hard', 'weak', 'back_to_back', 'rearm', 'history_save_pull', 'long_save_pull', 'adc_stall', 'frame_rows', 'power_log_pull', 'standby', 'sleep_blockers', 'imu_timeline',
           'imu_faults', 'imu_faults_pull', 'imu_backlog', 'settings', 'about_page', 'battery_icon', 'themes', 'milestones', 'request_burst', 'effect_set', 'nvs_tight',
           'effect_times', 'effect_bench_fixed', 'loaded_store', 'songs', 'wear_check', 'mic_page', 'stray_bytes']
# effect_stages needs a BEST_EFFECT_PROFILE build (!effectstages); run it by name on one.


def summary(rig, path):
    lines = ['# LaunchLab HIL run', '']
    total = failed = warned = uncovered = 0
    for s in rig.results:
        fails = [c for c in s['checks'] if not c['ok'] and c['severity'] == 'fail']
        warns = [c for c in s['checks'] if not c['ok'] and c['severity'] == 'warn']
        # An unmet 'info' check is something this run could not cover: listed, never counted as a pass.
        infos = [c for c in s['checks'] if not c['ok'] and c['severity'] == 'info']
        uncovered += len(infos)
        total += len(s['checks'])
        failed += len(fails)
        warned += len(warns)
        mark = 'PASS' if s.get('passed') else 'FAIL'
        lines.append(f"## {s['name']}: {mark} ({len(s['checks'])} checks, {len(fails)} failed, "
                     f"{len(warns)} warnings, {s.get('seconds')} s)")
        lines.append('')
        for c in fails + warns:
            lines.append(f"- {'FAIL' if c['severity'] == 'fail' else 'WARN'} {c['name']}: {c['detail']}")
        for c in infos:
            lines.append(f"- NOT COVERED {c['name']}: {c['detail']}")
        for n in s['notes']:
            lines.append(f'- note: {n}')
        for p in s.get('pulls', []):
            m = p.get('motion') or {}
            launch = p.get('launch') or {}
            metrics = p.get('metrics') or {}
            lines.append(f"- pull {p['label']}: rpm={metrics.get('selected_rpm', launch.get('peak_rpm', '-'))} "
                         f"valid={launch.get('valid', '-')} end={launch.get('end', '-')} "
                         f"history={'yes' if p.get('added') else 'no'} motion={m.get('quality', '-')} "
                         f"saturated={m.get('saturated', '-')}")
        lines.append('')
    lines.insert(2, f'{total} checks, {failed} failed, {warned} warnings, {uncovered} not covered.')
    lines.insert(3, '')
    Path(path).write_text('\n'.join(lines) + '\n')
    return failed


def source():
    """(commit, dirty) of the checkout running the rig."""
    try:
        def git(*args):
            return subprocess.run(['git', '-C', str(REPO), *args], check=True, capture_output=True, text=True).stdout
        return git('rev-parse', 'HEAD').strip(), bool(git('status', '--porcelain', '--', 'firmware').strip())
    except (OSError, subprocess.CalledProcessError):
        return None, True


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds')


def main():
    sys.stdout.reconfigure(line_buffering=True)
    p = argparse.ArgumentParser()
    p.add_argument('--port', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--corpus', type=Path, help='private replay corpus root (manifest.json) for the corpus scenario')
    p.add_argument('--soak-minutes', type=float, default=10)
    p.add_argument('--effects', help='comma-separated effect labels: effect_times and soak_effects preview only these (a quick re-measure; a release runs them all)')
    p.add_argument('--sensor', choices=('fitted', 'absent'),
                   help="whether an optical sensor is fitted: the songs scenario's ADC interference check counts only with 'fitted', "
                        "and is recorded as not covered otherwise")
    p.add_argument('--verbose', action='store_true', help='echo every device line')
    p.add_argument('--build-dir', type=Path, default=REPO / 'firmware/build-m5-hil',
                   help='build of the flashed image; its build-info.json is recorded in run.json')
    p.add_argument('scenario', nargs='+', help=f"scenario names, or 'all': {', '.join(scenarios.SCENARIOS)}")
    a = p.parse_args()
    names = DEFAULT if a.scenario == ['all'] else a.scenario
    unknown = [n for n in names if n not in scenarios.SCENARIOS]
    if unknown:
        p.error(f'unknown scenario(s): {unknown}')
    a.out.mkdir(parents=True, exist_ok=True)
    echo = (lambda line: print('   |', line.text)) if a.verbose else None
    dev = Device(a.port, log_path=a.out / 'serial.log', echo=echo)
    rig = Rig(dev, a.out)
    commit, dirty = source()
    info = a.build_dir / 'build-info.json'
    run = {'tool': 'firmware/tools/hil/run_hil.py', 'scenarios': names, 'runtime': None, 'boot_id': None,
           'source_commit': commit, 'source_dirty': dirty,
           'build_info': json.loads(info.read_text()) if info.exists() else None,
           'started_utc': utc(), 'finished_utc': None, 'completed': False, 'failed_checks': None}
    scenarios.SENSOR = a.sensor
    run['sensor'] = a.sensor or 'not declared'
    # The HIL image's internal RAM beyond production's (build-m5-hil.sh, tools/hil_overhead.py),
    # for the songs scenario's heap floor; only for the build that was flashed.
    overhead = a.build_dir / 'hil-overhead.json'
    overhead = json.loads(overhead.read_text()) if overhead.exists() else None
    if overhead and overhead.get('tree_fingerprint') != (run['build_info'] or {}).get('tree_fingerprint'):
        overhead = None
    scenarios.HIL_OVERHEAD = overhead
    run['hil_overhead'] = overhead
    if a.effects:
        wanted = a.effects.split(',')
        unknown = sorted(set(wanted) - {name for _, name, _ in scenarios.EFFECTS})
        if unknown:
            p.error(f'unknown effects: {", ".join(unknown)}')
        scenarios.EFFECTS = tuple(e for e in scenarios.EFFECTS if e[1] in wanted)
        run['effects'] = wanted
    try:
        version = rig.require_hil_build()
        run.update(runtime=version, boot_id=rig.boot_id)
        print(f'device {version}, boot_id {rig.boot_id}')
        for name in names:
            fn = scenarios.SCENARIOS[name]
            if name == 'corpus':
                if not a.corpus:
                    p.error('corpus needs --corpus')
                rig.run(name, fn, a.corpus)
            elif name in ('soak', 'soak_themes', 'soak_milestones', 'soak_effects'):
                rig.run(name, fn, minutes=a.soak_minutes)
            else:
                rig.run(name, fn)
        run['completed'] = True
    finally:
        try:
            rig.recover()
        finally:
            failed = summary(rig, a.out / 'summary.md')
            (a.out / 'results.json').write_text(json.dumps(rig.results, indent=1))
            run.update(finished_utc=utc(), failed_checks=failed)
            (a.out / 'run.json').write_text(json.dumps(run, indent=1) + '\n')
            dev.close()
    print(f'\n{failed} failed checks; see {a.out / "summary.md"}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
