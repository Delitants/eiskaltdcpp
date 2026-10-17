# Slovak main-window actions and prompts

## Scope

Completed 24 unfinished MainWindow entries covering navigation, speed limits,
magnet/Torrent opening, nickname requirements and status labels. The regression
requires these exact 24 keys and checks finished state, exact translation,
placeholders and Qt compiled readback. The remaining 44 unfinished MainWindow
entries, including About and credits, are intentionally outside this batch.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 24 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.351 seconds).
- Full translation suite passed (103 tests, 63.913 seconds, no skips).
- Independent scope comparison confirmed exactly 24 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, missing-key, changed-placeholder, reversed-readiness,
  changed-file-extension and removed-status-separator controls each failed once,
  without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Qt tools were
  available; native-speaker and rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-main-actions-20261017/`.

## Remaining

Slovak unfinished entries decrease from 144 to 120; the overall catalog backlog
decreases from 6,682 to 6,658. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
