#!/usr/bin/env python3
"""scripts/firmware_proof.py: a pull request's proof must belong to its head tree.

The check CI runs (`firmware_proof.py check`) is exercised on throwaway git
repositories: a proof for the head passes, a proof for an older tree, a
failed or partial suite and a missing proof on a firmware change fail, and a
contributor's HIL report is shown but never decides alone.
"""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'firmware/tools/hil'))
import firmware_proof  # noqa: E402
import firmware_tests  # noqa: E402
import contributor_hil  # noqa: E402

SUITES = ['test_a', 'test_b', 'check_c']


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True, text=True).stdout.strip()


class Repo:
    def __init__(self, path):
        self.path = path
        git(path, 'init', '-q', '-b', 'main')
        git(path, 'config', 'user.email', 'test@example.com')
        git(path, 'config', 'user.name', 'Test')
        self.write('firmware/LaunchLabMini/sketch.h', 'int x = 1;\n')
        self.write('scripts/firmware_tests.py', '# runner\n')
        self.write('docs/README.md', 'docs\n')
        self.base = self.commit('base')

    def write(self, name, text):
        target = self.path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def commit(self, message):
        git(self.path, 'add', '-A')
        git(self.path, 'commit', '-q', '-m', message)
        return git(self.path, 'rev-parse', 'HEAD')

    def proof(self, **host):
        result = {
            'kind': firmware_proof.KIND, 'format': firmware_proof.FORMAT,
            'tree': {'commit': git(self.path, 'rev-parse', 'HEAD'),
                     'fingerprint': firmware_tests.fingerprint(self.path),
                     'objects': firmware_proof.tested_objects(self.path)},
            'host_suite': {'passed': True, 'complete': True, 'checkout': 'public', 'suites': len(SUITES), 'failed': [],
                           'private_only_not_run': 16, 'suites_sha256': firmware_proof.suite_names_sha256(SUITES),
                           'tree_changed_during_run': False, 'compile_cache': True, 'started_utc': 'now',
                           'seconds': 300},
            'tools': {'compiler': 'clang', 'python': '3.12', 'os': 'test'},
            'hil': None,
        }
        result['host_suite'].update(host)
        return result

    def verdict(self, body, base=None):
        event = {'pull_request': {'head': {'sha': git(self.path, 'rev-parse', 'HEAD')},
                                  'base': {'sha': base or self.base}, 'body': body}}
        return firmware_proof.verdict(self.path, event, suite_names=lambda root: list(SUITES))


def body(*proofs, crlf=False):
    text = '## Summary\n\nA change.\n\n## Test proof\n\n' + ''.join(firmware_proof.block(p) for p in proofs)
    return text.replace('\n', '\r\n') if crlf else text


class Check(unittest.TestCase):
    def setUp(self):
        work = Path(tempfile.mkdtemp(prefix='launchlab-proof-test-')).resolve()
        self.addCleanup(shutil.rmtree, work, True)
        self.repo = Repo(work)
        self.repo.write('firmware/LaunchLabMini/sketch.h', 'int x = 2;\n')
        self.repo.commit('firmware change')

    def assertVerdict(self, body, ok, contains=()):
        passed, lines = self.repo.verdict(body)
        text = '\n'.join(lines)
        self.assertEqual(passed, ok, text)
        for part in contains:
            self.assertIn(part, text)
        return text

    def test_proof_for_the_head_passes(self):
        self.assertVerdict(body(self.repo.proof()), True, ['matches the head', 'not run (optional)'])

    def test_browser_line_endings_parse(self):
        self.assertVerdict(body(self.repo.proof(), crlf=True), True)

    def test_firmware_change_needs_a_proof(self):
        self.assertVerdict(body(), False, ['No test proof'])

    def test_proof_of_an_older_tree_fails(self):
        old = self.repo.proof()
        self.repo.write('firmware/LaunchLabMini/sketch.h', 'int x = 3;\n')
        self.repo.commit('pushed after the proof')
        self.assertVerdict(body(old), False, ['another tree', 'firmware differ'])

    def test_failed_or_partial_suites_fail(self):
        self.assertVerdict(body(self.repo.proof(passed=False, failed=['test_b'])), False, ['failed: test_b'])
        self.assertVerdict(body(self.repo.proof(complete=False)), False, ['complete=False'])
        self.assertVerdict(body(self.repo.proof(suites=2, suites_sha256='0' * 64)), False, ['whole suite'])
        self.assertVerdict(body(self.repo.proof(tree_changed_during_run=True)), False)

    def test_two_proof_blocks_fail(self):
        self.assertVerdict(body(self.repo.proof(), self.repo.proof()), False, ['more than one proof'])

    def test_the_template_text_around_the_block(self):
        template = ('## Test proof\n\n<!--\nRun it and paste the ```json block it prints below, replacing this '
                    'comment.\n-->\n\n')
        self.assertVerdict(template + firmware_proof.block(self.repo.proof()) + '\n## HIL run\n\n<!-- ``` -->\n', True)
        self.assertVerdict(template.replace('\n', '\r\n') + body(self.repo.proof(), crlf=True), True)

    def test_other_json_blocks_are_ignored(self):
        text = '```json\n{"kind": "something else"}\n```\n' + body(self.repo.proof())
        self.assertVerdict(text, True)

    def test_change_outside_firmware_needs_no_proof(self):
        base = git(self.repo.path, 'rev-parse', 'HEAD')
        self.repo.write('docs/README.md', 'better docs\n')
        self.repo.commit('docs')
        passed, lines = self.repo.verdict(body(), base=base)
        self.assertTrue(passed, lines)
        self.assertIn('no proof is needed', '\n'.join(lines))

    def test_hil_is_reported_and_its_failure_counts(self):
        proof = self.repo.proof()
        fingerprint = proof['tree']['fingerprint']
        proof['hil'] = {
            'contributor_reported': True, 'passed': True,
            'image': {'sha256': 'ab' * 32, 'readback_sha256': 'ab' * 32, 'runtime': '0.20.8-sticks3-songs-hil-pm',
                      'tree_fingerprint': fingerprint},
            'scenarios': {'selection': 'all', 'run': 31, 'failed': [], 'warnings': 0, 'log_sha256': 'cd' * 32},
            'restored': {'runtime': '0.20.8-sticks3-songs-pm', 'data_restored': True, 'complete': True},
        }
        self.assertVerdict(body(proof), True, ['contributor-reported', '31 scenarios', 'this tree'])
        other = copy.deepcopy(proof)
        other['hil']['image']['tree_fingerprint'] = '0' * 32
        self.assertVerdict(body(other), True, ['**another tree**', 'says nothing about this head'])
        failed = copy.deepcopy(proof)
        failed['hil'].update(passed=False)
        failed['hil']['scenarios']['failed'] = ['normal']
        self.assertVerdict(body(failed), False, ['did not pass'])
        mismatch = copy.deepcopy(proof)
        mismatch['hil']['image']['readback_sha256'] = 'ef' * 32
        self.assertVerdict(body(mismatch), False, ['did not read back'])

    def test_changes_to_what_ci_runs_are_flagged(self):
        self.repo.write('scripts/firmware_tests.py', '# a different runner\n')
        self.repo.commit('runner change')
        self.assertVerdict(body(self.repo.proof()), True, ['changes what CI runs', 'scripts/firmware_tests.py'])


class HilFiles(unittest.TestCase):
    """The HIL wrapper reads the updater's backup file and rebuilds it from the device's K output."""

    def container(self, size=120):
        return contributor_hil.BACKUP_MAGIC + (7).to_bytes(4, 'little') + bytes(range(size - 8))

    def test_backup_round_trip(self):
        data = self.container()
        work = Path(tempfile.mkdtemp(prefix='launchlab-backup-test-'))
        self.addCleanup(shutil.rmtree, work, True)
        path = work / 'launchlab-backup.json'
        path.write_text(contributor_hil.backup_file_text(data, '0.20.8-sticks3-songs-pm', 'now'))
        self.assertEqual(contributor_hil.read_backup_file(path), (data, 7))
        lines = contributor_hil.restore_text(data).splitlines()
        self.assertTrue(all(len(line) <= len('BACKUP_DATA ') + 96 for line in lines))
        k_output = f'BACKUP_BEGIN bytes={len(data)}\n' + contributor_hil.restore_text(data) + 'BACKUP_END\n'
        self.assertEqual(contributor_hil.backup_from_lines(k_output), data)

    def test_bad_backups_are_refused(self):
        work = Path(tempfile.mkdtemp(prefix='launchlab-backup-test-'))
        self.addCleanup(shutil.rmtree, work, True)
        for name, text in [('not-json', '{'), ('other-kind', '{"kind": "x", "data": ""}'),
                           ('not-llbk', json.dumps({'kind': contributor_hil.BACKUP_KIND, 'format': 7,
                                                    'data': 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'}))]:
            path = work / name
            path.write_text(text)
            with self.assertRaises(contributor_hil.HilError):
                contributor_hil.read_backup_file(path)
        data = self.container()
        short = f'BACKUP_BEGIN bytes={len(data) + 1}\n' + contributor_hil.restore_text(data) + 'BACKUP_END\n'
        with self.assertRaises(contributor_hil.HilError):
            contributor_hil.backup_from_lines(short)

    def test_stable_release_from_either_index(self):
        work = Path(tempfile.mkdtemp(prefix='launchlab-stable-test-'))
        self.addCleanup(shutil.rmtree, work, True)
        image = bytes([0xE9]) + bytes(63)
        digest = contributor_hil.sha256(image)
        public = work / 'public'
        (public / 'site/public/firmware/9.9.9').mkdir(parents=True)
        (public / 'site/public/firmware/9.9.9/application.bin').write_bytes(image)
        (public / 'site/public/firmware/9.9.9/catalog.json').write_text(json.dumps({
            'version': '9.9.9', 'runtime': '9.9.9-sticks3-songs-pm',
            'files': [{'path': 'firmware/9.9.9/application.bin', 'offset': 0x10000, 'size': 64, 'sha256': digest}]}))
        (public / 'site/public/firmware/versions.json').write_text(json.dumps({
            'latest': '9.9.9', 'releases': [{'version': '9.9.9', 'catalog': 'firmware/9.9.9/catalog.json', 'channel': 'stable'}]}))
        found = contributor_hil.stable_release(public)
        self.assertEqual((found['runtime'], found['sha256']), ('9.9.9-sticks3-songs-pm', digest))
        self.assertEqual(contributor_hil.checked_image(found['path'], found['sha256']), image)
        private = work / 'private'
        (private / 'firmware/channels').mkdir(parents=True)
        for version, channel in (('9.9.8', 'stable'), ('9.9.10', 'stable'), ('9.9.11', 'beta')):
            folder = private / f'firmware/releases/{version}-sticks3-songs-pm'
            folder.mkdir(parents=True)
            (folder / 'LaunchLabMini.ino.bin').write_bytes(image)
            (folder / 'manifest.json').write_text(json.dumps({'runtime_version': f'{version}-sticks3-songs-pm'}))
        (private / 'firmware/channels/index.json').write_text(json.dumps({'releases': [
            {'version': v, 'channel': c, 'private_release': f'{v}-sticks3-songs-pm', 'application_sha256': digest}
            for v, c in (('9.9.8', 'stable'), ('9.9.10', 'stable'), ('9.9.11', 'beta'))]}))
        self.assertEqual(contributor_hil.stable_release(private)['runtime'], '9.9.10-sticks3-songs-pm')
        with self.assertRaises(contributor_hil.HilError):
            contributor_hil.checked_image(found['path'], '0' * 64)

    def test_a_device_left_on_a_hil_image_is_refused(self):
        # 7C:E8, 2026-11-09: a backup taken on a gate's HIL image held the gate's test data, not the owner's.
        with self.assertRaises(contributor_hil.HilError):
            contributor_hil.require_production('0.20.8-sticks3-songs-hil-pm')
        contributor_hil.require_production('0.20.8-sticks3-songs-pm')

    def test_hil_output_stays_out_of_the_repository(self):
        with self.assertRaises(contributor_hil.HilError):
            contributor_hil.run(port='/dev/null', backup='x', out=ROOT / 'hil-out', build_dir=ROOT, scenarios=['all'])


if __name__ == '__main__':
    unittest.main()
