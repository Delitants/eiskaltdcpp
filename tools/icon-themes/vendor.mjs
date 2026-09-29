import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { neededSources } from './catalog.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const lock = JSON.parse(readFileSync(join(here, 'source-lock.json'), 'utf8'));
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const args = process.argv.slice(2);
if (!(args.length === 1 && args[0] === '--fetch') && !(args.length === 2 && args[0] === '--archive')) {
  throw new Error('Usage: node tools/icon-themes/vendor.mjs --fetch | --archive /path/core-2.1.1.tgz');
}
const temp = mkdtempSync(join(tmpdir(), 'phosphor-vendor-'));
try {
  let archive;
  if (args[0] === '--fetch') {
    archive = join(temp, 'core-2.1.1.tgz');
    const result = spawnSync('curl', ['--fail', '--location', '--silent', '--show-error', '--max-time', '120', lock.archive, '-o', archive], { stdio: 'inherit' });
    if (result.status !== 0) throw new Error('Pinned download failed; request network approval or supply --archive.');
  } else archive = resolve(args[1]);
  const bytes = readFileSync(archive);
  const integrity = `sha512-${createHash('sha512').update(bytes).digest('base64')}`;
  if (integrity !== lock.integrity || hash(bytes) !== lock.archiveSha256) throw new Error('Archive integrity mismatch; nothing imported.');
  const sources = neededSources();
  const extracted = spawnSync('tar', ['-xzf', archive, '-C', temp, 'package/LICENSE', ...sources.map(path => `package/${path}`)], { encoding: 'utf8' });
  if (extracted.status !== 0) throw new Error(`Source extraction failed: ${extracted.stderr}`);
  const license = readFileSync(join(temp, 'package/LICENSE'));
  if (!license.toString().includes('MIT License')) throw new Error('Unexpected license');
  const files = sources.map(path => ({ path, sha256: hash(readFileSync(join(temp, 'package', path))) }));
  const destination = join(here, 'vendor');
  const allowed = new Set(['LICENSE', 'provenance.json', ...sources]);
  const walk = (dir, prefix = '') => !existsSync(dir) ? [] : readdirSync(dir, { withFileTypes: true }).flatMap(entry => {
    const path = prefix + entry.name;
    if (entry.isSymbolicLink()) throw new Error(`Refusing vendor symlink: ${path}`);
    return entry.isDirectory() ? walk(join(dir, entry.name), `${path}/`) : [path];
  });
  for (const path of walk(destination)) {
    if (!allowed.has(path)) throw new Error(`Unexpected vendor file retained for manual review: ${path}`);
  }
  for (const path of ['LICENSE', ...sources]) {
    const data = readFileSync(join(temp, 'package', path));
    const target = join(destination, path);
    if (existsSync(target) && !readFileSync(target).equals(data)) throw new Error(`Refusing to overwrite modified vendor source: ${path}`);
  }
  for (const path of ['LICENSE', ...sources]) {
    const target = join(destination, path);
    mkdirSync(dirname(target), { recursive: true });
    if (!existsSync(target)) writeFileSync(target, readFileSync(join(temp, 'package', path)), { flag: 'wx' });
  }
  writeFileSync(join(destination, 'provenance.json'), `${JSON.stringify({ ...lock, licenseSha256: hash(license), files }, null, 2)}\n`);
  console.log(`Verified ${lock.package}@${lock.version}; retained ${files.length} source SVGs and MIT license.`);
} finally {
  rmSync(temp, { recursive: true, force: true });
}
