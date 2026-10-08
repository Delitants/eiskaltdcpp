# Current Libtorrent Materials Plan

> **For agentic workers:** Use superpowers:executing-plans inline. This is the approved release-readiness source/material follow-up, not deployment.

**Goal:** Replace stale ABI-1 libtorrent material selection with a separately staged source set matching the ABI-2 providers selected by the current both-CPU test builds.

**Architecture:** Reconstruct the hash-pinned upstream archive plus current no-fuzz patch in a new private directory. Compare its complete source inventory, installed headers, compiler dependency records and provider binaries with retained build evidence. Produce deterministic source-material archives and read them back without changing old bundles or materials.

**Tech Stack:** Python, tar, SHA-256, native patch and retained CMake/link/dependency records.

**Spec:** `docs/superpowers/specs/2026-09-29-built-in-updates-design.md`, source/notice and final-material gates in the corresponding implementation plan.

## Constraints And Rulings
- Preserve the existing approved branch, seven dirty source/test paths, allocation fixture, all sealed QA, providers and older materials.
- Ruling: continue in the approved checkout with isolated private output; an empty worktree would omit the reviewed local runtime repairs that the fresh consumers actually contain.
- Do not run an app, helper, installer or full build. No network, production signing, profile/key/VM/server changes, installation, push or feed publication.
- Ruling: existing offline release-profile regressions may compile small disposable Mach-O fixtures and ad-hoc-sign a fixture app, without production key access or application launch. This is tool verification, not release signing or a full application build.
- Source reconstruction and deterministic material packing do not prove historical compiler inputs, reproducible binaries, complete dependency closure, legal certification or final release acceptance.
- Do not copy old ABI-1 source selections into a new manifest and label them current. Refuse missing, altered, extra, escaping and stale inputs.
- Retain original failures; never edit or reseal their evidence. New documentation appends must be checked against retained prefix baselines.

## Task 1: Match Current Providers To Source Materials
- [x] Capture current source/provider/consumer inputs and prior evidence readback.
- [x] Observe exactly the stale ABI and patch-identity failures in the old materials.
- [x] Reconstruct the pinned archive and patch with no fuzz or offsets; compare the complete current provider source tree.
- [x] Verify both providers' installed headers, compiler-listed source inputs, link objects and selected current-consumer libraries; report external inputs separately.
- [x] Stage current lock, patch, upstream archive, complete modified sources, original notices and provider/build receipts in a new private material set.
- [x] Produce two deterministic archives, require identical hashes and full member/content readback; refuse altered/missing/extra inputs and escaping links in disposable controls.
- [x] Recheck preserved inputs and independently review the bounded audit/material claims.
- [x] Record exact results and remaining gates; locally commit only this plan and public documentation.

## Outcome

The scoped checkpoint passes 27 local audit/control tests and 30 existing release-
profile tests. Both providers match 1,653 reconstructed source files and 295
installed headers, with 697 source-dependency inputs, 285 link-accounted objects
and 106 embedded archive members per CPU. The new 1,699-file material set packs
twice with identical bytes and complete readback. Independent bounded review is
clear after retained RED controls and audit hardening. Earlier failures, 11,614
sealed artifacts, 3,506 source pins and 12,237 material pins remain intact.

This closes current libtorrent material selection only. Complete external/current
dependency/resource/notice/source supersets, whole binary reproducibility,
historical/legacy attribution, shutdown/lifecycle/native/live/soak/platform and
translation acceptance, final whole review/application/material rebuilding,
allocation/signing/notarization/signed-v3/install/publication/readback remain
separate. Production signed feeds remain last.

## Review Focus
- Stale ABI/patch identity accepted despite matching package version.
- A source or installed-header subset being mistaken for a complete inventory.
- Built dylib or current test-consumer link selection differing from staged provider receipts.
- Generated/external compiler inputs being mislabeled as exact upstream source.
- Archive metadata, extra files, escaping links or retry overwrite weakening material verification.
