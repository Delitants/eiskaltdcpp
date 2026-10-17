# Greek commands, favorites and IP filter

## Scope

Completed 16 unfinished Greek translations: UCModel (1), UIUserCommands (11),
FavoriteUsers (1), UIFavoriteHubEditor (2), and UIIPFilter (1). The regression
covers all 65 active entries in these contexts. Command-editor UI context was
checked for Raw, Context, and Send once per nick.

Previously finished translations, source inventory and non-translation XML
remain unchanged. This source-only batch does not modify the installed app,
user profile or published prerelease.

## Verification

- Test-first run: 16 expected failures in 0.337 seconds.
- Focused Qt compiled-catalog roundtrip: passed in 0.216 seconds.
- Full translation suite: 116 tests passed in 75.251 seconds, no skips.
- Scope check: exactly 16 unfinished-to-finished translation changes.
- Seven disposable mutation controls each produced one expected failure:
  unfinished entry, missing key, per-nick send count, private-message scope,
  favorite-hub description scope, user description scope, and import direction.
- Independent read-only review found no actionable findings; this is not
  native-language certification or rendered-layout validation.
- `git diff --check` passed.

Local evidence: `Work/qa/greek-commands-20261017/` contains the test-first,
focused, full-suite, scope and mutation results and fixture scripts.

## Remaining

Greek unfinished entries decreased from 348 to 332; global unfinished entries
decreased from 6,462 to 6,446. Counts do not certify extraction coverage.
Further translations, language/layout review, platform QA, dependency
reproducibility, optional notarization and an updated build remain pending.
