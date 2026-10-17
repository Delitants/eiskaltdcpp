# Slovak console and notes translations

## Scope

Completed 25 unfinished entries in CmdDebug, DialogScriptConsole, Secretary,
UICmdDebug and UISecretary. The regression covers all 45 active entries in these
contexts and pins the exact wording of the 25 new translations, including
shortcuts and IP filter labels. Existing finished translations are unchanged.

This is source-only work. Published binaries, updater feeds, installed app and
profile are untouched; these translations require a future application build.

## Verification

- Before catalog edits, the focused regression failed on all 25 unfinished entries.
- After edits, focused Qt compilation/readback passed (1 test, 0.333 seconds).
- The full translation suite passed (96 tests, 70.597 seconds, no skips).
- Independent XML comparisons confirmed exactly 25 unfinished translations changed,
  with existing finished translations and all non-translation XML unchanged.
- Disposable unfinished-entry, altered-shortcut, missing-key and changed-filter-
  meaning controls each failed once as expected, without errors or skips.
- Independent review found no issues. The 20 pre-existing translations are checked
  for completion and compiled roundtrip, not pinned wording; their preservation
  was verified by the independent scope comparison.
- `git diff --check` passed.

Local evidence: `Work/qa/slovak-notes-20261016/`.

## Remaining

Slovak unfinished entries decrease from 298 to 273; the overall catalog backlog
decreases from 6,836 to 6,811. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
