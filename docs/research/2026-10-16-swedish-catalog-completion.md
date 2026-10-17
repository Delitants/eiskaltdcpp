# Swedish catalog completion

## Scope

Completed 91 unfinished Swedish entries covering connection settings, Torrent
status/actions, chat, configuration migration, appearance and diagnostic logs.
Other locales and previously finished Swedish entries are unchanged.

Published archives, signed updater feeds, installed application and live profile
are untouched. These source translations require a future application build.

## Verification

- Before catalog edits, focused tests failed on 91 unfinished entries plus
  three corresponding rich-text checks.
- After translation, both focused tests passed. Qt `lrelease -nounfinished`
  and `lconvert` verify every active Swedish entry is finished, nonempty and
  survives compilation with its exact translated value.
- Scope checks preserve source strings, contexts, locations, placeholders,
  literal percent signs, newlines, HTML/links, IPv6 examples, technical names,
  units and file-filter syntax. Exactly 91 previously unfinished values changed.
- Disposable negative controls reverting one entry to unfinished and changing
  a help link each produced the expected single regression failure.
- Swedish test entry points reuse the existing completeness and rich-help
  helpers without changing their behavior.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain separate.
- The full 37-test suite passed in 49.650 seconds without failures, errors or
  skips, using Qt translation tools 6.11.1.

Local evidence: `Work/qa/swedish-completion-20261016/`.

## Remaining

Swedish unfinished entries decrease from 91 to zero. The total across all 25
catalogs decreases from 9,000 to 8,909. These are catalog-content counts, not
fresh full-source extraction or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future build remain separate tasks.
