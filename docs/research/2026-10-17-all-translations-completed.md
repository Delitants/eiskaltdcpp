# Translation catalog completion

## Result

Completed the entire remaining catalog backlog in one coordinated pass:

| Catalog Set | Catalogs Checked | Previously Unfinished Entries Completed | Remaining |
|---|---:|---:|---:|
| Qt interface | 25 | 6,351 | 0 |
| DC++ core gettext | 25 | 2,075 | 0 |
| GTK interface gettext | 24 | 10,580 | 0 |
| Total | 74 | 19,006 | 0 |

The GTK total includes 972 English source-language entries, explicitly populated
with their English text. Product names, protocol names, units and conventional
loanwords remain unchanged where appropriate. Obsolete catalog records are
excluded from completion counts.

Seven Qt catalogs changed: Danish, Brazilian Portuguese, Simplified Chinese,
Basque, Vietnamese, Greek and Interlingue (Occidental). All previously incomplete
core and GTK catalogs were also completed. Existing finished translations,
source keys, comments, locations and surrounding metadata were preserved.

## Verification

- Full translation regression suite: 124 tests passed in 91.305 seconds,
  with no skips.
- All 52,150 active Qt entries passed `lrelease -nounfinished` compilation and
  `lconvert` roundtrip comparison across all 25 GUI catalogs.
- All 49 gettext catalogs are covered by `msgfmt --check --check-format`.
- The completion guard checks nonblank Qt translations and all required gettext
  plural forms, and rejects fuzzy non-header gettext records.
- Gettext records are first canonicalized by GNU `msgcat`, so adjacent records
  without blank separators are handled by gettext's own parser.
- Eight disposable negative controls correctly reject unfinished/empty Qt text,
  fuzzy gettext records, whitespace-only text, partially empty plurals, empty
  single/multiline entries and adjacent incomplete records.
- Independent comparison against the starting commit confirms exactly 19,006
  formerly unfinished entries changed, with previous translations and source
  inventory preserved. Checks include placeholders, HTML, CLI syntax where
  applicable, and GTK mnemonic markers.
- A scan of new translations found no unchanged long English prose fragments
  outside the English source-language catalog.
- `git diff --check` passed.

## Review Corrections

Independent cross-review identified and resolved:

- Upload-recipient wording in Vietnamese, Chinese, Basque, Interlingue and Greek,
  verified against `UploadManager::addConnection()`.
- Vietnamese GTK grant-slot wording, Chinese GTK duplicate accelerator display,
  and a Greek GTK upload-completion article.
- Missing GTK mnemonic markers introduced during translation reuse.
- Completion-check gaps for fuzzy records, whitespace, partial plurals and
  adjacent PO records. Failed checks and fixtures were retained before repair.

Compact `C` clear-button labels were retained, following the existing compact
control convention. Existing gettext template-header warnings were preserved;
they do not prevent compilation.

Local evidence and per-language work logs are under
`Work/qa/all-translations-20261017/`. This includes the starting inventory,
catalog report, structural audit, English-fragment scan, reviewer controls and
full regression output. Cross-review and automated checks do not replace
native-speaker or rendered-layout review.

## Remaining Release Work

The translation catalog backlog is closed. Language/layout review, platform QA,
dependency reproducibility, optional notarization and a new application build
incorporating the accumulated changes remain separate release work.
