# Slovak transfer translations

## Scope

Completed 24 unfinished entries in TransferView, TransferViewModel, UIFileHasher
and UIFinishedTransfers. The scope covers transfer columns/actions, Torrent
states, download/upload counters, clipboard links and completed-transfer filters.
Existing finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 24 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 92 tests in 81.007 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions check required source/context keys, active/nonempty values,
  compiled equality, placeholder multiplicity, literal percent counts, trailing
  whitespace and exact action/state/direction labels.
- Independent scope checks confirm exactly 24 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, reversed-direction, changed-placeholder and
  removed-trailing-space controls each produced one expected failure without
  errors or skips.

Local evidence: `Work/qa/slovak-transfers-20261016/`.

## Remaining

Slovak unfinished entries decrease from 379 to 355; the overall catalog backlog
decreases from 6,917 to 6,893. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
