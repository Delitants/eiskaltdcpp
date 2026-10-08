# Listener And Proxy-Retirement Catalog Repair

## Scope

The maintainer approved the bounded repair identified by the current-candidate
catalog audit. No networking, listening, shutdown, retry, proxy-routing or update
handoff behavior is changed. No app/profile/server/VM/key mutation, installation,
signing, push or feed activation is part of this checkpoint.

Exactly three finished entries were added to each of the 25 Qt catalogs:

- `TorrentEngine`: `Torrent could not accept an incoming connection.`
- `TorrentEngine`: `Torrent could not bind a configured listening endpoint.`
- `TorrentProxyAdapter`: `The proxy adapter is retiring for application quit.`

The first two messages now use `TorrentEngineText::tr` with the `TorrentEngine`
runtime context. Their existing translations in
`eiskalt::torrent::TorrentEngine` were reused verbatim, not removed or rewritten.
The proxy-retirement caption received 25 localized entries. Every pre-existing
catalog byte is recovered by removing only the new blocks; XML message values
and TS attributes are also unchanged. The existing seven-file runtime/test
repair set remains separate and byte-preserved.

## Verification

Private evidence is retained under
`Work/qa/catalog-context-repair-20261007/`; it is not a release asset.

- RED: the new compiled-QM regression failed on exactly the expected 75
  locale/context/source combinations, without errors or skips.
- GREEN: the focused regression passes; the full translation suite passes
  all 24 tests, without failures, errors or skips.
- Actual `lrelease -nounfinished` and `lconvert` verify finished, nonempty
  translations survive compilation in the correct context. The checks retain
  placeholder, literal-percent and newline semantics and reject long English
  fallback text in non-English catalogs. Qt tools are version 6.11.1.
- A separate negative fixture changes only the copied French listener
  translation and fails exactly the new verbatim-reuse assertion, with no
  other failures, errors or skips. The production catalogs are untouched by
  this fixture. The final regression also checks reuse against the prior
  context in every locale.
- Fresh extraction of 356 production-manifest files yields 2,066 runtime keys.
  Every catalog has zero missing keys and zero ambiguous runtime entries. All
  extracted-source hashes match the pre-repair audit.
- Exactly 75 entries were added: 50 reused and 25 newly localized. No existing
  message, TS attribute or test body was changed; the regression is additive.
- The inherited inventory is unchanged: 9,327 unfinished runtime translations,
  including 9,159 empty ones. This remains a separate quality/completion backlog.
- The original local runtime/test repairs and the earlier 20-file catalog and
  54-file buffered-drain evidence seals verify unchanged.

Compilation and extraction are not native-speaker certification or a rebuilt,
installed-app, minimum-OS, whole-branch or release-acceptance verdict.

## Bounded Review

A fresh read-only reviewer found no catalog correctness or clear semantic issue.
The one nonblocking regression weakness identified (verbatim reuse was not
guarded against future changes) was addressed by the per-locale/context equality
checks and their negative fixture. Follow-up review found no issue in that
refinement. The reviewer did not validate the revised full suite; the coordinator
ran it separately: all 24 tests pass in 62.339 seconds without errors or skips.
The final focused test passes in 15.451 seconds. This is a bounded-delta review,
not the pending whole-branch or native-language review.

## Remaining Gates

- The original Intel large-scope shutdown refusal is unresolved. Successful
  diagnostic repetitions do not waive it. Overall Torrent task 5,
  GUI-settings task 3 and updater tasks 7/8 remain open.
- Historical bind/UDP ownership and GUI freeze/recovery causes, broader
  lifecycle/soak/benchmark, active-download/selected-check GOST, partial-piece
  crash recovery and external/live/bootstrap acceptance remain separate.
- Native Intel/macOS 14 and Apple Silicon/macOS 26 acceptance remain pending;
  host macOS 27 and Rosetta results are not replacements.
- The inherited unfinished/native-language-quality backlog and complete
  dependency provenance/reproducibility remain pending.
- Independent final whole-updater/prerelease review, final app/material builds
  and numeric production build allocation remain pending.
- Developer ID signing/notarization, signed-v3 acceptance and guarded closed-app
  installation/readback remain pending.
- Authenticated GitHub/wiki/screenshots/prerelease publication and fresh public
  readback remain pending. Production signed feeds are last.
