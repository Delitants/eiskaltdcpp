# Slovak catalog completion

## Scope

Completed the final two unfinished Slovak entries: ToS HTML help and timestamp
format HTML help. Only prose changes; markup, links, numeric ranges, time-format
tokens and the superuser warning are preserved. A new non-Qt regression pins
translated prose and structure for both entries. A separate Qt regression checks
all 2,086 active Slovak messages for completion and exact compiled readback.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the two focused tests failed four subtests on the two unfinished
  entries (HTML checks and full-catalog checks).
- After edits, both focused tests passed (2 tests, 0.382 seconds).
- Full translation suite passed (111 tests, 54.578 seconds, no skips).
- Independent scope comparison confirmed exactly two formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Nine disposable HTML controls (unfinished entry, missing key, token case,
  numeric range, leading zero, privilege requirement, ToS cardinality, link and
  markup) each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues and verified the two
  source strings against their UI definitions. Qt tools were available for this
  run; native-speaker and rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-complete-20261017/`.

## Remaining

The Slovak catalog has zero unfinished entries and 2,086 active messages. The
overall catalog backlog decreases from 6,540 to 6,538. This is catalog completion,
not certification of full-source extraction, native-speaker accuracy or rendered
layout. Previously finished wording is protected by independent scope comparison,
not pinned baseline assertions.

Remaining work includes other languages, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
