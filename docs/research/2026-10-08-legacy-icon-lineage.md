# Legacy Icon Repository Lineage

This documentation-only audit covers the 440 resource paths previously grouped
as legacy icons in both retained split macOS candidates. The
[machine-readable index](2026-10-08-legacy-icon-lineage.json) pins current source
commit `c84e5207b` and historical candidate source commit `e325cb6cd`. Full commit
identities, resource/source paths, SHA-256 values, Git blob identities and latest
path-change commits at both refs are recorded per file. No artwork or license declaration was
changed, and neither historical app copy was rebuilt or edited.

## Exact Coverage

| Current Classification | Paths | Meaning |
| --- | ---: | --- |
| Approved Modern Classic replacements | 11 | One ICNS and ten app-brand PNGs match the approved generation manifest |
| Resource declarations | 5 | QRC source files, not independent artwork |
| Documentation | 1 | Torrent icon README, not artwork |
| Other artwork | 423 | Current bytes are unchanged from the historical candidates; external artistic origins remain unresolved |
| Total | 440 | Exact historical legacy-bucket coverage, not a count of original artists |

Both CPUs' original resource maps name the same 440 paths and bytes. Every
historical app resource matches its recorded SHA-256 and the historical Git
tree. Every current source file matches the current pinned Git tree. The eleven
changed paths match
[the Modern Classic manifest](../../data/icons/modern-classic-manifest.json),
not the obsolete candidate payload. The other 429 paths remain byte-identical.
The index does not imply that a new final application already contains these
current inputs.

## Attribution Boundaries

The index retains original paragraphs from
[the copyright inventory](../../full.copyrights.info.in.Debian.style) and records
the last matching existing paragraph for each path. This preserves the specific
Faenza, Haiku and Modern Classic entries instead of assigning every legacy path
the broader inherited LGPL entry. The recorded paragraphs are declarations
already present in this repository, not newly verified license conclusions.
The notice list is a conservative copy of the inventory's file paragraphs;
per-file stanza references distinguish the entries actually selected here.

Git records establish repository custody only. The latest commit touching a
path is not asserted to be its original artist, first upstream release or
external source archive. The audit does not follow renamed historical paths,
infer copyright ownership from a committer, or certify the 423 remaining images'
external origins. Even the generated replacement category does not establish
external rights to the reference artwork. Original reference attribution and
the generation record remain in
[the artwork provenance note](../../data/icons/READ_ME.txt).

The four Phosphor-derived themes are outside this legacy bucket and retain their
separate pinned MIT attribution. No inherited raster asset is reclassified as
Phosphor artwork. This is not legal certification, reproducible binary-build
proof, native minimum-OS testing or release acceptance.

## Verification Scope

The retained previous resource map fails the new lineage acceptance requirement
because it has no source-commit/blob/path-change schema. The replacement index
passes exact byte/path/Git readback and controls for omitted and duplicated
rows, escaped paths, altered current and historical bytes, wrong blobs/history,
wrong attribution, fabricated artist claims, stale replacement classification,
changed totals, altered notice paragraphs and changed input pins. A bounded
independent review identified acceptance of extra unvalidated provenance fields
and missing historical-ref latest-change identities. Four exact rejection REDs
precede the single repair pass: unknown fields and altered custody-only scope
are refused, and historical-ref latest-change commits/blobs are recorded and
verified. All 18 expanded controls pass. This is not a second independent review
of the correction or a final whole-branch review.

Existing immutable checkpoints and inherited source repairs are preserved.
Private audit scripts and logs are not release assets. This index is release
preparation, not a complete final source/notice archive or full-branch review.

## Remaining Gates

External origin evidence for 423 images, other dependency resources/source/
notices and reproducibility remain open. The original Intel shutdown refusal is
not rerun, waived or fixed by this audit. Broader Torrent/GUI/updater runtime,
native minimum-OS and translation QA, independent final review and complete
reviewed builds remain required. Production numbering, signing/notarization,
signed update acceptance, guarded installation, GitHub/wiki/screenshots and
prerelease publication/readback remain separate gates. Production signed feeds
remain last. Nothing was installed or published by this checkpoint.
