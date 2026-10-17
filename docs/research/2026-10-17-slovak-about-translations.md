# Slovak About text and credits

## Scope

Completed the 44 remaining unfinished MainWindow entries, including About text,
transfer totals, translation credits and contributor roles. All 198 active
MainWindow entries are now checked for completion and Qt compiled readback.
The regression pins all 44 new translations and checks placeholders and HTML.
Contributor names, dates, email addresses and URLs are unchanged.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 44 unfinished entries.
- After edits, Qt compilation/readback passed (1 test, 0.468 seconds).
- Full translation suite passed (104 tests, 48.084 seconds, no skips).
- Independent scope comparison confirmed exactly 44 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- The initial disposable negative-control harness selected an obsolete message
  alongside the active bug-report text and stopped. The initial harness and
  partial output are retained. Active-entry filtering corrected this harness
  only; neither translations nor the maintained regression changed.
- All eight final controls (unfinished, missing key, placeholder, transfer
  direction, markup, link, contributor identity and script variant) each failed
  once as expected, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no actionable issues. Native-speaker and
  rendered-layout validation remain outstanding.

Local evidence: `Work/qa/slovak-about-20261017/`.

## Remaining

Slovak unfinished entries decrease from 120 to 76; the overall catalog backlog
decreases from 6,658 to 6,614. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the independent scope comparison, not pinned baseline assertions.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
