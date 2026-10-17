# Russian catalog completion

## Scope

Completed 91 unfinished Russian entries covering connection settings, Torrent
status/actions, chat, configuration migration, appearance and diagnostic logs.
Previously finished entries and other locales are unchanged.

Peer counts use a count label and days use the abbreviation `дн.` to avoid
incorrect Russian endings in these non-plural source strings. Mbit/s is
localized as the equivalent `Мбит/с`; IPv6 and file-format identifiers remain
literal. No runtime behavior or plural-handling API is changed.

Published archives, signed feeds, installed application and live profile are
untouched. These translations require a future application build.

## Verification

- Before edits, focused tests failed on 91 unfinished entries and three
  overlapping rich-text checks.
- The focused compilation/rich-help checks passed after translation.
- Qt `lrelease -nounfinished` and `lconvert` check every active Russian entry is
  finished, nonempty and survives compilation with its exact translated value.
- Scope checks preserve source strings, contexts, locations, placeholders,
  percent signs, newlines, HTML/links, IPv6 examples and file-filter syntax.
  Exactly 91 previously unfinished values changed.
- Disposable negative controls reverting an entry to unfinished and changing
  a help link each produced the expected single regression failure.
- Russian test wrappers reuse existing helpers without changing their behavior.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain separate.
- The full 41-test suite passed in 47.263 seconds without failures, errors or
  skips, using Qt translation tools 6.11.1.

Local evidence: `Work/qa/russian-completion-20261016/`.

## Remaining

Russian unfinished entries decrease from 91 to zero. The total across all 25
catalogs decreases from 8,818 to 8,727. These counts cover catalog contents, not
fresh full-source extraction or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future build remain separate tasks.
