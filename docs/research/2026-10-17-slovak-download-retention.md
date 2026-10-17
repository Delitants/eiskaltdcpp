# Slovak source handling and download retention

## Scope

Completed the 17 remaining unfinished UISettingsDownloads messages. All 73 active
entries in that section now have completion and Qt compiled-readback coverage.
The regression pins the new 17 translations and checks their line breaks and
numeric values. Removal versus disconnection, completed queue-entry retention,
automatic-search warnings, MB/zero-auto segment sizing and simultaneous-upload
scope are preserved.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 17 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.260 seconds).
- Full translation suite passed (106 tests, 55.742 seconds, no skips).
- Independent scope comparison confirmed exactly 17 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Nine disposable controls (unfinished entry, missing key, disconnect-only rule,
  file movement, disabled retention, segment unit, zero sentinel, upload scope
  and file-list exception) each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-retention-20261017/`.

## Remaining

Slovak unfinished entries decrease from 61 to 44; the overall catalog backlog
decreases from 6,599 to 6,582. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
