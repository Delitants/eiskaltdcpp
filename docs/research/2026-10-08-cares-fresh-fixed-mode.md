# Fresh-Source c-ares Fixed-Mode Archive Probe

## Result

**PASS for the isolated source-to-archive recipe.** Four fresh c-ares 1.34.8
builds reproduce all 91 selected object payloads per CPU. Fixing only new object
copies to mode `0644`, then using native `libtool -static -D`, produces identical
archive bytes across each CPU's repeats. New candidate archive files also have
consistent filesystem mode `0644`. Four fresh controlled-header offline consumers
pass. No existing provider or production recipe is replaced or modified.

The [machine-readable summary](2026-10-08-cares-fresh-fixed-mode.json) records
hashes, raw/fixed container comparisons, headers and flags. Base is `2b6120108`;
private evidence is under `Work/qa/cares-fresh-fixed-mode-20261008/`. The earlier
[copy-only feasibility result](2026-10-08-cares-fixed-mode-archive-probe.md) and
historical failed archive gates remain unchanged. This is not whole-application,
native Intel/minimum-OS, production integration or release acceptance.

| CPU | Fresh Builds | Objects Per Build | Upstream Dependency Inputs Per Build | Fixed Archive Differing Bytes | Offline Consumers |
| --- | ---: | ---: | ---: | ---: | ---: |
| Apple Silicon | 2 | 91 | 130 | 0 | 2 passed |
| Intel | 2 | 91 | 130 | 0 | 2 passed via Rosetta |

## Fresh Inputs And Build Correspondence

The upstream source archive SHA-256 is
`c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`.
All 553 safe file/directory entries contain 5,169,456 uncompressed bytes. Every
one of the 532 regular upstream files is hashed against the archive before
compilation and again afterwards in each fresh tree. No unexpected source files
appear. No source byte normalization or patching is used.

Builds use four separate source/build/HOME/cache/tmp/private-prefix locations,
serially with two compiler jobs and historical per-CPU CMake settings. First
and second runs have different process umasks, `077` and `022`. Tool/test targets
are disabled; no existing prefix install target is run. The restricted environment
is explicit, but is not a separately verified OS-level network sandbox.

The installed toolchain readback is CMake 4.4.2, Apple Clang 21.0.0
(`clang-2100.3.34.2`) and the Xcode SDK on macOS 27. Apple Silicon targets macOS
26; Intel targets 14 and preserves the repeated architecture/deployment flags and
historical `-O2` preceding Release `-O3`. These flags are not minimum-OS execution
evidence. Historical tool/recipe inputs are checksum-pinned; the command template
is not presented as a newly captured historical compiler trace.

Every native raw library has the selected provider's 91 object CPU identities,
payload hashes and order. Every object maps one-to-one to a fresh compile command
and dependency record. Those records match the checked historical template after
labeling source/build path origins only. Duplicate records, missing records and
per-object dependency omissions are rejected. Both generated configuration
headers and per-CPU flags match the selected provider and repeats.

Each build lists 130 checksum-matching upstream dependency inputs. Other listed
inputs are the two generated headers or SDK/toolchain paths. External dependency
hashes are post-build readbacks, not immutable pre-compilation SDK snapshots.
Repeated readbacks reuse the pinned helper's per-pass path-resolution cache,
not a payload-hash cache, and must match the entire uncached result. A mid-pass
external symlink retarget is outside the claimed immutable SDK custody scope.

## Native Archive And Consumer Gates

Raw repeated libraries remain different: all 92 timestamps and 91 object modes
differ. There are 458 differing raw bytes on Apple Silicon and 440 on Intel,
entirely member metadata; no payload, symbol-index-body or other byte differences.
The same standalone equality suite first fails on exactly both raw CPU cases,
then passes on both normalized-copy/native-D candidate pairs.

Every new object copy has before/after records proving fixed mode `0644` without
payload, timestamp or ownership changes. Input-copy timestamps still differ.
All 92 candidate members have zero date/UID/GID fields, object modes `0644` and
symbol-index mode `0100644`. Native archiver umasks differ; only each newly created
candidate file is set to external filesystem mode `0644`, with an explicit
before/after whole-file hash/stat check. Nothing inside an archive is rewritten.
Private parent/input directories remain `0700`. Cross-user ownership was not
perturbed, and reproducibility of complete package/tree metadata is not claimed.

Four consumers link against their respective candidate archives using byte-matched
public headers and fresh generated `ares_build.h` in controlled include views.
Dependency and link-map readbacks bind the actual headers and library; executable
CPU fields, c-ares symbol definitions and system-only dynamic dependencies are
checked. Fixture/header/archive hashes match before and after links.

The consumers check version 1.34.8, initialization, fixed DNS-question parsing and
truncated-input rejection. No resolver channel or network query is created.
Native ARM execution and Rosetta Intel execution on this host do not establish
native Intel or actual minimum-OS acceptance. No complete app is built or run.

The six existing native no-symbol warning lines per archiver case are retained
and checked exactly against earlier evidence; all other diagnostic stderr is
rejected. Successful exit status alone is never a warning/acceptance waiver.

## Controls, Review Repair And Custody

Twenty-two synthetic member/compile-record controls pass. Eleven exact-RED
preflight controls also pass, for 33 controls total. Independent bounded static
review found that the coordinator's exit-1 check could allow normalization before
validating the precise failure set. One repair pass adds a fail-closed guard at
the normalizer entry point, before any candidate/copy creation. It requires both
named failures, statuses, exception/count/summary shape and actual unequal raw
files. Mixed results, errors, skips and unexpected failures are rejected.

Eight negative receipt cases fail before the guard correction; a further
extra-error-header case fails once before its predicate correction. All final
controls and actual preflight receipts pass. The failed controls are retained.
These are report/receipt controls, not raw-container mutation tests. Final actual
archive/source/consumer/provenance facts are separately recomputed from files.

The reviewer performed no execution; final native results were not yet present
at review completion. The reviewer does not accept custody/docs/seal, production
integration or whole-branch/release readiness. Final native readback is the
parent's verification, not an independent runtime rerun.

Custody covers 3,534 enumerated source/repair paths, the inherited source patch,
416 original input hash/stat pins, nine protected profile hashes and the complete
preceding 542-artifact checkpoint. Another 3,304 processing-input facts freeze
fresh source/object/header/dependency/recipe paths along with the original inputs.
Earlier evidence/providers remain unchanged. Additional untracked/profile paths
and other historical QA trees are outside the complete custody inventory.

## Remaining

Turn this private recipe into reviewed, maintained build tooling; verify its
integration and complete package/tree metadata policy. Other dependency/resource/
preferred-source/notices and whole-app reproducibility remain open. Historical
raw-archive mismatches are preserved, not silently waived or replaced.

Original Intel shutdown/bind causes and broader GUI/runtime/native/minimum-OS/
language QA; overall Torrent/settings/updater integration; final whole-branch
review and complete reviewed builds; production numbering/signing/notarization/
signed-v3 acceptance; guarded installation and GitHub/wiki/screenshots/prerelease
publication/readback remain pending. Nothing installed or published. Production
signed feeds last.
