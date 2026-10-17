# Bulgarian catalog completion

## Scope

Completed 184 unfinished Bulgarian entries, including command/time-format help,
queue-retention guidance, migration/backup notices, media metadata and credits.
Previously finished entries, including the connection/TLS batch, and other
locales are unchanged. Source/context/location content is unchanged.

Published binaries, updater feeds, installed app and user profile are untouched.
These translations require a future application build.

## Verification

- Initial focused run: 188 expected failures, covering 184 unfinished entries
  and overlapping rich-help/command assertions.
- Both focused tests pass. Active entries are finished, nonempty and survive
  Qt compilation/readback with exact translated values.
- Scope checks confirm exactly 184 formerly unfinished entries changed.
  Edit-time checks preserve structure, HTML, placeholders and newlines.
- Disposable unfinished-entry, damaged-link, time-token and command-token
  controls each produced one expected failure, without errors or skips.
- Full translation suite passed: 63 tests in 69.273 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Numeric ranges and
  semantic accuracy were reviewed manually, not asserted by these wrappers;
  native-speaker and rendered-layout review remain pending.

Local evidence: `Work/qa/bulgarian-completion-20261016/`.

## Remaining

Bulgarian unfinished entries decrease from 184 to zero. The overall catalog
backlog decreases from 7,746 to 7,562. These are catalog-content counts, not
fresh full-source extraction coverage or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and an updated application build remain separate tasks.
