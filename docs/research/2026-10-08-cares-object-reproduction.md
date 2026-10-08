# Selected c-ares Object Reproduction

## Result

Four fresh c-ares 1.34.8 static builds reproduce every selected Mach-O object
payload on both CPUs. **Whole-archive bit reproducibility remains HELD.** Both
repeated builds produce different archive hashes. Every differing byte is in
archive member timestamps; comparisons with the retained archives additionally
differ in member permission modes. No source, compiler flags or archive bytes
were normalized, patched or replaced to obtain a passing result.

The [machine-readable summary](2026-10-08-cares-object-reproduction.json) records
the six archive hashes and all six complete container comparisons. Audit base
is `ea14fa263` with the inherited local source repairs unchanged. Private evidence
is retained under `Work/qa/cares-reproducibility-20261008/`.

| CPU | Fresh Builds | Objects Per Archive | Upstream Inputs Per Build | Objects Match Selected And Repeat | Whole Archives Match |
| --- | ---: | ---: | ---: | --- | --- |
| Apple Silicon | 2 | 91 | 130 | Yes | No |
| Intel | 2 | 91 | 130 | Yes | No |

The four builds yield 364 fresh objects; all match their corresponding selected
archive payloads. Each archive also includes its symbol index. All six container
comparisons account for every differing byte, including the symbol-index payload
and extended member names. There are zero non-metadata differing bytes. This is
an exact byte result for these inputs, not a dependency functionality test.

## Inputs And Build Records

The pinned source archive SHA256 is
`c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`.
Its 553 safe regular-file/directory entries contain 5,169,456 uncompressed bytes.
Four separate source, build, HOME, cache, temporary and private-prefix paths are
used. Builds run serially with two compiler jobs; nothing is installed into an
existing dependency prefix. Download/test targets are disabled. The existing
restricted tool environment is used, not a separately verified network wrapper.

The actual compiler is Apple Clang 21.0.0.21000334 with CMake 4.4.2 and SDK 27.0
on macOS 27.0. Historical per-CPU flags and defines are preserved. Apple Silicon
targets macOS 26.0; Intel targets 14.0 and retains its repeated architecture/
deployment flags and `-O2` preceding `-O3`. These compiler targets do not establish
execution on either minimum OS or on native Intel hardware.

Every member maps one-to-one to an actual compiler object, compile command and
dependency target. The per-object source/dependency sets match the selected
provider's retained records, whose bytes match earlier audit pins. Individual
commands match that provider's `flags.make` command template after labeling only
source/build path origins; binary contents are never transformed. The retained
template is not a newly captured historical compile-command trace.

Each build's 130 dependency-listed upstream inputs match the source archive.
Both generated configuration headers match the selected provider. All remaining
listed inputs are from the Xcode SDK/toolchain; their hashes are current
post-build readbacks, not immutable pre-compilation snapshots.

## Why The Archive Gate Is Held

Each archive has 92 members including the symbol table. All 92 timestamps differ
between repeats. In addition, all 91 object-member modes differ between retained
and fresh archives: retained members record `0644`, while the private audit's
deliberate `umask 077` produces `0600`. Mode differences are an explicit audit
environment difference, not evidence of unpredictable compiler output.

The standalone whole-archive acceptance command exits **1** with exactly four
selected-versus-fresh archive mismatches. Successful compilation and measurement
commands do not waive that gate. A future deterministic-container design must
define timestamp and member-mode handling before this can be closed. No such
recipe or release input is changed here.

## Controls And Custody

Twenty-four named controls pass. The initial missing-object-byte check fails
exactly one control before implementation. Independent audit review identifies
incomplete per-object provenance checking; duplicate compile records and a header
omitted from just one object's dependencies then fail exactly two controls before
correction. Other controls cover missing CPU/run/member/header inventories,
changed flags/digests, equal-object but different-archive results, and symbol-index/
object-body differences falsely classified as container metadata.

Final verification recomputes all measurements from actual files and checks the
exact initial/review failures, complete passing control list and held gate exit.
Custody readback covers 3,528 tracked/repair paths, the inherited patch, 23 selected
recipe/provider/tool inputs, nine current protected profile files and all 60
artifacts in the previous sealed Haiku checkpoint. It does not fully inventory
other historical QA trees or additional untracked source/profile paths. Those
are not modified by the audit, but broader custody is not claimed as verified.

## Remaining

Whole static-container and application reproducibility remain open, along with
other dependency/resource/source/notice closure. The original Intel shutdown and
bind causes, broader GUI/runtime/native/minimum-OS/translation acceptance and the
overall Torrent, settings and updater integration tasks remain unresolved.

Final whole-branch review, complete reviewed builds, production build numbering,
Developer ID signing/notarization, signed-v3 update acceptance, guarded closed-app
installation and GitHub/wiki/screenshots/prerelease publication/readback remain
pending. Production signed feeds remain last. Nothing installed or published.
