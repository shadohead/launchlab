#!/usr/bin/env python3
"""A HIL run on a contributor's own M5StickS3, put back the way it was.

Run through `scripts/firmware_proof.py hil`, which adds the result to the
contributor's proof file. The steps, each refusing to go on when it fails:

1. Check the backup the contributor saved with the USB updater
   (launchlab.beywatch.gg/update, "Back up to a file"): a
   `launchlab-device-backup` file holding an LLBK container.
2. Refuse a device that already runs a HIL image (a stopped run or a release
   gate left it: its data would be test data), then take a fresh portable
   backup over USB (`K`) into the run folder, in the updater's file format,
   so it can be restored with the updater too.
3. Write only the HIL application at 0x10000 (never the bootloader, the
   partition table, OTA data or NVS) and read it back: the proof records the
   SHA-256 of what is in flash.
4. `!nvsfill 0` and Mounting Top, then run_hil.py with the chosen scenarios.
5. Always, even after a failure or Ctrl-C: write the stable release's
   application back at 0x10000, read it back, and restore the fresh backup
   with `R`, so the device leaves with its own data on stable firmware.

Logs and backups stay in the run folder, outside the repository: they hold the
device's history. The proof holds only their hashes.
"""
import base64
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE))
from port_guard import require_free  # noqa: E402

APP_OFFSET = 0x10000
BACKUP_KIND = 'launchlab-device-backup'
BACKUP_MAGIC = b'LLBK'
MOUNTING_ROW = 9  # Settings row of Mounting; 0 is Top, which the scenarios expect.


class HilError(RuntimeError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds')


# ---- backups

def read_backup_file(path):
    """(container bytes, format) of an updater backup file, or HilError."""
    try:
        file = json.loads(Path(path).read_text())
    except (OSError, ValueError) as error:
        raise HilError(f'{path}: not a LaunchLab backup file ({error})') from None
    if not isinstance(file, dict) or file.get('kind') != BACKUP_KIND or not isinstance(file.get('data'), str):
        raise HilError(f'{path}: not a LaunchLab backup file (save one with the updater: "Back up to a file")')
    try:
        data = base64.b64decode(file['data'], validate=True)
    except ValueError:
        raise HilError(f'{path}: the backup data is damaged') from None
    if len(data) < 48 or data[:4] != BACKUP_MAGIC:
        raise HilError(f'{path}: the backup data is not an LLBK container')
    container_format = int.from_bytes(data[4:8], 'little')
    if container_format != file.get('format'):
        raise HilError(f'{path}: the file says format {file.get("format")}, its data is format {container_format}')
    return data, container_format


def backup_from_lines(text):
    """Container bytes from the device's K output (BACKUP_BEGIN bytes=N, BACKUP_DATA..., BACKUP_END)."""
    lines = text.splitlines()
    begin = next((line for line in lines if line.startswith('BACKUP_BEGIN ')), None)
    if begin is None or 'BACKUP_END' not in lines:
        raise HilError('the device backup did not finish')
    data = base64.b64decode(''.join(line[len('BACKUP_DATA '):] for line in lines if line.startswith('BACKUP_DATA ')))
    declared = next((field[len('bytes='):] for field in begin.split() if field.startswith('bytes=')), None)
    if declared is None or int(declared) != len(data) or data[:4] != BACKUP_MAGIC:
        raise HilError('the device backup arrived incomplete')
    return data


def backup_file_text(data, runtime, created):
    """The updater's file format (beywatch src/lib/launchlab/device-data.ts backupFile)."""
    return json.dumps({'kind': BACKUP_KIND, 'format': int.from_bytes(data[4:8], 'little'), 'created': created,
                       'runtime': runtime, 'data': base64.b64encode(data).decode()}, indent=2) + '\n'


def restore_text(data):
    """K-style lines for Device.restore(): 96 base64 characters each, as the updater sends."""
    text = base64.b64encode(data).decode()
    return ''.join(f'BACKUP_DATA {text[i:i + 96]}\n' for i in range(0, len(text), 96))


# ---- the stable release to put back

def stable_release(repo=REPO):
    """{'version', 'runtime', 'path', 'sha256', 'size'} of the newest stable application.

    From the public site's versions.json in the launchlab repository, or from
    firmware/channels/index.json and the frozen release folder in the private one.
    """
    versions = repo / 'site/public/firmware/versions.json'
    channels = repo / 'firmware/channels/index.json'
    if versions.is_file():
        index = json.loads(versions.read_text())
        stable = next((r for r in index.get('releases', []) if r.get('version') == index.get('latest')), None)
        if not stable or stable.get('channel') != 'stable':
            raise HilError(f'versions.json names no stable release ({index.get("latest")})')
        catalog = json.loads((versions.parent.parent / stable['catalog']).read_text())
        app = next((f for f in catalog['files'] if f.get('offset') == APP_OFFSET), None)
        if app is None:
            raise HilError(f'{stable["catalog"]} has no application at 0x10000')
        return {'version': catalog['version'], 'runtime': catalog['runtime'], 'path': versions.parent.parent / app['path'],
                'sha256': app['sha256'], 'size': app['size']}
    if channels.is_file():
        stable = [r for r in json.loads(channels.read_text())['releases'] if r.get('channel') == 'stable']
        if not stable:
            raise HilError('firmware/channels/index.json has no stable release')
        newest = max(stable, key=lambda r: tuple(int(part) for part in r['version'].split('.')))
        folder = repo / 'firmware/releases' / newest['private_release']
        path = folder / 'LaunchLabMini.ino.bin'
        return {'version': newest['version'], 'runtime': json.loads((folder / 'manifest.json').read_text())['runtime_version'],
                'path': path, 'sha256': newest['application_sha256'], 'size': path.stat().st_size}
    raise HilError('no release index here (site/public/firmware/versions.json or firmware/channels/index.json)')


def checked_image(path, expected_sha256=None):
    data = Path(path).read_bytes()
    if not data or data[0] != 0xE9:
        raise HilError(f'{path} is not an ESP32 application image')
    if expected_sha256 and sha256(data) != expected_sha256:
        raise HilError(f'{path} does not match its published SHA-256 {expected_sha256}')
    return data


# ---- esptool

def esptool_command():
    for name in ('esptool', 'esptool.py'):
        found = shutil.which(name)
        if found:
            return [found]
    try:
        import esptool  # noqa: F401
    except ImportError:
        raise HilError('esptool is not installed: python3 -m pip install "esptool>=5" pyserial') from None
    return [sys.executable, '-m', 'esptool']


def esptool(port, *arguments):
    """One guarded esptool call (port_guard.py refuses a port another process holds)."""
    command = [sys.executable, str(HERE / 'port_guard.py'), *esptool_command(), '--chip', 'esp32s3', '--port', port,
               '--after', 'watchdog-reset', *arguments]
    print('$', ' '.join(command[2:]), flush=True)
    result = subprocess.run(command)
    if result.returncode != 0:
        raise HilError(f'esptool failed (exit {result.returncode})')


def write_application(port, image, readback_dir):
    """Write only the application at 0x10000 and return the SHA-256 read back from flash."""
    data = Path(image).read_bytes()
    esptool(port, 'write-flash', '--flash-mode', 'keep', '--flash-size', 'keep', hex(APP_OFFSET), str(image))
    readback = Path(readback_dir) / f'readback-{sha256(data)[:12]}.bin'
    esptool(port, 'read-flash', hex(APP_OFFSET), str(len(data)), str(readback))
    flashed = sha256(readback.read_bytes())
    readback.unlink()
    if flashed != sha256(data):
        raise HilError(f'read back {flashed} at 0x10000, wrote {sha256(data)}')
    return flashed


# ---- the device over USB

def open_device(port, log, timeout=40):
    """A Device once the port is back after a flash or restart."""
    from hil_device import Device
    deadline = time.monotonic() + timeout
    while not os.path.exists(port):
        if time.monotonic() > deadline:
            raise HilError(f'{port} did not come back')
        time.sleep(0.5)
    return Device(port, log_path=log)


def runtime(dev, timeout=8):
    """The firmware's runtime string from its status block (`p`)."""
    for _ in range(3):
        start = dev.mark()
        dev.write('p')
        line = dev.wait('STATUS v=', timeout, since=start)
        if line and line.f.get('v'):
            return line.f['v']
    raise HilError('the device did not report its firmware (is it a LaunchLab 0.11 or newer?)')


def require_production(version):
    """Refuse a device that already runs a HIL image: its data may be a stopped run's test data."""
    if '-hil' in version:
        raise HilError(f'the device runs the HIL test image {version}, so an earlier run (or a release gate) did not '
                       'finish: a backup now would keep its test data, not yours. Put it back first: install the stable '
                       'release with the updater, restore your own backup from before that run, then start again')


def wait_runtime(port, log, predicate, timeout=60):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            dev = open_device(port, log)
        except Exception as error:  # the port can vanish again while the app starts
            last = error
            time.sleep(1)
            continue
        try:
            version = runtime(dev)
            if predicate(version):
                return dev, version
            last = f'runs {version}'
        except Exception as error:
            last = error
        dev.close()
        time.sleep(1)
    raise HilError(f'the device did not come up as expected: {last}')


# ---- the run

def summarize(run_dir):
    """The proof's view of run_hil.py's run.json and results.json."""
    if not (run_dir / 'run.json').is_file() or not (run_dir / 'results.json').is_file():
        raise HilError(f'run_hil.py stopped before writing its results in {run_dir}; see its output above')
    run = json.loads((run_dir / 'run.json').read_text())
    results_bytes = (run_dir / 'results.json').read_bytes()
    results = json.loads(results_bytes)
    failed, warned = [], 0
    for scenario in results:
        checks = scenario.get('checks', [])
        if any(not c['ok'] and c.get('severity') == 'fail' for c in checks):
            failed.append(scenario['name'])
        warned += sum(1 for c in checks if not c['ok'] and c.get('severity') == 'warn')
    names = [scenario['name'] for scenario in results]
    log = run_dir / 'serial.log'
    return {
        'completed': run.get('completed') is True,
        'runtime': run.get('runtime'),
        'requested': run.get('scenarios', []),
        'run': len(names),
        'failed': failed,
        'failed_checks': run.get('failed_checks'),
        'warnings': warned,
        'names_sha256': sha256('\n'.join(names).encode()),
        'results_sha256': sha256(results_bytes),
        'log_sha256': sha256(log.read_bytes()) if log.exists() else None,
        'build_tree_fingerprint': (run.get('build_info') or {}).get('tree_fingerprint'),
    }


def run(*, port, backup, out, build_dir, scenarios, sensor=None, stable=None):
    """Steps 1-5 above. Returns the proof's `hil` section; raises HilError when the run could not be made."""
    out = Path(out).expanduser().resolve()
    if REPO in out.parents or out == REPO:
        raise HilError(f'{out} is inside the repository; keep HIL output outside it (it holds the device history)')
    out.mkdir(parents=True, exist_ok=True)
    build_dir = Path(build_dir)
    image = build_dir / 'LaunchLabMini.ino.bin'
    info = json.loads((build_dir / 'build-info.json').read_text()) if (build_dir / 'build-info.json').is_file() else {}
    hil_image = checked_image(image)
    stable = stable or stable_release()
    stable_image = checked_image(stable['path'], stable['sha256'])
    updater_backup, updater_format = read_backup_file(backup)
    require_free(port)
    started = utc()
    start = time.monotonic()
    section = {
        'contributor_reported': True,
        'started_utc': started,
        'image': {'sha256': sha256(hil_image), 'bytes': len(hil_image), 'tree_fingerprint': info.get('tree_fingerprint')},
        'updater_backup': {'format': updater_format, 'bytes': len(updater_backup)},
    }

    # Before anything is written: the device's own data, as it is now.
    dev = open_device(port, out / 'device.log')
    try:
        before = runtime(dev)
        require_production(before)
        print(f'device runs {before}; taking a fresh backup (K)', flush=True)
        fresh = backup_from_lines(dev.backup())
    finally:
        dev.close()
    (out / 'device-backup-before-hil.json').write_text(backup_file_text(fresh, before, started))
    section['device_before'] = {'runtime': before, 'backup_matches_updater_file': fresh == updater_backup}
    if fresh != updater_backup:
        print('note: the device holds data your updater backup does not (or the other way round); '
              f'the run restores the fresh backup, {out / "device-backup-before-hil.json"}', flush=True)

    hil_ok = False
    try:
        print(f'writing the HIL application ({len(hil_image)} bytes) at 0x10000 only', flush=True)
        section['image']['readback_sha256'] = write_application(port, image, out)
        dev, version = wait_runtime(port, out / 'device.log', lambda v: '-hil' in v)
        try:
            section['image']['runtime'] = version
            # A stopped earlier run can leave NVS filler; the scenarios expect Mounting Top.
            dev.hil('nvsfill 0', expect='HIL_OK nvsfill', timeout=30)
            dev.hil(f'set {MOUNTING_ROW} 0', expect='HIL_SET', timeout=6)
        finally:
            dev.close()
        command = [sys.executable, str(HERE / 'run_hil.py'), '--port', port, '--out', str(out / 'hil'),
                   '--build-dir', str(build_dir), *(['--sensor', sensor] if sensor else []), *scenarios]
        print('$', ' '.join(command[1:]), flush=True)
        returncode = subprocess.run(command).returncode
        summary = summarize(out / 'hil')
        section['scenarios'] = {'selection': ' '.join(scenarios), **summary}
        hil_ok = returncode == 0 and summary['completed'] and not summary['failed'] and summary['failed_checks'] == 0
    finally:
        section['restored'] = restore(port, out, stable, stable_image, fresh, before)
    section['passed'] = hil_ok and section['restored']['complete']
    section['seconds'] = round(time.monotonic() - start)
    return section


def restore(port, out, stable, stable_image, backup, before):
    """Stable application at 0x10000, read back, then the fresh backup. Never raises."""
    restored = {'version': stable['version'], 'sha256': stable['sha256'], 'readback_sha256': None,
                'runtime': None, 'data_restored': False, 'complete': False}
    try:
        print(f'putting back stable {stable["version"]} at 0x10000', flush=True)
        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as handle:
            handle.write(stable_image)
        try:
            restored['readback_sha256'] = write_application(port, handle.name, out)
        finally:
            os.unlink(handle.name)
        dev, version = wait_runtime(port, out / 'device.log', lambda v: v == stable['runtime'])
        restored['runtime'] = version
        try:
            print('restoring the device data from the fresh backup (R)', flush=True)
            dev.restore(restore_text(backup))
            restored['data_restored'] = True
        finally:
            dev.close()
        restored['complete'] = True
    except Exception as error:  # report, never mask the run's own result
        restored['error'] = str(error)
        print('\n!! The device could not be put back automatically: ' + str(error), file=sys.stderr)
        print(f'!! Open the updater (launchlab.beywatch.gg/update), install stable {stable["version"]}, then restore '
              f'{out / "device-backup-before-hil.json"} (or your own updater backup) with "Restore from a file".\n',
              file=sys.stderr)
    if before and '-hil' not in before and restored['runtime'] and restored['runtime'] != before:
        print(f'note: the device ran {before} before the run; it now runs stable {restored["runtime"]}', flush=True)
    return restored
