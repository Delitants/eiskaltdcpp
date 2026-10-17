# Polish migration and log translations

## Scope

Completed 18 unfinished QObject/SettingsLog/UISettingsLog entries, including
migration, backup outcomes, diagnostic logging and shared dialog labels.
Previously finished translations and source/context/location content are
unchanged. Published binaries, updater feeds, installed app and profile are
untouched; these source changes require a future application build.

## Verification

- Initial focused regression failed on all 18 unfinished entries.
- Focused compilation/readback passes for active entries in all three contexts.
- Checks preserve placeholders, file filters, help markup/links, migration
  paragraphs and backup success/failure wording.
- Scope checks confirm exactly 18 formerly unfinished entries changed.
- Disposable backup-failure reversal, damaged placeholder, preservation reversal
  and altered-file-glob controls each produced one expected failure without
  errors or skips.
- Review identified a missing presence assertion: deleted or obsolete warnings
  could evade the original loop. An explicit context/source-prefix inventory now
  requires all 18 source keys exactly once in the active-entry map. Disposable deletion
  and obsolescence controls each fail once as expected.
- Follow-up static review closed the missing-entry finding. The map does not
  independently detect duplicate XML messages. Native-speaker/layout review
  remains pending.
- Final full translation suite passed: 69 tests in 50.517 seconds, without
  failures, errors or skips. `git diff --check` passed.

Local evidence: `Work/qa/polish-migration-20261016/`.

## Remaining

Polish unfinished entries decrease from 219 to 201; the overall catalog backlog
decreases from 7,488 to 7,470. Counts describe catalog content, not fresh
full-source extraction coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
