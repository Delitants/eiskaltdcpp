# Greek history and personal settings

## Scope

Completed 15 unfinished entries in UISettingsHistory and UISettingsPersonal.
All 21 active entries in these sections are checked for completion and Qt compiled
readback; the new 15 have exact wording and newline assertions. Exit timing,
per-hub overrides, away-mode conditions, optional email sharing, upload-speed
guidance and units are preserved.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 15 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.668 seconds).
- Full translation suite passed (113 tests, 70.764 seconds, no skips).
- Independent scope comparison confirmed exactly 15 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Eight disposable controls (unfinished entry, missing key, exit timing,
  per-hub override, away condition, speed warning, rate unit and interval unit)
  each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/greek-personal-20261017/`.

## Remaining

Greek unfinished entries decrease from 404 to 389; the overall catalog backlog
decreases from 6,518 to 6,503. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
