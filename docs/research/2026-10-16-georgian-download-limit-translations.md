# Georgian download limits and priorities

## Scope

Completed 21 unfinished UISettingsDownloads entries for simultaneous-download
limits, unfinished-download storage, public hub lists and their separate HTTP
proxy, automatic priority thresholds and the lowest-priority option.
Previously finished translations and all non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 21 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 83 tests in 67.472 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions check required source keys, active/nonempty values, compiled
  equality, numeric values, line/percent counts, technical units and exact
  highest/high/normal/low priority labels.
- Independent scope checks confirm exactly 21 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, highest/high confusion, changed-default-size and
  altered-unit controls each produced one expected failure without errors or skips.

Local evidence: `Work/qa/georgian-download-limits-20261016/`.

## Remaining

Georgian unfinished entries decrease from 193 to 172; the overall catalog backlog
decreases from 7,134 to 7,113. These are catalog counts, not full-source extraction
coverage or native-speaker certification. The remaining download settings include
slow-source handling, automatic alternative searches and queue retention.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
