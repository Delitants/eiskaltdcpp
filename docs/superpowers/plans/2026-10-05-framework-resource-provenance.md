# Framework Resource Provenance Plan

> **For agentic workers:** Use superpowers:executing-plans inline under the approved built-in-updates release-readiness plan. Do not dispatch implementers.

**Goal:** Replace package-only framework resource attribution with read-only, file-level evidence for the two retained updater-enabled QA candidates.

**Architecture:** Inventory all non-code framework files, including shipped headers and helper metadata, excluding separately audited code-signature seals. Compare Sparkle files against the hash-pinned SDK and Qt files against their actual retained deployment providers. Trace source-package members and notice evidence without mistaking byte matching or generated-file association for reproducibility or legal certification.

**Tech Stack:** Python, SHA-256, tar/plist readers, retained offline source/SDK archives.

**Spec:** `docs/superpowers/specs/2026-09-29-built-in-updates-design.md`; resource/material gates in the corresponding implementation plan.

## Constraints And Review Focus
- Preserve both accepted historical QA candidates; they are not final current-source release builds.
- Preserve installed app, profile, providers, signing keys, VM, services and all prior evidence.
- No archive extraction into providers, network, app/helper launch, signing, installation, push or feed activation.
- Missing source evidence remains explicitly unresolved; do not assign a file-level license from a package name alone.
- Verify inside-framework symlink confinement and source/SDK pins; reject missing, changed, extra or escaping inputs.
- Binary localizations and generated nib/plist/module metadata need explicit proof levels, not false byte-exact source claims.

## Task 1: Complete Framework File Inventory
- [x] Inventory both candidates and capture immutable before receipts.
- [x] Confirm the prior package-only receipts lack the newly required file mappings.
- [x] Map every regular non-code framework file to the pinned Sparkle SDK or exact Qt deployment provider.
- [x] Verify symlinks remain inside their framework and preserve code/signature files separately.
- [x] Reject an omitted record and changed copied bytes in disposable controls.

## Task 2: Source And Notice Evidence
- [x] Verify relevant offline source/SDK archive and notice hashes against retained pins.
- [x] Trace source members for copied data/headers/localizations; distinguish generated artifacts.
- [x] Record exact, semantic, generated-associated and unresolved proof levels explicitly.
- [x] Recheck candidate/provider/protected bytes and record remaining gates for the public documentation checkpoint.

This bounded audit does not complete legacy icon provenance, all dependency input
provenance, native-language review, reproducible builds, independent whole-branch
review, native minimum-OS/live acceptance or final release generation. Signed
feeds remain last.

## Audit Outcome

The old package-only receipts produce exactly 210 expected coverage failures:
114 Apple Silicon and 96 Intel file mappings. The completed coverage check and
four rejection controls pass without skips. Omission and hash controls each
reject exactly one record; disposable changed bytes and an escaping symlink are
also rejected. No candidate is altered for these controls.

All 210 files match SDK/provider bytes. Across both candidates, 93 source files
match byte-for-byte, 72 localization dictionaries match source values and 12
generated files have source associations. Source-generation linkage is unresolved
for 33 files: 29 Qt framework metadata files, two public Sparkle module maps and
two helper PkgInfo files. These are copied generated artifacts, not missing files
or observed runtime defects. Package notices are verified, not promoted into
file-level legal certification.

Complete candidate tree bytes, types, link targets and executable bits remain
unchanged; strict signature readbacks pass. Five source/SDK/notice pins, 58 Qt
provider files and nine protected app/profile hashes remain unchanged. The
candidates still come from historical source `e325cb6cd`, not the current source.
No application/installer launch, native-OS acceptance or fresh C++ build occurred.

An initial notice lookup used the materials prefix instead of the bundle's
`Components` prefix; its failed log is retained as a harness error. The corrected
mapping verifies the bundle notice paths against their recorded manifests.
