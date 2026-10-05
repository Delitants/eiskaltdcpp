# Release Source And Notice Inventory

This records the earlier standalone macOS ARM64 `3.0.0-pre.1` inventory,
split-CPU input preparation and the remaining updater-enabled release gates.
It is not a legal certification or a claim that both final CPU flavors have
complete source/notice sets. Verify the actual distributed artifacts and their
checksums.

## Project Sources

The project-wide statement is in [COPYING](../COPYING), with the GPL version 3
text in [LICENSE](../LICENSE). COPYING specifies GPL version 3 or later and an
OpenSSL linking exception. Preserve file-level notices: some inherited core
files carry GPL version 2-or-later headers, while Qt frontend files carry
GPL version 3-or-later headers. This inventory does not replace or relicense them.
The existing [copyright inventory](../full.copyrights.info.in.Debian.style)
and [AUTHORS](../AUTHORS) remain part of the source distribution.

## Added Icon Families

Prism, Contour, Slate and Reborn use geometry from the pinned Phosphor core
2.1.1 package. The [source lock](../tools/icon-themes/source-lock.json) pins the
archive checksum; [vendor provenance](../tools/icon-themes/vendor/provenance.json)
records retained source hashes and the MIT license hash. Each theme contains
the exact upstream copyright/permission notice and attribution.

Run the offline generator check and tests documented in
[the icon tooling guide](../tools/icon-themes/README.md). Confirm those notices
survive the actual application/installer packaging, not just the source tree.
The legacy icons keep their existing provenance and are not covered by the
Phosphor notice.

## Modified Qt Cocoa Plugin

The optional macOS plugin override is built from Qt Base 6.11.1 plus the two
patches in [macos/qt-cocoa-backport](../macos/qt-cocoa-backport/README.md).
That guide pins the upstream archive SHA-256 and documents rebuilding.
The accessibility patch is a local correction, not an upstream backport.
Retain the original source copyright/SPDX headers and applicable Qt notices;
different files carry different license identifiers.

A prerelease using this override must identify the exact Qt source archive,
both patches, build instructions and resulting plugin hash. Prepare the matching
modified-source artifact and notice bundle alongside the application release.
The staged source package contains the complete pinned Qt Base source with both
patches applied and the standalone build instructions. All 99 Cocoa source and
resource files were compared with the actual patched-plugin build inputs.
The other Qt modules and non-Qt libraries are inventoried separately below.

## GOST And Operational Tools

The application implements the GOST protocol itself. The standalone GOST server
executable used by opt-in interoperability tests is not vendored or selected for
the application/source release. Its throwaway certificates, credentials, logs
and generated configuration are local QA artifacts. Private server deployment
scripts/configuration and developer profiles are not part of the public candidate.

## Sparkle Updater Dependency

The updater dependency is pinned to Sparkle 2.10.0, source commit
`eef1a539a373c1f1a320624b1130fc5de7b2e100`. The SDK/source URLs and complete archive
SHA-256 values are in [sparkle.lock.json](../macos/updater/sparkle.lock.json).
[Offline staging](../macos/updater/README.md) verifies both archives and matching
original license bytes before preparing separate thin framework/helper trees.
No SDK test application, signer/developer tools or symbol bundles are included.

Each staged flavor retains the unmodified upstream LICENSE, including its
external-component notices, plus provenance recording input/source pins,
architecture/OS ceiling, signing mode, preparation and final binary hashes.
Keep the matching pinned source archive available alongside the binary release.
The offline fixture ZIP round-trip verifies these materials and helper signatures
survive packaging; it is not evidence that the actual release app includes them.

Opt-in CMake/framework packaging is implemented and audited on disposable
bundles; it does not enable the updater backend. Final signing of both actual apps,
release source/notice inventories and native minimum-OS/installer acceptance are
still pending. The older 38-package candidate inventory below predates Sparkle;
it is not a completeness claim for the future updater-enabled release.

## Dependency Closure Inventory

The read-only [closure auditor](../macos/audit-bundle-closure.py) emits hashes,
architecture, install IDs, runpaths and context-specific resolved imports for
every thin Mach-O image in an app, including helper executables and unlinked
plugins. The package gate runs it before ZIP creation and after extraction.
Follow [the release guide](../macos/RELEASE.md) to retain its JSON report beside
the source/notice mapping. A complete dynamic graph is not a source-completeness
claim: static components, runtime-loaded data and source/patch identity still
need independent inventory. An older UUID match is useful provenance evidence,
not proof that its notice/source set describes a changed binary.

## Earlier ARM64 Candidate Materials

The candidate inventory covers 38 dependency packages: the copied frameworks,
libraries and plugins, plus statically linked c-ares/libmaxminddb and Boost
headers. All 38 upstream archives were checked against the source hashes in the
installed package recipes. Ten external/local recipe patches were retrieved and
verified; inline patches and build substitutions remain in the recipes.

For the two recipes without local-patch hashes (GLib and Lua), the historical
recipe match excludes only bottle metadata and an equivalent post-install
directory spelling. The pinned historical recipe, patch revision and hashes are
recorded. No current unversioned patch is silently substituted.

The notice set retains 2,692 dependency notice files, eight Qt build SBOMs and
the project/vendored notices. Qt/Chromium notices are a conservative superset,
not a claim that every listed component is used. Full module source archives
preserve embedded-component source and file-level notices. No GeoIP database or
Aspell dictionary files are present in this candidate. Apple system libraries
and general-purpose build tools are not redistributed as app dependencies.

The distribution layout is:

- Application: `Contents/Resources/ThirdPartyNotices`, with notices, provenance,
  recipes, patches, SBOMs and a hashed manifest.
- Third-party source archive: the same materials, 38 pinned upstream archives,
  matching modified Cocoa source and its input-hash inventory.
- Project source archive: the exact curated Eiskalt tree, including original
  file-level notices, vendored BLAKE3, icon provenance and build scripts.

Keep the source archive available alongside the binary release. Recreate this
inventory when dependencies, plugins, static libraries or packaged data change;
it is not a reusable completeness assertion for future builds.

## Split-CPU Input Preparation

The 2026-10-02 preparation separately records the actual thin Intel/macOS 14+
and Apple Silicon/macOS 26+ application inputs. These are pre-integration
bundles, not final updater-enabled release artifacts. The Intel graph contains
40 Mach-O images; ARM contains 84. All 39 Intel and 83 ARM third-party images
match their selected providers' file-backed code/data section bytes. Whole-file
hashes are also recorded, but load-command relocation and signing can change
them. Section matches establish copied-input identity, not reproducible builds
or native minimum-OS acceptance.

Do not reuse the earlier ARM inventory unchanged for Intel:

| Component | Intel Input | ARM Input |
| --- | --- | --- |
| Qt | Official Qt 6.11.1 SDK, vendor source/build SBOMs | Homebrew Qt 6.11.1, retained recipes and SBOMs |
| gettext | 0.24 | 1.0 |
| PCRE2 | 10.45 | 10.47 revision 1 |
| FFmpeg | Qt-supplied 7.1.3, five dylibs | Not present in this prepared bundle |
| libtorrent | 2.1.1 with maintained selected-recheck patch | Same source and patch, separately built |
| Sparkle | 2.10.0 reserved, not yet in this input bundle | Same reserved dependency |

The newly retained Intel source archives are:

- [gettext 0.24](https://ftp.gnu.org/gnu/gettext/gettext-0.24.tar.gz), SHA-256
  `c918503d593d70daf4844d175a13d816afacb667c06fba1ec9dcd5002c1518b7`.
- [PCRE2 10.45](https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.45/pcre2-10.45.tar.bz2), SHA-256
  `21547f3516120c75597e5b30a992e27a592a31950b5140e7b8bfde3f192033c4`.
- [FFmpeg 7.1.3](https://ffmpeg.org/releases/ffmpeg-7.1.3.tar.xz), SHA-256
  `f0bf043299db9e3caacb435a712fc541fbb07df613c4b893e8b77e67baf3adbe`.

gettext/PCRE2 match their exact historical installed recipes. FFmpeg's official
detached signature verifies against fingerprint
`FCF986EA15E6E293A5644F10B4322F04D67658D8`, published on the
[FFmpeg verification page](https://ffmpeg.org/download.html#release-verification).
Its version is independently identified by Qt's vendor SBOM and the library's
embedded configuration. Retain FFmpeg's source-level notices and Qt's embedded
component attributions, not just an LGPL label. The recorded Intel configure
options enable shared libraries and omit GPL/nonfree enablement; they do not
replace the complete licensing inventory.

Qt Base/SVG vendor commit identifiers match the official `v6.11.1` release tags.
The retained module archives match 22,492 Qt Base, 621 Qt SVG and 2,957 Qt
Multimedia vendor-listed source files. The Base/SVG differences are explicitly
recorded: Git export omits `.gitignore`/`.gitattributes`, and generated `.tag`
contents differ. No other vendor-listed source content differs. Preserve this
comparison and the full vendor SBOM rather than treating a version label alone
as source proof.

The [selected-recheck source lock](../dependencies/libtorrent/source.json) pins
both the upstream archive and patch. A fresh no-fuzz reconstruction matches all
1,653 files in each actual provider build source tree. Both Cocoa providers use
the same patched Qt source; all 99 recorded Cocoa source/resource files match
the modified-source archive and build inputs. Each CPU retains its own provider
binary hash, build recipe and linked static c-ares/libmaxminddb archive hashes;
Boost 1.92.0 headers are recorded separately from dynamic libraries.

Local preparation retains 17 Intel package records and a conservative 39-package
ARM superset, each including reserved Sparkle sources/notices. ARM recipe
metadata used to pin shared source bytes is not labeled as an Intel build
receipt. Actual Intel build recipes, historical receipts and official Qt vendor
archive checksums remain distinct. Both bundles' resources are hashed; neither
contains GeoIP databases or Aspell dictionary payloads in this preparation.

Still required: final static/header and resource attribution review, superset
curation, project sources from the final reviewed commit, notices staged into
the final apps, and signed binary/source archive readback. This preparation
does not activate the updater, install an app or publish release assets.

## Input Attribution And QA Notice Placement

The follow-up input audit records 91 c-ares objects and two libmaxminddb objects
per CPU. Intel's installed archive members match the retained build objects;
130 c-ares and six libmaxminddb dependency-listed upstream inputs match the
pinned source archives. Generated header hashes and compiler flags are retained
separately. ARM's thin archive members, exact recipes and installation receipts
are verified, but original per-object compilation records were not retained.
Do not infer historical build-input proof from an installation receipt. The
controlled candidate build below does not establish how those older ARM inputs
were compiled.

Compiler dependency listings identify 2,022 Intel and 2,023 ARM Boost headers
whose current bytes match the pinned Boost 1.92.0 source archive. The five
vendored BLAKE3 source/header files and selected CC0 notice are recorded
separately. These are retained dependency listings with current-byte comparisons,
not immutable snapshots of every historical compiler input.

All 1,215 resource paths in each prepared bundle have exact byte provenance:
project assets, generated deployment configuration, catalogs and icon families.
The four Phosphor-derived themes retain identical upstream MIT license bytes
both beside their copied assets and in the staged notice tree. Offline
regeneration verifies all 688 SVGs, eight attribution/license files and the icon
manifest. Legacy icon families remain covered by their original copyright
inventory entries, not by the Phosphor license. Compiled QRC inputs and the
external default QRC declaration are distinguished in the audit.

**Historical catalog warning:** these input bundles contain the 25 Qt TS files
from commit `4824e481bac1ead3de42fb544451e6dc0fe37fdf`, before the later hub-filter
and updater catalog additions. Recompiling those exact TS bytes reproduces all
25 bundled QM payloads for both CPUs. This establishes old-input provenance,
not current translation coverage. Final packages must rebuild current TS/QM
together and include the newer translated interface.

Two fresh QA copies contain notices for the actual 16 Intel and 38 ARM dependency
packages: 580 and 2,814 notice/provenance files respectively. Reserved Sparkle
sources stay separate because its framework is absent from these input bundles;
its notices must accompany the eventual updater-enabled builds. Before QA
signing, every original binary/resource payload remains byte-identical. No
shared dependency prefix, source input app, installed app or profile is changed.
These notice trees and their private assembly/readback scripts are preparation
artifacts, not a new public release-tool interface or final legal certification.

Both notice-bearing QA copies pass strict ad-hoc signing and complete ZIP
readback: 1,898 Intel and 4,230 ARM regular-file/symlink records survive exactly.
The ZIPs remain below the existing 512 MiB gate. Main executable code/data
sections and all dependency/resource payloads remain unchanged; QA signing
updates signature metadata. Only `CFBundleVersion` is adjusted to fixture value
`1` in copied plists, with all other fields unchanged. This is not a production
build-number allocation, notarization, installation or native minimum-OS test.

## Controlled ARM Static Candidate

An owned, separate prefix now contains c-ares 1.34.8 and libmaxminddb 1.13.3 built
offline from the pinned archives for arm64/macOS 26. All 91 c-ares and two
libmaxminddb archive members match retained compiler objects and declare that
architecture/deployment target. Source-tree hashes captured before compilation,
generated configuration, exact compiler commands and per-object dependency
readbacks are retained. SDK/system dependency hashes are post-build readbacks,
not immutable pre-build snapshots.

SDK 27 marks `pipe2` as macOS 27-only, so this macOS 26 candidate explicitly uses
`HAVE_PIPE2=OFF`. Neither archive nor the static-link fixture imports `pipe2`.
The offline DNS-parser/database-lookup fixture imports only Apple libSystem;
all 26 MaxMind upstream tests and five frozen-prefix integrity checks pass.
The c-ares GoogleTest suite was not built; parser fixtures provide narrower
coverage. Host execution is not native minimum-OS acceptance.

This prefix has not replaced shared providers or been linked into the installed
or earlier QA apps. Final reviewed application builds must explicitly consume
the controlled prefix and regenerate matching source/notice receipts.

## Current Updater Candidate Resource Audit

The 2026-10-04 read-only audit covers the updater-enabled ARM and Intel QA
candidates built from source `e325cb6cd`. It does not modify the accepted bundles
or reuse the historical catalog reconstruction as current translation proof.
Each candidate has 1,215 copied project-resource files mapped to matching current
source bytes or retained build outputs:

| Resource Class | Files Per CPU | Evidence |
| --- | --- | --- |
| Phosphor themes | 696 | 688 SVGs and eight attribution/license files; exact retained MIT notice |
| Legacy icons | 440 | Matching project bytes and inherited copyright inventory; upstream file-level origin unresolved |
| Project help and Qt TS files | 28 | Matching current project source bytes |
| Qt QM catalogs | 25 | Matching current candidate build outputs and recorded TS inputs |
| Gettext MO catalogs | 25 | Matching current candidate build outputs and recorded PO inputs |
| Deployment configuration | 1 | Exact output of the actual `write_qt_conf.sh` writer |

The copyright inventory now explicitly distinguishes Phosphor MIT/Expat assets
from the broader inherited LGPL icon entry. The scoped override includes the
pinned vendor SVGs and notice, generated SVGs and their attribution/license files;
it does not relicense the generator or inherited raster/brand assets. The icon
regression reproduces the previous incorrect classification, checks every
generated/vendor geometry path and verifies that legacy license overrides remain
unchanged. The complete 14-test icon suite passes, including offline regeneration
and deliberate asset-tampering rejection.

The actual app link inputs identify 694 compiled QRC declarations: 688 Phosphor
SVGs, five inherited brand icons and one Torrent raster fallback. A separate
test-only QRC declaration is not counted as an app-compiled resource. The default
external QRC's 83 inputs are recorded separately; declarations and retained object
hashes are not runtime extraction or immutable compiler-input proof.

Country/language flags are rendered from regional-indicator characters using the
platform font. No font payloads are present in either audited app outside the
retained notice documents; this is not redistribution of Apple's font artwork.
Dependency-framework resources remain under the earlier package-level mapping,
not newly established file-level source proof in this project-resource audit.

**Remaining attribution gate:** original upstream author/revision evidence for
the 440 legacy icon files per CPU, full dependency-resource review and final
source/notice refresh. Matching project bytes does not establish those origins,
reproducible compilation, linguistic completeness or legal certification. The
immutable QA candidates still contain the preceding copyright inventory and must
receive the corrected inventory through the final reviewed-source rebuild, not
an in-place edit or reseal. Installation and publication remain separate gates.

## Final Distribution Gate

For the final artifact, enumerate dynamic frameworks/libraries, static linked
components, plugins and data such as dictionaries/GeoIP. Record exact versions,
source provenance and notice files. Bundle matching notices and prepare source
materials as applicable, then verify the archive contents. Preserve dependency
notices rather than assuming this document, a package-manager license label or
the top-level GPL text substitutes for them. Staged materials must survive the
final archive/signature checks and be uploaded with the actual release; local
preparation is not publication.
