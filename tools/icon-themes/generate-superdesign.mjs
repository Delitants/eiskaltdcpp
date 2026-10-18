import { createHash } from 'node:crypto';
import { existsSync, lstatSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { icons, palette } from './superdesign-catalog.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const args = process.argv.slice(2);
let check = false;
let output = join(root, 'eiskaltdcpp-qt/icons/appl/superdesign');
for (let i = 0; i < args.length; ++i) {
  if (args[i] === '--check') check = true;
  else if (args[i] === '--output-root' && args[i + 1]) output = resolve(args[++i]);
  else throw Error('Usage: generate-superdesign.mjs [--check] [--output-root DIRECTORY]');
}
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const vendor = join(here, 'vendor-lucide');
const provenance = JSON.parse(readFileSync(join(vendor, 'provenance.json')));
const bankBytes = readFileSync(join(vendor, 'nodes.json'));
const license = readFileSync(join(vendor, 'LICENSE'));
if (sha(bankBytes) !== provenance.nodesSha256 || sha(license) !== provenance.licenseSha256)
  throw Error('Modified Lucide geometry or license');
for (const source of provenance.sources) {
  if (sha(readFileSync(join(vendor, source.local))) !== source.sha256)
    throw Error(`Modified Lucide source: ${source.local}`);
}
const bank = JSON.parse(bankBytes);
const escape = value => String(value).replaceAll('&', '&amp;').replaceAll('"', '&quot;').replaceAll('<', '&lt;');
function geometry(name) {
  if (!bank[name]) throw Error(`Missing offline source: ${name}`);
  return bank[name].map(([tag, attributes]) => {
    if (!['path', 'circle', 'ellipse', 'rect', 'line', 'polyline', 'polygon'].includes(tag))
      throw Error(`Unsupported SVG tag ${tag}`);
    return `<${tag} ${Object.entries(attributes).map(([key, value]) => `${key}="${escape(value)}"`).join(' ')}/>`;
  }).join('\n    ');
}
const files = new Map();
for (const surface of ['light', 'dark']) {
  for (const icon of icons) {
    const color = palette[icon.color][surface === 'dark' ? 1 : 0];
    const body = `<g${icon.badge ? ' transform="translate(0 0) scale(0.78)"' : ''}>\n    ${geometry(icon.source)}\n  </g>`;
    const badge = icon.badge ? `\n  <g transform="translate(13 13) scale(0.43)" stroke="${palette[icon.badge === 'circle-minus' ? 'red' : 'gold'][surface === 'dark' ? 1 : 0]}">\n    ${geometry(icon.badge)}\n  </g>` : '';
    files.set(`${surface === 'dark' ? 'dark/' : ''}${icon.stem}.svg`,
      `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">\n  <title>${escape(icon.stem)}</title>\n  ${body}${badge}\n</svg>\n`);
  }
}
files.set('LICENSE', license);
files.set('README.md', '# Superdesign Icon Theme\n\nOutlined Lucide 1.8.0 geometry, ISC license with included upstream notices.\nDerived from the selected compact-native Superdesign draft; explicit light/dark\naccent palettes and semantic compositions. Branding remains the original Apex\nfallback. Regenerate offline with node tools/icon-themes/generate-superdesign.mjs.\nSource modules, geometry, provenance and hashes: tools/icon-themes/vendor-lucide/.\n');
files.set('manifest.json', JSON.stringify({theme: 'superdesign', source: 'lucide@1.8.0',
  reference: '.superdesign/tmp/compact-native.html', palette, icons,
  files: [...files].map(([path, bytes]) => ({path, sha256: sha(bytes)}))}, null, 2) + '\n');
const previous = existsSync(join(output, 'manifest.json')) ? JSON.parse(readFileSync(join(output, 'manifest.json'))) : null;
function inspect(directory, prefix = '') {
  if (!existsSync(directory)) return;
  if (lstatSync(directory).isSymbolicLink()) throw Error(`Refusing output symlink ${directory}`);
  for (const entry of readdirSync(directory, {withFileTypes: true})) {
    const name = prefix + entry.name;
    if (entry.isSymbolicLink()) throw Error(`Refusing output symlink ${name}`);
    if (entry.isDirectory() && name === 'dark') inspect(join(directory, entry.name), 'dark/');
    else if (!entry.isFile() || !files.has(name)) throw Error(`Unexpected file retained: ${name}`);
    else {
      const bytes = readFileSync(join(directory, entry.name));
      const expected = previous?.files.find(file => file.path === name)?.sha256;
      if (name !== 'manifest.json' && sha(bytes) !== sha(files.get(name)) && (!expected || sha(bytes) !== expected))
        throw Error(`Refusing to overwrite modified output: ${name}`);
    }
  }
}
inspect(output);
for (const [name, bytes] of files) {
  const path = join(output, name);
  if (check) {
    if (!existsSync(path) || !readFileSync(path).equals(Buffer.from(bytes))) throw Error(`Stale or missing ${path}`);
  } else {
    mkdirSync(dirname(path), {recursive: true});
    writeFileSync(path, bytes);
  }
}
console.log(`${check ? 'Verified' : 'Generated'} ${icons.length * 2} Superdesign SVGs offline`);
