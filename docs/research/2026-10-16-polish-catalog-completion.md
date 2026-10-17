# Polish catalog completion

## Scope

Completed the remaining 82 unfinished Polish entries, covering appearance,
language names, shortcuts, history, notifications, scripts and time-format help.
Previously finished translations and non-translation XML content are unchanged.
Published binaries, updater feeds, installed app and profile remain untouched.
These changes require a future application build.

## Verification

- Initial focused regression produced 83 expected failures: 82 unfinished
  entries and the missing translated time-format help.
- Both focused catalog compilation/readback and rich-help tests passed.
- Full translation suite passed: 74 tests in 75.677 seconds, with no skips.
- Scope verification confirmed exactly 82 formerly unfinished entries changed
  and all non-translation XML content remained unchanged.
- Edit-time checks preserved HTML tags/attributes, placeholders and newlines.
- Disposable unfinished-entry, altered-link and time-token controls each
  produced one expected failure, without errors or skips.
- Independent read-only static review found no actionable issues in the catalog
  and test changes. This is not native-speaker or rendered-layout certification.
- `git diff --check` passed.

Local evidence: `Work/qa/polish-completion-20261016/`.

## Remaining

Polish unfinished entries decrease from 82 to zero. The overall catalog backlog
decreases from 7,351 to 7,269. These are catalog counts, not fresh full-source
extraction coverage or language certification.

Remaining work includes other translations, native-speaker/layout review,
minimum-OS and physical Intel testing, wider platform QA, dependency
reproducibility, optional notarization and an updated application build.
