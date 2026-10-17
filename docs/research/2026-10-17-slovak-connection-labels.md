# Slovak connection labels and warnings

## Scope

Completed 16 unfinished UISettingsConnection labels and warnings. The regression
requires all 16 keys, pins exact wording and checks Qt compiled readback. Protocol
names, binding targets, units, override negation and the enabled-state exit-hang
warning are preserved. Long ToS help remains outside this batch.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 16 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.236 seconds).
- Full translation suite passed (109 tests, 64.379 seconds, no skips).
- Independent scope comparison confirmed exactly 16 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Eight disposable controls (unfinished entry, missing key, override negation,
  protocol, binding target, warning condition, rate unit and reconnect interval)
  each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-connection-labels-20261017/`.

## Remaining

Slovak unfinished entries decrease from 18 to 2 (ToS and timestamp-format HTML
help); the overall catalog backlog decreases from 6,556 to 6,540. These counts do
not certify full-source extraction coverage or native-speaker accuracy.
Previously finished wording is protected by independent scope comparison,
not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
