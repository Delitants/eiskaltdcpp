import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, writeFileSync, rmSync, symlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { runInNewContext } from 'node:vm';
import test from 'node:test';
import { icons } from './superdesign-catalog.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const sha = value => createHash('sha256').update(value).digest('hex');
const generate = (...args) => spawnSync(process.execPath, [join(here, 'generate-superdesign.mjs'), ...args], {encoding: 'utf8'});

test('vendored nodes exactly match pinned upstream module geometry', () => {
  const vendor = join(here, 'vendor-lucide');
  const provenance = JSON.parse(readFileSync(join(vendor, 'provenance.json')));
  const bank = JSON.parse(readFileSync(join(vendor, 'nodes.json')));
  assert.equal(provenance.version, '1.8.0');
  for (const source of provenance.sources) {
    const bytes = readFileSync(join(vendor, source.local));
    assert.equal(sha(bytes), source.sha256);
    const literal = bytes.toString().match(/const \w+ = (\[[\s\S]*?\]);/)[1];
    const geometry = JSON.parse(JSON.stringify(runInNewContext(`(${literal})`, {}, {timeout: 1000})));
    assert.deepEqual(geometry, bank[source.local.replace('sources/', '').replace('.js', '')]);
  }
});

test('Superdesign covers the unchanged legacy catalog with distinct action semantics', () => {
  const old = JSON.parse(readFileSync(join(here, 'manifest.json')));
  assert.deepEqual(icons.map(icon => icon.stem).sort(), old.icons.map(icon => icon.stem).sort());
  const mapping = Object.fromEntries(icons.map(icon => [icon.stem, icon]));
  for (const [a, b] of [['download', 'go-down-search'], ['media-play', 'media-pause'], ['edit-delete', 'dialog-close']])
    assert.notEqual(mapping[a].source, mapping[b].source);
  assert.equal(mapping.torrent.source, 'cloud-download');
  assert.equal(mapping.configure.source, 'settings');
  assert.equal(mapping['transfer-highlight'].badge, 'bell');
});

test('offline generation is reproducible and rejects tampering, extra files and symlinks', () => {
  const scratch = mkdtempSync(join(tmpdir(), 'superdesign-test-'));
  const output = join(scratch, 'theme');
  try {
    assert.equal(generate('--check').status, 0);
    assert.equal(generate('--output-root', output).status, 0);
    assert.equal(generate('--check', '--output-root', output).status, 0);
    const shipped = join(root, 'eiskaltdcpp-qt/icons/appl/superdesign');
    for (const icon of icons) for (const surface of ['', 'dark/'])
      assert.deepEqual(readFileSync(join(output, surface + icon.stem + '.svg')), readFileSync(join(shipped, surface + icon.stem + '.svg')));
    const path = join(output, 'configure.svg');
    const original = readFileSync(path);
    writeFileSync(path, 'modified');
    assert.match(generate('--output-root', output).stderr, /Refusing to overwrite modified/);
    assert.equal(readFileSync(path, 'utf8'), 'modified');
    writeFileSync(path, original);
    writeFileSync(join(output, 'unowned'), 'retain');
    assert.match(generate('--output-root', output).stderr, /Unexpected file retained/);
    rmSync(join(output, 'unowned'));
    symlinkSync(path, join(output, 'link'));
    assert.match(generate('--output-root', output).stderr, /Refusing output symlink/);
  } finally {
    rmSync(scratch, {recursive: true, force: true});
  }
});
