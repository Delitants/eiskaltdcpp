# Slovak hubs, search and browser translations

## Scope

Completed 35 unfinished entries covering hub-list cache/download status, chat
image selection, search and blacklist actions, share restrictions, file counts
and user-role labels. Existing finished translations and non-translation XML
remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 35 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 93 tests in 84.899 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions check required source/context keys, active/nonempty values,
  compiled equality, placeholders, percent counts, HTML, trailing whitespace,
  technical labels and exact restriction/user-role wording.
- Independent scope checks confirm exactly 35 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, changed-placeholder, swapped-role, damaged-markup
  and removed-trailing-space controls each produced one expected failure without
  errors or skips.

Local evidence: `Work/qa/slovak-browser-20261016/`.

## Remaining

Slovak unfinished entries decrease from 355 to 320; the overall catalog backlog
decreases from 6,893 to 6,858. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
