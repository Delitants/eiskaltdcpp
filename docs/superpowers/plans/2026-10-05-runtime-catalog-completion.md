# Runtime Catalog Completion Plan

> **For agentic workers:** Use superpowers:executing-plans inline. The user approved the next two pending tasks without stopping.

**Goal:** Close the remaining MainWindow14 and TransferViewModel9 extracted-key gaps in all 25 catalogs.

**Architecture:** Add context-specific finished/QM regression checks before inserting translations. Preserve every pre-existing catalog message and TS attribute. Use the unchanged production extraction to inventory missing keys independently of compilation tests.

**Tech Stack:** Qt TS/QM, Python unittest, lrelease/lconvert.

**Spec:** Approved localization/acceptance requirements in `docs/superpowers/plans/2026-09-29-built-in-updates.md` and its referenced design.

## Constraints And Review Focus
- No production C++ behavior changes, installation, signing, push or feed activation.
- Preserve profile, installed app, providers, unrelated files and all failed evidence.
- Preserve placeholders, literal percentages, multiline tooltip structure, BEP numbers and DC sharing restrictions.
- Missing extracted keys and inherited unfinished translations are different inventories; native-language certification remains separate.
- Current catalog-only acceptance is not a full rebuilt-app, native OS or whole-branch release verdict. Signed feeds last.

## Task 1: MainWindow Runtime Messages
- [x] Add compiled-QM/context checks for the 14 missing MainWindow messages.
- [x] Observe exactly 350 missing-key failures in 25 locales.
- [x] Add localized translations; reuse exact completed file/size labels when unambiguous.
- [x] Run focused and full catalog checks and verify all prior messages are unchanged.

## Task 2: TransferViewModel Runtime Messages
- [x] Add compiled-QM/context checks for the nine missing status/flag/peer tooltips.
- [x] Observe exactly 225 missing-key failures before adding those translations.
- [x] Preserve peer-direction semantics, sorting explanation, hash/BEP meanings and private-torrent discovery restrictions.
- [x] Run all catalog/icon tests without skips; inventory zero missing active runtime keys while reporting inherited unfinished entries.
- [x] Obtain a fresh bounded-delta review and record remaining release gates for the explicit local checkpoint.

## Bounded Outcome

Both focused compiled-QM checks and the full 23-test catalog suite pass. All 14
icon tests pass, without skips. Exactly 575 entries were added, and every prior
message and TS attribute is preserved. The unchanged 2,065-key production
extraction has no missing active entries in all 25 catalogs; inherited unfinished
counts are unchanged. A fresh read-only reviewer found no actionable delta issue.

This closes these two source-only tasks, not complete localization or release
acceptance. Native-language review, retained Torrent/quit diagnostics,
resource/dependency provenance, reproducibility, native minimum-OS/live checks,
independent whole-branch review, final application/material rebuilds, numeric
production build allocation, signing/notarization, updater acceptance, guarded
closed-app installation/readback and documentation/prerelease publication remain
separate gates. Signed feeds remain last. No app installation or push occurred.

## Current-Candidate Audit: 2026-10-07

The earlier 575-entry checkpoint remains historical. A fresh unchanged 23-test
audit now finds exactly 75 missing-key failures: two listener messages moved to
the `TorrentEngine` runtime context and one new `TorrentProxyAdapter` retirement
message, each missing in all 25 catalogs. Fifty existing listener translations
are reusable verbatim; the retirement message needs 25 new entries. Fresh
production-manifest extraction reads 356 files / 2,066 keys, with no source or
catalog mutations. The inherited unfinished/native-language-quality backlog
remains separate from these missing context keys.

The bounded context/compiled-QM repair design is awaiting maintainer approval;
no new completion claim or catalog edit is made. See
`docs/research/2026-10-07-buffered-drain-and-catalog-audit.md`. The Intel shutdown
refusal and wider native/lifecycle/provenance/signing/install/publication gates
remain held; production signed feeds stay last.

## Approved Context Repair: 2026-10-07

- [x] Observe exactly 75 expected failures in the new compiled-QM regression.
- [x] Reuse the 50 existing listener translations in `TorrentEngine` without
  modifying or removing their prior-context entries.
- [x] Add the proxy-retirement caption in all 25 catalogs.
- [x] Verify the focused test and full 24-test translation suite without skips.
- [x] Verify byte-for-byte preservation after removing only the added blocks,
  all prior XML messages/TS attributes and the original seven-file local repairs.
- [x] Extract 356 current source files / 2,066 keys: zero missing or ambiguous
  runtime entries in all catalogs, with inherited unfinished counts unchanged.

This closes only the three-message catalog gap, not native-language certification
or the broader shutdown/release gates. The separate backlog remains 9,327
unfinished runtime entries, including 9,159 empty ones. See
`docs/research/2026-10-07-catalog-context-repair.md`. No application installation
or publication occurred; production signed feeds remain last.
