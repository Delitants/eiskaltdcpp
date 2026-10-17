# Belarusian catalog completion

## Scope

Completed the remaining 109 unfinished Belarusian entries, including migration
and backup notices, Torrent/chat controls, diagnostics and rich time-format
help. Previously finished entries, including the connection-settings batch,
are unchanged. Source/context/location content and other locales are unchanged.

Published binaries, updater feeds, installed application and user profile are
untouched. Source translations require a future application build.

## Verification

- Initial focused run: 113 expected failures (109 unfinished entries plus
  overlapping rich-help and time-format checks).
- Full-catalog compilation/readback and rich-help focused tests pass. Active
  entries are finished, nonempty and survive Qt compilation with exact values.
- Dedicated checks protect time-format tokens, AM/PM variants and HTML tags.
- Scope checks confirm exactly 109 formerly unfinished entries changed.
  Edit-time checks preserve placeholders, markup and newline counts.
- Three disposable negative controls (unfinished entry, damaged help link,
  damaged time token) each caused one expected failure without errors or skips.
- Full translation suite passed: 51 tests in 49.021 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/belarusian-completion-20261016/`.

## Remaining

Belarusian unfinished entries decrease from 109 to zero. Across all 25 catalogs,
the count decreases from 8,252 to 8,143. These are catalog-content counts, not
fresh full-source extraction coverage or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future app build remain separate tasks.
