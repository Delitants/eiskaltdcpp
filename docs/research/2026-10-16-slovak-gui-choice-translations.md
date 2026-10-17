# Slovak GUI choices and chat-picture storage

## Scope

Completed 25 unfinished SettingsGUI entries: language names, icon themes,
chat-picture storage and automatic cleanup. The regression checks all 48 active
entries for completion and compiled Qt readback, with exact wording for the 25
new translations. Serbian script variants, age comparison and suffix spacing
are explicitly covered.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 25 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.445 seconds).
- Full translation suite passed (99 tests, 61.358 seconds, no skips).
- Independent scope comparison confirmed exactly 25 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, missing-key, wrong-script, reversed-age,
  missing-leading-space and wrong-time-unit controls each failed once, without
  errors or skips.
- `git diff --check` passed.
- Independent review found no actionable issues. Qt tools were available in
  this run; tests skip their compile checks when those tools are absent.
- The day suffix follows the existing fixed-text QSpinBox design, not dynamic
  pluralization. Number-sensitive grammar remains part of language/layout QA.

Local evidence: `Work/qa/slovak-gui-20261016/`.

## Remaining

Slovak unfinished entries decrease from 225 to 200; the overall catalog backlog
decreases from 6,763 to 6,738. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
