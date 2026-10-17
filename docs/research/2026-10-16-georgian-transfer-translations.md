# Georgian transfer translations

## Scope

Completed 14 unfinished entries in TransferView, TransferViewModel, UIFileHasher
and UIFinishedTransfers. These cover protocol and Torrent actions, transfer
states and counters, file-hash search help and the completed-transfer filter.
Existing finished translations and non-translation XML are unchanged.

Published binaries, updater feeds, installed app and profile remain unchanged;
these source changes require a future application build.

## Verification

- The new required-key regression failed on all 14 unfinished entries before
  editing and passed after translation with Qt compilation/readback.
- Full translation suite passed: 77 tests in 71.732 seconds, without skips.
- `git diff --check` passed.
- The test verifies active/nonempty translated values, required context/source
  keys, placeholder multiplicity, literal percent counts and eight exact labels.
- Independent scope checks confirm exactly 14 formerly unfinished entries
  changed, with non-translation XML unchanged.
- Disposable unfinished-label, changed-placeholder and reversed-direction
  controls each produced one expected failure, without errors or skips.
- Initial control selectors matched reused text or a longer status string;
  those failed harness runs are retained. Selectors were narrowed to the exact
  context and translation element without changing production code.
- Independent static review found no actionable issues. This does not certify
  native-speaker accuracy or rendered layout.

Local evidence: `Work/qa/georgian-transfers-20261016/`.

## Remaining

Georgian unfinished entries decrease from 308 to 294; overall unfinished entries
decrease from 7,249 to 7,235. These are catalog counts, not full-source extraction
coverage or linguistic certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
