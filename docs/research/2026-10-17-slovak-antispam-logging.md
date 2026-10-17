# Slovak anti-spam and logging settings

## Scope

Completed 10 unfinished entries in AntiSpam, UIAntiSpam and UISettingsLog.
The regression covers all 41 active entries for completion and Qt compiled
readback, and pins the 10 new translations, placeholders, HTML and line breaks.
Graylist/blacklist identity, logging persistence and pre-crash heartbeat retention
are preserved. The compact C clear-button label is retained for its 25px control.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 10 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.336 seconds).
- Full translation suite passed (108 tests, 78.289 seconds, no skips).
- Independent scope comparison confirmed exactly 10 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Seven disposable controls (unfinished entry, missing key, list identity,
  placeholder, help link, persistence and heartbeat timing) each failed once,
  without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-logs-20261017/`.

## Remaining

Slovak unfinished entries decrease from 28 to 18; the overall catalog backlog
decreases from 6,566 to 6,556. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
