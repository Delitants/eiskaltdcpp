import { createHash } from 'node:crypto';
import { existsSync, lstatSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { additionalStems, excludedStems, icons, neededSources, resolveComponent, themeNames } from './catalog.mjs';
import { material, themes } from './paint.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const defaultRoot = join(root, 'eiskaltdcpp-qt/icons/appl');
const legacyTorrent = { path: 'eiskaltdcpp-qt/icons/torrent/torrent.png',
  sha256: 'e83ed3f29d332ef6e9fe71b114b8b85325cc695cae3a9080271a4bd7022c1eee',
  purpose: 'Unmodified raster fallback for legacy themes; not generated' };
const hash = value => createHash('sha256').update(value).digest('hex');
const encode = value => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
const json = value => `${JSON.stringify(value, null, 2)}\n`;
let outputRoot = defaultRoot;
let check = false;
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--check') check = true;
  else if (args[i] === '--output-root' && args[i + 1]) outputRoot = resolve(args[++i]);
  else throw new Error('Usage: node tools/icon-themes/generate.mjs [--check] [--output-root DIRECTORY]');
}

function sourceBank() {
  const lock = JSON.parse(readFileSync(join(here, 'source-lock.json'), 'utf8'));
  const provenance = JSON.parse(readFileSync(join(here, 'vendor/provenance.json'), 'utf8'));
  for (const [key, value] of Object.entries(lock)) {
    if (provenance[key] !== value) throw new Error(`Vendor provenance mismatch: ${key}`);
  }
  if (hash(readFileSync(join(here, 'vendor/LICENSE'))) !== provenance.licenseSha256) throw new Error('Modified vendor license');
  const needed = neededSources();
  if (JSON.stringify(needed) !== JSON.stringify(provenance.files.map(file => file.path))) throw new Error('Source bank does not match catalog; run vendor.mjs.');
  const bank = new Map();
  for (const file of provenance.files) {
    const svg = readFileSync(join(here, 'vendor', file.path), 'utf8');
    if (hash(svg) !== file.sha256) throw new Error(`Modified source: ${file.path}`);
    if (!svg.includes('viewBox="0 0 256 256"')) throw new Error(`Unexpected source coordinates: ${file.path}`);
    const paths = [...svg.matchAll(/<path\b([^>]*)\/>/g)].map(([, attrs]) => {
      const d = attrs.match(/\bd="([^\"]+)"/)?.[1];
      const opacity = attrs.match(/\bopacity="([^\"]+)"/)?.[1];
      if (!d || (opacity && opacity !== '0.2')) throw new Error(`Unexpected source path: ${file.path}`);
      return { d, background: opacity === '0.2' };
    });
    const remainder = svg.replace(/<svg\b[^>]*>|<\/svg>|<path\b[^>]*\/>/g, '').trim();
    if (!paths.length || remainder) throw new Error(`Unsupported source geometry: ${file.path}`);
    bank.set(file.path, paths);
  }
  return { bank, provenance };
}

function render(icon, theme, surface, bank) {
  const layers = icon.components.map(part => resolveComponent(part, theme)).filter(Boolean);
  const defs = [];
  const groups = [];
  layers.forEach((layer, index) => {
    const paint = material(layer.paint, theme, surface, `p${index}`);
    defs.push(...paint.defs);
    const paths = bank.get(layer.source);
    const hasBody = paths.some(path => path.background);
    const geometry = [];
    for (const path of paths) {
      const rim = theme === 'prism' && hasBody && !path.background;
      const fill = rim ? paint.ink : paint.fill;
      const edge = theme === 'prism' && !hasBody ? ` stroke="${paint.rim}" stroke-width="3" stroke-linejoin="round"` : '';
      geometry.push(`<path d="${path.d}" fill="${fill}"${edge}/>`);
      if (theme === 'prism' && (path.background || !hasBody)) {
        geometry.push(`<path d="${path.d}" fill="${paint.shine}"/>`);
      }
    }
    groups.push(`<g transform="matrix(${layer.transform.join(' ')})">\n${geometry.map(path => `    ${path}`).join('\n')}\n  </g>`);
  });
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">\n`
    + `  <title>${encode(icon.title)}</title>\n`
    + `  <desc>${themes[theme].label} ${surface} surface. Phosphor Icons 2.1.1, copyright 2023 Phosphor Icons, MIT. Generated; see tools/icon-themes.</desc>\n`
    + (defs.length ? `  <defs>\n${defs.map(def => `    ${def}`).join('\n')}\n  </defs>\n` : '')
    + `  ${groups.join('\n  ')}\n</svg>\n`;
  return { svg, layers };
}

function assertSafeDirectory(dir, expected, allowDark) {
  if (!existsSync(dir)) return;
  if (lstatSync(dir).isSymbolicLink() || !lstatSync(dir).isDirectory()) throw new Error(`Refusing unsafe output directory: ${dir}`);
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    if (entry.isSymbolicLink()) throw new Error(`Refusing output symlink: ${join(dir, entry.name)}`);
    if (entry.name === 'dark' && entry.isDirectory() && allowDark) continue;
    if (!entry.isFile() || !expected.has(entry.name)) throw new Error(`Unexpected unowned output retained: ${join(dir, entry.name)}`);
  }
}

function attribution(theme) {
  return `# ${themes[theme].label} Icon Theme\n\n`
    + `Scalable icons derived from Phosphor Icons, @phosphor-icons/core 2.1.1.\n`
    + `Copyright (c) 2023 Phosphor Icons. Licensed under the MIT License; the\n`
    + `complete upstream notice is included in LICENSE beside this file.\n\n`
    + `Upstream: https://github.com/phosphor-icons/core\n\n`
    + `Theme adaptations: colors, material shading, component positioning, and\n`
    + `compositions of official Phosphor paths. ${themes[theme].style}.\n\n`
    + `Root SVGs target light surfaces; dark/ contains the same filenames for\n`
    + `dark surfaces. The upstream license applies to both sets. Retain LICENSE\n`
    + `when redistributing this theme, including in binary packages.\n\n`
    + `Reproducible source: tools/icon-themes/ in the EiskaltDC++ source tree.\n`;
}

function main() {
  if (hash(readFileSync(join(root, legacyTorrent.path))) !== legacyTorrent.sha256) throw new Error('Legacy Torrent PNG changed; refusing generation.');
  if (icons.length !== new Set(icons.map(icon => icon.stem)).size) throw new Error('Duplicate icon stem');
  for (const icon of icons) {
    if (!/^[a-z0-9_-]+$/.test(icon.stem) || excludedStems.includes(icon.stem)) throw new Error(`Invalid or excluded stem: ${icon.stem}`);
  }
  const expected = new Set(icons.map(icon => `${icon.stem}.svg`));
  const expectedRoot = new Set([...expected, 'LICENSE', 'README.md']);
  for (const theme of themeNames) {
    assertSafeDirectory(join(outputRoot, theme), expectedRoot, true);
    assertSafeDirectory(join(outputRoot, theme, 'dark'), expected, false);
  }
  const { bank, provenance } = sourceBank();
  const outputs = [];
  const supportingFiles = [];
  const pending = [];
  for (const theme of themeNames) {
    for (const [name, body] of [['LICENSE', readFileSync(join(here, 'vendor/LICENSE'), 'utf8')], ['README.md', attribution(theme)]]) {
      const relative = `${theme}/${name}`;
      supportingFiles.push({ path: `eiskaltdcpp-qt/icons/appl/${relative}`, sha256: hash(body) });
      pending.push({ path: join(outputRoot, relative), body });
    }
    for (const surface of ['light', 'dark']) {
      for (const icon of icons) {
        const { svg, layers } = render(icon, theme, surface, bank);
        const relative = `${theme}/${surface === 'dark' ? 'dark/' : ''}${icon.stem}.svg`;
        outputs.push({ stem: icon.stem, theme, surface, path: `eiskaltdcpp-qt/icons/appl/${relative}`, sha256: hash(svg), layers });
        pending.push({ path: join(outputRoot, relative), body: svg });
      }
    }
  }
  const manifest = {
    schemaVersion: 1, defaultTheme: 'reborn', generator: 'node tools/icon-themes/generate.mjs',
    coordinateSystem: { viewBox: [0, 0, 256, 256], opticalTransform: null,
      note: 'Native Phosphor coordinates; local transforms only for composed components' },
    layout: { light: '{theme}/{stem}.svg', dark: '{theme}/dark/{stem}.svg' },
    license: { id: 'MIT', file: 'tools/icon-themes/vendor/LICENSE', source: provenance.repository, package: provenance.package, version: provenance.version },
    counts: { stems: icons.length, surfacesPerTheme: 2, themes: themeNames.length, generatedSVGs: outputs.length, vendorSVGs: provenance.files.length, supportingFiles: supportingFiles.length },
    excluded: { brandFallbacks: excludedStems.filter(stem => /^(icon_|qt-logo)/.test(stem)),
      legacyAtlases: ['toolbar20', 'toolbar20-highlight'], other: ['user icon sets', 'country flags', 'Dock application icons'] },
    preservedLegacyAssets: [legacyTorrent],
    additionalStems, themes, icons, outputs, supportingFiles,
  };
  const manifestPath = outputRoot === defaultRoot ? join(here, 'manifest.json') : join(outputRoot, 'manifest.json');
  const previous = existsSync(manifestPath) ? JSON.parse(readFileSync(manifestPath, 'utf8')) : null;
  const previousHashes = new Map([...(previous?.outputs ?? []), ...(previous?.supportingFiles ?? [])]
    .map(out => [out.path.replace('eiskaltdcpp-qt/icons/appl/', ''), out.sha256]));
  const stale = pending.filter(file => !existsSync(file.path) || readFileSync(file.path, 'utf8') !== file.body);
  if (check) {
    if (!existsSync(manifestPath) || readFileSync(manifestPath, 'utf8') !== json(manifest)) stale.push({ path: manifestPath });
    if (stale.length) throw new Error(`Generated assets differ (${stale.length}):\n${stale.map(file => file.path).join('\n')}`);
    console.log(`Verified ${outputs.length} SVGs, ${supportingFiles.length} attribution/license files, and manifest; offline regeneration is byte-identical.`);
    return;
  }
  for (const file of stale) {
    if (existsSync(file.path)) {
      const relative = file.path.slice(outputRoot.length + 1);
      if (hash(readFileSync(file.path)) !== previousHashes.get(relative)) throw new Error(`Refusing to overwrite modified/unowned asset: ${file.path}`);
    }
  }
  for (const file of stale) {
    mkdirSync(dirname(file.path), { recursive: true });
    writeFileSync(file.path, file.body);
  }
  mkdirSync(dirname(manifestPath), { recursive: true });
  writeFileSync(manifestPath, json(manifest));
  console.log(`Generated ${outputs.length} SVGs (${icons.length} stems x 4 themes x 2 surfaces) plus ${supportingFiles.length} attribution/license files; ${stale.length} files written.`);
}

try {
  main();
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
