# Slovak appearance and chat settings

## Scope

Completed 16 unfinished UISettingsGUI entries covering icons, window behavior,
chat settings, units and transfer colors. The regression requires all 16 keys,
pins exact wording and checks Qt compiled readback. The timestamp-format HTML
help remains unfinished and outside this batch.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 16 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.278 seconds).
- Full translation suite passed (107 tests, 49.575 seconds, no skips).
- Independent scope comparison confirmed exactly 16 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Eight disposable controls (unfinished entry, missing key, icon negation,
  restart requirement, unit spelling, close behavior, icon mode and unread/read
  distinction) each failed once, without errors or skips.
- `git diff --check` passed.
- Independent review found no actionable issues, confirmed Reset's color-only
  handler and unit distinctions, and independently passed the full suite plus
  four in-memory negative controls. Native-speaker/layout QA remains outstanding.

Local evidence: `Work/qa/slovak-appearance-20261017/`.

## Remaining

Slovak unfinished entries decrease from 44 to 28; the overall catalog backlog
decreases from 6,582 to 6,566. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
