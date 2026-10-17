# Greek notification and sound settings

## Scope

Completed 20 unfinished UISettingsNotification entries. All 21 active entries in
the section are checked for completion and Qt compiled readback; the new 20 have
exact wording and newline assertions. Active/inactive window conditions, visible
window behavior, private-message sound scope and the external-player example are
preserved.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 20 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.265 seconds).
- Full translation suite passed (112 tests, 70.617 seconds, no skips).
- Independent scope comparison confirmed exactly 20 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Eight disposable controls (unfinished entry, missing key, inactive-window
  condition, visibility, private-message scope, unsupported-format condition,
  player path and newline) each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/greek-notifications-20261017/`.

## Remaining

Greek unfinished entries decrease from 424 to 404; the overall catalog backlog
decreases from 6,538 to 6,518. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
