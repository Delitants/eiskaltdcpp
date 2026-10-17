# Post-release catalog repair

## Scope

The post-release audit found one shutdown-refusal runtime message missing from
all 25 Qt catalogs and a blank Italian `/away` help entry marked finished.
This repair adds only that message and corrects the Italian help text. It does
not change shutdown logic, published release archives, signed update feeds,
the installed application or the live profile.

The diagnostic uses the exact `eiskalt::torrent::TorrentEngine` runtime context
and preserves `%1` through `%6`: failed stages, last attempted operation,
elapsed microseconds, active checks, journals and pending saves.

## Verification

- Before catalog edits, the two focused regressions produced 26 expected
  failures: one per missing locale entry and the blank Italian help text.
- Tests compile the diagnostics using `lrelease -nounfinished`, read the QM
  contents back with `lconvert`, and check text, placeholders and formatting.
- The Italian help regression checks the command, nonblank text, trailing
  newline and exact compiled-catalog survival.
- Removing the added messages and restoring the one Italian help value
  reconstructs the original catalog bytes, without unrelated catalog edits.
- Independent static review checked context and placeholder semantics. Its
  Italian journal-terminology finding was corrected to match the existing
  verification-journal terminology.
- After review refinements, the full 26-test translation suite passed in
  62.988 seconds with no failures, errors or skips (Qt tools 6.11.1).

Local RED, initial GREEN and final regression logs are retained in
`Work/qa/translation-repair-20261016/`. Automated checks and static review do not
constitute native-speaker certification.

## Remaining

The inherited 9,423 unfinished entries from the active-catalog inventory are
unchanged. Native-speaker review, minimum-OS and physical Intel coverage,
broader platform QA, dependency reproducibility and optional Developer ID
signing/notarization remain separate. These source changes require a future
build; the published pre.1 artifacts remain immutable.
