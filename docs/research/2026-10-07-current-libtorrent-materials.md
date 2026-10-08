# Current Libtorrent Source-Material Checkpoint

This is offline source-material preparation for the libtorrent providers
selected by the fresh both-CPU test consumers. It is not a final application,
complete release source archive, historical compilation proof or reproducible
binary claim. Older source sets and bundles remain unchanged.

## Stale Selection And Reconstruction

The older staged material lock describes extension ABI 1 and patch
`368a5693e88d25bd0bc9f1120d883c49a91ce59a7d22c3ab58b9e55f253af9fc`.
The current [source lock](../../dependencies/libtorrent/source.json) requires
ABI 2 and patch
`43c451f74613ba1959150c99d402b75752cda029b6882556c1ac1f580802012e`.
The behavioral selection check fails on exactly those two differences; source
archive identity and the original license identifier still match.

The pinned libtorrent 2.1.1 archive is reconstructed with the current patch,
without fuzz or offsets. Its 1,652 original files become 1,653 modified-source
files, matching the complete retained source tree shared by both provider
builds, including file modes. Both recorded CMake source selections point to
that tree. This is current-byte comparison against retained build records,
not an immutable snapshot of what the compiler read historically.

## Both-CPU Provider Evidence

| Check | Apple Silicon | Intel |
| --- | --- | --- |
| Installed source-matching headers | 295 | 295 |
| Source files named in compiler dependencies | 697 | 697 |
| Compiler objects accounted for by the final link | 285 | 285 |
| Matching objects in three embedded static archives | 106 | 106 |
| External compiler-input readbacks | 3,391 | 3,396 |
| Fresh built consumers selecting this libtorrent | 4 | 4 |

The built and installed provider dylibs match whole-file hashes and their
respective thin CPU architectures. Five fresh test binaries per CPU match the
preceding sealed binary/link receipts: four select the exact current provider
path and encode its install ID; the core-only consumer has no libtorrent import.
Generated but unbuilt app/test link rules are not counted as built consumers.
No binary is launched to establish loader routing in this audit.

The three embedded libraries are libdatachannel, usrsctp and libjuice. Archive
members match compiler objects as a multiset of filename and content hash;
legitimate duplicate filenames remain distinct. Their complete sources and
embedded notices are retained within the upstream and modified-source trees.
External Boost, OpenSSL and SDK inputs are separately pinned post-build
readbacks, not newly curated external source packages.

## Staged Materials And Verification

A new private 1,699-file material set retains the original upstream archive,
current lock/patch and build guide/script, complete modified sources, 12 selected
standalone upstream notice files, both CPUs' raw build configuration/link/flag
receipts, and source/provider inventories. All selected standalone notices match
the original archive; the full sources preserve additional file-level notices.
This scoped set must not replace the complete release notice/source superset.

Two tar archives from that same frozen material set are byte-identical:
`9019257c9685c8860c56fdebec03a096a9c4a8c6c15e735e4142eddc7c2816db`.
Every member, content hash, mode and normalized owner/timestamp is read back.
This proves repeatable packing of those inputs, not cross-host or binary build
reproducibility. Retry overwrite, changed bytes/modes, missing/extra files,
escaping links and duplicate distribution members are refused.

The initial header rule omitted upstream's one installed `.h` file; another
initial rule treated a linker install ID as an input path. Both failed attempts
are retained. The original duplicate-member refusal and the first preservation
check's omission of five directory-link receipts are also retained, not rewritten.
The corrected checks follow the actual install/link rules and preserve link
types/targets. Audit roots redirected through symlinks are refused. Optimized
Python is rejected before work, and RED-log verification requires the exact
failed identities, count and statuses, with no errors or skips.

The final 27 local audit/control tests and 30 existing release-profile tests
pass. The latter use small disposable Mach-O/ad-hoc-signature fixtures, not
production signing or a full application build. Independent read-only review
found audit weaknesses in root handling, optimized execution and mixed-log
acceptance; retained RED controls precede the repairs, and the final bounded
review has no remaining material findings. This is not whole-branch review or
a new run of the complete C++ suite.

Final readback preserves 11,614 preceding sealed artifacts, 3,506 candidate
source pins and 12,237 material-input pins, including the unrelated dirty
runtime/test repairs. Both historical candidate trees and strict signatures,
five earlier material/archive pins, 58 provider resources and the selected
protected-file manifests also remain intact. Only the two current status/
inventory documents are append-authorized; no previous seal is rewritten.

## Remaining Gates

Current-source verification of the other dependencies/resources and complete
material supersets, whole-build reproducibility, unresolved legacy attribution,
the original Intel shutdown refusal, broader lifecycle/native/minimum-OS/live/
soak/platform QA and translation quality remain open. Final whole-candidate
review, actual application/source/notice rebuilding, numeric build allocation,
production signing/notarization, signed-v3 acceptance, guarded installation and
authenticated GitHub/wiki/prerelease publication/readback are still separate.
Production signed feeds remain last. Nothing installed or published here.
