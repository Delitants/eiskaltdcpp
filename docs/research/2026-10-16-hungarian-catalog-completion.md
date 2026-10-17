# Hungarian catalog completion

## Scope

Completed 141 unfinished Hungarian entries, including command help, rich
time-format help, download-queue retention guidance, migration and backup
notices, media metadata, chat and credits. Previously finished entries,
including the connection/TLS batch, and other locales are unchanged.

Published binaries, updater feeds, installed app and user profile are untouched.
These source translations require a future application build.

## Verification

- Initial focused regression run: 145 expected failures, covering 141 unfinished
  entries and overlapping rich-help/command checks.
- Full-catalog compilation/readback and rich-help focused checks pass. Active
  entries are finished, nonempty and survive compilation with exact text.
- Dedicated checks preserve command prefixes/arguments and newlines, rich
  HTML tags/attributes, time tokens and AM/PM variants.
- Scope checks confirm exactly 141 previously unfinished translations changed.
  Edit-time checks preserve source structure, markup, placeholders and newlines.
- Disposable unfinished-entry, damaged-link, time-token and command-token
  controls each produced one expected failure, without errors or skips.
- Full translation suite passed: 55 tests in 46.849 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Numeric time ranges
  were reviewed manually; native-speaker and rendered-layout review remain
  pending.

Local evidence: `Work/qa/hungarian-completion-20261016/`.

## Remaining

Hungarian unfinished entries decrease from 141 to zero. The overall catalog
backlog decreases from 8,119 to 7,978. These counts describe catalog contents,
not fresh full-source extraction coverage or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future application build remain separate tasks.
