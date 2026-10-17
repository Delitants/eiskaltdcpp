# Georgian personal, history and notification translations

## Scope

Completed 21 unfinished entries in UISettingsPersonal, UISettingsHistory and
UISettingsNotification. The scope includes history retention/clearing, active
and inactive window notifications, media-player help, away responses, per-hub
overrides, upload-speed guidance and encoding.

Existing finished translations and non-translation XML remain unchanged.
Published binaries, updater feeds, installed app and profile are untouched;
these source changes require a future application build.

## Verification

- The required-key regression failed on all 21 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 80 tests in 74.376 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions check required context/source keys, active/nonempty translations,
  compiled equality, newline counts, technical identifiers and exact active/
  inactive notification choices. The standard unit Mbit/s stays untranslated.
- Independent scope checks confirm exactly 21 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, active-window inversion, changed-player-path and
  altered-unit controls each produced one expected failure without errors or skips.

Local evidence: `Work/qa/georgian-personal-20261016/`.

## Remaining

Georgian unfinished entries decrease from 255 to 234; the overall catalog backlog
decreases from 7,196 to 7,175. Counts describe catalog content, not full-source
extraction coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
