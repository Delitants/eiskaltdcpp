# Polish connection policy help

## Scope

Completed eight remaining UISettingsConnection entries: override prohibition,
Type-of-Service label/options and rich help, and DHT port. Existing finished
entries, source/context/location content and other locales are unchanged.

Published binaries, update feeds, installed app and user profile are untouched.
These translations require a future application build.

## Verification

- Initial focused regression failed on all eight unfinished entries.
- Focused compilation/readback check passes, preserving exact translated text.
- Regression assertions protect HTML/link structure, line breaks, option names,
  administrator-privilege warning and the one-TOS-value restriction.
- Scope checks confirm exactly eight formerly unfinished entries changed.
- Disposable unfinished-entry, warning reversal, restriction reversal and
  altered-link controls each produced one expected failure without errors/skips.
- Full translation suite passed: 66 tests in 58.085 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no issues. Native-speaker and rendered-layout
  review remain pending.

Local evidence: `Work/qa/polish-policy-20261016/`.

## Remaining

Polish unfinished entries decrease from 269 to 261; the overall catalog backlog
decreases from 7,538 to 7,530. Counts describe catalog content, not fresh full-source
extraction coverage or native-speaker certification.

Other translations, native-speaker/layout review, minimum-OS and physical Intel
testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and an updated app build remain separate tasks.
