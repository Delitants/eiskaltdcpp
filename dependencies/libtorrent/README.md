# Selected-Recheck Dependency

Eiskalt's selected-file verification requires the maintained extension described
in `docs/superpowers/specs/2026-09-30-selected-file-recheck-design.md`.
`source.json` pins upstream 2.1.1 and the exact patch checksum. The upstream
BSD-3-Clause license is retained in `LICENSE`; its original source notices are
preserved. The patch is maintained separately rather than copying build outputs
into Git.

Build into this checkout's staging directory:

```sh
tools/dependencies/build-libtorrent.sh \
  --prefix "$PWD/Work/dependencies/selected-recheck/reproducible-arm64" \
  --architecture arm64 --deployment-target 26.0 --jobs 2
```

The script verifies the archive and patch before extraction/application. Only
HTTPS downloads are permitted. Staging and installation prefixes stay beneath
this checkout's `Work` directory, including after symlink resolution. Fresh
patches get their own source/build directories. A caller may supply an existing
verified source archive with `--source-archive`. System package-manager libraries
are never replaced.

Configure a separate application build with `LibtorrentRasterbar_DIR` pointing to
`PREFIX/lib/cmake/LibtorrentRasterbar` and `EISKALT_REQUIRE_SELECTED_RECHECK=ON`.
The capability test compiles the ABI-2 header check and executes the linked
library's public ABI marker, requiring it to return 2. Run results are cleared
on every configure, so changing only the library cannot reuse a prior success.
Cross-compilation requires a runnable `CMAKE_CROSSCOMPILING_EMULATOR` for this
feature; without one, optional builds disable selected rechecking and required
builds fail. A preseeded run result is not accepted in place of execution.
This small configure probe runs the trusted provider, not the application or
a Torrent session. It does not establish native minimum-OS release acceptance.
Consumers prefer the selected
package's headers to transitive system include roots and retain assert-layout ABI
matching. Ordinary system-library builds can omit the feature.

The extension exposes preparation, checking, cancellation, finalization and
worker-thread snapshot APIs through `libtorrent/eiskalt_selected_recheck.hpp`.
Preparation drains one torrent's writes and prevents peers from restarting it.
Storage fences reload persisted partfile slot maps before fresh reads, so a
cached map cannot authenticate bytes that a new session would not read.
ABI 2 adds an error-reporting disk fence: a failed partfile metadata save retains
dirty slot mappings and fails preparation without admitting a selected hash.
Successful saves still reload persisted maps. Custom disk backends must implement
the stronger fence explicitly; the default rejects it. ABI 1 providers must be
rebuilt and cannot enable selected checking in this application.
Start clears only requested piece availability and disk-cache state, then uses
native disk hashing. v2 results compare fresh block Merkle roots with authenticated
native nodes; absent hashes yield unavailable. Terminal results remain paused
until the caller saves resume state and finalizes. While a selected check is
active, resume snapshots retain the full conservative availability bitmap, not
the unrelated full checker's sequential checked-piece prefix. Application callers
must own the recovery journal, user intent and publication barriers before
enabling UI.
Finalization restores native download-queue bookkeeping and peer candidacy when
a previously finished torrent loses pieces, without unpausing it.

The patch also initializes skipped-file/partfile mappings through read-only
metadata reads when a local checking handle bypasses normal resume verification.
This permits stopped-job checks without creating payload files or directories.

Dependency tests are an explicit target:

```sh
cmake --build BUILD --target eiskalt-selected-recheck-dependency-tests -j2
BUILD/tests/torrent/eiskalt-selected-recheck-dependency-tests '[selected-recheck-dependency]'
```

They instrument the native disk boundary and real filesystem fixtures. Tests
cover shared boundaries, v1/v2/hybrid corruption, missing authenticated hashes,
read-only payloads, cancellation, stale requests, local-only stopped checks,
partfiles, empty scopes, pre-finalization checkpoint bits and a concurrent
loopback transfer. The unpatched-library test must fail rather than silently
skip missing capability.

The application journal, lifecycle, scoped DC hash invalidation and translated
native controls are implemented locally. Integrated repair/restart acceptance,
independent patch review and native Intel macOS14+/Apple Silicon macOS26+ package
acceptance remain required. Host-linked Homebrew dependencies are development
evidence, not minimum-OS release proof; the user's installed app is unchanged.

## Bundle Audit

After dependency normalization, audit a trusted, locally built bundle:

```sh
tools/dependencies/audit-selected-recheck.sh \
  'Work/staging/EiskaltDC++.app' arm64 26.0
```

The audit checks every Mach-O file for one matching CPU and an acceptable
deployment floor. It rejects escaping symlinks, external/nonrelocatable install
names, external runpaths and unresolved relative dependencies. The application
must reference the extension ABI and all five operations exported by the single
bundled libtorrent. Required CMake configuration separately rechecks matching
headers and the library's returned ABI on every configure, including after
header/library/prefix changes, before the application is built.

On a matching native host, an isolated tiny caller verifies that the bundled
library returns ABI 2. It runs at the executable location in a temporary bundle
copy, preserving loader-relative dependency identities. It loads that library
and its initializers, not the application or a Torrent session; use only trusted
build artifacts. The source bundle is not modified. Cross-architecture
inspection may use `--static-only`, which
explicitly leaves runtime ABI acceptance pending and is not a release pass.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/macos/bundle/selected-recheck.py "$PWD"
ctest --test-dir BUILD -R eiskalt-macos-selected-recheck-bundle --output-on-failure
```

Actual thin Mach-O fixtures cover absent/mismatched ABI, missing operations or
consumer references, reexport chains, loader-relative identities, outside-Homebrew
links/runpaths, unresolved relative dependencies, wrong CPU/newer OS and escaping
symlinks. CMake fixtures reject both header/library mismatch directions, missing
markers, stale library-only successes and invalid cross-runner receipts. A real
consumer verifies enabled/disabled compile definitions, including ordinary
unpatched providers and cross-builds without a runner.
Signing, source/patch provenance,
full app-bundle audits and matching minimum-OS runtime tests remain separate
release gates; neither fixtures nor cross-inspection substitute for them.
