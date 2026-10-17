# Georgian main-window translations

## Scope

Completed 24 unfinished MainWindow entries covering navigation, away behavior,
connection warnings, Torrent prompts, nickname requirements, protocol details
and credits. Existing finished translations and non-translation XML are unchanged.
The author/year credit intentionally retains the source text.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 24 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 82 tests in 76.048 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions check required source keys, active/nonempty values, exact compiled
  readback, placeholders, HTML tags/attributes, leading whitespace and technical
  identifiers including platform names and credit attribution.
- Independent scope checks confirm exactly 24 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, changed-placeholder, altered-link and removed
  leading-space controls each produced one expected failure without errors or skips.

Local evidence: `Work/qa/georgian-mainwindow-20261016/`.

## Remaining

Georgian unfinished entries decrease from 217 to 193; the overall catalog backlog
decreases from 7,158 to 7,134. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
