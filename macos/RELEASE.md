# macOS Release Flavors

Offline authenticated update-feed preparation is documented in
[updater/README.md](updater/README.md). That tooling is not publication or
installer acceptance. Release builds and package audits require a separately
allocated numeric build number, shared by both CPU flavors and the signed feed.
The same guide documents offline, pinned, CPU-specific Sparkle framework/helper
staging with notices and strict signatures. That dependency stage is not yet
integrated into production bundles and does not enable update installation.

The v3 prerelease uses two separate bundles, not a universal binary:

| Profile | Architecture | Minimum macOS | ZIP filename |
| --- | --- | --- | --- |
| `intel` | Intel `x86_64` only | 14 Sonoma | `EiskaltDCpp-3.0.0-pre.1-macOS14-plus-Intel-x86_64.zip` |
| `apple-silicon` | Apple Silicon `arm64` only | 26 Tahoe | `EiskaltDCpp-3.0.0-pre.1-macOS26-plus-AppleSilicon-arm64.zip` |

These are release targets, not claims of completed runtime testing. Intel Macs
running macOS 13 or earlier and Apple Silicon Macs running macOS 25 or earlier
are outside this release's support targets. Native execution on the advertised
minimum OS remains a separate acceptance check from a successful build/audit.

## Build Separately

Use CMake 3.25+ (preset schema 6), Python 3.9+ and Xcode command-line tools.
Prepare a separate dependency prefix for each architecture. Qt 6.11 supports
x86_64 and arm64 on macOS 13+, but that does not establish the minimum OS of
any particular package-manager build: see [Qt's platform requirements](https://doc.qt.io/qt-6/macos.html).
Every bundled dependency must satisfy the chosen floor. Changing Info.plist,
rewriting a load command, or setting only the app's deployment target cannot
make a newer dependency compatible with an older OS.

Both flavors retain Torrent/DC DHT support and the same application features.
Build all required dependencies, including libtorrent 2.1, OpenSSL, Qt and the
optional enabled libraries, for the target architecture and minimum OS. Retain
the exact recipes, patches and source archives, including static libraries.
Do not silently disable features to get the Intel build through configuration.

Both release presets require the checksum-pinned, patched libtorrent described in
[dependencies/libtorrent/README.md](../dependencies/libtorrent/README.md).
Build it into a private prefix for the same architecture and OS floor, using
matching Boost headers and OpenSSL. For cross-builds, the dependency build script
accepts CMake's `CMAKE_TOOLCHAIN_FILE` environment variable in a fresh build tree.
Keep the toolchain and dependency recipes with the source inventory. Ordinary
unpatched development builds may keep selected rechecking unavailable; release
configuration must fail rather than silently omit it.

Build a **matching-architecture Qt 6.11.1 Cocoa plugin** using the instructions in
[qt-cocoa-backport/README.md](qt-cocoa-backport/README.md), with both patches and
explicit `CMAKE_OSX_ARCHITECTURES` and `CMAKE_OSX_DEPLOYMENT_TARGET`. Do not copy
the ARM64 plugin into the Intel app, or replace it with stock Qt during packaging.

Set these environment variables to the prepared, absolute local paths:
`QT_PREFIX` must expose the complete Qt installation, including Svg, Multimedia
and LinguistTools, not only a split `qtbase` package. For an existing modular
Homebrew installation this is usually the common prefix, not `opt/qtbase`.
LinguistTools also needs its own runtime dependencies, including QtQml, even
when the application does not enable QML.

```sh
export DEPS_PREFIX=/path/to/target-dependencies
export QT_PREFIX=/path/to/matching-qt
export COCOA_PLUGIN=/path/to/matching-patched/libqcocoa.dylib
export COCOA_PLUGIN_SHA256="$(shasum -a 256 "$COCOA_PLUGIN" | cut -d ' ' -f1)"
export RECHECK_PREFIX=/path/to/matching-private-libtorrent
export EISKALT_MACOS_BUILD_NUMBER=30000001 # Example only; use the allocated release build.

# Choose one; the presets have separate build directories and two build workers.
cmake --preset macos-intel \
  -DLibtorrentRasterbar_DIR="$RECHECK_PREFIX/lib/cmake/LibtorrentRasterbar"
cmake --build --preset build-macos-intel
ctest --test-dir out/build/macos-intel --output-on-failure -j1

# Or, with the Apple Silicon dependency paths exported instead:
cmake --preset macos-apple-silicon \
  -DLibtorrentRasterbar_DIR="$RECHECK_PREFIX/lib/cmake/LibtorrentRasterbar"
cmake --build --preset build-macos-apple-silicon
ctest --test-dir out/build/macos-apple-silicon --output-on-failure -j1
```

The presets are unavailable until all five required environment variables are
set (dependency prefix, Qt prefix, Cocoa plugin/path digest and numeric build).
Their outputs are `out/build/<preset>/eiskaltdcpp-qt/EiskaltDC++.app`.
Never reuse the other architecture's CMake cache. On an ARM host, running Intel
tests also requires a working x86_64 execution environment; Rosetta testing is
not native Intel/macOS 14 acceptance. Do not install Rosetta implicitly.

The legacy `build-using-homebrew.sh` and general CI DMGs are not this release
path. In particular, do not run `fix-bundle-self-containment.sh` on these bundles:
it can replace the patched Cocoa plugin. Normalize dependency IDs with
`normalize-bundle-dylib-ids.sh` only when needed, then sign and audit the result.
When using universal Qt dependencies, thin every copied Mach-O in the staged
Intel bundle to `x86_64` before re-signing. Do not thin the shared Qt installation
or rewrite OS deployment metadata. Make the matching Qt plugin root available
under the dependency prefix so the deploy step cannot pick up host-architecture
style or SQLite plugins.

Deployment stages the exact shared libtorrent selected by its imported CMake
target under that library's SONAME before running macdeployqt. A same-named
system library must not replace the maintained extension.
The staged file must be regular, not an existing symlink or directory. Only that
copy is made owner-writable for Qt's strip/install-name edits; the shared
dependency prefix is not chmodded or rewritten.
The normalizer removes
build-machine and escaping search paths while retaining resolvable paths inside
the bundle/system roots; it does not rewrite dependency selection or OS floors.
Always rerun the closure/extension audit and re-sign after normalization.
The application uses the SQLite SQL driver only. If a full Qt SDK ships database
plugins requiring absent Mimer/ODBC/PostgreSQL clients, use a separate deployment
tool/plugin view containing SQLite and the other required plugin categories;
do not alter the shared SDK or disable application SQLite support. Override
`MACDEPLOYQT_EXECUTABLE` with that view's tool and retain its recipe and readback.

## Audit And Package

Stage the matching `Contents/Resources/ThirdPartyNotices` and sign the app
**before** packaging. Follow the [third-party inventory](../docs/third-party-release-inventory.md)
for each actual bundle. ARM64 build recipes/notices are not automatically the
right inventory for the Intel build. Keep matching project and dependency source
archives with both binary downloads and document which sources belong to which.

```sh
tools/dependencies/audit-selected-recheck.sh /path/to/Intel/EiskaltDC++.app x86_64 14.0
python3 macos/release.py check --profile intel --version 3.0.0-pre.1 \
  --build-number "$EISKALT_MACOS_BUILD_NUMBER" \
  --app /path/to/Intel/EiskaltDC++.app
python3 macos/release.py package --profile intel --version 3.0.0-pre.1 \
  --build-number "$EISKALT_MACOS_BUILD_NUMBER" \
  --app /path/to/Intel/EiskaltDC++.app --output /path/to/release-assets

tools/dependencies/audit-selected-recheck.sh /path/to/AppleSilicon/EiskaltDC++.app arm64 26.0
python3 macos/release.py package --profile apple-silicon --version 3.0.0-pre.1 \
  --build-number "$EISKALT_MACOS_BUILD_NUMBER" \
  --app /path/to/AppleSilicon/EiskaltDC++.app --output /path/to/release-assets
```

The tool checks the displayed `CFBundleShortVersionString`, separately allocated
numeric `CFBundleVersion` and exact advertised minimum OS. `--build-number` is
mandatory and must be canonical positive decimal, no greater than
`9223372036854775807`, matching the signed manifest's `build_number`. Both CPUs
must use the same immutable allocation; do not reuse it for changed release
contents. Ordering against the last published release remains a separate feed
validation gate. Archive names retain the displayed version.

Ordinary non-preset development builds use numeric build `1` unless explicitly
configured with `-DEISKALT_MACOS_BUILD_NUMBER=<allocated-number>`. That default
is not a production allocation or publication-history check. A build-number-only
change flows through `MacBundleMetadata.cmake`, causing relinking and post-build
signing. Unchanged numbers leave metadata intact. Makefiles avoid re-signing on
a no-op build; Xcode's existing unconditional post-build phase can still run.

The selected-recheck audit additionally checks the reachable extension library,
all five operations and an isolated native ABI caller. It accepts bundle-relative
`@rpath`, `@loader_path` and `@executable_path` IDs, rejecting expanded paths that
escape the bundle. On a nonmatching CPU, `--static-only` permits inspection but
explicitly retains runtime ABI acceptance as pending. Neither that inspection nor
Rosetta proves native minimum-OS compatibility.
It scans **every regular Mach-O**, including extensionless/non-executable
framework binaries, rejects universal/mixed architectures, missing deployment
metadata, wrong deployment platforms and dependencies above the floor. Internal
framework symlinks are allowed; broken or out-of-bundle symlinks are rejected.

Packaging additionally requires notices, strict deep signature verification and
the external-link audit. It preserves the source app, verifies ZIP CRCs, extracts
and re-audits the result, and writes a per-archive `.zip.sha256` file. Existing
assets are never overwritten. The checks do not establish notice completeness,
all runtime-loaded dependency resolution, or minimum-OS runtime compatibility.
Ad-hoc signatures are not Developer ID signing/notarization. Keep isolated
profile/Homebrew-denied startup and native GUI acceptance as release gates.

Run the packaging regression independently of a full application build:

```sh
python3 tests/macos/bundle/release-profiles.py "$PWD"
sh tests/macos/bundle/dylib-ids.sh "$PWD"
```

Tests compile actual x86_64/arm64 fixtures, exercise both CMake presets, build a
fat binary rejection fixture, inspect a non-executable framework, and round-trip
a signed ZIP. They do not run either fixture as the real application.
