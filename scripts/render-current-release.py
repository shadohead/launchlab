#!/usr/bin/env python3
"""Write the current firmware release into the documents that show it.

Only the release index names the current release:
site/public/firmware/versions.json ("latest" is the stable release, "beta" the
newest beta while one is newer) with the catalogs beside it. Documents show it
through generated blocks, which this script rewrites:

    <!-- current-release:start KIND -->...<!-- current-release:end -->

Markers on one line make an inline block; a start marker that ends its line
makes a block of whole lines. Kinds:

    version          the stable release's version
    runtime          its runtime name, <version>-sticks3-<name>
    release-notes    a link to the stable release's notes in firmware/releases
    build-context    a link to the stable release's docs/FIRMWARE_<version>.md
    summary          the stable release and the beta, if any (docs/UPDATER.md)
    source           which release's source firmware/LaunchLabMini holds
    channel VERSION  whether VERSION is stable or beta, in its own document

Publishing or promoting a release is: write versions.json and catalog.json, add
docs/FIRMWARE_<version>.md for a new release, then run

    python3 scripts/render-current-release.py

Nothing else names the current release, so nothing else is edited by hand.
`--check` changes nothing and exits 1 when a block is stale or a document names
the current release outside a block; scripts/verify-artifacts.py runs it, after
scripts/test_render_current_release.py.

A new docs/FIRMWARE_<version>.md starts like this, so that neither its own
promotion nor any later release ever edits it:

    # Firmware <version> build context

    <!-- current-release:start channel <version> -->
    <!-- current-release:end -->
    This document describes the preserved <version> release; the updater guide names [the current release](UPDATER.md#current-release).
"""
from pathlib import Path
import json, os, re, sys

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT/'site/public/firmware'
START, END = '<!-- current-release:start', '<!-- current-release:end -->'
BLOCK = re.compile(r'<!-- current-release:start ([a-z-]+)(?: (\d+\.\d+\.\d+))? -->(.*?)<!-- current-release:end -->', re.S)
# Frozen or generated trees: never read for blocks, never rewritten.
SKIP = ('firmware/releases/', 'hardware/', 'site/public/', 'site/dist/', 'site/node_modules/', '.git/')
POINTER = '[the current release](UPDATER.md#current-release)'
RELEASE_DOC = re.compile(r'docs/FIRMWARE_(\d+\.\d+\.\d+)\.md')
VERSION = r'\d+\.\d+\.\d+'
# Prose that names a release as the current one. Inside a block it is generated;
# outside one it goes stale at the next promotion.
STALE = [re.compile(p) for p in (
    # "current 1.2.3", "Current revision: **1.2.3", "the current stable release is 1.2.3": at most
    # three words in between and no sentence end, so "current layout, unchanged since 1.2.3" is history.
    r'(?i)\b(?:current|recommended|latest)\b(?:[^\w.|\n]+[a-z]+){0,3}?[^\w.|\n]+' + VERSION + r'(?![\w.]*\d)',
    r'(?i)(?<![\w.])' + VERSION + r'\b[^.|\n]{0,40}?\b(?:is now|remains) the (?:current|recommended|latest)\b',
    r'(?i)\brecommended release (?:is now|remains)\b',
    r'(?i)\bupdater recommends ' + VERSION,
    r'(?i)\bfirmware \**' + VERSION + r'\** supports\b',
)]


class Problem(Exception):
    pass


def key(version):
    return tuple(map(int, version.split('.')))


def load(path):
    return json.loads(path.read_text())


class Releases:
    def __init__(self):
        self.index = load(FIRMWARE/'versions.json')
        self.latest, self.beta = self.index['latest'], self.index['beta']
        default = load(FIRMWARE/'catalog.json')
        if default['version'] != self.latest:
            raise Problem(f"site/public/firmware/catalog.json is {default['version']}, but versions.json says latest is {self.latest}")
        self.channels = {e['version']: e['channel'] for e in self.index['releases'] + self.index['betaReleases']}
        # The version whose source firmware/LaunchLabMini holds (scripts/verify-artifacts.py checks it does).
        self.source = load(ROOT/'site/package.json')['version']
        for version in filter(None, (self.latest, self.beta, self.source)):
            for path in (self.notes(version), self.context(version)):
                if not (ROOT/path).is_file():
                    raise Problem(f'{path} is missing: firmware {version} needs it')

    def catalog(self, version):
        return load(FIRMWARE/version/'catalog.json')

    def notes(self, version):
        return f"firmware/releases/{self.catalog(version)['release']}/README.md"

    def context(self, version):
        return f'docs/FIRMWARE_{version}.md'


def link(text, target, document):
    return f"[{text}]({Path(os.path.relpath(ROOT/target, document.parent)).as_posix()})"


def render(kind, argument, document, releases):
    r, latest = releases, releases.latest
    both = lambda version: (f"{link(version + ' release notes', r.notes(version), document)} and "
                            f"{link('build context', r.context(version), document)}")
    if (argument is not None) != (kind == 'channel'):
        raise Problem(f'a "{kind}" block takes {"a version" if kind == "channel" else "no version"}')
    if kind == 'version':
        return latest
    if kind == 'runtime':
        return r.catalog(latest)['runtime']
    if kind == 'release-notes':
        return link(latest + ' release notes', r.notes(latest), document)
    if kind == 'build-context':
        return link('build context', r.context(latest), document)
    if kind == 'summary':
        stable = f"The current stable release is **{latest}** (`{r.catalog(latest)['runtime']}`): {both(latest)}."
        if r.beta is None:
            return stable + '\nNo beta is newer than it.'
        return stable + f"\nThe current beta is **{r.beta}** (`{r.catalog(r.beta)['runtime']}`): {both(r.beta)}."
    if kind == 'source':
        if r.source == latest:
            return ''
        what = 'the newer' if key(r.source) > key(latest) else 'the older'
        channel = r.channels.get(r.source, 'unlisted')
        return (f"This folder holds the source of {what} **{r.catalog(r.source)['runtime']}** {channel} release: "
                f"see its {both(r.source)}.")
    if kind == 'channel':
        channel = r.channels.get(argument)
        if channel == 'beta':
            return f'Firmware {argument} is a **beta**: it passed the automated gates and has not been hand-tested for stable.'
        if channel == 'stable':
            promoted = r.catalog(argument).get('published_channel') == 'beta'
            return (f'Firmware {argument} is a **stable** release' +
                    (': it was published as a beta and promoted after the maintainer hand-tested it.' if promoted else '.'))
        return f'Firmware {argument} is not offered by the updater.'
    raise Problem(f'unknown block kind "{kind}"')


def documents():
    for path in sorted(ROOT.rglob('*.md')):
        relative = path.relative_to(ROOT).as_posix()
        if not relative.startswith(SKIP):
            yield path, relative


def rewrite(text, document, releases):
    def block(match):
        kind, argument, old = match.group(1), match.group(2), match.group(3)
        content = render(kind, argument, document, releases)
        whole_lines = old.startswith('\n')
        if '\n' in content and not whole_lines:
            raise Problem(f'a "{kind}" block needs its markers on their own lines')
        body = f'\n{content}\n' if whole_lines else content
        return f"{START} {kind}{' ' + argument if argument else ''} -->{body}{END}"
    rendered, count = BLOCK.subn(block, text)
    if text.count(START) != count or text.count(END) != count:
        raise Problem('unbalanced or malformed current-release markers')
    return rendered


def lint(text, relative, releases):
    """What would go stale at the next promotion, outside the generated blocks."""
    problems = []
    prose = BLOCK.sub('', text)
    doc = RELEASE_DOC.fullmatch(relative)
    for line in prose.splitlines():
        if any(p.search(line) for p in STALE) or (doc and 'Superseded by' in line):
            problems.append(f'names a release as the current one outside a current-release block: "{line.strip()[:110]}"')
    if doc:
        version = doc.group(1)
        if POINTER not in text:
            problems.append(f'must point at {POINTER} rather than name a newer release')
        channels = [m.group(2) for m in BLOCK.finditer(text) if m.group(1) == 'channel']
        if any(c != version for c in channels):
            problems.append(f'its channel block must be for {version}')
        # A beta newer than the stable release can still be promoted: its document
        # must already say everything a promotion would otherwise have to edit.
        promotable = releases.channels.get(version) == 'beta' and key(version) > key(releases.latest)
        if promotable or channels:
            title = text.splitlines()[0] if text else ''
            if not channels:
                problems.append(f'needs a "<!-- current-release:start channel {version} -->" block, so its promotion edits nothing by hand')
            if 'beta' in title.lower():
                problems.append('its title must not say beta: the channel block says it')
    return problems


def main(argv):
    if argv not in ([], ['--check']):
        print(__doc__)
        return 2
    check = argv == ['--check']
    try:
        releases = Releases()
        stale, problems, blocks = {}, [], 0
        for path, relative in documents():
            text = path.read_text()
            try:
                rendered = rewrite(text, path, releases)
            except Problem as problem:
                raise Problem(f'{relative}: {problem}')
            blocks += text.count(START)
            problems += [f'{relative}: {p}' for p in lint(rendered, relative, releases)]
            if rendered != text:
                stale[relative] = (path, rendered)
        if not blocks:
            raise Problem('no current-release blocks found')
    except Problem as problem:
        print(f'FAIL: {problem}', file=sys.stderr)
        return 1
    # Nothing is written unless every document rendered; prose problems are reported after the blocks are fresh.
    if not check:
        for relative, (path, rendered) in stale.items():
            path.write_text(rendered)
            print(f'updated {relative}')
    for problem in problems:
        print(f'FAIL: {problem}', file=sys.stderr)
    if check and stale:
        print('FAIL: stale current-release blocks in ' + ', '.join(stale) + '; run python3 scripts/render-current-release.py', file=sys.stderr)
    if problems or (check and stale):
        return 1
    beta = f', beta {releases.beta}' if releases.beta else ', no beta'
    print(f"PASS: {blocks} current-release blocks show stable {releases.latest}{beta}")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
