# Slovak hub, filter and shortcut dialogs

## Scope

Completed 22 unfinished entries in SearchBlackListModel, ShortcutGetter,
UIFavoriteHubEditor, UIIPFilter, UIQuickConnect, UIScriptManager and
UISearchBlacklistDialog. The regression checks completion and compiled readback
for all 55 active entries, and exact wording for the 22 new translations.

IN/OUT are network directions, confirmed against IPFilterFrame.cpp. Quick-connect
help renders the source typo "IP address of DNS name" with the intended "or".
Active/passive modes, minimum interval in seconds and reload semantics are
preserved. Existing finished translations and non-translation XML are unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 22 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.464 seconds).
- Full translation suite passed (98 tests, 68.794 seconds, no skips).
- Independent scope comparison confirmed exactly 22 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, reversed-direction, missing-key, reversed-mode,
  wrong-time-unit and incorrect-reload-action controls each failed once, without
  errors or skips.
- `git diff --check` passed.
- Independent review found no issues and independently ran the focused test.
  Native-speaker and rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-dialogs-20261016/`.

## Remaining

Slovak unfinished entries decrease from 247 to 225; the overall catalog backlog
decreases from 6,785 to 6,763. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Preservation of previously finished wording
is checked by independent scope comparison, not exact regression assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
