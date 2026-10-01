# Selected-File Torrent Recheck Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Preserve the user's Native execution preference.

**Goal:** Verify selected Torrent files and update their piece availability without scanning unrelated payload or losing user activity, priority, proxy or sharing policy.

**Architecture:** A privately built, pinned libtorrent extension owns selective disk verification and piece-state transitions. A small Qt adapter and crash-safe operation journal connect it to TorrentEngine; scoped DC hash invalidation and Files-table controls consume typed snapshots. Capability and feasibility gates precede UI activation.

**Tech Stack:** C++20, Qt6, libtorrent 2.1.1, Catch2, CMake, native macOS packaging.

**Spec:** `docs/superpowers/specs/2026-09-30-selected-file-recheck-design.md`

Status: approved for Native inline execution on 2026-09-30. Tasks 1-5 are complete
locally, including held-I/O/fault acceptance and conservative restart recovery.
Scoped DC hash invalidation preserves unaffected cache evidence across restart.
Native Files controls are implemented locally in maintained-extension development
builds, with stable multi-selection, scope confirmation, progress and cancellation.
Current-host native Cocoa light/dark, resize, cancellation and priority checks pass.
Task 6 controls and all 25 verification catalogs are complete locally. The full
translation audit and compiled-catalog/native controls checks pass. Current full
Qt passes 548 cases with 8 external-GOST cases explicitly skipped, no failures.
Task 7's local package/review gates pass: registered actual-Mach-O fixtures cover
ABI/symbol/dependency/CPU/OS rejection and a fresh required header-pair configure.
Capability reconfiguration cannot reuse a stale success. An exact-current-pin
arm64 dependency rebuild passes native 17 / 586 and backend 152 / 18,430 tests,
with actual loader readback. The Qt repair/redownload/sharing-cache restart
scenario passes 1 / 148; full Qt, native Cocoa controls and core/DC-only matrices
pass. Independent review findings are resolved with storage-race, partfile-map,
paused-discovery, repair-scheduling and loader-relative regressions. These are
current-host isolated fixtures, not minimum-OS proof. Both complete app bundles
are built and audited: 84 thin arm64 binaries targeting macOS26 and 40 thin
x86_64 binaries targeting macOS14, with the exact maintained native provider
and matching patched Cocoa plugins. Native ARM and Rosetta Intel bundled ABI
probes return 1; version-only Cocoa launches pass with private dependency
prefixes denied. Rosetta and version-only probes are not native minimum-OS or
full live-session acceptance. Those gates, final signing/notices, updater and
installation/publication remain pending. No app is installed.

## Global Constraints

- Preserve file priorities, wanted selections, proxy routes, DC sharing exclusions, and paused/stopped state.
- Original files used by read-only created-Torrent seeding must never be repaired or written.
- Do not pause the entire libtorrent session or disturb other jobs.
- No hashing, disk drain or joins on the GUI thread.
- Normal verification reads no unrelated payload pieces.
- No `native_handle()` private API calls, fake whole-Torrent scans, priority tricks or `add_piece()` writeback verification.
- Independently test Intel macOS14+ and Apple Silicon macOS26+ packages.
- Do not change system Homebrew, production profile, private proxy, installed app or release feeds.
- One build owner, maximum `-j2`; run full suites serially. Use disposable fixtures and isolated prefixes. Never use stale `build-qt`.
- Keep unrelated untracked files; stage explicit task paths only. Local checkpoints are not permission to push or publish.

## Review Focus

- A Files table re-sort between confirmation and execution must not change selected identities (Tasks 2, 6).
- A same-size external replacement or renamed ancestor must not reuse stale verification/DC hashes (Tasks 2, 4, 5).
- A late save-resume alert must not resurrect unchecked pieces after cancel or restart (Tasks 3, 4).
- Skipped neighboring files may store boundary bytes in partfiles; do not create/open guessed full files (Tasks 1, 2).
- A stopped/read-only job must not open discovery, announce or repair merely because a local check was requested (Tasks 1, 4).

## File And Interface Map

- `dependencies/libtorrent/`: immutable source manifest, license, maintained patch and extension contract. No vendored build outputs.
- `tools/dependencies/build-libtorrent.sh`: checksum-verified build into a caller-supplied project-owned prefix.
- `cmake/SelectedRecheck.cmake`: optional capability probe plus required-release capability gate.
- `torrent/SelectedRecheck.{h,cpp}`: pure piece-scope planner and adapter contract, separate from the large engine implementation.
- `torrent/RecheckJournal.{h,cpp}`: bounded atomic operation records and recovery decisions.
- `torrent/TorrentEngine.{h,cpp}`, `TorrentTypes.h`: owner-thread admission, worker lifecycle and snapshots.
- `dcpp/ShareManager.{h,cpp}`, `eiskaltdcpp-qt/src/TorrentRuntime.cpp`: selective TTH invalidation and publication barriers.
- `eiskaltdcpp-qt/src/TorrentWindow.{h,cpp}`: Files context menu, confirmation, status/results and cancellation.
- Existing Catch2 targets, translation catalogs and macOS dependency audit tooling provide integration coverage.

### Task 1: Prove The Native Engine Extension

**Files:** Create `dependencies/libtorrent/source.json`, `dependencies/libtorrent/README.md`, `dependencies/libtorrent/patches/0001-selected-recheck.patch`, `tools/dependencies/build-libtorrent.sh`, `tests/torrent/test_selectedrecheck_dependency.cpp`, `cmake/SelectedRecheck.cmake`. Modify `torrent/CMakeLists.txt`, `tests/torrent/CMakeLists.txt`.

**Interfaces:** Extension capability `LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI=1` and linked `libtorrent::eiskalt_selected_recheck_abi()` returning `int`. The maintained patch adds a public `libtorrent/eiskalt_selected_recheck.hpp`; no application code includes private headers.

Define `selected_recheck_request { uint64_t token; vector<piece_index_t> pieces; }`, `selected_recheck_phase { quiesced, progress, complete, canceled, failed }`, and a polled `selected_recheck_status { uint64_t token; selected_recheck_phase phase; vector<piece_index_t> passed, failed, unavailable; int64_t checked_bytes, total_bytes; error_code error; }`.

Expose asynchronous free functions `prepare_selected_recheck(torrent_handle, uint64_t)`, `start_selected_recheck(torrent_handle, selected_recheck_request)`, `cancel_selected_recheck(torrent_handle, uint64_t)`, `finish_selected_recheck(torrent_handle, uint64_t, bool resume_network)`, and snapshot `query_selected_recheck(torrent_handle, uint64_t) -> selected_recheck_status`. Prepare drains peer/disk activity and holds the job quiesced; start is legal only in that state; terminal results remain held until finish after Eiskalt persistence. Invalid/stale tokens produce errors without changing another operation. Polling must not block for hashing.

- [x] Add dependency tests tagged `[selected-recheck-dependency]`. Start with v1 files A/B sharing a piece plus unrelated C. Assert exact disk read set, changed availability after same-size corruption, unchanged priorities, and continued transfer on a second job. The unpatched library must fail the capability test, not silently skip the test target.
- [x] Record RED using a dedicated dependency-test target `eiskalt-selected-recheck-dependency-tests`. Existing ordinary system-library builds remain usable; explicitly requested capability tests must fail until patched.
- [x] Pin `https://github.com/arvidn/libtorrent/releases/download/v2.1.1/libtorrent-rasterbar-2.1.1.tar.gz`, expected SHA256 `0f163516ecef2e3331500266751de3098835a3c3ae0c2290448046c632bc0e93` (local installed formula provenance). Verify downloaded bytes before extraction; preserve BSD-3-Clause notices and record the patch SHA256. Do not accept redirects to arbitrary schemes or silently substitute a newer version.
- [x] Implement the patch using libtorrent's native disk/hash/picker machinery, including per-job network inhibition before checking. Instrument disk reads in tests, not production. Ensure v2/hybrid validation, failure eviction, partial-block/hash-cache invalidation and local corruption without peer blame. No payload writes during verification.
- [x] Extend dependency tests for skipped-file partfiles, v2/hybrid, absent authenticated v2 hashes, zero-size/padding, cancellation, stopped local-only handle creation, and source-read-only storage. Unknown hashes must produce unavailable, not success. Assert zero network announcements and source writes.
- [x] Build in `Work/dependencies/selected-recheck/arm64` using the new script, `--prefix` and `--jobs 2`; expose script `--help` and reject any prefix outside the supplied development/release staging root. Compile/run `eiskalt-selected-recheck-dependency-tests '[selected-recheck-dependency]'`. Require all cases to pass and exact read-set evidence.
- [x] Stop here if safe local-only checking or native v2 state correction cannot be demonstrated. Record the failing contract and revise the design with the user; do not continue to UI work. Otherwise commit only this task's paths as `Add isolated libtorrent selected-recheck capability`.
### Task 2: Add A Stable Piece-Scope Planner

**Files:** Create `torrent/SelectedRecheck.h`, `torrent/SelectedRecheck.cpp`, `tests/torrent/test_selectedrecheck.cpp`. Modify both Torrent CMake files.

**Interfaces:** `RecheckPlan { QList<int> selectedFiles, affectedFiles, pieces; quint64 revision; qint64 totalBytes; }`; `RecheckPlanResult { std::optional<RecheckPlan> plan; QString error; }`; `planSelectedRecheck(const libtorrent::torrent_info&, const QList<int>& files, quint64 revision) -> RecheckPlanResult`. Indices refer to metadata, never view rows. Sum each affected piece's actual payload extent once; handle zero pieces explicitly.

- [x] Write `[selected-recheck-plan]` tests: duplicate selections deduplicate, overlapping boundaries count once, unrelated C is absent, invalid/empty input is rejected, padding/zero-size behavior is explicit, and selected IDs survive reordered view rows. Include a last partial piece and file sizes/offsets above 32-bit range.
- [x] Run `eiskalt-torrent-tests '[selected-recheck-plan]'` and record RED before implementing the planner.
- [x] Implement with native metadata/layout helpers, not filename heuristics. Keep path/storage validation separate; no I/O or mutation in the planner.
- [x] Run the same filter to GREEN and add metadata fixtures for v1/v2/hybrid. Commit the explicit files as `Plan selected torrent verification scopes`.

### Task 3: Implement Crash-Safe Pending Checks

**Files:** Create `torrent/RecheckJournal.h`, `torrent/RecheckJournal.cpp`, `tests/torrent/test_recheckjournal.cpp`; modify Torrent CMake files.

**Interfaces:** `RecheckRecord { QString jobId, metadataDigest, storageIdentity; quint64 token, revision, resumeGeneration; QList<int> selectedFiles, pieces; bool sourceReadOnly; }`; `RecheckJournal(QString stateDirectory)` with `begin(const RecheckRecord&, QString *error) -> bool`, `pending(const QString& jobId, QString *error) -> std::optional<RecheckRecord>`, and `finish(const QString& jobId, quint64 token, quint64 durableResumeGeneration, QString *error) -> bool`. A nonempty read error means blocked recovery, never no pending check.

- [x] Add `[selected-recheck-journal]` tests for interrupted atomic replacement, truncated/malformed input, token mismatch, old resume generation and removal before matching durable resume. Require corrupt records to block the affected job. Cap input at 16 MiB, indices at metadata's validated count and strings at existing engine metadata/path limits; reject duplicates/out-of-range pieces on reconciliation.
- [x] Run the filter to RED. Implement owner-controlled QSaveFile records using opaque safe job-derived filenames, with bounded decoding. `finish` rejects mismatched tokens/generations and keeps the pending record on persistence failure.
- [x] Add subprocess crash/fault injection around begin, result-save and finish. An older resume completion must not satisfy a newer operation's durable-generation requirement.
- [x] Run to GREEN; commit as `Persist selective verification recovery barriers`. Atomic app-crash recovery is required; do not claim power-loss guarantees beyond the existing persistence layer.

### Task 4: Integrate Engine Lifecycle And Recovery

Checkpoint: generation-fenced admission now quiesces only the selected native
job, writes a journal before invalidating pieces and commits a matching resume
snapshot before clearing that journal. Stopped checks use a local-only session.
Valid interrupted scopes are conservatively checkpointed and remain paused;
malformed or changed storage identities block the job. Real engine subprocess
exits cover journal/result/finalization boundaries. The maintained dependency
now saves the full conservative piece bitmap during selected checks rather
than using the full checker's sequential prefix. Extended held-I/O, partfile,
source-write, independent transfer, payload-replacement and timeout acceptance
pass. Maximum-scope owner admission fell from 71/73 ms to 2 ms. Focused adapter
tests pass 30 cases / 750 assertions; the full backend passes 149 / 18,238 and
native dependency tests pass 14 / 434. No UI, install or release proof is claimed.

**Files:** Modify `torrent/TorrentEngine.h`, `torrent/TorrentEngine.cpp`, `torrent/TorrentTypes.h`, `tests/torrent/test_torrent.cpp`; use Tasks 1-3 components without reimplementing hashing.

**Interfaces:** Add `recheckFiles(const QString& id, const QList<int>& indices, quint64 expectedRevision)` and `cancelFileRecheck(const QString& id, quint64 token)` owner-thread methods. Add `FileCheckOutcome { Pending, Verified, FailedPiece, Unavailable, Canceled }`, `FileCheckResult { int index; FileCheckOutcome outcome; QString detail; }`, `FileRecheckSnapshot { quint64 token; bool active, canceling; qint64 checkedBytes, totalBytes; QList<FileCheckResult> files; QString error; }` to job snapshots; new fields default safely for aggregate callers. Add capability query `supportsFileRecheck() const -> bool`.

- [x] Add `[selected-recheck-engine]` tests to the existing private engine-probe translation unit. Assert stale revisions and invalid indices reject before storage invalidation, user priorities remain identical, completion progress stays separate, and requests for conflicting operations return visible reasons. Run RED.
- [x] Implement validated admission, synchronous publication cancellation, worker preparation, journal begin, scoped invalidation and start in that order. Take no backend mutex while calling direct publication slots. Poll extension snapshots with bounded existing refresh cadence, reject stale tokens and keep the GUI responsive.
- [x] Commit final resume state with matching generation before journal finish and restoring activity. Gate every save-resume reply by generation; never assign an older snapshot over a completed check. Missing/corrupt pieces become unavailable and redownload only under permitted activity.
- [x] Implement stopped local-only checking and source-read-only protection proven by Task 1; prepare must never advertise stale seed state. Cancel drains reads and conservatively invalidates unchecked affected pieces. Pause changes subsequent activity intent; Stop cancels and wins over old running state; moves/selection/priority changes reject during the check.
- [x] Reconcile pending journals before normal `start`, completion or DC publication. Interrupted jobs remain paused, affected pieces untrusted, unrelated pieces intact. Offer recheck or explicit resume/redownload except read-only sources, which permit recheck only. Corrupt journal/unsafe paths block the job.
- [x] Test same-size replacement, moved/symlinked ancestor, delayed alerts, second-job traffic, configure/remove/shutdown during checks, disk-full, permission denial, cancellation, and restart. Verify zero source writes and zero direct-route fallback. Run `eiskalt-torrent-tests '[selected-recheck-engine]'` then the full backend suite; require zero failures. Commit as `Integrate selective checks with torrent lifecycle`.

### Task 5: Preserve Unrelated DC Hashes

Completed locally: validated affected metadata paths revoke only their owner's
TTH records after publication cancellation and before native check scheduling.
Count/byte accounting and replacement fencing remain correct; unrelated hashes
survive restart and reuse. Whole-Torrent Recheck behavior is unchanged. Focused
managed-sharing tests pass 30 / 353; full core passes 842 / 146,784 and full
maintained backend passes 150 / 18,335. TorrentRuntime consumers compile; patched
runtime/native Qt scenario acceptance remains in Task 7.

**Files:** Modify `dcpp/ShareManager.h`, `dcpp/ShareManager.cpp`, `torrent/TorrentEngine.h`, `torrent/TorrentEngine.cpp`, `eiskaltdcpp-qt/src/TorrentRuntime.cpp`, `tests/test_managedshare.cpp`, `tests/torrent/test_torrent.cpp`.

**Interfaces:** Overload `ShareManager::forgetManagedFileHashes(const string& owner, const StringList& absoluteFiles)`; retain the whole-owner overload. Add direct signal `fileHashRecheckRequested(QString id, QStringList affectedPaths)` following synchronous publication cancellation and validated request admission. Paths derive only from the validated affected-files plan, not UI input.

- [x] Write `[selected-recheck-sharing]` regressions: recheck A touching B's boundary drops A/B cache only, keeps unrelated C reusable across restart, and fences an in-flight replacement that captured old A/B records. Run core filter to RED.
- [x] Implement scoped record removal with accurate byte/count accounting and cache persistence, without reentrant callbacks under the share lock. Runtime may retract the whole job while checking; preserve unchanged file hash records.
- [x] Test private/excluded/wanted rules, cancellation/restart, changed path identities and selective republishing. Verify BitTorrent verification does not substitute for TTH hashing. Run focused tests plus existing managed-share tests to GREEN; commit as `Invalidate only affected torrent sharing hashes`.

### Task 6: Add Native Files-Table Controls

**Files:** Modify `eiskaltdcpp-qt/src/TorrentWindow.h`, `TorrentWindow.cpp`, `tests/qt/test_torrentwindow.cpp`, existing `eiskaltdcpp-qt/ts/*.ts` catalogs and `tools/translations/test_audit.py` as required by extraction.

**Interfaces:** Add private `recheckSelectedFiles()` and `cancelSelectedFileRecheck()`; controls consume Task 4 snapshots. Action object names: `torrentRecheckFiles`, `torrentCancelFileRecheck`. Selection uses metadata index item data and captured job revision, never checked/wanted state or visual row number.

- [x] Add `[selected-recheck-ui]` tests for multi-selection, sorting between confirmation and execution, unapplied edits, disappearing job, missing capability, disabled conflicts and cancel availability. Run Qt filter to RED.
- [x] Implement native context actions, scope/boundary confirmation, status `Rechecking selected files... %1%`, progress and outcomes; retain normal file priorities and download checkboxes. Show zero-piece checks without a bogus percentage. Confirm canceled unchecked pieces need verification/download before reuse.
- [x] Revalidate revision and metadata indices at execution. Render failed boundary pieces without falsely accusing one particular file. Full Recheck and Finder remain distinct actions.
- [x] Run focused Qt tests to GREEN, catalog/extraction audit, then native Cocoa light/dark screenshots and responsive resize/cancel checks on fixtures. Verify translated strings and accessibility names; commit as `Expose selected-file checks in torrent details`.

### Task 7: Package, Review And Accept

**Files:** Modify `cmake/SelectedRecheck.cmake`, `tools/dependencies/build-libtorrent.sh`, `dependencies/libtorrent/README.md`, `macos/normalize-bundle-dylib-ids.sh` only if the audit demonstrates a change is needed. Create `tools/dependencies/audit-selected-recheck.sh` and evidence under ignored `Work/qa/selected-recheck-20260930/`.

**Interfaces:** `EISKALT_REQUIRE_SELECTED_RECHECK=ON` requires extension capability at configure/link time. Audit takes a bundle path, expected architecture and deployment floor; fails on missing/mismatched ABI, wrong CPU, newer floor or dependencies outside the bundle/system allowlist. Ordinary unpatched dev builds keep capability OFF with explicit UI explanation.

- [x] Add audit negative fixtures for missing extension symbol, wrong header/library pair, outside-Homebrew dylib, wrong architecture and too-new deployment target. Record RED, then implement the smallest audit changes and run to GREEN.
- [x] Run current-source core, Torrent, Qt and DC-only builds/suites serially. Use the selected-recheck build tree for patched-dependent tests and record CMake cache, source/archive/patch hashes and binary paths. Never report old system-library test results as extension coverage.
- [x] Exercise a local peer download, selected check with injected corruption, permitted redownload and restart. Also verify a stopped/read-only check remains network-inactive and other jobs continue. Keep real user torrents/profile out of fixtures.
- [x] Build/audit the two approved CPU/OS flavors. Current-host ARM and Rosetta Intel ABI/version probes pass; native Intel/macOS14 and Apple Silicon/macOS26 machines are unavailable, so minimum-OS and full live-session acceptance remain pending. Compilation/cross-architecture inspection does not prove native minimum-OS behavior.
- [x] Request independent review of the extension, journal/state machine and publication/cache boundaries. If no reviewer tool is available, report independent review pending rather than impersonate a fresh reviewer. Resolve findings and rerun affected tests.
- [x] Checkpoint explicit packaging/doc changes only after the local audit/regression gates. Remaining minimum-OS, live-session, updater, installation and publication gates are recorded above. Do not push, install or publish merely because this feature's tests pass.

## Verification Commands And Evidence

Configure a dedicated `build-selected-recheck` by reproducing current `build-torrent` options and selecting the private prefix with `LibtorrentRasterbar_DIR`; do not repoint existing build trees in place. Capture both caches and exact configure command. The dependency-test target and capability CMake module are created by Task 1 before these commands are usable.

```sh
cmake --build build-selected-recheck --target eiskalt-selected-recheck-dependency-tests eiskalt-torrent-tests eiskaltdcpp-qt-tests -j2
build-selected-recheck/tests/torrent/eiskalt-selected-recheck-dependency-tests '[selected-recheck-dependency]'
build-selected-recheck/tests/torrent/eiskalt-torrent-tests '[selected-recheck-plan]'
build-selected-recheck/tests/torrent/eiskalt-torrent-tests '[selected-recheck-journal]'
build-selected-recheck/tests/torrent/eiskalt-torrent-tests '[selected-recheck-engine]'
build-selected-recheck/tests/torrent/eiskalt-torrent-tests
QT_QPA_PLATFORM=offscreen build-selected-recheck/tests/qt/eiskaltdcpp-qt-tests '[selected-recheck-ui]'
QT_QPA_PLATFORM=offscreen build-selected-recheck/tests/qt/eiskaltdcpp-qt-tests
cmake --build build-gost-core --target eiskaltdcpp-tests -j2
build-gost-core/tests/eiskaltdcpp-tests '[selected-recheck-sharing]'
build-gost-core/tests/eiskaltdcpp-tests
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/translations
git diff --check
```

Each command requires exit zero and no failed tests; an empty/missing test filter
does not count as a pass. New tests must be registered explicitly. Record skipped
external-GOST gates separately. Grant loopback/native GUI access only to temporary
fixture runs when the sandbox requires it. Record evidence per task, including
RED before GREEN, without copying production secrets or profile data.

## Self-Review And Handoff

Spec coverage: dependency/native hashing (1), scope/boundaries (2), crash recovery
(3-4), activity/read-only/network invariants (1,4), DC cache/publication (5), native
UX/localization (6), ABI/CPU/OS and regression/live gates (7). All five review-focus
conditions have explicit owning tests. Interface names/types are consistent
across tasks; implementation bodies are intentionally not prewritten.

Review this plan before execution. Native inline execution is already the user's
chosen method; do not ask them to select it again. After review, start Task 1,
not UI changes, updater activation or installation. A failed feasibility gate
requires reporting the constraint and revisiting design rather than inventing a
successful selective-check result.
