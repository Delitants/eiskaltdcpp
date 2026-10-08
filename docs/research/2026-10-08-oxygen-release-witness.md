# Default Theme Oxygen Release Witnesses

This read-only audit covers 86 unchanged default-theme artwork rows in the
[legacy custody index](2026-10-08-legacy-icon-lineage.json): 82 PNGs and four
SVG arrows. The [matching index](2026-10-08-oxygen-release-witness.json) records
every row, current bytes, exact release-member matches and unresolved results.
No artwork, license declaration, provider, installed app or profile changed.

## Original Release Evidence

The retained [KDE Oxygen 6.0.0 archive](https://download.kde.org/Attic/oxygen-icons/oxygen-icons-6.0.0.tar.xz)
matches SHA-256 `28ec182875dcc15d9278f45ced11026aa392476f1f454871b9e2c837008e5774`
from [KDE's original release announcement](https://mail.kde.org/pipermail/kde-announce/2024-February/000347.html).
The [official archive index](https://download.kde.org/Attic/oxygen-icons/),
announcement bytes and original `AUTHORS`, `COPYING` and `COPYING.LIB` files are
pinned and retained. This establishes matching HTTPS archive/checksum custody;
it is not independently verified release-signature or historical-import proof.
No downloaded scripts or build rules were executed.

## Exact Coverage

| Covered App Images | Exact Encoded Match | Exact Raw RGBA Match | Unmatched |
| --- | ---: | ---: | ---: |
| 82 PNGs | 0 | 24 | 58 |
| Four SVGs, not rendered | 0 | Not attempted | 4 |
| Total | 0 | 24 | 62 |

All 24 matches preserve every pixel and dimension, including RGB values under
fully transparent pixels, despite different encoded PNG bytes. The table does
not collapse an unmatched result into a filename or visual-similarity guess.
For example, app `application-exit.png` matches upstream
`22x22/actions/system-shutdown.png`, and app `go-top.png` matches upstream
`22x22/actions/go-up.png`. All matching paths and payload/pixel hashes are in the
index. The four dropdown/submenu SVG arrows remain explicitly unmatched.

The archive has 6,330 regular PNG payloads: 6,164 compatible PNGs are decoded
without lossy depth reduction for exact 8-bit RGBA comparison. The remaining
166 16-bit PNGs retain encoded-byte evidence only, with explicit decoding status;
none contributes to the 24 verified matches. The app's own 16-bit
`ball_green.png` is retained as encoded-byte evidence only, with no match;
the other 81 app PNGs are decoded at their supported precision. The scan also
hashes 69 plain SVG and 5,020
compressed SVG payloads without rendering or decompressing them. All 2,492
archive links are counted but never followed; their aliases are not independent
payload comparisons. Files are read without extraction or archive-link traversal.

## Attribution Boundaries

The original archive contains editable vector artwork, but this audit does not
establish a matching editable source or rendering recipe per app PNG. It does
not identify the historical import release, original artist or certified license
for an individual file. Existing copyright declarations are unchanged. Raw
pixel equality is stronger than similarity but is not artistic authorship.

Together with the [Faenza witness](2026-10-08-faenza-release-witness.md), 25 of the
423 legacy-image paths now have exact archived release pixel witnesses. The
remaining 398 lack such witnesses in these two audits; broader artistic-origin,
preferred-source and notice closure remains open rather than being waived for
any of the 423 images. Old indexes and failed evidence are not rewritten.

## Verification And Remaining Gates

The preceding custody index fails the new Oxygen release-witness schema gate.
The new partial index is checked against independently pinned archive, metadata,
authorized source commit and preceding sealed index bytes. Controls cover
omitted/duplicate rows, SVG exclusion, forged matches, altered inputs/notices,
false complete coverage, unsafe archive paths and unsupported pixel precision.
Initial high-bit-depth acceptance and scanner-refusal failures are retained;
the scanner now records high-bit-depth encoded bytes without reducing precision.
The first exploratory result had 25 matches, but one depended on reducing both
the app's 16-bit `ball_green.png` and upstream 16-bit
`22x22/status/user-online.png` to eight bits. That match is excluded from the final
24-witness result; the original attempt remains in private evidence, not accepted
as exact-pixel proof.
Earlier immutable evidence and inherited source repairs are preserved.

All 38 controls pass, including current/source high-bit-depth handling and
explicit inclusion of the four SVGs. A bounded independent static review found
no actionable important findings in the audit scope. It did not execute tests,
notice or preservation checks and is not whole-branch or release approval.

Other artwork and dependency-resource/source/notice closure, reproducibility,
original Intel shutdown and wider runtime/native/translation QA remain pending.
Final whole-branch review and complete reviewed builds, production numbering,
signing/notarization/update acceptance, guarded installation and GitHub/wiki/
screenshots/prerelease publication/readback remain separate gates. Production
signed feeds remain last. Nothing was installed or published.
