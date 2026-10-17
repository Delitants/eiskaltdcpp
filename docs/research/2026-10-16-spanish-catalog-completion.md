# Spanish catalog completion

## Scope

Completed 93 previously unfinished Spanish catalog entries covering connection
settings, Torrent status and actions, chat controls, configuration migration,
appearance and diagnostic logging. Other locales and previously finished
Spanish entries are unchanged.

The application source strings, contexts and locations are preserved. No
published archive, signed feed, installed application or profile is modified.
These translations require a future build to reach users.

## Verification

- Before editing translations, the focused tests failed on 93 unfinished
  entries plus the three corresponding rich-help checks.
- After translation, both focused tests passed. Qt `lrelease -nounfinished`
  and `lconvert` check every active Spanish entry is finished, nonempty and
  survives compilation with its exact translated value.
- Scope checks confirm all 93 edits were previously unfinished entries, with
  placeholders, newlines, markup/links, IPv6 addresses, units, technical names
  and file-filter syntax preserved.
- Disposable negative controls reverting one entry to unfinished and altering
  a help link each caused the expected single regression failure.
- French and Spanish use the same rich-help helper, with separate test entry
  points. The existing Ukrainian-specific time-format help check is unchanged.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout verification remain separate.
- Final full suite: 33 tests passed in 61.312 seconds, with no failures, errors
  or skips, using Qt translation tools 6.11.1.

Local evidence: `Work/qa/spanish-completion-20261016/`.

## Remaining

Spanish unfinished entries decrease from 93 to zero; the total across all 25
catalogs decreases from 9,215 to 9,122. These are catalog-content counts, not
fresh full-source extraction or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future build remain separate tasks.
