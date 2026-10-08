# c-ares Fixed-Copy-Mode Archive Probe

## Result

**PASS for isolated feasibility:** copying objects into private directories,
fixing only those copies to mode `0644`, and using native `libtool -static -D`
produces byte-identical archive pairs on Apple Silicon and Intel. All 91 selected
object payloads and their order are preserved. Four controlled-header offline
consumers link and run successfully. No provider or production recipe is changed.

The [machine-readable summary](2026-10-08-cares-fixed-mode-archive-probe.json)
records archive/consumer hashes and complete byte comparisons. Audit base is
`a001f5744`; private evidence is under `Work/qa/cares-fixed-mode-probe-20261008/`.
The [preceding failed probe](2026-10-08-cares-deterministic-archive-probe.md)
remains unchanged. This result does not waive historical raw-container or
whole-application reproducibility gates, nor establish release acceptance.

| CPU | New Archives | Objects Per Archive | Differing Archive Bytes | Controlled Offline Consumers |
| --- | ---: | ---: | ---: | ---: |
| Apple Silicon | 2 | 91 | 0 | 2 passed |
| Intel | 2 | 91 | 0 | 2 passed under Rosetta |

## Recipe And Exact Perturbations

The two previously reproduced object sets per CPU are copied without recompiling
source. Each copy's incoming metadata deliberately differs: first mode `0600`,
timestamp 1,000,000,000; second mode `0644`, timestamp 1,700,000,000. Both are then
set to mode `0644`. Before/after records verify that only the copied mode changes,
with no payload, timestamp or ownership change. Original input hashes, modes,
timestamps, owners and tool-link targets are frozen and read back separately.

Native archiver runs use different process umasks (`077` and `022`) with explicit
private HOME/cache/tmp environments. Outer, case and input directories remain
`0700`. All 92 archive members, including the symbol index, have zero date/UID/GID
fields; object-member modes are `0644`, symbol-index mode `0100644`. Raw names,
symbol-index payloads, padding, layout and complete file bytes match per CPU.
Nothing is rewritten inside an archive. Cross-user ownership was not perturbed.

The resulting archive files themselves retain different filesystem permissions
(`0600` and `0644`) due to the process umasks. Those external file permissions are
not part of archive-byte equality, and private parent directories protect both.
A production packaging design must specify output-file metadata as well; no
reproducible package/tree claim follows from identical static-library bytes.

Actual archiver/compiler/run/symbol/dependency commands and exits are retained.
Every archiver case emits exactly the same six no-symbol warning lines as its
preceding sealed native-D case. They are retained and checked, not discarded or
treated as an arbitrary successful-stderr exemption. No new diagnostics are
accepted by the actual-file validator.

## Consumers And Controls

Four consumers use byte-matched private header views with upstream public headers
and generated `ares_build.h` (one shared include view per CPU). Dependency readbacks
verify the actual controlled headers. Each consumer links explicitly against its
candidate archive and has the expected executable CPU, defined c-ares API symbols,
no unresolved c-ares symbols and only system `libresolv`/`libSystem` dependencies.
Link maps identify the candidate. Fixture/archive/header hashes match before and
after links.

The fixture initializes c-ares, checks version 1.34.8, parses a fixed DNS question
and rejects a truncated one. It creates no resolver channel or network query.
Intel execution uses Rosetta; macOS 14/26 deployment flags are not actual
minimum-OS execution evidence. These small consumers are not complete app builds.

The same real whole-byte equality tests fail on exactly both earlier native-D
permission-mismatch cases, then pass on both new fixed-mode pairs. Sixteen
synthetic member-report controls pass after exactly two mode-policy REDs. They
cover inventory/order/payload substitutions and metadata/mode omissions or changes.
These controls are report-level checks, not raw-container mutation tests. Actual
archive/consumer/input facts are independently recomputed from retained files.

Two harness failures are preserved: initial input freeze rejected Xcode's `nm`
symlink; the corrected freeze pins the restricted tool links and target bytes.
The initial actual-file validator incorrectly required empty archiver stderr;
the corrected check requires the exact six previously recorded warning lines.
No candidate archive was rebuilt or modified during these harness corrections.

## Custody And Scope

Custody checks cover 3,532 enumerated source/repair paths, the inherited source
patch, 391 input hash/stat pins, nine protected profile file hashes and all 1,012
artifacts in the preceding sealed probe. Public status documents are append-only;
earlier reports remain unchanged. Other historical QA trees and extra untracked/
profile paths are outside the complete inventory. Nothing installed, signed,
pushed, published or changed in an existing provider or product recipe.

Independent bounded read-only review found no material actionable issues in the
probe/consumer evidence; custody/docs/seal and production integration were outside
that review. Parent preservation separately checks frozen nanosecond mtimes and
the complete preceding seal. These checks do not certify broader release gates.

## Remaining

The fixed-copy recipe is proven only with previously compiled, verified c-ares
objects. Fresh-source build integration and packaging metadata policy still need
a reviewed design and fresh repeat builds; other dependencies and complete-app
reproducibility remain open. Historical failed gates are preserved, not waived.

Other dependency/resource/preferred-source/notices closure; original Intel
shutdown/bind causes and broader GUI/runtime/native/minimum-OS/language QA;
Torrent/settings/updater integration; final whole-branch review and complete
reviewed builds; production numbering/signing/notarization/signed-v3 acceptance;
guarded install and GitHub/wiki/screenshots/prerelease publication/readback
remain pending. Production signed feeds last.
