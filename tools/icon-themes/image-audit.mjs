import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const args = process.argv.slice(2);
if (args.length && !(args.length === 2 && args[0] === '--report')) throw new Error('Usage: node tools/icon-themes/image-audit.mjs [--report FILE]');
const sha = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const checked = (command, args, options = {}) => {
  const result = spawnSync(command, args, { encoding: 'utf8', ...options });
  if (result.status !== 0) throw new Error(result.error?.message || result.stderr || `${command} failed`);
  return result.stdout;
};
const modern = [];
for (const theme of ['prism', 'contour', 'slate', 'reborn']) {
  for (const surface of ['light', 'dark']) {
    for (const size of [22, 28]) {
      for (const dpr of [1, 2]) {
        const dir = join(root, 'eiskaltdcpp-qt/icons/appl', theme, surface === 'dark' ? 'dark' : '');
        modern.push({ id: `${theme}/${surface}/${size}px@${dpr}`, theme, surface, size, dpr,
          first: join(dir, 'download.svg'), second: join(dir, 'go-down-search.svg') });
      }
    }
  }
}
const legacy = ['apex', 'default', 'faenza', 'haiku', 'monochrome'].map(theme => ({
  id: theme, size: 0, dpr: 1,
  first: join(root, `eiskaltdcpp-qt/icons/appl/${theme}/download.png`),
  second: join(root, `eiskaltdcpp-qt/icons/appl/${theme}/go-down-search.png`),
}));
const scratch = mkdtempSync(join(tmpdir(), 'icon-image-audit-'));
try {
  const binary = join(scratch, 'image-audit');
  const flags = checked('pkg-config', ['--cflags', '--libs', 'Qt6Core', 'Qt6Gui', 'Qt6Svg']).trim().split(/\s+/);
  checked(process.env.CXX || 'c++', ['-std=c++17', join(here, 'image-audit.cpp'), '-o', binary, ...flags]);
  const results = JSON.parse(checked(binary, [], {
    input: JSON.stringify([...modern, ...legacy]), env: { ...process.env, QT_QPA_PLATFORM: 'offscreen' },
  }));
  const report = { renderer: `QtSvg ${checked('pkg-config', ['--modversion', 'Qt6Svg']).trim()}`,
    pixelHashFormat: 'row-major premultiplied RGBA8; hidden fully transparent RGB normalized by Qt',
    modern: results.slice(0, modern.length),
    legacy: results.slice(modern.length).map((result, index) => ({ ...result,
      download: { path: legacy[index].first.slice(root.length + 1), sha256: sha(legacy[index].first) },
      finished: { path: legacy[index].second.slice(root.length + 1), sha256: sha(legacy[index].second) },
    })),
  };
  const output = `${JSON.stringify(report, null, 2)}\n`;
  if (args.length) writeFileSync(resolve(args[1]), output);
  process.stdout.write(output);
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
