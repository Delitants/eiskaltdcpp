# Greek file browsing, search and hashing

## Scope

Completed 18 unfinished Greek translations: FileBrowserModel (7), HashProgress
(1), ShareBrowser (4), ShareBrowserSearch (1), UIFileHasher (2), and
UIShareBrowserSearch (3). Regression coverage includes all 74 active entries in
these contexts. Source usage was inspected for hashing progress and selection
and matching status messages.

Existing finished translations, source inventory and non-translation XML remain
unchanged. The installed app, user profile and published prerelease are untouched.

## Verification

- Test-first run: 18 expected failures in 0.361 seconds.
- Focused compiled-catalog roundtrip passed in 0.330 seconds.
- Full suite: 117 tests passed in 72.021 seconds, with no skips.
- Exact translations, placeholder multisets, HTML tags/entities, trailing spaces
  and the hashing percentage sign are checked.
- Independent scope check confirmed exactly 18 unfinished-to-finished changes.
- Nine disposable mutation controls each produced exactly one expected failure:
  unfinished entry, missing key, changed placeholder, removed percent sign,
  removed HTML break, removed HTML spacing, file/directory scope reversal,
  search/magnet link reversal and removed trailing space.
- Independent read-only review found no actionable defects. Structural checks
  cover the 18 edits; all 74 active entries receive completion and roundtrip
  checks. The test requires Qt tools and skips if they are unavailable.
- `git diff --check` passed.

Local evidence: `Work/qa/greek-files-20261017/` contains test and scope logs and
fixture scripts. Linguistic certification and rendered-layout review are not
claimed by these checks.

## Remaining

Greek unfinished entries decreased from 332 to 314; global unfinished entries
decreased from 6,446 to 6,428. Counts do not certify extraction coverage.
Further translations, language/layout review, platform QA, dependency
reproducibility, optional notarization and an updated build remain pending.
