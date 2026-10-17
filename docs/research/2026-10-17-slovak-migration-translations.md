# Slovak migration and shared UI messages

## Scope

Completed 12 unfinished QObject messages covering settings migration, backup
success/failure, image file filters and shared UI controls. The regression checks
all 15 active entries for completion and Qt compiled readback, and pins the 12
new translations. Migration retention/reset semantics, paragraph breaks,
placeholders and file-filter syntax are preserved.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 12 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.492 seconds).
- Full translation suite passed (102 tests, 54.773 seconds, no skips).
- Independent scope comparison confirmed exactly 12 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, missing-key, changed-placeholder, reversed-backup-
  failure, reversed-retention, changed-paragraph, broken-filter-separator and
  changed-wildcard controls each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-migration-20261017/`.

## Remaining

Slovak unfinished entries decrease from 156 to 144; the overall catalog backlog
decreases from 6,694 to 6,682. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
