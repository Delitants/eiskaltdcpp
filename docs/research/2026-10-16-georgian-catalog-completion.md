# Georgian catalog completion

## Scope

Completed the final 87 unfinished Georgian entries, covering GUI migration
warnings, themes, chat behavior, shortcuts, diagnostics, logging and time-format
help. Previously finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- Initial two-test regression produced 89 expected failures: 87 unfinished
  catalog entries and the incomplete rich/time-help checks.
- Focused whole-catalog Qt compilation/readback and rich-help/command tests passed.
- Full translation suite passed: 89 tests in 76.650 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Edit-time checks preserved XML structure, HTML tags/attributes, placeholders
  and newline counts.
- Independent scope verification confirms exactly 87 formerly unfinished entries
  changed and non-translation XML remained unchanged.
- Disposable unfinished-entry, time-token and help-link controls each produced
  one expected failure without errors or skips.
- The initial help-link control matched both active and obsolete messages; its
  failed harness run is retained. The selector was scoped to the active message
  without modifying the production catalog or test.

Local evidence: `Work/qa/georgian-completion-20261016/`.

## Remaining

Georgian unfinished entries decrease from 87 to zero; the overall catalog backlog
decreases from 7,028 to 6,941. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes other translations, native-speaker/layout review,
minimum-OS and physical Intel testing, wider platform QA, dependency
reproducibility, optional notarization and an updated application build.
