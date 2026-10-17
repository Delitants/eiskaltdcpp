# Georgian slow-source translations

## Scope

Completed 19 unfinished UISettingsDownloads entries for automatic slow-source
handling, timings, minimum sizes/source counts, file-list exceptions and the
option to disconnect sources without removing them from the queue.
Previously finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 19 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Assertions check required source keys, active/nonempty values, compiled
  equality, numeric values, newline counts, unit boundaries and exact
  disconnect-only labels/help.
- Independent scope checks confirm exactly 19 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- The initial unit control exposed a test gap: substring matching accepted
  KiB/s where B/s was required. That failed control run is retained. The new
  test now checks ASCII-letter/slash boundaries around technical tokens.
- After that repair, disposable unfinished-entry, queue-removal inversion,
  changed-timing and altered-unit controls each produced one expected failure
  without errors or skips. The focused test also passed again.
- The initial full-suite run passed 84 tests in 79.024 seconds. Because it had
  started before the unit assertion changed, a fresh full run was performed.
- The final full suite passed: 84 tests in 73.702 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable translation issues and accepted
  the test repair on follow-up review. Native-speaker/layout validation is pending.

Local evidence: `Work/qa/georgian-slow-sources-20261016/`.

## Remaining

Georgian unfinished entries decrease from 172 to 153; the overall catalog backlog
decreases from 7,113 to 7,094. These are catalog counts, not full-source extraction
coverage or native-speaker certification. Remaining download settings include
automatic alternative searches and queue retention.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
