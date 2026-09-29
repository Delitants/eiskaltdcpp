# Release Source And Notice Inventory

This records the source and notice inventory for the staged macOS ARM64
`3.0.0-pre.1` candidate. It is not a legal certification or a claim about other
platforms. Verify the final distributed artifacts and their checksums.

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

## macOS Candidate Materials

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

## Final Distribution Gate

For the final artifact, enumerate dynamic frameworks/libraries, static linked
components, plugins and data such as dictionaries/GeoIP. Record exact versions,
source provenance and notice files. Bundle matching notices and prepare source
materials as applicable, then verify the archive contents. Preserve dependency
notices rather than assuming this document, a package-manager license label or
the top-level GPL text substitutes for them. Staged materials must survive the
final archive/signature checks and be uploaded with the actual release; local
preparation is not publication.
