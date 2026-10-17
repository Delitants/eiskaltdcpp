# Georgian sharing translations

## Scope

Completed 21 unfinished UISettingsSharing entries covering share exclusions,
hidden files, directory management, FastHash options, ADL search, upload slots,
hashing limits and automatic refresh. Existing finished translations and all
non-translation XML content remain unchanged.

Published binaries, updater feeds, installed app and profile are unchanged.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 21 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 79 tests in 78.619 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout review remain pending.
- Assertions cover required source keys, active/nonempty values, exact readback,
  newline counts, trailing whitespace, signed numeric values and technical units.
- Independent scope checks confirm exactly 21 formerly unfinished entries
  changed and non-translation XML remained unchanged.
- Disposable unfinished-entry, changed-numeric-limit, altered-unit and removed
  line-break controls each produced one expected failure, without errors or skips.

Local evidence: `Work/qa/georgian-sharing-20261016/`.

## Remaining

Georgian unfinished entries decrease from 276 to 255; the overall catalog backlog
decreases from 7,217 to 7,196. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
