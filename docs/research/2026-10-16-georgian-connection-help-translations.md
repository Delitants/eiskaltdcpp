# Georgian connection help translations

## Scope

Completed 16 unfinished entries in SettingsConnection and UISettingsConnection:
six network-mode explanations, port privilege warning, override restrictions,
IP updates, throttling/reconnect labels and rich-text Type-of-Service help.
Existing finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The regression failed on all 16 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 87 tests in 76.256 seconds, without skips.
- `git diff --check` passed.
- Required short keys and unique long-help prefixes ensure missing help is not
  silently skipped. Assertions verify active/nonempty values, compiled equality,
  numbers, protocol identifiers, HTML tags/attributes, newlines and trailing space.
- Rich-help checks explicitly preserve the at-most-one-TOS-value rule and
  superuser warning. Other semantic caveats received static review.
- Independent scope checks confirm exactly 16 formerly unfinished entries
  changed and non-translation XML remained unchanged.
- Disposable unfinished-entry, altered-port-limit, changed-link and removed-
  privilege-warning controls each produced one expected failure without errors
  or skips.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.

Local evidence: `Work/qa/georgian-connection-help-20261016/`.

## Remaining

Georgian unfinished entries decrease from 103 to 87; the overall catalog backlog
decreases from 7,044 to 7,028. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
