# German catalog completion

## Scope

Completed 122 unfinished German entries covering connection settings, progress
messages, chat and Torrent controls, notes, configuration migration, appearance
and diagnostic logging. The existing April 1 quit prompt was translated without
changing its behavior. Other locales and previously finished German entries
are unchanged.

Published archives, updater feeds, installed application and live profile are
untouched. These translations require a future application build.

## Verification

- Before catalog edits, the focused tests failed on 122 unfinished entries
  plus three corresponding rich-text checks.
- Both focused tests passed after translation. Qt `lrelease -nounfinished`
  and `lconvert` verify all active German entries are finished, nonempty and
  survive compilation with their exact translated values.
- Scope checks confirm exactly 122 previously unfinished values changed, with
  sources, contexts, locations, HTML/links, newlines, placeholders, literal
  percent signs, IPv6 examples, technical names and file filters preserved.
- Disposable negative controls reverting an entry to unfinished and changing
  a help link each produced one expected regression failure.
- The added test entry points reuse existing compilation and rich-help checks;
  no shared helper behavior is changed.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain separate.
- The full 35-test suite passed in 46.183 seconds without failures, errors or
  skips, using Qt translation tools 6.11.1.

Local evidence: `Work/qa/german-completion-20261016/`.

## Remaining

German unfinished entries decrease from 122 to zero. The total across all 25
catalogs decreases from 9,122 to 9,000. These are catalog-content counts, not a
fresh full-source extraction or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future build remain separate tasks.
