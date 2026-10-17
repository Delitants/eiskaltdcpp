# Greek hub users, sidebar and command help

## Scope

Completed 19 unfinished Greek translations: HubFrame (9), SideBarModel (2),
ToolBar (1), and UserListModel (7). Regression coverage includes all 149 active
catalog entries in these contexts. Existing finished translations, source
inventory and non-translation XML remain unchanged. The installed app, user
profile and published prerelease are untouched.

## Verification

- Test-first run: 19 expected failures in 0.465 seconds.
- Focused compiled-catalog roundtrip passed in 0.301 seconds.
- Exact wording, HTML tags, placeholder multisets, line counts and literal
  command prefixes/arguments are checked for the new translations.
- Scope check confirmed exactly 19 unfinished-to-finished changes.
- Nine disposable mutation controls each produced exactly one expected failure:
  unfinished entry, missing key, operator/user role reversal, removed HTML break,
  changed command name, reordered command arguments, reversed read behavior,
  removed command-help newline and changed IPv6 version.
- Review identified that the initial `/rebuild` wording incorrectly implied
  recalculating hashes. Verified `HashStore::prepareRebuild()` reuses existing
  hash trees; changed the wording to rebuilding the hash database. The revised
  expectation first failed once against the old text (0.239 seconds).
- An additional mutation rejects the old recalculation wording. All ten final
  mutation controls were rejected with one expected failure each.
- Corrected focused test passed in 0.199 seconds. Final full suite passed all
  120 tests in 68.592 seconds, with no skips. The earlier pre-correction suite
  also passed (75.852 seconds); its results remain retained but are superseded.
- Independent follow-up review confirmed the wording finding is resolved and
  found no further issues. Native-language certification is not claimed.
- `git diff --check` passed.

Local evidence: `Work/qa/greek-hub-users-20261017/` contains test and scope logs
and fixture scripts. These checks do not provide native-language certification,
rendered-layout validation or runtime-behavior approval.

## Remaining

Greek unfinished entries decreased from 278 to 259; global unfinished entries
decreased from 6,392 to 6,373. Counts do not certify extraction coverage.
Further translations, language/layout review, platform QA, dependency
reproducibility, optional notarization and an updated build remain pending.
