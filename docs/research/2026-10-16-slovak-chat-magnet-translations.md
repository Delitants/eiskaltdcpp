# Slovak chat and magnet translations

## Scope

Completed 28 unfinished entries in HubFrame, UIHubFrame, UIPrivateMessage and
UIMagnet. The regression covers all 167 active entries for completion and Qt
compiled readback, and pins the 28 new translations. Command names and arguments,
placeholders, shortcuts and line breaks are preserved. The magnet help retains
the enabled-autosearch condition and no-search-dialog behavior.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 28 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.407 seconds).
- Full translation suite passed (100 tests, 60.118 seconds, no skips).
- Independent scope comparison confirmed exactly 28 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, missing-key, changed-command, changed-placeholder,
  removed-newline, changed-shortcut and reversed-magnet-condition controls each
  failed once, without errors or skips.
- `git diff --check` passed.
- Independent review found no actionable issues. Native-speaker and rendered
  layout validation remain outstanding.

Local evidence: `Work/qa/slovak-chat-20261016/`.

## Remaining

Slovak unfinished entries decrease from 200 to 172; the overall catalog backlog
decreases from 6,738 to 6,710. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
