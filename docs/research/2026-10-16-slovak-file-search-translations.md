# Slovak file, queue and search translations

## Scope

Completed 26 unfinished entries across DownloadQueue, FileBrowserModel,
FinishedTransfers, FinishedTransfersModel, HashProgress, SearchModel,
ShareBrowserSearch and UIShareBrowserSearch. The regression checks all 97 active
entries in these contexts and pins the 26 new translations. Source placeholders,
HTML tags, nonbreaking spaces and existing unit notation are preserved.

Existing finished translations and non-translation XML are unchanged. Published
binaries, updater feeds, installed app and profile are untouched. These source
changes require a future application build.

## Verification

- Before edits, the focused regression failed on all 26 unfinished entries.
- After edits, focused Qt compilation/readback passed (1 test, 0.547 seconds).
- Full translation suite passed (97 tests, 54.942 seconds, no skips).
- Independent scope comparison confirmed exactly 26 formerly unfinished entries
  changed, preserving all finished translations and non-translation XML.
- Disposable unfinished-entry, changed-placeholder, missing-key, broken-markup
  and file/directory-meaning controls each failed once, without errors or skips.
- `git diff --check` passed.
- Independent read-only review found no issues. Qt tools are required for this
  regression; they were available and the test did not skip in this run.

Local evidence: `Work/qa/slovak-files-20261016/`.

## Remaining

Slovak unfinished entries decrease from 273 to 247; the overall catalog backlog
decreases from 6,811 to 6,785. These counts do not certify full-source extraction
coverage or native-speaker accuracy. Previously finished wording is protected by
the scope comparison; the new test checks its completeness and compiled readback,
not a pinned baseline translation.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
