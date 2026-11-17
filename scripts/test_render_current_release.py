#!/usr/bin/env python3
"""Tests for scripts/render-current-release.py on a small fixture repository.

The script finds its repository from its own location, so each test copies it
into a temporary tree and runs it there exactly as a release does.
scripts/verify-artifacts.py runs this file; run it alone with

    python3 scripts/test_render_current_release.py
"""
from pathlib import Path
import json, shutil, subprocess, sys, tempfile, unittest

SCRIPT = Path(__file__).resolve().parent/'render-current-release.py'
S, E = '<!-- current-release:start %s -->', '<!-- current-release:end -->'
POINTER = 'the updater guide names [the current release](UPDATER.md#current-release).'
STABLE, BETA = 'The current stable release is', 'The current beta is'


def block(kind, content=''):
    return S % kind + content + E


def lines(kind, content=''):
    return S % kind + '\n' + (content + '\n' if content else '') + E


class Fixture(unittest.TestCase):
    """1.0.0 is stable (published as a beta), 0.9.0 stable from before channels, 0.9.5 an old beta."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='current-release-'))
        self.addCleanup(shutil.rmtree, self.root)
        self.write('scripts/render-current-release.py', SCRIPT.read_text())
        self.index = {'latest': '1.0.0', 'beta': None,
                      'releases': [self.row('1.0.0', 'stable'), self.row('0.9.0', 'stable')],
                      'betaReleases': [self.row('0.9.5', 'beta')]}
        for version, name, published in [('1.0.0', 'alpha', 'beta'), ('0.9.5', 'old', 'beta'), ('0.9.0', 'first', None), ('1.1.0', 'next', 'beta')]:
            self.release(version, name, published)
        self.state(source='1.0.0')
        self.write('docs/UPDATER.md', '# Update\n\n## Current release\n\n' + lines('summary') + '\nHand-written line.\n')
        self.write('docs/GUIDE.md', f"Firmware {block('version')} runs as `{block('runtime')}`.\n"
                   f"See {block('release-notes')} and {block('build-context')}.\n"
                   'From 0.9.0 the menu has a Back row (the default before 0.9.5 was different).\n')
        self.write('firmware/LaunchLabMini/README.md', f"Current revision: **{block('runtime')}**. {block('source')}\n"
                   f"See {block('build-context')} and {block('release-notes')}.\n1.0.0 adds a page.\n")
        self.doc('1.0.0', channel=True)
        self.doc('0.9.5')
        self.doc('0.9.0')
        # Frozen trees are never read: stale words and markers there are not this script's business.
        self.write('firmware/releases/1.0.0-sticks3-alpha/notes.md', 'The current 0.1.0 firmware. ' + S % 'bogus' + '\n')
        self.write('hardware/v1/README.md', 'The recommended release is now 0.1.0.\n')

    def row(self, version, channel):
        return {'version': version, 'catalog': f'firmware/{version}/catalog.json', 'channel': channel}

    def write(self, relative, text):
        path = self.root/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def read(self, relative):
        return (self.root/relative).read_text()

    def release(self, version, name, published):
        release = f'{version}-sticks3-{name}'
        catalog = {'version': version, 'runtime': release + '-pm', 'release': release}
        if published:
            catalog['published_channel'] = published
        self.write(f'site/public/firmware/{version}/catalog.json', json.dumps(catalog))
        self.write(f'firmware/releases/{release}/README.md', f'# Firmware {version}\n')

    def doc(self, version, channel=False, title=None, pointer=True, extra=''):
        self.write(f'docs/FIRMWARE_{version}.md', (title or f'# Firmware {version} build context') + '\n\n' +
                   (lines(f'channel {version}') + '\n' if channel else '') +
                   (f'This document describes the preserved {version} release; {POINTER}\n' if pointer else '') + extra)

    def state(self, source=None):
        """What the release tools write: the index, the default catalog and the source version."""
        self.write('site/public/firmware/versions.json', json.dumps(self.index))
        shutil.copyfile(self.root/f"site/public/firmware/{self.index['latest']}/catalog.json", self.root/'site/public/firmware/catalog.json')
        if source:
            self.write('site/package.json', json.dumps({'version': source}))

    def beta(self, version='1.1.0', source='1.1.0'):
        self.index['betaReleases'].insert(0, self.row(version, 'beta'))
        self.index['beta'] = version
        self.doc(version, channel=True)
        self.state(source=source)

    def promote(self, version='1.1.0'):
        self.index['betaReleases'] = [r for r in self.index['betaReleases'] if r['version'] != version]
        self.index['releases'].insert(0, self.row(version, 'stable'))
        self.index.update(latest=version, beta=None)
        self.state()

    def run_script(self, *arguments):
        return subprocess.run([sys.executable, str(self.root/'scripts/render-current-release.py'), *arguments], capture_output=True, text=True)

    def render(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): p.read_text() for p in self.root.rglob('*.md')}

    def refused(self, *messages, arguments=('--check',)):
        """The run fails with every message and writes nothing."""
        before = self.snapshot()
        result = self.run_script(*arguments)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for message in messages:
            self.assertIn(message, result.stderr)
        self.assertEqual(self.snapshot(), before)
        return result


class Rendering(Fixture):
    def test_every_kind_with_no_beta(self):
        result = self.render()
        self.assertIn('PASS: 10 current-release blocks show stable 1.0.0, no beta', result.stdout)
        self.assertEqual(self.read('docs/GUIDE.md').splitlines()[:2], [
            f"Firmware {block('version', '1.0.0')} runs as `{block('runtime', '1.0.0-sticks3-alpha-pm')}`.",
            f"See {block('release-notes', '[1.0.0 release notes](../firmware/releases/1.0.0-sticks3-alpha/README.md)')}"
            f" and {block('build-context', '[build context](FIRMWARE_1.0.0.md)')}."])
        self.assertIn(lines('summary', f'{STABLE} **1.0.0** (`1.0.0-sticks3-alpha-pm`): '
                            '[1.0.0 release notes](../firmware/releases/1.0.0-sticks3-alpha/README.md) and [build context](FIRMWARE_1.0.0.md).\n'
                            'No beta is newer than it.'), self.read('docs/UPDATER.md'))
        # Links are relative to the document that holds them, and the source note is empty for the stable release.
        self.assertEqual(self.read('firmware/LaunchLabMini/README.md').splitlines()[:2], [
            f"Current revision: **{block('runtime', '1.0.0-sticks3-alpha-pm')}**. {block('source')}",
            f"See {block('build-context', '[build context](../../docs/FIRMWARE_1.0.0.md)')}"
            f" and {block('release-notes', '[1.0.0 release notes](../releases/1.0.0-sticks3-alpha/README.md)')}."])
        self.assertIn(lines('channel 1.0.0', 'Firmware 1.0.0 is a **stable** release: it was published as a beta and promoted after the maintainer hand-tested it.'),
                      self.read('docs/FIRMWARE_1.0.0.md'))

    def test_render_is_idempotent_and_leaves_other_text_and_frozen_trees_alone(self):
        frozen = self.read('firmware/releases/1.0.0-sticks3-alpha/notes.md')
        self.render()
        once = self.snapshot()
        self.assertEqual(self.render().stdout.count('updated'), 0)
        self.assertEqual(self.snapshot(), once)
        self.assertEqual(self.run_script('--check').returncode, 0)
        self.assertIn('\nHand-written line.\n', once['docs/UPDATER.md'])
        self.assertIn('1.0.0 adds a page.', once['firmware/LaunchLabMini/README.md'])
        self.assertEqual(once['firmware/releases/1.0.0-sticks3-alpha/notes.md'], frozen)

    def test_a_beta_newer_than_stable_and_its_source(self):
        self.render()
        self.beta()
        result = self.render()
        self.assertIn('show stable 1.0.0, beta 1.1.0', result.stdout)
        self.assertEqual(sorted(line.split()[1] for line in result.stdout.splitlines() if line.startswith('updated')),
                         ['docs/FIRMWARE_1.1.0.md', 'docs/UPDATER.md', 'firmware/LaunchLabMini/README.md'])
        self.assertIn(f'{BETA} **1.1.0** (`1.1.0-sticks3-next-pm`): [1.1.0 release notes](../firmware/releases/1.1.0-sticks3-next/README.md)'
                      ' and [build context](FIRMWARE_1.1.0.md).\n' + E, self.read('docs/UPDATER.md'))
        self.assertIn(f'{STABLE} **1.0.0**', self.read('docs/UPDATER.md'))
        self.assertIn(block('source', 'This folder holds the source of the newer **1.1.0-sticks3-next-pm** beta release: see its '
                            '[1.1.0 release notes](../releases/1.1.0-sticks3-next/README.md) and [build context](../../docs/FIRMWARE_1.1.0.md).'),
                      self.read('firmware/LaunchLabMini/README.md'))
        self.assertIn(block('runtime', '1.0.0-sticks3-alpha-pm'), self.read('firmware/LaunchLabMini/README.md'))
        self.assertIn(lines('channel 1.1.0', 'Firmware 1.1.0 is a **beta**: it passed the automated gates and has not been hand-tested for stable.'),
                      self.read('docs/FIRMWARE_1.1.0.md'))

    def test_a_promotion_is_only_the_index_and_a_render(self):
        self.beta()
        self.render()
        before = self.snapshot()
        self.promote()
        self.refused('stale current-release blocks in docs/FIRMWARE_1.1.0.md, docs/GUIDE.md, docs/UPDATER.md, firmware/LaunchLabMini/README.md')
        self.render()
        after = self.snapshot()
        self.assertEqual(self.run_script('--check').returncode, 0)
        # Older release documents never change again; the promoted one changes only its channel line.
        for unchanged in ('docs/FIRMWARE_1.0.0.md', 'docs/FIRMWARE_0.9.5.md', 'docs/FIRMWARE_0.9.0.md'):
            self.assertEqual(after[unchanged], before[unchanged])
        self.assertEqual(after['docs/FIRMWARE_1.1.0.md'], before['docs/FIRMWARE_1.1.0.md'].replace(
            'is a **beta**: it passed the automated gates and has not been hand-tested for stable.',
            'is a **stable** release: it was published as a beta and promoted after the maintainer hand-tested it.'))
        self.assertIn(f"Firmware {block('version', '1.1.0')} runs as `{block('runtime', '1.1.0-sticks3-next-pm')}`.", after['docs/GUIDE.md'])
        self.assertIn(f'{STABLE} **1.1.0** (`1.1.0-sticks3-next-pm`)', after['docs/UPDATER.md'])
        self.assertIn('No beta is newer than it.', after['docs/UPDATER.md'])
        self.assertIn(block('source') + '\n', after['firmware/LaunchLabMini/README.md'])

    def test_channel_of_an_old_stable_an_old_beta_and_an_unlisted_release(self):
        self.doc('0.9.0', channel=True)
        self.doc('0.9.5', channel=True)
        self.release('0.8.0', 'gone', None)
        self.doc('0.8.0', channel=True)
        self.render()
        self.assertIn(lines('channel 0.9.0', 'Firmware 0.9.0 is a **stable** release.'), self.read('docs/FIRMWARE_0.9.0.md'))
        self.assertIn(lines('channel 0.9.5', 'Firmware 0.9.5 is a **beta**: it passed the automated gates and has not been hand-tested for stable.'),
                      self.read('docs/FIRMWARE_0.9.5.md'))
        self.assertIn(lines('channel 0.8.0', 'Firmware 0.8.0 is not offered by the updater.'), self.read('docs/FIRMWARE_0.8.0.md'))

    def test_source_of_an_older_or_unlisted_release(self):
        self.state(source='0.9.0')
        self.render()
        self.assertIn('This folder holds the source of the older **0.9.0-sticks3-first-pm** stable release: see its '
                      '[0.9.0 release notes](../releases/0.9.0-sticks3-first/README.md)', self.read('firmware/LaunchLabMini/README.md'))
        self.release('1.2.0', 'private', None)
        self.doc('1.2.0')
        self.state(source='1.2.0')
        self.render()
        self.assertIn('the newer **1.2.0-sticks3-private-pm** unlisted release', self.read('firmware/LaunchLabMini/README.md'))


class Refusals(Fixture):
    def setUp(self):
        super().setUp()
        self.render()

    def test_stale_block(self):
        guide = self.read('docs/GUIDE.md')
        self.write('docs/GUIDE.md', guide.replace(block('version', '1.0.0'), block('version', '0.9.0')))
        self.refused('stale current-release blocks in docs/GUIDE.md; run python3 scripts/render-current-release.py')
        self.render()
        self.assertEqual(self.read('docs/GUIDE.md'), guide)

    def test_malformed_markers(self):
        guide = self.read('docs/GUIDE.md')
        for broken, message in [
                (guide + S % 'version' + '1.0.0\n', 'docs/GUIDE.md: unbalanced or malformed current-release markers'),
                (guide + '1.0.0' + E + '\n', 'docs/GUIDE.md: unbalanced or malformed current-release markers'),
                (guide + '<!-- current-release:start -->1.0.0' + E + '\n', 'docs/GUIDE.md: unbalanced or malformed current-release markers'),
                (guide + block('newest') + '\n', 'docs/GUIDE.md: unknown block kind "newest"'),
                (guide + block('channel') + '\n', 'docs/GUIDE.md: a "channel" block takes a version'),
                (guide + block('version 1.0.0') + '\n', 'docs/GUIDE.md: a "version" block takes no version'),
                (guide + 'Now: ' + block('summary') + '\n', 'docs/GUIDE.md: a "summary" block needs its markers on their own lines')]:
            self.write('docs/GUIDE.md', broken)
            for arguments in (('--check',), ()):
                self.refused(message, arguments=arguments)

    def test_missing_document_or_release_notes(self):
        (self.root/'docs/FIRMWARE_1.0.0.md').unlink()
        self.refused('docs/FIRMWARE_1.0.0.md is missing: firmware 1.0.0 needs it')
        self.doc('1.0.0', channel=True)
        self.render()
        (self.root/'firmware/releases/1.0.0-sticks3-alpha/README.md').unlink()
        self.refused('firmware/releases/1.0.0-sticks3-alpha/README.md is missing: firmware 1.0.0 needs it', arguments=())

    def test_a_beta_or_a_source_release_needs_its_document_too(self):
        self.beta(source='1.0.0')
        (self.root/'docs/FIRMWARE_1.1.0.md').unlink()
        self.refused('docs/FIRMWARE_1.1.0.md is missing: firmware 1.1.0 needs it')
        self.index['betaReleases'].pop(0)
        self.index['beta'] = None
        self.state(source='1.1.0')
        self.refused('docs/FIRMWARE_1.1.0.md is missing: firmware 1.1.0 needs it')

    def test_default_catalog_must_be_the_latest_stable(self):
        shutil.copyfile(self.root/'site/public/firmware/0.9.0/catalog.json', self.root/'site/public/firmware/catalog.json')
        self.refused('site/public/firmware/catalog.json is 0.9.0, but versions.json says latest is 1.0.0')

    def test_hand_written_current_release_prose(self):
        guide = self.read('docs/GUIDE.md')
        for prose in ('The current **1.0.0** firmware supports it directly.', 'The browser updater installs the current 0.9.0 firmware.',
                      '| Kit | Current 1.0.0, QRE profile |', 'Current revision: **1.0.0-sticks3-alpha-pm**, the stable release.',
                      'Use the latest 1.0.0 build.', 'It is a beta: the recommended release is now [1.0.0](FIRMWARE_1.0.0.md).',
                      'It is a beta: the recommended release remains [0.9.0](FIRMWARE_0.9.0.md).', '0.9.0 is now the recommended production release.',
                      'The current updater recommends 1.0.0; this document describes an older one.', 'Firmware 1.0.0 supports both saved sensor profiles.'):
            self.write('docs/GUIDE.md', guide + prose + '\n')
            for arguments in (('--check',), ()):
                self.refused(f'docs/GUIDE.md: names a release as the current one outside a current-release block: "{prose[:110]}"', arguments=arguments)
        # History is not a claim about the current release, and a block may sit between the words.
        self.write('docs/GUIDE.md', guide + 'String wear check arrived in 1.0.0 and the default changed from 0.9.0.\n'
                   f"The current **{block('version', '1.0.0')}** firmware supports it. The current layout is unchanged since 0.9.5 was a beta long ago, far from here.\n")
        self.assertEqual(self.run_script('--check').returncode, 0)

    def test_release_document_must_point_at_the_current_release(self):
        self.doc('0.9.0', pointer=False)
        self.refused('docs/FIRMWARE_0.9.0.md: must point at [the current release](UPDATER.md#current-release) rather than name a newer release')
        self.doc('0.9.0', extra='Superseded by [1.0.0](FIRMWARE_1.0.0.md); this document describes the preserved 0.9.0 release.\n')
        self.refused('docs/FIRMWARE_0.9.0.md: names a release as the current one outside a current-release block: "Superseded by [1.0.0]')

    def test_promotable_beta_document_needs_the_channel_block(self):
        self.beta()
        self.render()
        self.assertEqual(self.run_script('--check').returncode, 0)
        self.doc('1.1.0')
        self.refused('docs/FIRMWARE_1.1.0.md: needs a "<!-- current-release:start channel 1.1.0 -->" block, so its promotion edits nothing by hand')
        self.doc('1.1.0', channel=True, title='# Firmware 1.1.0 build context (beta)')
        self.refused('docs/FIRMWARE_1.1.0.md: its title must not say beta: the channel block says it')
        self.write('docs/FIRMWARE_1.1.0.md', f'# Firmware 1.1.0 build context\n\n{lines("channel 1.0.0")}\nThis document describes the preserved 1.1.0 release; {POINTER}\n')
        self.refused('docs/FIRMWARE_1.1.0.md: its channel block must be for 1.1.0')
        # A superseded beta can no longer be promoted, so its older document needs no channel block.
        self.doc('1.1.0', channel=True)
        self.doc('0.9.5', title='# Firmware 0.9.5 build context (beta)')
        self.render()

    def test_no_blocks_at_all_and_usage(self):
        for path in self.root.rglob('*.md'):
            path.write_text(BLOCK_FREE)
        self.doc('1.0.0')
        self.refused('no current-release blocks found')
        result = self.run_script('--bogus')
        self.assertEqual(result.returncode, 2)
        self.assertIn('current-release:start KIND', result.stdout)


BLOCK_FREE = 'Nothing generated here.\n'

if __name__ == '__main__':
    unittest.main()
