# Georgian hub-chat translations

## Scope

Completed 33 unfinished HubFrame entries, including command descriptions,
favorites, join/offline messages, password/redirect status, counts and warnings.
Command names and argument syntax remain unchanged. The IPv6 label intentionally
retains its standard spelling. Existing finished translations and all
non-translation XML content remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 33 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 86 tests in 72.208 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions check required source keys, active/nonempty values, compiled
  equality, placeholders, leading/trailing whitespace, newlines, command prefixes,
  argument syntax, magnet action numbers and Lua identifiers.
- Independent scope checks confirm exactly 33 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, changed-command, altered-placeholder,
  removed-leading-space and changed-magnet-action controls each produced one
  expected failure without errors or skips.

Local evidence: `Work/qa/georgian-hub-chat-20261016/`.

## Remaining

Georgian unfinished entries decrease from 136 to 103; the overall catalog backlog
decreases from 7,077 to 7,044. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
