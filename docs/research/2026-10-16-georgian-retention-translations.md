# Georgian download search and retention translations

## Scope

Completed the final 17 unfinished UISettingsDownloads entries: alternative
searches, queue matching, zero-byte/duplicate exclusions, retention of completed
queue entries, segmentation and simultaneous uploads. This context now has no
unfinished entries. Existing finished translations and non-translation XML
remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 17 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 85 tests in 71.512 seconds, without skips.
- `git diff --check` passed.
- Assertions check required source keys, active/nonempty values, compiled
  equality, numeric values, newline counts and bounded technical identifiers.
- Independent scope checks confirm exactly 17 formerly unfinished entries
  changed, non-translation XML remained unchanged, and UISettingsDownloads
  contains no unfinished translations.
- Disposable unfinished-entry, altered-unit, changed-default and removed-line-
  break controls each produced one expected failure without errors or skips.
- Independent static review found no actionable issues, including in retention
  versus physical file deletion. Native-speaker/layout validation remains pending.

Local evidence: `Work/qa/georgian-retention-20261016/`.

## Remaining

Georgian unfinished entries decrease from 153 to 136; the overall catalog backlog
decreases from 7,094 to 7,077. These are catalog counts, not full-source extraction
coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
