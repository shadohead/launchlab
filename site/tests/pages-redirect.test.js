import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const site = fileURLToPath(new URL('../', import.meta.url));
const target = 'https://launchlab.beywatch.gg/update';
function files(root, prefix = '') {
  return readdirSync(root + prefix, { withFileTypes: true }).flatMap(entry => {
    const path = prefix + entry.name;
    return entry.isDirectory() ? files(root, path + '/') : [path];
  }).sort();
}

test('legacy entry points to the exact canonical updater without a flashing interface', () => {
  const html = readFileSync(site + 'index.html', 'utf8');
  assert.ok(html.includes(`<meta http-equiv="refresh" content="0; url=${target}">`));
  assert.ok(html.includes(`<link rel="canonical" href="${target}">`));
  assert.ok(html.includes(`<a href="${target}">`), 'Manual fallback works without scripts');
  assert.doesNotMatch(html, /<script\b|<iframe\b|<form\b|<button\b|navigator\.serial/i);
});

test('Pages output preserves every static byte and excludes unhosted transfer guard source', () => {
  const built = spawnSync(process.execPath, ['../scripts/build-pages-redirect.mjs'], { cwd: site, encoding: 'utf8' });
  assert.equal(built.status, 0, built.stderr);
  const published = files(site + 'public/');
  assert.deepEqual(files(site + 'dist/'), [...published, 'index.html'].sort());
  assert.deepEqual(readFileSync(site + 'dist/index.html'), readFileSync(site + 'index.html'));
  for (const path of published) {
    assert.deepEqual(readFileSync(site + 'dist/' + path), readFileSync(site + 'public/' + path), path);
  }
  assert.ok(!files(site + 'dist/').some(path => /\.(?:js|mjs|css)$/.test(path)), 'No second updater code is hosted');
});
