# Greek logging, scripts and search monitor

## Scope

Completed 21 unfinished Greek translations across SettingsLog (1), UISettingsLog
(15), UIScriptManager (3), and UISpy (2). Tests cover all 29 active entries in
these contexts. The HTML help translation changes only the visible Help label;
markup, CSS and destination remain identical.

Source-only work: installed application, user profile and published prerelease
remain unchanged. Previously finished translations and non-translation XML are
unchanged.

## Verification

- Test-first run: 21 expected failures in 0.340 seconds.
- Focused compiled-catalog roundtrip: passed in 0.332 seconds.
- Initial full suite: 115 tests passed in 52.662 seconds, no skips.
- Final post-formatting suite: 115 tests passed in 59.201 seconds, no skips.
- `git diff --check` passed.
- Scope check: exactly 21 unfinished-to-finished changes; source inventory and
  non-translation XML unchanged.
- Eight disposable mutation controls each produced exactly one expected failure:
  unfinished entry, missing key, reload action, TTH scope, help URL, own-file-list
  ownership, log persistence and last-heartbeat retention.
- Independent read-only review of final test formatting found no actionable
  findings. This does not constitute native-speaker or rendered-layout approval.

Local evidence: `Work/qa/greek-logs-20261017/`. The final post-formatting suite
result is retained in `full-final.log`.

## Remaining

Greek unfinished entries decreased from 369 to 348; global unfinished entries
decreased from 6,483 to 6,462. Counts do not certify extraction coverage.
Remaining work includes translation batches, linguistic/layout review, platform
QA, dependency reproducibility, optional notarization and an updated build.
