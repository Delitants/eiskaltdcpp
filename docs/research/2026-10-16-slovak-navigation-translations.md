# Slovak navigation and settings messages

## Scope

Completed 16 unfinished entries in FavoriteUsers, Notification, QueuedUsers,
Settings, SettingsLog, SideBarModel, ToolBar, UIUserCommands and WulforSettings.
The regression checks all 74 active entries for completion and Qt compiled
readback, and pins the 16 new translations. GUI-setting placeholders, Torrent
save-failure meaning and private-message meaning are preserved. Sidebar names
use the terminology established for the console and notes panel.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 16 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.327 seconds).
- Full translation suite passed (101 tests, 62.923 seconds, no skips).
- Independent scope comparison confirmed exactly 16 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, missing-key, changed-placeholder, failure-to-success
  and private-to-public controls each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-navigation-20261016/`.

## Remaining

Slovak unfinished entries decrease from 172 to 156; the overall catalog backlog
decreases from 6,710 to 6,694. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
