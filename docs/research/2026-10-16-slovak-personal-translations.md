# Slovak personal, history and notification translations

## Scope

Completed 15 unfinished entries in UISettingsPersonal, UISettingsHistory and
UISettingsNotification. Two existing active/inactive-window notification labels
are unchanged but now explicitly covered by the new regression.
All previously finished translations and non-translation XML are unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 15 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 94 tests in 80.208 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions cover 17 required keys, active/nonempty values, compiled equality,
  newline counts, bounded technical tokens and exact notification/clear actions.
- Independent scope checks confirm exactly 15 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, active-window inversion, altered-unit and removed-
  line-break controls each produced one expected failure without errors or skips.

Local evidence: `Work/qa/slovak-personal-20261016/`.

## Remaining

Slovak unfinished entries decrease from 320 to 305; the overall catalog backlog
decreases from 6,858 to 6,843. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
