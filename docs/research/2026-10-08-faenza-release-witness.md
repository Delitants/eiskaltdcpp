# Faenza Archived Release Witness

This read-only check covers the 82 unchanged Faenza PNG rows in the
[legacy custody index](2026-10-08-legacy-icon-lineage.json). It does not replace
that historical record, change artwork or license declarations, or claim that
all 423 outstanding artistic origins have been established. The
[bounded matching index](2026-10-08-faenza-release-witness.json) records every
covered file, its current bytes and raw RGBA digest, the exact release member
matches, source archive hashes, notice hashes and unresolved results.

## Original Release Inputs

The original project's [Google Code archive](https://code.google.com/archive/p/faenza-icon-theme/)
retains [download metadata](https://storage.googleapis.com/google-code-archive/v2/code.google.com/faenza-icon-theme/downloads-page-1.json)
and [project metadata](https://storage.googleapis.com/google-code-archive/v2/code.google.com/faenza-icon-theme/project.json).
Each retrieved archive matches that download record's size and SHA-1; the
stronger SHA-256 pins below bind the retained bytes. This is HTTPS archive
custody, not independently authenticated release-signature evidence.

| Archived Input | SHA-256 |
| --- | --- |
| [Faenza 0.9 package](https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/faenza-icon-theme/faenza-icon-theme_0.9.tar.gz) | `15ce984ce4a32231289fac35b64e74544dbcd4515c5fa91553c7f6bf3f3d3fff` |
| [Faenza 1.3 release](https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/faenza-icon-theme/faenza-icon-theme_1.3.zip) | `d4486fda0413f8a81a87e0dd2329f50f2a8a7cb4147b48cf147f0160add8174a` |
| [Faenza 1.3 source artwork](https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/faenza-icon-theme/faenza-sources_1.3.tar.gz) | `b0864925cbbefe7210cd13e6cf954aae699eb7818cc6141e6ac120ca83c1c8f2` |

Original `AUTHORS`, `COPYING` and `README` bytes from both packaged releases are
retained unmodified in private evidence. The 1.3 source archive has 2,945 regular
SVG members; retaining those sources is not proof that a specific SVG generated
an app PNG. No downloaded installer, uninstall script or other code was run.

## Exact Result

| Match Class | Covered App Files |
| --- | ---: |
| Identical encoded PNG bytes | 0 |
| Identical untransformed raw RGBA pixels | 1 |
| No match in the inspected regular PNG payloads | 81 |
| Total | 82 |

`eiskaltdcpp-qt/icons/appl/faenza/network-disconnect.png` matches all 24-by-24
RGBA pixels of `Faenza/status/24/network-offline.png` in the 0.9 package, despite
different encoded PNG bytes. No app image matches the 1.3 package. The scan
decodes 4,190 regular PNG payloads from 0.9 and 6,416 from all seven nested 1.3
theme archives, retaining every exact match rather than choosing by name.
The three regular PNG animation strips in the 1.3 editable-source archive are
also inspected; none adds an app-image match.

Comparison includes RGB values under fully transparent pixels. It performs no
resizing, recoloring, compositing, hidden-RGB normalization, perceptual comparison
or similarity threshold. Symlinks and hardlinks are counted but never followed;
their aliases are not independent scanned PNG payloads. All archives are read
in memory without extracting or traversing archive links.

An exact pixel witness is not the historical import release, first creator,
authorship certification or proof of regeneration from preferred editable source.
The 0.9 release postdates the initial Faenza import in this repository. The
existing attribution inventory is unchanged. The 81 unmatched Faenza images and
341 images in other legacy groups still lack an exact external release witness
in this audit; broader artistic-origin and preferred-source questions remain open
for the complete 423-image group.

## Verification And Remaining Work

The previous custody-only index fails the new release-witness schema gate. The
new partial-result index passes exact archive, notice, current-file and pixel
readback, including controls that refuse fabricated matches, altered members,
false complete-coverage claims, tampered archives, omitted notices, extra
authorship fields and unsafe archive paths. Earlier immutable evidence and all
inherited source repairs remain preserved.

One bounded independent static review found that standalone validation could
trust an adjusted baseline manifest and source-commit label. Two exact rejection
REDs precede the repair: the authorized commit and preceding sealed index hash
are now bound independently of that manifest. An additional RED covers omission
of the source archive's three PNG strips. All 32 final controls pass. This is
not a second independent review or a final whole-branch review.

Other artwork and dependency-resource/source/notice closure, binary
reproducibility, original Intel shutdown and broader runtime/native/translation
QA remain pending. Final whole-branch review and complete reviewed builds,
production numbering/signing/notarization/update acceptance, guarded install and
GitHub/wiki/screenshots/prerelease publication/readback remain separate gates.
Production signed feeds remain last. Nothing was installed or published.
