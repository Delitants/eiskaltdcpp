# macOS Release Flavors

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

Build a **matching-architecture Qt 6.11.1 Cocoa plugin** using the instructions in
[qt-cocoa-backport/README.md](qt-cocoa-backport/README.md), with both patches and
explicit `CMAKE_OSX_ARCHITECTURES` and `CMAKE_OSX_DEPLOYMENT_TARGET`. Do not copy
the ARM64 plugin into the Intel app, or replace it with stock Qt during packaging.

Set these environment variables to the prepared, absolute local paths:
`QT_PREFIX` must expose the complete Qt installation, including Svg, Multimedia
and LinguistTools, not only a split `qtbase` package. For an existing modular
Homebrew installation this is usually the common prefix, not `opt/qtbase`.

```sh
export DEPS_PREFIX=/path/to/target-dependencies
export QT_PREFIX=/path/to/matching-qt
export COCOA_PLUGIN=/path/to/matching-patched/libqcocoa.dylib
export COCOA_PLUGIN_SHA256="$(shasum -a 256 "$COCOA_PLUGIN" | cut -d ' ' -f1)"

# Choose one; the presets have separate build directories and two build workers.
cmake --preset macos-intel
cmake --build --preset build-macos-intel
ctest --test-dir out/build/macos-intel --output-on-failure -j1

# Or, with the Apple Silicon dependency paths exported instead:
cmake --preset macos-apple-silicon
cmake --build --preset build-macos-apple-silicon
ctest --test-dir out/build/macos-apple-silicon --output-on-failure -j1
```

The presets are unavailable until all four environment variables are set.
Their outputs are `out/build/<preset>/eiskaltdcpp-qt/EiskaltDC++.app`.
Never reuse the other architecture's CMake cache. On an ARM host, running Intel
tests also requires a working x86_64 execution environment; Rosetta testing is
not native Intel/macOS 14 acceptance. Do not install Rosetta implicitly.

The legacy `build-using-homebrew.sh` and general CI DMGs are not this release
path. In particular, do not run `fix-bundle-self-containment.sh` on these bundles:
it can replace the patched Cocoa plugin. Normalize dependency IDs with
`normalize-bundle-dylib-ids.sh` only when needed, then sign and audit the result.

## Audit And Package

Stage the matching `Contents/Resources/ThirdPartyNotices` and sign the app
**before** packaging. Follow the [third-party inventory](../docs/third-party-release-inventory.md)
for each actual bundle. ARM64 build recipes/notices are not automatically the
right inventory for the Intel build. Keep matching project and dependency source
archives with both binary downloads and document which sources belong to which.

```sh
python3 macos/release.py check --profile intel --version 3.0.0-pre.1 \
  --app /path/to/Intel/EiskaltDC++.app
python3 macos/release.py package --profile intel --version 3.0.0-pre.1 \
  --app /path/to/Intel/EiskaltDC++.app --output /path/to/release-assets

python3 macos/release.py package --profile apple-silicon --version 3.0.0-pre.1 \
  --app /path/to/AppleSilicon/EiskaltDC++.app --output /path/to/release-assets
```

The tool checks both bundle version fields and the exact advertised minimum OS.
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
