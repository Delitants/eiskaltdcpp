# Polish download and sharing help

## Scope

Completed 17 unfinished UISettingsDownloads/UISettingsSharing entries covering
source removal, source matching, zero-size files, duplicate prevention, queue
retention, simultaneous uploads, sharing controls and hashing limits.
Previously finished entries and source/context/location content are unchanged.
Published binaries, feeds, installed app and profile remain untouched.

## Verification

- Initial test produced 18 expected failures: 17 unfinished entries plus the
  final retention-entry count, whose checks had not yet been reached.
- Focused compilation/readback check now passes for all active entries in both
  contexts, with assertions for retention warnings and numeric defaults/limits.
- Scope checks confirm exactly 17 formerly unfinished entries changed.
- Disposable controls reject reversed file-retention and upload-slot wording,
  altered slot interval and altered unlimited-hashing sentinel, with only
  expected assertion failures and no errors or skips.
- Full translation suite passed: 68 tests in 71.650 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. All added multiline
  newline counts were checked during editing; maintained newline assertions
  cover the two retention messages. Native-speaker/layout review remains pending.

Local evidence: `Work/qa/polish-downloads-20261016/`.

## Remaining

Polish unfinished entries decrease from 236 to 219; the overall catalog backlog
decreases from 7,505 to 7,488. These are catalog-content counts, not fresh
full-source extraction coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
