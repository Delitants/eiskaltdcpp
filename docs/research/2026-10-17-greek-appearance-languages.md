# Greek appearance and language settings

## Scope

Completed 22 unfinished Greek SettingsGUI translations: language names, icon
themes, and chat-picture storage/cleanup labels. Regression coverage includes all
48 active catalog entries. Existing finished translations, source inventory and
non-translation XML remain unchanged. The installed app, user profile and
published prerelease are untouched.

## Verification

- Test-first run: 22 expected failures in 0.386 seconds.
- Focused compiled-catalog roundtrip passed in 0.354 seconds.
- Full suite: 121 tests passed in 75.922 seconds, with no skips.
- Exact new wording, formatting and leading/trailing spaces are checked.
- Scope check confirmed exactly 22 unfinished-to-finished changes.
- Nine disposable mutation controls each produced exactly one expected failure:
  unfinished entry, missing key, regional language, writing system, icon-theme
  scope, cleanup age direction, time unit, suffix spacing and picture-folder scope.
- The first fixture used a Serbian prefix matching two variants and stopped.
  Its log is retained. `check-specific.py` selects Serbian (Cyrillic) explicitly;
  all nine controls then passed. No catalog repair was required for this issue.
- Independent read-only review found no actionable findings.
- `git diff --check` passed.

Local evidence: `Work/qa/greek-appearance-20261017/` contains test and scope logs
and fixture scripts. These checks do not provide native-language certification
or rendered-layout validation. The day label remains a fixed plural suffix;
dynamic singular/plural UI behavior is outside this translation batch.

## Remaining

Greek unfinished entries decreased from 259 to 237; global unfinished entries
decreased from 6,373 to 6,351. Counts do not certify extraction coverage.
Further translations, language/layout review, platform QA, dependency
reproducibility, optional notarization and an updated build remain pending.
