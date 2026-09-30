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
The capability test links the public ABI marker. Consumers prefer the selected
package's headers to transitive system include roots and retain assert-layout ABI
matching. Ordinary system-library builds can omit the feature.

The extension exposes preparation, checking, cancellation, finalization and
worker-thread snapshot APIs through `libtorrent/eiskalt_selected_recheck.hpp`.
Preparation drains one torrent's writes and prevents peers from restarting it.
Start clears only requested piece availability and disk-cache state, then uses
native disk hashing. v2 results compare fresh block Merkle roots with authenticated
native nodes; absent hashes yield unavailable. Terminal results remain paused
until the caller saves resume state and finalizes. While a selected check is
active, resume snapshots retain the full conservative availability bitmap, not
the unrelated full checker's sequential checked-piece prefix. Application callers
must own the recovery journal, user intent and publication barriers before
enabling UI.

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

This dependency checkpoint does not itself enable the application feature.
Separate journal/engine/UI integration, upstream patch review and native Intel
macOS14+/Apple Silicon macOS26+ package acceptance remain required. Host-linked
Homebrew dependencies are development evidence, not minimum-OS release proof.
