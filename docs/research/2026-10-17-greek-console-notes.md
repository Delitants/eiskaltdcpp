# Greek console and notes translations

## Scope

Completed 20 unfinished Greek translations in CmdDebug (1), Secretary (3),
UICmdDebug (4), and UISecretary (12). The regression covers all 40 active
entries in these contexts. Existing finished translations, source keys,
locations, and other XML metadata are unchanged.

This is source-only translation work. The installed application, user profile,
and published prerelease are unchanged.

## Verification

- New focused test first failed on all 20 unfinished entries (0.205 seconds).
- After translation, the focused test passed (0.288 seconds), including Qt
  lrelease/lconvert compilation and roundtrip checks.
- Full translation suite: 114 tests passed in 74.021 seconds, with no skips.
- Independent scope check confirmed exactly 20 unfinished-to-finished changes
  and unchanged non-translation XML and previously finished translations.
- Seven disposable-fixture controls each produced exactly one expected failure:
  unfinished entry, missing source key, changed IP filter scope, wrong separator,
  public/private chat reversal, changed duplicate-filter meaning, and changed
  BitTorrent protocol name.
- Independent read-only review found no actionable findings. It does not
  constitute native-speaker certification or rendered layout validation.
- `git diff --check` passed.

Local evidence: `Work/qa/greek-console-20261017/` (red, focused, full-suite,
scope and mutation logs; fixture scripts).

## Remaining

Greek unfinished entries decreased from 389 to 369; the global catalog count
decreased from 6,503 to 6,483. These counts do not certify extraction coverage.
Remaining work includes further translation batches, linguistic and rendered
layout review, platform QA, dependency reproducibility, optional notarization,
and a new build incorporating changes since the published prerelease.
