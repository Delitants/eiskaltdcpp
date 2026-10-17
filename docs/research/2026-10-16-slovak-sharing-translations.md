# Slovak sharing translations

## Scope

Completed the seven remaining unfinished UISettingsSharing entries: add/remove,
hidden files, zero-size files, a numeric choice, own-list-only ADL search and
startup hashing delay. The regression checks all 39 active entries in the
section, including previously translated upload-slot and hashing settings.
Existing finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The section regression failed on all seven unfinished entries before edits.
- Focused Qt compilation/readback passed for all 39 active entries after edits.
- The test requires the seven source keys and active section count, checks
  finished/nonempty values and compiled equality throughout, and checks new
  translations for numeric values, line counts and key semantic wording.
- Independent scope checks confirm exactly seven formerly unfinished entries
  changed and non-translation XML remained unchanged.
- Review identified that substring assertions did not protect the complete
  hidden-file tooltip. A disposable option-off/option-on reversal escaped the
  original test; that failed control is retained. An exact full-tooltip assertion
  now protects every clause without changing the production translation.
- Disposable condition-reversal, unfinished-entry, inverted-hidden-file-exclusion,
  changed-negative-sentinel and broadened-ADL-scope controls each produced one
  expected failure without errors or skips after the test repair.
- Final focused Qt compilation/readback regression passed (1 test, 0.372 seconds).
- Final full translation suite passed (95 tests, 80.654 seconds, no skips).
- `git diff --check` passed.
- Independent follow-up review found no remaining issues. Native-speaker
  validation remains outstanding.

Local evidence: `Work/qa/slovak-sharing-20261016/`.

## Remaining

Slovak unfinished entries decrease from 305 to 298; the overall catalog backlog
decreases from 6,843 to 6,836. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
