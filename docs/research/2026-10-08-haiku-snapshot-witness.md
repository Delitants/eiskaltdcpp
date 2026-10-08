# Haiku Theme Export-Snapshot Witness

This read-only audit covers all 82 unchanged Haiku-theme artwork rows in the
[legacy custody index](2026-10-08-legacy-icon-lineage.json). The
[machine-readable result](2026-10-08-haiku-snapshot-witness.json) records every
app image, its current bytes and exact comparison outcome. No artwork, license
declaration, runtime code, provider or installed application was changed.

## Pinned External Snapshot

The [Haiku icon export repository](https://github.com/darealshinji/haiku-icons/tree/ccf434a0cf31aae47cc8aac91934651619c1b9b2)
is retained at commit `ccf434a0cf31aae47cc8aac91934651619c1b9b2`.
The HTTPS snapshot archive has SHA-256
`36869263c1791efee632cb9a7700f1fb6745bc9d91580a1385b2d7762a4a23db`.
Its complete file/mode Git tree is independently recomputed as
`46615009347fe362808db2de385ee1340e26cc73`, matching the pinned commit metadata.
This binds the archive's 4,426 regular files to that recorded snapshot tree,
not to an independently verified signature or historical import release.

Original `LICENSE` and `README.md` bytes and hashes are retained. The
[snapshot README](https://github.com/darealshinji/haiku-icons/blob/ccf434a0cf31aae47cc8aac91934651619c1b9b2/README.md)
describes Icon-O-Matic exports, identifies a Haiku source commit and warns that
some exports may be incorrect. Its declared upstream commit
`49f62bc1728ce870478ae4bdb82201f0ee0c204b` is recorded as a declaration only;
that original Haiku source tree and export recipe were not independently
verified or executed. The existing repository's Haiku attribution is unchanged.

## Exact Comparison Outcome

| App Images | Exact Encoded Matches | Exact Raw RGBA Matches | Unmatched |
| --- | ---: | ---: | ---: |
| 82 PNGs | 0 | 0 | 82 |

The archive contains 2,217 PNGs, all decoded at supported precision without
resizing, recoloring or similarity thresholds. The comparison preserves
dimensions, alpha and RGB values under fully transparent pixels. Generic
16-bit controls require encoded-byte evidence only, without depth reduction.
No SVG or HVIF source is rendered to manufacture a match.

The app images include 65 at 22x22, twelve at 16x16, two 550x22 sprite strips,
and one each at 128x128, 46x55 and 64x64. The export pack's main PNG directories
contain 16x16, 32x32 and 64x64 icons. These dimensions help explain why a filename
or theme-name comparison is insufficient; they do not prove derivation or its
absence. This snapshot may differ from the actual donor artwork or export path.

No exact external witness is added by this scan. Together with the earlier
Faenza/Oxygen audits, 25 of the 423 legacy-image paths retain exact release pixel
witnesses; 398 lack such witnesses in these three audits. Broader artistic origin,
historical import, preferred editable source and notice closure remain open for
all 423 rather than being waived by these counts.

## Verification And Limits

The preceding custody index fails exactly the new external-snapshot schema
requirement. Forty-eight named controls pass, covering complete rows, forged
matches/claims, original-source declaration boundaries, input/notice/tree pins,
hidden RGB/alpha, high-bit-depth refusal, unsafe archive paths, duplicate/special/
symlink/setuid members, file-mode sensitivity and archive-size limits.
The archive is read without extraction or following links; downloaded code is
not executed. Earlier immutable evidence and inherited source repairs are preserved.

A bounded independent static review identified missing positive-match controls,
unchecked retained notice copies in the audit verifier and duplicate JSON fields
that could hide contradictory claims. Two private matcher-omission controls and
four exact rejection REDs precede one correction pass: positive byte/pixel
fixtures are sensitive to a disabled matcher, original notice copies are checked,
and duplicate fields are rejected recursively. Final controls include those six
cases. The review did not execute tests or verify parent preservation receipts;
final readback is performed separately, not asserted as a second review.
An initial parent verification was invoked before the final control receipt
existed; that orchestration failure is retained and excluded from acceptance.
Completed test receipts are collected before the final preservation readback.

This negative matching result is not a licensing judgment, proof of unrelated
artwork, preferred-source regeneration, complete provenance or release acceptance.
Historical Intel/bind causes, broader runtime/native/minimum-OS/language QA,
other material/source/notice closure and reproducibility remain pending. Final
whole review and complete reviewed builds, signing/notarization/update acceptance,
guarded installation and GitHub/wiki/screenshots/prerelease publication/readback
remain separate gates. Production signed feeds last. Nothing installed or published.
