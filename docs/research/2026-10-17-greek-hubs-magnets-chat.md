# Greek hubs, magnets and chat labels

## Scope

Completed 17 unfinished Greek translations across ChatEdit (2),
DHTBootstrapList (3), PublicHubs (3), PublicHubsList (1), SearchFrame (2),
UIHubFrame (1), UIMagnet (3), UIPrivateMessage (1), and UIPublicHubs (1).
The regression covers all 126 active entries in these contexts.

Existing finished translations, source inventory and non-translation XML remain
unchanged. The installed app, user profile and published prerelease are untouched.

## Verification

- Test-first run: 17 expected failures in 0.338 seconds.
- Focused compiled-catalog roundtrip passed in 0.573 seconds.
- Full suite: 118 tests passed in 73.205 seconds, with no skips.
- Exact wording and placeholder multisets are checked for the new entries,
  along with the search percentage sign and both Shift+Enter hints.
- Scope check confirmed exactly 17 unfinished-to-finished changes.
- Nine disposable mutation controls each produced exactly one expected failure:
  unfinished entry, missing key, wrong placeholder, removed percent sign,
  changed hub-chat shortcut, changed private-message shortcut, altered local
  cache meaning, search/magnet link reversal and changed DHT protocol name.
- Independent read-only review found no actionable defects. Compilation checks
  do not verify runtime lookup, layout or actual shortcut behavior. Exact wording
  is pinned for the 17 edits, not the other 109 entries; the test requires Qt tools.
- `git diff --check` passed.

Local evidence: `Work/qa/greek-hubs-chat-20261017/` contains test and scope logs
and fixture scripts. These checks do not provide native-language certification
or rendered-layout validation.

## Remaining

Greek unfinished entries decreased from 314 to 297; global unfinished entries
decreased from 6,428 to 6,411. Counts do not certify extraction coverage.
Further translations, language/layout review, platform QA, dependency
reproducibility, optional notarization and an updated build remain pending.
