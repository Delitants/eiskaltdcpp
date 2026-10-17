# Italian catalog completion

## Scope

Completed 204 unfinished Italian entries covering connection and TLS settings,
command help, media information, credits, Torrent status/actions, chat,
configuration migration, appearance and diagnostic logging. Previously finished
Italian entries, including the earlier `/away` repair, and other locales are
unchanged.

Published archives, signed feeds, installed application and live profile are
untouched. These source translations require a future application build.

## Verification

- Before edits, focused tests failed on 204 unfinished entries plus three
  overlapping rich-text checks.
- Focused compilation/rich-help checks passed after translation. Qt
  `lrelease -nounfinished` and `lconvert` check every active Italian entry is
  finished, nonempty and survives compilation with its exact translated value.
- Scope checks preserve source strings, contexts, locations, placeholders,
  percent signs, newlines, HTML/links, IPv6 examples and file-filter syntax.
  Exactly 204 previously unfinished values changed.
- Dedicated tests protect disable/allow/require TLS and untrusted-certificate
  wording, command prefixes/arguments, and rich-text time-format tokens.
- Five disposable negative controls (unfinished entry, swapped TLS policy,
  changed command, damaged time token, altered link) each caused the expected
  single regression failure.
- Full translation regression suite passed: 44 tests in 61.181 seconds with
  no failures, errors or skips, using Qt 6.11.1 translation tools.
- Independent static review found no actionable issues in the catalog and
  regression-test changes. Native-speaker and rendered-layout review remain
  pending.

Local evidence: `Work/qa/italian-completion-20261016/`.

## Remaining

Italian unfinished entries decrease from 204 to zero. The total across all 25
catalogs decreases from 8,727 to 8,523. These counts cover catalog contents, not
fresh full-source extraction or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future build remain separate tasks.
