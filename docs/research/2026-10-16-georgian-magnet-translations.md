# Georgian magnet, favorites and message translations

## Scope

Completed 17 unfinished entries covering magnet search/download help, transfer
statistics, favorite-hub descriptions and credentials help, automatic slot
granting, private-message labels, quick-connect help and per-nickname commands.
Existing finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 17 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 81 tests in 76.855 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Assertions check required context/source keys, active/nonempty translations,
  compiled equality, placeholder multiplicity, line counts and technical tokens.
- Independent scope checks confirm exactly 17 formerly unfinished entries
  changed and non-translation XML content remained unchanged.
- Disposable unfinished-entry, changed-placeholder, altered-shortcut and removed
  line-break controls each produced one expected failure without errors or skips.

Local evidence: `Work/qa/georgian-magnets-20261016/`.

## Remaining

Georgian unfinished entries decrease from 234 to 217; the overall catalog backlog
decreases from 7,175 to 7,158. These are catalog counts, not full-source extraction
coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
