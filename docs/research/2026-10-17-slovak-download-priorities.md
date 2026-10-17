# Slovak download priorities and slow-source thresholds

## Scope

Completed 15 unfinished UISettingsDownloads entries covering the public-hub-list
HTTP proxy scope, automatic priority thresholds and slow-source criteria. The
test requires all 15 keys and pins exact wording, numeric defaults, line breaks,
units and Qt compiled readback. The other 17 unfinished download-settings entries
remain outside this batch.

The seconds-unit matcher requires numeric or parenthesized context so that the
Slovak preposition "s" is not treated as a unit. Byte-unit boundaries reject
prefixed substitutions such as KiB/s for B/s.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 15 unfinished entries,
  including after refining the seconds-unit matcher.
- After edits, Qt compilation/readback passed (1 test, 0.319 seconds).
- Review identified seven translations that narrowed "drop" to disconnect-only.
  DownloadManager.cpp confirms sources may also be removed. Revised exact
  expectations failed on all seven old translations before repairing the catalog
  to general discard/drop wording; explicit "disconnected" wording is retained.
- The initial full suite passed (105 tests, 57.774 seconds) before this semantic
  repair; final-code verification is recorded separately below.
- Final focused regression passed (1 test, 0.617 seconds); the final full suite
  passed (105 tests, 104.499 seconds, no skips).
- Independent scope comparison confirmed exactly 15 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, missing-key, unit, default, threshold-direction,
  disabled-priority, proxy-scope and priority-boundary controls each failed once,
  without errors or skips.
- A ninth final negative control rejects a return to disconnect-only wording.
- Independent follow-up review found no remaining actionable issues.
- `git diff --check` passed.

Local evidence: `Work/qa/slovak-priorities-20261017/`.

## Remaining

Slovak unfinished entries decrease from 76 to 61; the overall catalog backlog
decreases from 6,614 to 6,599. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
