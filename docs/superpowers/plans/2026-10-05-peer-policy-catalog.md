# Peer Policy Diagnostics And Proxy Catalog Plan

> **For agentic workers:** Use superpowers:executing-plans inline. The user approved two uninterrupted pending tasks.

**Goal:** Improve retained peer-policy failure evidence and close nine missing runtime catalog keys in all 25 locales.

**Architecture:** Keep the Torrent security behavior and test budgets unchanged. Add test-only contextual diagnostics, prove their failure output with a private invalid-endpoint control, then run the real isolated Torrent suite. Add catalog regression coverage before filling the exact missing runtime keys.

**Tech Stack:** Catch2, libtorrent, Qt, Python unittest, lrelease/lconvert.

**Spec:** Existing approved updater plan and its design at `docs/superpowers/plans/2026-09-29-built-in-updates.md` and `docs/superpowers/specs/2026-09-29-built-in-updates-design.md`; retained verification gate and catalog coverage only.

## Global Constraints
- Preserve installed app, real profile, dependencies, servers, keys and all failed evidence.
- Use loopback-only disposable application tests. Compile at most two jobs, serially with application tests.
- Do not change security assertions, deadlines, networking fallbacks or production Torrent behavior without causal evidence.
- No release acceptance, signing, installation, push or feed activation. Signed feeds remain last.

## Review Focus
- A silent connection failure must not be mistaken for a successful policy rejection.
- A passing rerun does not waive the original intermittent failure.
- Qt context, placeholders and finished status must survive QM compilation.
- Missing locale entries must not be replaced with blanket English copies.
- Native-speaker review and the inherited unfinished inventory remain separate gates.

## Task 1: Peer-policy failure diagnostics
- [x] Record policy identity, listener ports, proxy counters, peer state and available alerts in the test failure context.
- [x] Build a private unchanged-assertion control using an invalid endpoint. Confirm the exact rejection-counter failure has useful diagnostic output.
- [x] Build both matching architecture test harnesses and run focused security sections plus full Torrent suites serially. Retain failures and report any unresolved cause explicitly.

## Task 2: Proxy and Torrent-menu catalog gaps
- [x] Add a runtime-context/QM compilation test for five SettingsConnection and four TransferView keys. Observe exactly 225 missing-entry subtest failures before edits.
- [x] Add localized translations to the 25 TS files, preserving unrelated catalog contents and all placeholders.
- [x] Run all translation and icon regression tests without skips. Inventory remaining extraction gaps and document native-language limitations.
- [x] Review the owned delta and preserve receipts; record results and remaining gates in the existing ledger.

## Bounded Outcome

The source-order review finding was corrected in one pass: freeze the original payload/upload/rejection observations before best-effort diagnostics. Controlled late-counter RED/Green proves the distinction; invalid-endpoint diagnostics retain the expected failure. Both corrected architecture harnesses pass all 152 Torrent cases and focused seven-section security checks. The original natural intermittent cause remains unresolved, not waived by these reruns.

Exactly 225 runtime entries were added and compiled in their proper contexts. Inherited zero-indented context closing tags exposed an initial insertion error, caught by the catalog gate and corrected. The remaining extraction inventory is 23 missing keys per locale. Native-speaker review, inherited unfinished catalogs, whole-branch/platform/provenance and final release/signing/install/publication gates remain open. No installed app or live profile change; signed feeds last.
