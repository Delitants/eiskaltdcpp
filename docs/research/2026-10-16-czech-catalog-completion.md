# Czech catalog completion

## Scope

Completed 184 unfinished Czech entries, including command and time-format help,
queue-retention guidance, migration/backup notices, media metadata and credits.
Previously finished entries, including connection/TLS translations, are unchanged.
Other locales and source/context/location content are unchanged.

Reused the Hungarian rich-help/command test helper with explicit language and
localized conjunction parameters. Published binaries, updater feeds, installed
app and user profile remain untouched; a future build is needed for these changes.

## Verification

- Initial focused run: 188 expected failures, covering 184 unfinished entries
  and overlapping rich-help/command assertions.
- Three focused tests pass, including the existing Hungarian helper coverage.
- Compilation/readback checks all active Czech entries are finished, nonempty
  and compile to exact translated values.
- Scope checks confirm exactly 184 formerly unfinished entries changed;
  edit-time checks preserve structure, HTML, placeholders and newlines.
- Disposable unfinished-entry, damaged-link, time-token and command-token
  controls each produced one expected failure, without errors or skips.
- Full translation suite passed: 59 tests in 52.302 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/czech-completion-20261016/`.

## Remaining

Czech unfinished entries decrease from 184 to zero. The overall catalog count
decreases from 7,954 to 7,770. These are catalog-content counts, not fresh
full-source extraction coverage or native-speaker certification.

Other-language completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and an updated application build remain separate tasks.
