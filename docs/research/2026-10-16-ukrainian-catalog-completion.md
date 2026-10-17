# Ukrainian catalog completion

## Scope

Completed the remaining 99 unfinished Ukrainian catalog entries after the
connection-settings batch. Coverage includes chat controls, main-window
actions, Torrent controls and status, notes, appearance settings, configuration
migration notices, logging diagnostics and rich-text help.

Exactly 99 previously unfinished translations changed. Existing source text,
contexts and locations are preserved, as are placeholders, newline counts,
HTML tags and link targets in the changed messages. Other locales are unchanged.

This is a source-catalog update for a future build. Published pre.1 binaries,
signed feeds, installed application and user profile remain unchanged.

## Verification

- Before translation edits, the completeness regression failed on exactly
  99 unfinished entries.
- The focused completed-catalog check passed: every active Ukrainian entry
  is finished, nonempty and survives `lrelease -nounfinished` and `lconvert`
  round-trip compilation with its exact translated value.
- Rich-help tests retain markup, attributes, links, newlines and time-format
  tokens. Disposable negative controls changing a link target and a time token
  each failed the regression as expected.
- Independent static review found no actionable findings in the catalog and
  regression-test diff. This is not native-speaker certification.
- Final full suite: all 29 tests passed in 64.016 seconds, with no failures,
  errors or skips, using Qt translation tools 6.11.1.

Local evidence: `Work/qa/ukrainian-completion-20261016/`.

## Remaining

The Ukrainian active catalog has zero unfinished entries; the total across
all 25 active catalogs is 9,306. These counts cover catalog contents, not a
fresh full-source extraction. Finished status does not certify linguistic
quality. Native-speaker and rendered-layout review remain necessary.

Other-language completion, minimum-OS and physical Intel testing, wider
platform QA, dependency reproducibility, optional Developer ID/notarization,
and a future build containing the source changes remain separate tasks.
