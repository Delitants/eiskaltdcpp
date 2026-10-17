# Greek transfers and torrent settings

## Scope

Completed 19 unfinished Greek translations: TransferView (8), TransferViewModel
(8), Settings (2), and Notification (1). Regression coverage includes 104 active
catalog entries, where active means non-obsolete, not necessarily runtime-used.
Existing finished translations, source inventory and non-translation XML remain
unchanged. The installed app, profile and published prerelease are untouched.

## Verification

- Test-first run: 19 expected failures in 0.199 seconds.
- Focused compiled-catalog roundtrip passed in 0.264 seconds.
- Full suite: 119 tests passed in 63.638 seconds, no skips.
- Scope check confirmed exactly 19 unfinished-to-finished changes.
- Ten disposable mutation controls each produced exactly one expected failure:
  unfinished entry, missing key, peer-count placeholder, download direction,
  upload direction, paused state, percentage, status separator, save-error
  placeholder and trailing space.
- Initial mutation fixture stopped because a prefix matched both short and
  combined download-status messages. Retained the original fixture/log and
  corrected only that selector to an exact match in `check-exact.py`.
- Independent read-only review found no actionable defects. Four selected model
  strings are retained catalog keys without current source call sites. Exact
  wording/format checks cover the 19 edits; roundtrip/completion covers all 104.
  Qt tools are required; the new test skips if either is unavailable.
- `git diff --check` passed.

Local evidence: `Work/qa/greek-transfers-20261017/`. These checks do not certify
native-language quality, rendered layout or runtime behavior.

## Remaining

Greek unfinished entries decreased from 297 to 278; global unfinished entries
decreased from 6,411 to 6,392. Counts do not certify extraction coverage.
Further translations, language/layout review, platform QA, dependency
reproducibility, optional notarization and an updated build remain pending.
