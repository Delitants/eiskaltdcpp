# Development Status

Checkpoint: 2026-10-06. `v3.0.0-pre.1` remains this development checkpoint's release target.

## Implemented In Development

The maintainer-selected Modern Classic globe app icon (variant 1) is integrated
as source assets for macOS ICNS, Windows ICO, desktop PNG resolutions and all
five legacy Qt app-brand directories. Modern toolbar themes use the same brand
resources. Original artwork attribution and generated-reference provenance are
retained, with a checksum manifest and reproducible current-tool packaging
recipe. This is not an installed-app or Windows/Linux runtime acceptance claim.
The asset-update verification passes eleven packaging checks, 617 Qt cases per
architecture and all three GUI layouts (101/101/99 cases) on ARM and
Intel/Rosetta. Six recovery repetitions per architecture also pass; the earlier
natural intermittent recovery timeout remains unexplained and unwaived.

A separate icon-checkpoint review found and corrected incremental MinGW resource
dependencies, generated-brand copyright metadata and missing non-Mac notice
installation. Tests exercise the real CMake resource dependency graph with a
substitute compiler, exact provenance scopes and actual install rules with
Linux/Windows destination layouts. This is not native Windows/Linux acceptance.
Two additional full single-line GUI runs pass in randomized order. Independently,
held search-blacklist and anti-spam write/flush calls reproduce a GUI-thread
stall on both ARM and Intel/Rosetta. Auxiliary checkpoint writes still need an
owned asynchronous snapshot/receipt repair; the original recovery timeout is
not attributed to this diagnostic.

The integrated Torrent UI and proxy routing, managed DC publication, native UI
updates and selective upstream maintenance are present in the development tree.
The audited Torrent translation batches cover 25 catalogs; compiled-string and
format checks do not replace native-speaker review. The DHT advertisement option
is independently configurable, off by default and takes effect on reconnect.

The UDP search-result queue shutdown race is repaired with deterministic
pre-worker-entry and producer/shutdown regressions. Context-specific protocol
validation and resource limits, authoritative outgoing Date/BaseDate metadata,
and interruptible socket waits with proxy revocation are now integrated in the
development tree. The consolidated functional review found two older Shadowsocks
integration defects: pending final ciphertext could be stranded, and coalesced
reply frames could wait indefinitely for more network input. Both are fixed with
regressions for legacy/2022 routes, buffered commands, file sends, graceful
disconnect and forced shutdown. This review covered the prioritized maintenance
paths, not an exhaustive audit of every source file.

The earlier September 29 macOS checkpoint passed 1,427 cases and 61,767 assertions
across core, Qt, Torrent and window suites, with the real-GOST tests enabled.
This is development verification, not an installed or published release.

A previous versioned macOS ARM64 candidate passed bundle dependency and
ad-hoc signature checks. A packaging regression also covers extensionless,
non-executable framework binaries, which must not escape the link/OS audits.
Its isolated CLI startup passes with network, real-profile and Homebrew reads
denied. The candidate declares macOS 26 because of its bundled dependencies;
testing was on macOS 27, not macOS 26 or older. The approved release split is an
Intel-only macOS 14+ ZIP and an Apple-Silicon-only macOS 26+ ZIP, with explicit
architecture/minimum-OS filenames. Separate thin Intel and Apple Silicon
verification builds now pass. Intel verification runs through Rosetta, not native
Intel hardware; native minimum-OS acceptance remains pending. The earlier ARM64
candidate is not the final rebuilt release for either architecture.
Dependency notices and matching source materials have been staged
for the exact candidate; final distribution checks are still required. Ad-hoc
verification is not Developer ID signing or notarization, and CLI startup is
not live GUI QA.

The eight optional real-GOST interoperability cases have passed on macOS using
disposable loopback identities and servers. Coverage includes authenticated TLS,
TCP/uTP Torrent transfers on IPv4/IPv6, UDP tracker and DHT exchanges, remote DNS,
and completed-session resume after a fixture-server restart. Direct destination
access is denied in the application fixtures. These checks do not contact or
verify a deployed proxy server, and do not establish partial-piece crash recovery.

## Updater And Quit Checkpoint

Built-in update settings, verified staging and committed-normal-quit installation
are integrated in development. Download routing, signature/archive acceptance
and installer recovery are separate checks; a transport success alone is not an
accepted update or installed release. Production signed-feed activation remains
a release gate.

Core checkpoint writes now use owned background workers after lifecycle
retirement. Settings Save callbacks remain on the startup owner. Pending or failed
receipts retain the application; a timed-out quit requires explicit retry and
cannot install an update merely because its worker finishes later. This is not a
total hard deadline for lifecycle joins, serialization or every filesystem call.

Search-blacklist and anti-spam quit saves also use an owned background batch.
Immutable paths and rule bytes are captured on the GUI owner; worker completion
includes physical thread teardown. A pending-phase timeout retains the application
until explicit retry. Auxiliary recovery preserves successful owner receipts and
permanently cancels update-install consent. Anti-spam retains its existing single
receipt for four files: a partial failure retries that batch, not only individual
failed files. GUI settings sync and broader filesystem responsiveness remain
separate acceptance work.

Focused persistence/quit checks and all three applicable window layouts pass on
ARM and Intel/Rosetta. Regular core and Qt suites also pass on both; the Intel
Torrent suite passes. An ARM Torrent peer-policy rejection-counter failure is
retained for investigation despite a passing focused rerun. Earlier intermittent
bootstrap, publication and bind causes are not waived by green reruns.

Peer-policy test failures now include the policy variant, listener ports, proxy
forwarding and available leech peer/alert state. A deliberately invalid loopback
endpoint reproduces the counter failure without payload transfer and verifies
the diagnostic output. It does not establish the cause of the retained natural
failure or justify changing the security policy.
The diagnostic observers were reviewed and corrected to freeze the original
assertion values before collecting extra details. A controlled late-counter
change verifies that boundary. Corrected full Torrent suites pass on ARM and
Intel/Rosetta (152 cases each); the original intermittent failure remains open.

The 32 previously missing proxy-test, Torrent-menu, magnet-sharing and transfer
runtime keys now have entries in all 25 catalogs. The unchanged production
extraction contains 2,065 keys, with no missing active keys in those catalogs.
Catalog format and compiled-string checks do not establish complete localization:
inherited unfinished translations and native-speaker review remain pending.
Complete resource/dependency provenance and reproducibility remain separate
gates. Independent static review of the complete updater change range found two
receipt defects: late backend offer revocation and incomplete GUI layout saving.
The repairs also cover uninitialized file-browser snapshots, hidden history
loaders and shared/inactive history layout keys. Final narrow static review
finds no remaining actionable issue in that correction. Latest ARM layout and
recovery checks pass, including complete single-line, multiline and applicable
sidebar suites. Fresh Intel/Rosetta core, Qt, Torrent and all three applicable
GUI-layout suites also pass; focused controller checks pass on both architectures.
An earlier complete single-line checkpoint-recovery timeout remains unexplained
despite passing reruns. A sorting-test teardown abort was traced to that fixture
destroying a live database loader; the corrected fixture joins it and retires
GUI workers before destruction. Failed evidence is retained. Static review and
isolated tests are not native release acceptance or security certification.
These receipt repairs are implemented and reviewed; the retained intermittent
failures and the final release gates below remain open.
The current catalog audit also passes all 23 tests without skips; this checks
extraction, formats and compiled strings, not native-language quality.

The retained historical QA bundles now have file-level framework mappings for
114 Apple Silicon and 96 Intel non-code files, including shipped headers and
helper metadata. All match the pinned Sparkle SDK or deployed Qt providers.
Across both bundles, 93 files match source bytes and 72 localization dictionaries
match source values; 12 generated resources have source associations, not
reconstructed-build proof. All 33 initially unresolved metadata/module-map files
now have byte-exact reconstruction evidence. Nineteen ARM Qt plists and four
Sparkle files matched current-generator fixtures. The ten Intel Qt plists match
an unchanged, independently retrieved official CMake 3.30.6 template; CMake added
the bundle-name field in 3.31. The original ten failures and the newer-template
rejection control remain retained. A matching template does not identify the
historical SDK producer, compiler or complete framework build recipe.

The 440 inherited icon/resource files in each historical bundle also have
byte-exact immutable public project repository origins: 283 in upstream Eiskalt
and 157 in this fork. Project edit history is not proof of the original artist
or derivative-work licensing chain. Inherited notices remain unchanged; legal
review and reproducibility are not certified. These bundles are not current-
source release assets. Retained bundles/providers and protected live files remain
unchanged.

## Before Prerelease Publication

1. Finish retained regression diagnoses, localization, resource/provenance checks
   and independent review of the final source candidate.
2. Complete native/minimum-OS and live GUI/lifecycle acceptance for the approved
   Intel-only macOS 14+ and Apple-Silicon-only macOS 26+ split.
3. Rebuild final reviewed-source assets; preserve matching dependency notices and
   source materials. Complete numeric build allocation, signing, updater acceptance
   and guarded closed-app installation/readback. Keep profiles, credentials,
   private infrastructure configuration and QA artifacts out of release packages.
4. Publish the maintained wiki, approved screenshots and prerelease assets, then
   confirm public readback. Activate signed feeds only after the final gates;
   signed feeds are last. Wiki sources remain available in `docs/wiki` meanwhile.

## Acceptance Still Separate

Native-speaker review, Windows/Linux Torrent packaging, older macOS compatibility,
live proxy reconnection and DHT behavior, sharing-cache restart acceptance and
extended crash/freeze soak must be reported as tested or untested individually.
A green macOS test suite is not proof for other platforms or a live deployment.

## Later Roadmap

Mixed DC/Torrent pieces, SQLite share storage, parallel TTH hashing, MCN1,
HBRI/SUDP, temporary sharing, richer chat/retry policies and compressed/indexed
lists are separate work, not promised features of the first prerelease.
