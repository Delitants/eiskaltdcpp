# Turkish catalog completion

## Scope

Completed 91 unfinished Turkish entries covering connection settings, Torrent
status/actions, chat, configuration migration, appearance and diagnostic logs.
Other locales and previously finished Turkish entries are unchanged.

Published archives, signed feeds, installed application and live profile are
untouched. These source translations require a future application build.

## Verification

- Before edits, focused tests failed on 91 unfinished entries and three
  overlapping rich-text checks.
- Both focused tests passed after translation. Qt `lrelease -nounfinished`
  and `lconvert` verify every active Turkish entry is finished, nonempty and
  survives compilation with its exact translated value.
- Scope checks preserve source strings, contexts, locations, placeholders,
  percent signs, newlines, HTML/links, IPv6 examples, technical names, units and
  file-filter syntax. Exactly 91 previously unfinished values changed.
- Disposable negative controls reverting an entry to unfinished and changing
  a help link each produced the expected single regression failure.
- Turkish test wrappers reuse existing helpers without changing their behavior.
- Independent static review identified ambiguous connect-versus-bind wording
  in two IPv6 labels. Both now explicitly describe local IPv6 binding. No other
  actionable findings were reported; native-speaker/layout review remains.
- After review corrections, the final 39-test suite passed in 49.259 seconds
  without failures, errors or skips, using Qt translation tools 6.11.1.

Local evidence: `Work/qa/turkish-completion-20261016/`.

## Remaining

Turkish unfinished entries decrease from 91 to zero. The total across all 25
catalogs decreases from 8,909 to 8,818. These counts cover catalog contents, not
fresh full-source extraction or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future build remain separate tasks.
