# Updater Review Receipts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for the approved Native inline execution.

**Goal:** Resolve the two static review findings on frozen revision a2bd3d63 without weakening update or quit admission.

**Architecture:** Accept matching backend revocations after discovery or verification has completed. Snapshot retained view layouts after GUI producer retirement, check the independent Torrent layout store, then freeze GUI settings mutations before the final persistence receipt. Later teardown may read the frozen state but may not replace it.

**Tech Stack:** C++20, Qt 6, Catch2, macOS ARM and Intel/Rosetta isolated QA.

**Spec:** `docs/superpowers/specs/2026-09-29-built-in-updates-design.md`, especially committed normal quit and preserved user state.

## Global Constraints

- Installation and scheduler activation remain held; no install on crash or dismissal.
- No direct network fallback; preserve authenticated offer, route and operation identity boundaries.
- Preserve installed app/profile, private providers, keys, old QA and unrelated untracked files.
- Serial compilation and application-test phases, at most two compiler jobs; disposable test profiles and loopback-only networking.
- ARM host and Intel/Rosetta results are not native minimum-OS acceptance.
- Signed feeds remain last; these repairs do not waive remaining release gates.

## Task 1: Revoked Offers

- [x] Add Available and Ready-with-consent regressions for matching backend failure, stale operation rejection, UI eligibility, duplicate rejection and no handoff.
- [x] Compile new tests against unchanged controller objects and retain the expected behavioral RED.
- [x] Restrict accepted failure phases to active operations or a matching Available/Ready offer; clear offer/readiness/consent using the existing cancellation path.
- [x] Run focused and complete Qt suites on both architectures; independently review the delta.

## Task 2: Complete GUI Persistence Receipt

- [x] Reproduce missing retained-view snapshots and separate Torrent layout write refusal in the full GUI fixture.
- [x] Add a common arena snapshot contract for persistence-owning views, plus the non-arena Transfer view snapshot.
- [x] Include hidden singleton owners and dynamic retained tabs, after physical GUI producer retirement.
- [x] Make Torrent layout sync return a checked, sticky quit result; stop later layout timers and teardown saves from bypassing it.
- [x] Freeze WulforSettings setters/load after snapshots, while retaining checked sync and frozen reads.
- [x] Prove current layout serialization, immutable late writes, disk refusal, sticky repeated refusal and no installer handoff on failed GUI persistence.
- [x] Run focused and whole suites for both CPU layouts; review and preserve live state.
- [x] Checkpoint exact owned files after final readback.

## Review Focus

- Matching-operation late failure versus genuinely stale callbacks and quit fencing.
- Hidden views missing from the visible tab list.
- Layout timers, hide/close/destructor saves after a successful receipt.
- QSettings error status and a separate Torrent layout destination.
- Auxiliary checkpoint retry must not reopen GUI mutation or replay a failed GUI receipt.

## Remaining Release Gates

Native minimum-OS and live lifecycle acceptance, inherited/native-language review, retained intermittent Torrent causes, broader filesystem/quit responsiveness and provenance/reproducibility remain separate. Final source rebuild/materials, numeric production allocation, signing/notarization, guarded closed-app install/readback and wiki/prerelease public readback remain pending. No release approval follows solely from this plan or static review.

## Residual Review Cases

The initial persistence repair exposed an uninitialized ShareBrowser save, an
initially parentless hidden history loader outside the original input/worker
roots, and unordered shared-key snapshots. Their corrected behavioral baseline
has nine intended failed assertions; the fresh focused ARM repair has six passing
cases and 204 assertions, including the isolated pre-initialization browser child.
These checks are not native lifecycle or release acceptance.

Final repair review identified one further history-mode case: saving a newer
Files layout while switching a tab to Users must not let another tab overwrite
Files at quit. Track history layout precedence per settings key rather than only
per current owner, and do not count restoring a mode as an edit. Cover Uploads
and Downloads leaving either Files or Users, comparing physical persisted bytes.
Complete affected full suites on both architectures before checkpointing code.

## Current Verification

The final narrow review closes the inactive-history-key finding with no further
actionable issue. The corrected real-row sorting check confirms that restoring
Files/Users also restores actual row order, not just the header indicator.
Its first proxy-property assertions were invalid because the proxy delegates
sorting to its source. A subsequent multiline fixture teardown aborted while
its database loader was running; the fixture now joins that producer before
inserting synthetic rows and uses the checked GUI fence before destruction.
Both failed test-fixture runs and the crash report remain retained.

The latest ARM focused layout/recovery run passes 14 cases and 502 assertions.
Full single-line, multiline and applicable sidebar GUI runs pass respectively
101/5,878, 101/5,786 and 99/5,692 cases/assertions, with no failed or skipped
assertions. The current core, Qt and Torrent binaries are byte-identical to the
fresh full-suite receipts of 1,035/150,514, 616/33,948 and 152/18,430.
Fresh Intel/Rosetta verification passes core 1,035/150,513, Qt 616/33,913 and
Torrent 152/18,430. Its focused layout/recovery run passes 14/502, and all three
applicable GUI layouts match the ARM case/assertion counts above. The focused
controller checks pass 26/293 on each architecture. These are isolated,
external-network-denied profiles on an ARM macOS 27 host; they are not native
Intel/macOS 14 or Apple Silicon/macOS 26 acceptance.

The current catalog audit passes all 23 tests with no skips. Final readbacks
preserve both historical bundle trees/strict seals, five archive/notice pins,
58 provider resources, nine protected live files and 28 retained evidence
hashes. The checkpoint contains only the 28 reviewed code/test paths and three
owned plan/status documents. No installation, signing, feed activation or
GitHub publication is performed by this checkpoint.

An earlier complete single-line run timed out in the checkpoint-recovery button
case. Current focused and full runs pass without changing its deadline, and
click/receipt diagnostics are now present, but its original intermittent cause
is not established. Passing reruns do not waive that acceptance limit.
