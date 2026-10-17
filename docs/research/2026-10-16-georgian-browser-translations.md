# Georgian hub and browser translations

## Scope

Completed 18 unfinished entries covering public hub list download/cache status,
hub list configuration, search progress and panel visibility, share-browser
counts/errors, chat image selection and the IPv6 column label.
Previously finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are unchanged.
These source changes require a future application build.

## Verification

- The required-key regression failed on all 18 unfinished entries before edits.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 78 tests in 76.359 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- The test checks required context/source keys, active/nonempty values,
  placeholders, literal percent counts, accelerator counts, trailing whitespace
  and URL/IPv6 identifiers. IPv6 intentionally remains untranslated.
- Independent scope checks confirm exactly 18 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry, changed-placeholder, missing-accelerator and
  removed-trailing-space controls each produced one expected failure, with no
  errors or skips.

Local evidence: `Work/qa/georgian-browser-20261016/`.

## Remaining

Georgian unfinished entries decrease from 294 to 276; the overall catalog backlog
decreases from 7,235 to 7,217. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
