import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const assets = join(root, 'eiskaltdcpp-qt/icons/appl');
const themes = ['prism', 'contour', 'slate', 'reborn'];
const extras = ['media-play', 'media-pause', 'document-new', 'sliders',
  'settings-notifications', 'settings-sharing', 'settings-advanced', 'settings-user-commands', 'torrent'];
const excluded = /^(?:icon_appl.*|icon_msg.*|qt-logo|toolbar20(?:-highlight)?)$/;
const sha256 = value => createHash('sha256').update(value).digest('hex');
const json = path => JSON.parse(readFileSync(path, 'utf8'));
const manifestPath = join(here, 'manifest.json');
const manifest = () => {
  assert.ok(existsSync(manifestPath), 'Generate the complete asset manifest first');
  return json(manifestPath);
};
const run = (...args) => spawnSync(process.execPath, [join(here, 'generate.mjs'), ...args], { encoding: 'utf8' });

function loaderStems(source) {
  const code = source.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\*[\s\S]*?\*\/|\/\/[^\r\n]*/g,
    token => token.startsWith('/') ? ' ' : token);
  return [...new Set([...code.matchAll(/\bFROMTHEME(?:_SIDE)?\s*\(\s*"([^\"]+)"/g),
    ...code.matchAll(/\bcacheIcon\s*\(\s*(?:\w+::)*ei\w+\s*,\s*"([^\"]+)"/g)]
    .map(match => match[1]))].sort();
}

function requiredStems() {
  const qrc = readFileSync(join(assets, 'apex/apex.qrc'), 'utf8');
  const source = readFileSync(join(root, 'eiskaltdcpp-qt/src/WulforUtil.cpp'), 'utf8');
  const loader = source.match(/\bbool\s+WulforUtil::loadIcons\s*\(\s*\)\s*\{([\s\S]*?)\breturn\s+!m_bError\s*;/)?.[1];
  assert.ok(loader, 'Could not locate the loadIcons body for coverage checking');
  return [...new Set([...qrc.matchAll(/alias="([^\"]+)\.png"/g)]
    .map(match => match[1]).concat(loaderStems(loader), extras))].filter(stem => !excluded.test(stem)).sort();
}

test('loader coverage recognizes cacheIcon-only, legacy, and mixed assignments', () => {
  const fixtures = [
    ['cacheIcon(eiSETTINGS_SHORTCUTS, "settings-shortcuts", resourceFound);\n'
      + 'cacheIcon( WulforUtil::eiTORRENT,\n "torrent", resourceFound, 28 );', ['settings-shortcuts', 'torrent']],
    ['FROMTHEME("find", resourceFound); FROMTHEME_SIDE ( "icon_appl_big", resourceFound, 128 );', ['find', 'icon_appl_big']],
    ['cacheIcon(eiFIND, "find", resourceFound); FROMTHEME("find", resourceFound);\n'
      + '// cacheIcon(eiOLD, "obsolete", resourceFound);\n'
      + '/* FROMTHEME("removed", resourceFound); */', ['find']],
  ];
  for (const [source, expected] of fixtures) assert.deepEqual(loaderStems(source), expected);
});

test('all four families cover every Apex/loader action and the nine additions on both surfaces', () => {
  const m = manifest();
  const stems = requiredStems();
  assert.deepEqual(m.icons.map(icon => icon.stem).sort(), stems);
  assert.equal(m.defaultTheme, 'reborn');
  assert.equal(m.outputs.length, stems.length * 8);
  for (const theme of themes) {
    for (const surface of ['light', 'dark']) {
      const dir = join(assets, theme, surface === 'dark' ? 'dark' : '');
      assert.ok(existsSync(dir), `Missing ${dir}`);
      const entries = readdirSync(dir).filter(name => name !== 'dark');
      const support = surface === 'light' ? ['LICENSE', 'README.md'] : [];
      assert.deepEqual(entries.sort(), [...stems.map(stem => `${stem}.svg`), ...support].sort());
      assert.equal(m.outputs.filter(out => out.theme === theme && out.surface === surface).length, stems.length);
    }
  }
});

test('SVGs retain native coordinates without a global fractional scale or jagged rendering hint', () => {
  const m = manifest();
  assert.equal(m.coordinateSystem.opticalTransform, null);
  for (const out of m.outputs) {
    const svg = readFileSync(join(root, out.path), 'utf8');
    assert.doesNotMatch(svg, /translate\(12\.8[ ,]+12\.8\)|scale\(0\.9\)|crispEdges|shape-rendering/);
    const transforms = [...svg.matchAll(/<g transform="matrix\(([^)]+)\)"/g)].map(match => match[1]);
    assert.deepEqual(transforms, out.layers.map(layer => layer.transform.join(' ')), out.path);
    assert.equal([...svg.matchAll(/<g\b/g)].length, out.layers.length, `Extra outer transform: ${out.path}`);
  }
});

test('Torrent uses an open segmented ring and one centered down arrow in every family', () => {
  const m = manifest();
  const torrents = m.outputs.filter(out => out.stem === 'torrent');
  assert.equal(torrents.length, 8);
  for (const out of torrents) {
    assert.equal(out.layers.length, 2);
    const [ring, arrow] = out.layers;
    assert.equal(ring.icon, 'circle-dashed');
    assert.equal(ring.weight, out.theme === 'contour' ? 'light' : 'bold');
    assert.deepEqual(ring.transform, [1, 0, 0, 1, 0, 0]);
    assert.equal(arrow.icon, out.theme === 'contour' ? 'arrow-down' : 'arrow-fat-down');
    assert.equal(arrow.paint, 'green');
    assert.equal(arrow.transform[0] * 128 + arrow.transform[4], 128, 'Arrow must be horizontally centered');
    const centerY = arrow.icon === 'arrow-down' ? 128 : 136;
    assert.equal(arrow.transform[3] * centerY + arrow.transform[5], 128, 'Arrow must be optically centered');
    const svg = readFileSync(join(root, out.path), 'utf8');
    assert.doesNotMatch(svg, /globe|arrows-clockwise|<image\b/);
  }
});

test('the legacy Torrent raster remains byte-identical', () => {
  assert.equal(sha256(readFileSync(join(root, 'eiskaltdcpp-qt/icons/torrent/torrent.png'))),
    'e83ed3f29d332ef6e9fe71b114b8b85325cc695cae3a9080271a4bd7022c1eee');
});

test('every SVG is standalone, transparent, XML-valid, locally resolved, and manifest-hashed', () => {
  const m = manifest();
  const files = [];
  for (const out of m.outputs) {
    assert.match(out.path, /^eiskaltdcpp-qt\/icons\/appl\/(prism|contour|slate|reborn)\/(dark\/)?[a-z0-9_-]+\.svg$/);
    const path = join(root, out.path);
    const svg = readFileSync(path, 'utf8');
    assert.equal(sha256(svg), out.sha256, out.path);
    assert.match(svg, /<svg[^>]+viewBox="0 0 256 256"/);
    assert.match(svg, /xmlns="http:\/\/www\.w3\.org\/2000\/svg"/);
    assert.doesNotMatch(svg, /currentColor|var\(|<!DOCTYPE|<!ENTITY|<style\b|style=|<script\b|<image\b|<text\b|<foreignObject\b|<use\b|<mask\b|<filter\b|href=|font-/i);
    const tags = [...svg.matchAll(/<\/?([a-zA-Z][\w-]*)\b/g)].map(match => match[1]);
    assert.ok(tags.every(tag => ['svg', 'title', 'desc', 'defs', 'linearGradient', 'stop', 'g', 'path'].includes(tag)), out.path);
    const ids = [...svg.matchAll(/\bid="([^\"]+)"/g)].map(match => match[1]);
    assert.equal(new Set(ids).size, ids.length, `Duplicate IDs: ${out.path}`);
    for (const [, ref] of svg.matchAll(/url\(([^)]+)\)/g)) {
      assert.ok(ref.startsWith('#') && ids.includes(ref.slice(1)), `Unresolved paint: ${out.path} ${ref}`);
    }
    assert.ok([...svg.matchAll(/<path\b/g)].length > 0, `Empty asset: ${out.path}`);
    files.push(path);
  }
  for (let i = 0; i < files.length; i += 100) {
    const result = spawnSync('xmllint', ['--nonet', '--noout', ...files.slice(i, i + 100)], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.error?.message || result.stderr);
  }
});

test('all emitted geometry comes verbatim from the pinned, MIT-licensed minimal source bank', () => {
  const m = manifest();
  const provenance = json(join(here, 'vendor/provenance.json'));
  assert.equal(provenance.package, '@phosphor-icons/core');
  assert.equal(provenance.version, '2.1.1');
  assert.equal(provenance.license, 'MIT');
  assert.equal(provenance.archiveSha256, '313332be6190b724da24107addd781799b48bf76b13963f24501112ffe1baadd');
  const license = readFileSync(join(here, 'vendor/LICENSE'));
  assert.equal(sha256(license), provenance.licenseSha256);
  assert.match(license.toString(), /Copyright \(c\) 2023 Phosphor Icons/);
  const used = new Set(m.outputs.flatMap(out => out.layers.map(layer => layer.source)));
  assert.deepEqual(provenance.files.map(file => file.path).sort(), [...used].sort());
  const geometry = new Map();
  for (const file of provenance.files) {
    const body = readFileSync(join(here, 'vendor', file.path), 'utf8');
    assert.equal(sha256(body), file.sha256, file.path);
    geometry.set(file.path, new Set([...body.matchAll(/\bd="([^\"]+)"/g)].map(match => match[1])));
  }
  for (const out of m.outputs) {
    const allowed = new Set(out.layers.flatMap(layer => [...geometry.get(layer.source)]));
    const svg = readFileSync(join(root, out.path), 'utf8');
    for (const [, d] of svg.matchAll(/\bd="([^\"]+)"/g)) {
      assert.ok(allowed.has(d), `Unlicensed or altered path in ${out.path}`);
    }
  }
  const walk = dir => readdirSync(dir, { withFileTypes: true }).flatMap(entry => entry.isDirectory()
    ? walk(join(dir, entry.name)) : [join(dir, entry.name)]);
  const bank = walk(join(here, 'vendor')).map(path => path.slice(join(here, 'vendor').length + 1)).sort();
  assert.deepEqual(bank, ['LICENSE', 'provenance.json', ...used].sort());
});

test('each shipped theme carries the exact upstream license and source attribution', () => {
  const m = manifest();
  const license = readFileSync(join(here, 'vendor/LICENSE'));
  for (const theme of themes) {
    const licensePath = join(assets, theme, 'LICENSE');
    assert.ok(existsSync(licensePath), `Missing distributed license: ${theme}`);
    assert.ok(readFileSync(licensePath).equals(license), `Modified distributed license: ${theme}`);
    const readme = readFileSync(join(assets, theme, 'README.md'), 'utf8');
    assert.match(readme, /Phosphor Icons/);
    assert.match(readme, /2\.1\.1/);
    assert.match(readme, /https:\/\/github\.com\/phosphor-icons\/core/);
    assert.match(readme, /MIT/);
    assert.match(readme, /LICENSE/);
  }
  assert.equal(m.supportingFiles.length, 8);
  for (const file of m.supportingFiles) assert.equal(sha256(readFileSync(join(root, file.path))), file.sha256, file.path);
});

test('play, pause, close, delete, favorites, directions, and settings retain distinct meanings', () => {
  const m = manifest();
  const icon = stem => m.icons.find(entry => entry.stem === stem);
  for (const [stem, source] of [['media-play', 'play'], ['media-pause', 'pause'],
    ['dialog-close', 'x'], ['edit-delete', 'trash'],
    ['settings-notifications', 'bell'], ['settings-user-commands', 'terminal-window']]) {
    assert.ok(icon(stem).components.some(component => component.icon === source), stem);
  }
  const keys = icon('settings-shortcuts').components;
  assert.deepEqual(keys.map(key => key.icon), ['arrow-square-up', 'arrow-square-left', 'arrow-square-down', 'arrow-square-right']);
  const [up, left, down, right] = keys;
  assert.equal(up.transform[4], down.transform[4]);
  assert.ok(up.transform[5] < down.transform[5]);
  assert.equal(left.transform[5], down.transform[5]);
  assert.equal(right.transform[5], down.transform[5]);
  assert.ok(left.transform[4] < down.transform[4] && down.transform[4] < right.transform[4]);
  for (const stem of ['favserver', 'favusers']) {
    assert.ok(icon(stem).components.some(component => component.icon === 'star' && component.paint === 'gold'), stem);
  }
  assert.ok(icon('download').components.some(component => component.icon === 'list-bullets'));
  assert.ok(icon('download').components.some(component => component.icon === 'arrow-fat-down'));
  assert.ok(icon('go-down-search').components.some(component => component.icon === 'tray'));
  assert.ok(icon('go-down-search').components.some(component => component.icon === 'check-circle'));
  for (const [base, highlight] of [['transfer', 'transfer-highlight'], ['queued-users', 'queued-users-highlight']]) {
    for (const theme of themes) {
      for (const surface of ['light', 'dark']) {
        const hash = stem => m.outputs.find(out => out.stem === stem && out.theme === theme && out.surface === surface).sha256;
        assert.notEqual(hash(base), hash(highlight), `${theme}/${surface}/${highlight}`);
      }
    }
  }
  for (const theme of themes) {
    for (const surface of ['light', 'dark']) {
      const hash = stem => m.outputs.find(out => out.stem === stem && out.theme === theme && out.surface === surface).sha256;
      assert.equal(new Set(['media-play', 'media-pause', 'dialog-close', 'edit-delete'].map(hash)).size, 4);
      assert.notEqual(hash('go-down-search'), hash('go-up-search'));
    }
  }
});

test('Qt renders queue and completed downloads with different shapes on both surfaces', () => {
  const audit = join(here, 'image-audit.mjs');
  assert.ok(existsSync(audit), 'Add the standalone Qt image audit');
  const result = spawnSync(process.execPath, [audit], { encoding: 'utf8', timeout: 120000 });
  assert.equal(result.status, 0, result.stderr || result.stdout);
  const report = JSON.parse(result.stdout);
  assert.equal(report.modern.length, 32);
  for (const pair of report.modern) {
    assert.equal(pair.identicalPixels, false, pair.id);
    assert.ok(pair.alphaChangedFraction >= 0.05, `${pair.id}: queue/completed geometry differs on less than 5% of pixels`);
  }
  assert.equal(report.legacy.length, 5);
});

test('Contour uses light outlines, Slate uses neutral solids, and Reborn stays bright on both surfaces', () => {
  const m = manifest();
  for (const out of m.outputs) {
    if (out.theme === 'contour') {
      assert.ok(out.layers.every(layer => ['light', 'regular'].includes(layer.weight)), out.path);
    } else if (['slate', 'reborn'].includes(out.theme)) {
      assert.ok(out.layers.every(layer => ['fill', 'bold'].includes(layer.weight)), out.path);
    }
  }
  for (const surface of ['light', 'dark']) {
    for (const key of ['primary', 'teal', 'green', 'gold']) {
      const rgb = m.themes.reborn.palettes[surface][key].match(/[\da-f]{2}/gi).map(hex => parseInt(hex, 16));
      assert.ok(Math.max(...rgb) >= 155 && Math.max(...rgb) - Math.min(...rgb) >= 90, `Dull Reborn ${surface}/${key}`);
    }
    const configure = m.outputs.find(out => out.theme === 'reborn' && out.surface === surface && out.stem === 'configure');
    assert.ok(configure.layers.some(layer => layer.weight === 'fill' && layer.icon === 'gear'));
    const svg = readFileSync(join(root, configure.path), 'utf8');
    assert.ok(svg.includes(m.themes.reborn.palettes[surface].primary));
  }
});

test('Prism dark duotone foregrounds remain visible on the native dark surface', () => {
  const luminance = hex => {
    const channels = hex.match(/[\da-f]{2}/gi).map(value => parseInt(value, 16) / 255)
      .map(value => value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4);
    return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
  };
  const background = luminance('#252a30');
  for (const stem of ['log_file', 'settings-main', 'favusers', 'favserver', 'settings-shortcuts']) {
    const out = manifest().outputs.find(entry => entry.theme === 'prism' && entry.surface === 'dark' && entry.stem === stem);
    const svg = readFileSync(join(root, out.path), 'utf8');
    out.layers.forEach((layer, index) => {
      if (layer.weight !== 'duotone') return;
      assert.ok(svg.includes(`fill="url(#p${index}-ink)"`), `Unreadable foreground: ${out.path}/${layer.icon}`);
      const gradient = svg.match(new RegExp(`<linearGradient id="p${index}-ink"[^>]*>(.*?)</linearGradient>`))?.[1];
      assert.ok(gradient, `Missing dark ink: ${out.path}`);
      for (const [, color] of gradient.matchAll(/stop-color="(#[\da-f]{6})"/g)) {
        assert.ok((luminance(color) + 0.05) / (background + 0.05) >= 3, `Low-contrast dark ink: ${out.path} ${color}`);
      }
    });
  }
});

test('offline regeneration is byte-identical and check mode rejects a tampered asset', () => {
  manifest();
  const checked = run('--check');
  assert.equal(checked.status, 0, checked.stderr || checked.stdout);
  const scratch = mkdtempSync(join(tmpdir(), 'icon-themes-test-'));
  try {
    const generated = run('--output-root', scratch);
    assert.equal(generated.status, 0, generated.stderr || generated.stdout);
    for (const out of manifest().outputs) {
      const relative = out.path.replace('eiskaltdcpp-qt/icons/appl/', '');
      assert.equal(sha256(readFileSync(join(scratch, relative))), out.sha256, out.path);
    }
    for (const file of manifest().supportingFiles) {
      const relative = file.path.replace('eiskaltdcpp-qt/icons/appl/', '');
      assert.equal(sha256(readFileSync(join(scratch, relative))), file.sha256, file.path);
    }
    const target = join(scratch, 'reborn/media-pause.svg');
    writeFileSync(target, readFileSync(target, 'utf8').replace(/<path\b[^>]*\/>/g, ''));
    const tampered = run('--check', '--output-root', scratch);
    assert.notEqual(tampered.status, 0);
    assert.match(tampered.stderr, /media-pause\.svg/);
    writeFileSync(join(scratch, 'reborn/unowned.svg'), '<svg/>');
    const unowned = run('--output-root', scratch);
    assert.notEqual(unowned.status, 0);
    assert.match(unowned.stderr, /Unexpected|unowned/);
    assert.equal(readFileSync(join(scratch, 'reborn/unowned.svg'), 'utf8'), '<svg/>');
  } finally {
    rmSync(scratch, { recursive: true, force: true });
  }
});
