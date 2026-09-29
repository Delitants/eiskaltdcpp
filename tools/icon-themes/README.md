# Application Icon Families

Asset-only delivery for Prism, Contour, Slate, and Reborn. Reborn is the approved
default design; setting the application default is intentionally left to runtime
integration.

## Deliverables

- 86 stems per surface, 172 individual SVGs per theme, 688 SVGs total.
- Light-surface files: `eiskaltdcpp-qt/icons/appl/{theme}/{stem}.svg`.
- Dark-surface files: `eiskaltdcpp-qt/icons/appl/{theme}/dark/{stem}.svg`.
- All eligible Apex QRC and `WulforUtil::loadIcons` stems, including the
  loader-only `settings-shortcuts` and both active highlight variants.
- Additional stems: `document-new`, `media-play`, `media-pause`, `sliders`,
  `settings-sharing`, `settings-notifications`, `settings-user-commands`,
  `settings-advanced`, `torrent`.
- `manifest.json` lists every semantic mapping, theme palette, component,
  source weight/path, transform, output path, and output SHA-256.

The theme roots contain SVGs, their `dark/` directories, the upstream MIT
`LICENSE`, and a short attribution `README.md`. They contain no PNG files,
resource manifests, or runtime code.

## Regenerate And Test

From the repository root, using Node.js 22 or newer:

```sh
node tools/icon-themes/generate.mjs
node tools/icon-themes/generate.mjs --check
node --test tools/icon-themes/test.mjs
```

Generation is deterministic, offline, and dependency-free. The tests additionally
use `xmllint`, a C++17 compiler, `pkg-config`, and Qt6 Core/Gui/Svg development
libraries. They compile only the standalone image-audit helper into a temporary
directory, never the shared application build. They check complete coverage, all 688 XML
documents, resolved local paints, exact licensed path geometry, checksums, the
minimal vendor bank, semantic distinctions, theme weights/palettes, four-key
Shortcuts layout, dark Prism ink contrast, distributed license copies,
clean-room regeneration, and tamper/unowned-file rejection. Loader coverage
supports both `FROMTHEME[_SIDE]("stem", ...)` and
`cacheIcon(eiENUM, "stem", resourceFound [, side])`, with cache-only fixtures
so the Apex QRC cannot hide a regression during migration.

The native Qt image test compares Queue and Finished Downloads at 22 and 28
logical pixels, DPR 1 and 2, for all four themes and both surfaces. At least 5%
of rendered pixels must have materially different alpha coverage, so changing
titles, hashes, or colors alone cannot satisfy the test. It also compares decoded
legacy PNG pixels without modifying them. To refresh the complete hash/pixel audit:

```sh
node tools/icon-themes/image-audit.mjs --report tools/icon-themes/download-image-audit.json
```

For isolated regeneration without touching the delivered assets or manifest:

```sh
node tools/icon-themes/generate.mjs --output-root /tmp/eiskalt-icon-check
node tools/icon-themes/generate.mjs --check --output-root /tmp/eiskalt-icon-check
```

Only the four theme subdirectories and a scratch `manifest.json` are created in
the alternate root. Existing unexpected files or symlinks inside a theme cause a
refusal, not deletion. In-place regeneration also refuses to overwrite SVGs whose
contents no longer match the prior generated manifest.

## Source And License

Geometry is from the official [Phosphor core repository](https://github.com/phosphor-icons/core),
pinned to the published npm package `@phosphor-icons/core@2.1.1`. The package's
release metadata uses its former repository name, `phosphor-icons/phosphor-core`;
both URLs are recorded in `source-lock.json`.

- `source-lock.json` pins the archive URL, npm SHA-512 integrity, and SHA-256.
- `vendor/provenance.json` records the same pin plus each retained source hash.
- `vendor/LICENSE` is the unmodified upstream MIT license, including attribution.
- `vendor/assets/` contains only source SVGs actually used by these families.
- No package installation, package scripts, network activity, or font dependency
  is needed for generation.

To reproduce the vendor bank from the pinned package, rather than trusting a
moving branch:

```sh
node tools/icon-themes/vendor.mjs --fetch
# Or use an already downloaded archive:
node tools/icon-themes/vendor.mjs --archive /path/to/core-2.1.1.tgz
```

Network-restricted environments require approval for the fetch, or an offline
archive. Both import paths verify the same pinned SHA-512 and SHA-256 before
extracting only the required SVGs and license. Modified source files and unknown
vendor files are never silently overwritten or deleted. If editing the catalog
removes sources, inspect and remove those now-unused vendor files explicitly
before reimporting; the importer reports them.

Distributors must retain the MIT copyright and permission notice when packaging
these derived icons. Each theme directory includes an exact copy of the upstream
license plus attribution, so the notice accompanies the shipped icon folders.
The parent integration owns embedding assets and final package verification.

## Rendering Contract

Each SVG has `viewBox="0 0 256 256"`, transparent surroundings, explicit colors,
and native Phosphor coordinates. No global scale or fractional optical translation
is applied. Only composed components retain local positioning/scaling. All
visible paths are verbatim upstream geometry. Favorites, attention markers,
Shortcuts, and Torrent use additional official Phosphor components, not invented
silhouettes. Curves retain normal antialiasing; no `crispEdges` hint is used.
The parent renders SVG QIcons directly at the requested size, rather than scaling
a previously rasterized toolbar image.

No external references, `currentColor`, CSS, text glyphs, fonts, images, masks,
filters, clipping paths, scripts, or `<use>` instances are used. Internal
`linearGradient` paint references are fully defined in the same SVG. Geometry
and paints use the conservative path/group/gradient subset for QtSvg rendering.

- Prism uses duotone bodies made opaque, colored material gradients, dark rims,
  and same-geometry specular highlights. Dedicated filled/bold sources avoid the
  unwanted rectangular duotone backgrounds in arrow/transfer glyphs. Dark-surface
  duotone foreground ink uses luminous blue-gray stops, rather than near-black,
  so document details, shoulders, and badges remain visible against `#252a30`.
- Contour uses light outline silhouettes. Small badges and the four Shortcuts
  keycaps use regular outlines for optical compensation after scaling. The
  upstream library represents its strokes as filled path outlines.
- Slate uses fill and bold sources in navy or light gray, with restrained teal,
  green/blue transfer direction accents, gold favorites/bells, and red destruction.
- Reborn uses the same substantial silhouettes in bright blue, teal, green, and
  amber on both surfaces. It is neither a navy theme nor a thin-outline theme.
- Shortcuts is four `arrow-square-*` keys, with Up above Left / Down / Right.
- Pause, Play, Close, and Delete use distinct pause-bars, triangle, X, and trash
  geometry. Downloads point down in green; uploads point up in blue.
- Download Queue is a queued list plus a down arrow. Finished Downloads is an
  inbox tray plus a completion check. Their silhouettes differ on both surfaces,
  not just their colors. Download settings and Finished Uploads are unchanged.
- The active `transfer-highlight` and `queued-users-highlight` variants carry
  explicit gold attention badges, not only a subtle color change.
- Torrent combines six open `circle-dashed` segments with one centered down
  arrow. Contour uses light ring/arrow paths; the other families use bold ring
  segments and a filled or shaded `arrow-fat-down`. The center remains open,
  without globe lines or refresh arrowheads. Prism/Reborn use green and blue
  segments; Contour/Slate retain their neutral ring with a restrained teal accent.

## Scope And Limitations

These are scalable library-derived interpretations of the approved reference
looks, not raster tracing or cropped preview sheets. The manifests record the
four approved reference filenames without copying their images into the repo.

`toolbar20` and `toolbar20-highlight` are excluded legacy 550x22 atlas strips;
no square replacement or new atlas is emitted. `icon_appl*`, `icon_msg*`, and
`qt-logo` remain Apex fallbacks. The original torrent raster
(`eiskaltdcpp-qt/icons/torrent/torrent.png`) stays byte-identical as the fallback
for legacy themes; the new families supply their own `torrent.svg` variants.
Its SHA-256 is pinned in the manifest and checked before generation and in the
tests. User icon sets, country flags, and Dock application icons are not changed.
`users`, `favusers`, and `im-user-away` here are application action glyphs, not
the separate user-icon/avatar set.

No loader, application code, CMake, QRC, shared build, publication, installation,
or Git operations are part of this delivery. Native Qt rendering/contact-sheet
QA, settings wiring, embedded resources, and runtime tests belong to the parent
integration. Asset checks do not claim that runtime QA has been completed.
