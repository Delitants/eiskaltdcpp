# AntiSpam Per-File Quit Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. The maintainer already selected Native/inline execution; do not request another execution-method choice.

**Goal:** Explicit quit recovery retries only AntiSpam files without checked commit receipts, without GUI blocking, duplicate writers or revived updater consent.

**Architecture:** AntiSpam retains one GUI-owned frozen snapshot and per-file checkpoint state. A value-only writer receives a copy of that state and returns a detailed receipt through the coordinator's existing owned thread-exit future. Both synchronous saves and asynchronous recovery use the same writer and receipt validator.

**Tech Stack:** Existing C++20, Qt6 Core/Widgets/Test, QSaveFile, std::thread/future, Catch2 and private macOS syscall interposers; no new external dependencies.

**Spec:** `docs/superpowers/specs/2026-10-07-antispam-per-file-quit-recovery-design.md`, approved on 2026-10-07 after `6d066c82a`.

Status: approved and executed Native/inline on 2026-10-07. Task1 is verified
and committed at `09a16b8f`; Task2's bounded final cold-build/matrix/evidence
acceptance is verified. Documentation checkpoint and post-commit confirmation
record the demonstrated scope. See `docs/research/2026-10-07-antispam-per-file-recovery.md`.

## Global Constraints

- The four files remain `antispam`, `blacklist`, `graylist` and `whitelist`, in that order.
- Preserve UTF-8 serialization, newlines, key uppercasing, empty-file handling and QSaveFile atomic replacement policy.
- Retries must not resolve paths again, reread disk or serialize live lists again.
- Public freeze/save and coordinator/session signatures stay unchanged; no ordinary save/load changes.
- Workers own values only; no QObject/context references, detached threads or worker-to-GUI progress mutation.
- Successful SearchBlacklist, GUI settings, core/layout/Torrent/sharing/proxy checkpoints are not replayed.
- Preserve original timeouts, polling intervals, quit prompt wording, explicit retry and permanent auxiliary-recovery update disarm.
- Capture or receipt-integrity failure is terminal for this quit session; valid file failures and proven unstarted launches may be retried explicitly.
- Preserve all seven existing dirty tracked files and unrelated untracked work; no broad staging, reset, amendment or replacement checkout.
- Native/inline implementation; independent review after the complete scoped patch, not separate implementation chats.
- Preserve earlier QA receipts and failed causes; no installation, profile/transfer/key/server/VM mutation, push, publication or feed activation. Signed feeds last.
- QA macOS build number is `1`, not a production allocation; ARM target `26.0`, Intel target `14.0`, host/Rosetta results are not native minimum-OS proof.

## Review Focus

- A direct synchronous call during an outstanding asynchronous job must not start another writer or erase prior receipts; Task2 tests this while I/O is held.
- A replaced/destroyed owner must not receive an old job's receipt, even if its paths match; Task2 exercises both timings with an owned pending job.
- A broken future may become ready before thread exit; Task2 checks that exception handling retains the job rather than joining on the GUI without physical readiness proof.
- Copy/capture/launch failure must not lose prior committed bits or turn unknown writes into retryable ones; Tasks1/2 test state admission and private fault controls.
- A ready/consented update must remain disarmed after partial save recovery, including a timed-out retry; Task2 checks the fake backend never receives install/handoff calls.

## File Map

- Create `eiskaltdcpp-qt/src/AntispamQuitPersistence.h` and `.cpp`: value types, file writer and GUI-owned checkpoint state machine; no QObject or thread ownership.
- Modify `eiskaltdcpp-qt/src/Antispam.h` and `.cpp`: capture one snapshot, guard owner/thread/admission, bridge public synchronous saves and private coordinator operations.
- Modify `eiskaltdcpp-qt/src/updates/ApplicationQuitCoordinator.h` and `.cpp`: detailed auxiliary receipts, launch rollback and explicit retry eligibility; retain the existing job owner.
- Create `tests/qt/test_antispamquit.cpp`: pure checkpoint and minimal-owner save regressions.
- Create `tests/qt/test_antispamquitrecovery.cpp`: composed session/owner/update and hidden held-I/O/control tests using ApplicationQuitTestFixture.
- Modify `tests/qt/CMakeLists.txt`: add the value-writer source and minimal tests, without staging its earlier unrelated dirty hunks.
- Modify `eiskaltdcpp-qt/CMakeLists.txt`: add composed tests to both existing full-source settings and main-window test consumers. The value-writer `.cpp` already falls under the existing top-level source collection after reconfiguration.
- Create `docs/research/2026-10-07-antispam-per-file-recovery.md` after execution: actual evidence, limits and remaining gates. Update this plan's checkboxes only from receipts.
- Private QA files live in a new `Work/qa/antispam-per-file-recovery-20261007/`; never reuse an existing directory destructively.

## QA Harness Contract

Task1's preflight creates private `prepare.sh`, `build.sh`, `run-case.sh`,
`check-results.py` and `seal.py` in the new QA directory. They are not release
sources. If that directory already exists, use a new timestamp-suffixed directory
and record it in the report; commands below use `qa` to identify the actual path.

- `prepare.sh`: capture HEAD, exact dirty patch, tracked sources plus the existing test-only allocation helper, protected-state manifests and hashes of retained build/provider/QA inputs. Fail if the expected seven-file baseline or empty index changed unexpectedly.
- `build.sh CPU PHASE`: CPU is `arm64`, `x86_64` or `no-torrent`; PHASE is `red`, `green` or `final`. Create fresh phase-specific source/build directories and use the inspected `quit-current-matrix-20261007/build-tests.sh` and `build-no-torrent.sh` recipes with only destination/source-snapshot changes. Keep presets, dependency/provider prefixes, Catch2 source pin and QA key/public metadata unchanged. Build Qt, settings and available main-window consumers with at most two compiler jobs; never execute the old scripts against their sealed destinations.
- `run-case.sh CPU PHASE CONSUMER LABEL SELECTOR`: CONSUMER is `qt`, `settings` or `gui`. Run the phase binary through the preserved loopback-only disposable-HOME wrapper `Work/qa/core-write-repair-20261005/run-tests.sh`; configure matching provider and Qt plugin paths, seed `594128903`, JUnit/log/exit receipts and fail-on-existing LABEL. Support the existing `EISKALT_QA_INTERPOSER` and `EISKALT_TEST_TAB_LAYOUT` inputs. No build or heavy hashing overlaps timed tests.
- `check-results.py red|green|final`: compare exact assertion failures for RED; require zero failures/errors/skips/fatal exits and complete case/section inventories for passing runs. Do not infer success from exit status alone or hardcode historical suite counts as current counts.
- `seal.py`: verify captured build inputs, unchanged baseline repair bytes and protected-state readback, then seal and fully reread new evidence. Never rewrite previous seals or failure receipts.

---

### Task 1: Per-File Checkpoint And Save Integration

**Files:** All value-writer, AntiSpam, coordinator, two new test and CMake files in the File Map. ApplicationQuitSession remains unchanged unless a regression demonstrates a required spec-preserving eligibility propagation fix.

**Interfaces:**
- New namespace `antispamquit`: `using Mask = std::array<bool, 4>`; `File { QString path; QByteArray bytes; }`; `Snapshot { QUuid id; std::array<File, 4> files; }`; `Token { QUuid snapshot; QUuid attempt; }`; `Attempt { Snapshot snapshot; Token token; Mask committed{}; }`; `Receipt { Token token; Mask committed{}; }`. Token equality compares both UUID fields; each snapshot and each admitted attempt uses a fresh non-null UUID. These are local correlation values, not authentication credentials.
- `enum class Status { NotStarted, Pending, Ready, RetryableFailure, TerminalFailure }`.
- Value writer: `Receipt execute(const Attempt&) noexcept`; catch failures per file, preserve prior true bits and return a complete value receipt.
- GUI-owned `Checkpoint`: `explicit Checkpoint(Snapshot)`; `std::optional<Attempt> begin()`; `Status complete(const Token&, const std::optional<Receipt>&) noexcept`; `bool cancelUnstarted(const Token&) noexcept`; `Status status() const noexcept`; `Mask committed() const noexcept`.
- `begin()` copies the full payload before publishing Pending; a null UUID or invalid receipt makes the state terminal. Calls while Pending/Ready/TerminalFailure return no new attempt. A payload-copy exception occurs before admission and leaves the snapshot/prior mask available for explicit retry. `cancelUnstarted()` requires the matching pending token, preserves bits and makes explicit retry eligible. Completion validates both token fields and mask monotonicity before adopting any new bits.
- AntiSpam private bridge: `std::optional<antispamquit::Attempt> beginQuitSave()`; `bool cancelUnstartedQuitSave(const antispamquit::Token&) noexcept`; `antispamquit::Status finishQuitSave(const antispamquit::Token&, const std::optional<antispamquit::Receipt>&) noexcept`; `bool quitSaveRetryable() const noexcept`. Retain `std::optional<antispamquit::Checkpoint> quitCheckpoint` and sticky `quitCaptureFailed` on the GUI owner; remove the old boolean-only private `quitSaveOperation()` when its coordinator caller migrates.
- Coordinator's private `AuxiliaryResult` adds `bool retryable = false`. `AuxiliaryWrite::Receipt` contains `bool search = true` and `std::optional<antispamquit::Receipt> spam`; the job also retains the expected optional AntiSpam token and a terminal-untrusted-completion flag.
- Test helpers in the new sources: scoped minimal/core GUI fixture, `readBytes(const QString&) -> QByteArray`, `writeMarker(const QString&) -> void`, and existing full `IsolatedMainWindow`, `waitFor`, `QuitBackend`. Helpers restore context/environment and assert all temporary file operations.

- [x] **Step 1: Capture preflight and write current-behavior regressions.** Add `AntiSpam retry preserves successful file commits` in the minimal test source and `Session AntiSpam recovery preserves successful file commits` in the composed source, tags `[antispam-file-recovery]`. Generate each blocked filename from the four fixed names. Use only current public APIs for these first tests; do not require new types to reproduce the existing bug. First save must fail, every other file must exist, and the repaired retry must finish without changing markers placed in successful files:

```cpp
// Per generated blocked file, after collecting the first refused save:
for (const auto& path : successfulPaths) writeMarker(path);
REQUIRE(QDir(blockedPath).removeRecursively());
// Direct: REQUIRE(spam.saveForQuitChecked());
// Session: request(3000), then require exactly one exitReady via waitFor.
for (const auto& path : successfulPaths)
    CHECK(readBytes(path) == QByteArray("already-saved-receipt\n"));
```

- [x] **Step 2: Establish exact behavioral RED on both CPUs.** Configure the two new test sources against captured unchanged production sources, then run:

```bash
bash "$qa/build.sh" arm64 red
bash "$qa/build.sh" x86_64 red
for cpu in arm64 x86_64; do
  bash "$qa/run-case.sh" "$cpu" red qt "red-direct-$cpu" '[antispam-file-recovery]' || :
  bash "$qa/run-case.sh" "$cpu" red settings "red-session-$cpu" '[antispam-file-recovery]' || :
done
python3 "$qa/check-results.py" red
```

Expected: exactly 12 marker-preservation failures per consumer per CPU (three successful files for each of four blocked filenames), no other failed assertion or missing receipt. The validator, not `|| :`, decides whether this is the intended RED. Retain these binaries and receipts unchanged.

- [x] **Step 3: Add value-state tests and implement AntispamQuitPersistence.** Name cases `Checkpoint skips committed files`, `Checkpoint accumulates partial receipts`, `Checkpoint rejects duplicate admission`, `Checkpoint rejects foreign and regressing receipts`, and `Checkpoint retains state after unstarted cancellation`, tags `[antispam-file-checkpoint]`. Pin zero-initialized masks, token equality, Ready/RetryableFailure/TerminalFailure transitions and null UUID refusal. Exercise real directory-blocked files and subsequent commits with the writer; exceptions are caught inside each per-file attempt, never by discarding the whole updated mask. Invalid receipts leave previous bits unchanged:

```cpp
CHECK(checkpoint.status() == antispamquit::Status::Pending);
CHECK_FALSE(checkpoint.begin().has_value());
CHECK(checkpoint.complete(token, forgedReceipt) ==
      antispamquit::Status::TerminalFailure);
CHECK(checkpoint.committed() == previousMask);
CHECK_FALSE(checkpoint.begin().has_value());
```

- [x] **Step 4: Implement the AntiSpam private bridge and synchronous save.** After GUI producer fencing, freeze mutations and atomically capture the four existing serializations with one non-null `QUuid::createUuid()` snapshot id. Keep capture failure sticky; never publish partial snapshots or unfreeze on failure. A retained Ready save returns true without I/O, Pending refuses without altering receipts, and other eligible saves begin, execute and accept one value attempt. `quitSaved` is derived only from a valid all-true mask. Add `Frozen AntiSpam serialization and paths survive retries` covering uppercase keys, Unicode lists, empty files, rejected mutations and changed global Util paths after the initial snapshot; restore paths before fixture teardown.

- [x] **Step 5: Integrate detailed asynchronous receipts and eligibility.** Allocate job/promise ownership before launch; retain expected tokens independently of future contents. Mark AntiSpam pending before thread submission. If no worker started, rollback only the matching unstarted token and keep the frozen snapshot/mask. The worker captures values only, executes search and spam independently and uses `set_value_at_thread_exit` for normal completion. Collect valid AntiSpam bits even when search failed, and report Ready only when every required receipt is Ready. Capture/receipt-integrity refusal sets auxiliary retryable false; valid partial save or proven failed launch sets it true. Initial checkpoint eligibility still requires safe primary owners and saved GUI settings; retry eligibility must also honor the new terminal distinction.

If `future::get()` throws, do not treat ready-with-exception as physical thread-exit proof: mark completion untrusted, retain the job and terminal refusal, and never join it on the normal GUI poll path. Valid thread-exit-delivered missing/foreign content can retire its finished job but cannot authorize retry or exit. Preserve existing forced-destructor join semantics; no detached cleanup.

- [x] **Step 6: Verify direct and composed GREEN on fresh consumers.** Build `green` for each CPU, run Qt `[antispam-file-recovery],[antispam-file-checkpoint]` and settings `[antispam-file-recovery],[settings-quit]`, then `check-results.py green`. Require all four direct and session failure positions recovered, exact frozen bytes and no marker replay. Repeated saves after Ready must make no writes. No change to original 5000ms helpers or request budgets.

- [x] **Step 7: Review and checkpoint only Task1-owned deltas.** Inspect the exact diff, validate current baseline preservation and stage new files plus clean owned files. For the already-dirty `tests/qt/CMakeLists.txt`, stage only the verified delta relative to preflight with a noninteractive cached patch, not the whole file. Commit as `fix(quit): retain AntiSpam per-file save receipts`. Do not commit the seven earlier repair changes. This checkpoint is not final recovery acceptance.

### Task 2: Retained-Worker, Integrity And Composed Acceptance

**Files:** Extend the two new test sources; repair only Task1's scoped production interfaces if their tests expose a defect. Private fault controls/harness are under the new QA directory. Add the execution report and update plan/evidence status after verified results.

**Interfaces:** Consume Task1's types/Checkpoint, unchanged ApplicationQuitSession signals/status, existing `CoreWriteProbe::arm(path, mode)` (`1` write, `3` flush, `6` thread-exit hold, `7` launch failure), IsolatedMainWindow and QuitBackend. No product-only fault API, friend backdoor or changed test deadline is introduced.

- [x] **Step 1: Write retained partial-recovery cases.** Add hidden `[.antispam-file-retirement]` cases `Held AntiSpam partial recovery retains one writer` and `AntiSpam thread-exit hold refuses late recovery`. First collect a real partial refusal, mark receipted AntiSpam/search/core/GUI files, repair only the failed file and hold its new write/flush or TLS teardown. Pre-retire primary owners before arming the launch probe so it targets the auxiliary worker. Use the existing 100ms request, 350ms watchdog hold, 5000ms wait helper and GUI heartbeat. While pending, invoke direct `saveForQuitChecked()` and repeat coordinator polling: both must preserve one writer, existing bits and markers. In the private interposer copy, count matching write-syscall entries before the hold and expose `eiskalt_qa_matching_writes()` through dlsym in the test; reset the counter when armed. Repeated polling/direct calls and Ready rechecks must not increase this counter. Preserve marker bytes and `stat` device/inode identity for already committed files. After timeout/release, require no late exit; an explicit request consumes the original job. Start with two blocked files, repair one, collect another partial failure, then repair the last and require a further explicit request rather than silently launching another writer.

- [x] **Step 2: Write owner and consent cases.** Name `Pending AntiSpam receipt cannot adopt a replacement owner`, `Lost AntiSpam owner blocks partial recovery`, and `Consented update stays disarmed through AntiSpam recovery`. Destroy/replace the owner before releasing the held worker and separately after a collected partial failure; wait for safe value-owned completion without adoption or exit. Use a Ready-consented QuitBackend in both ordinary recovery and a held timeout variant; assert `installs == 0`, one producer fence, revoked/disarmed consent and NoUpdate disposition after successful recovery. Also exercise a launch failure with that Ready update, retained snapshot and explicit retry. Add `Commit refusal preserves earlier AntiSpam receipts`: hold a nonempty file's flush before its first commit, create a directory at its still-absent destination in the disposable profile, release the hold and require real commit refusal plus receipts for the other files; repair the directory and verify selective retry. Keep existing foreign/modal/reentrant tests unchanged.

- [x] **Step 3: Verify invalid receipts and early future failure in private controls.** Extend pure Checkpoint tests for null content, wrong UUID/attempt, regressing mask and rejected duplicate/stale completion. Add hidden `[.antispam-receipt-control]` composed cases expecting terminal Refused, no Retry admission, no exit/install and unchanged prior markers. Privately relink one captured coordinator object per control, without altering workspace production sources: remove `saved.spam` before normal thread-exit publication; substitute a foreign token; publish an exception at thread exit; and publish an exception before a scoped mode-6 teardown hold. Apply each replacement exactly once against the captured source, record original/mutant hashes and fail closed on an unknown layout. The early-exception control must retain a responsive GUI and its owned job until the hold is released; never destroy that retained job while the hold is active. These are fault-path acceptance runs, separate from regular binaries and behavioral RED receipts.

Add private capture/copy controls using the same strict one-object relink approach, with hidden `[.antispam-allocation-control]` cases `Incomplete AntiSpam capture is terminal` and `Failed attempt copy retains committed receipts`. The capture variant throws `std::bad_alloc` immediately before publishing the complete snapshot; require frozen mutation rejection, no file I/O and no checkpoint retry. The copy variant throws once during Attempt construction before Pending is published, after a collected partial receipt; the next explicit request must preserve successful markers and write only remaining files. These mutations are private source copies, not product test hooks.

- [x] **Step 4: Run focused holds and controls serially on both CPUs.** Freshly compile a private copy of `tests/macos/updater/worker_io_hold.cpp` with only the write-counter addition into matching-CPU interposers; preserve the original source and old dylibs. Use the phase `green` run wrapper with the matching interposer for `[.antispam-file-retirement]` and existing `[.auxiliary-write-retirement]`. Use each sealed control binary with its specific named case under `[.antispam-receipt-control]` or `[.antispam-allocation-control]`, so a control never selects unrelated fault expectations. For every selector require complete expected case/section inventory and zero failed assertions/errors/skips. Investigate any failure before changing code; preserve original failing receipts and do not weaken predicates or extend deadlines.

- [x] **Step 5: Build final coherent test consumers and run broad acceptance.** Rebuild into empty `final` directories after all source/test changes. Build ARM and Intel/Rosetta Qt/settings/GUI plus ARM no-Torrent Qt/settings consumers using unchanged dependency recipes and actual architecture/minimum-target readback. Run serially:

```bash
for cpu in arm64 x86_64; do
  bash "$qa/build.sh" "$cpu" final
done
bash "$qa/build.sh" no-torrent final
for cpu in arm64 x86_64; do
  bash "$qa/run-case.sh" "$cpu" final qt "final-qt-$cpu" '~[interop]'
  bash "$qa/run-case.sh" "$cpu" final settings "final-settings-$cpu" '[settings-quit],[antispam-file-recovery]'
  for layout in singleline multiline sidebar; do
    selector=''; [ "$layout" != sidebar ] || selector='~[tab-navigation-mainwindow]'
    EISKALT_TEST_TAB_LAYOUT="$layout" bash "$qa/run-case.sh" "$cpu" final gui "final-gui-$layout-$cpu" "$selector"
  done
done
bash "$qa/run-case.sh" no-torrent final qt final-qt-no-torrent '~[interop]'
bash "$qa/run-case.sh" no-torrent final settings final-settings-no-torrent '[settings-quit],[antispam-file-recovery]'
python3 "$qa/check-results.py" final
```

The final settings consumers must additionally run all existing hidden settings storage/write/ready/admission retirement and composed Torrent/settings-freeze selectors with their matching private interposer. Rerun the Task2 holds on final GUI/settings consumers. Compare against retained ordinary/hidden case inventories, including the established-relay case added at `9b88fab06`; unchanged suite totals are not assumed. Do not claim a new full core/Torrent matrix or whole-app rebuild from these scoped consumers.

- [x] **Step 6: Obtain scoped independent review and close only proved gates.** Request a fresh independent reviewer for the immutable complete AntiSpam patch, spec, plan and captured evidence. Focus on exception safety, single-writer admission, per-file masks, owner/token identity, thread-exit readiness and permanent recovery disarm. Implement accepted fixes with their own RED/GREEN evidence; rerun affected final consumers before claiming acceptance. No review finding is waived by an unrelated green suite.

- [x] **Step 7: Preserve, report and checkpoint.** Run protected-state/baseline/provider/source readback and new evidence seal verification. Record exact binaries, source/input hashes, selectors, assertion and case inventories, fault-control boundaries and independent review result in the research report. Update the spec/plan's actual execution status and the existing development/updater pending ledgers only to the demonstrated scope. Stage only Task2-owned deltas and commit as `test(quit): verify AntiSpam partial-save recovery lifecycle`; leave unrelated work local. Report remaining historical causes, native/live/localization/provenance and final release gates explicitly. No signing, installation, push, publication or feed change.

## Plan Review Gate

The written spec and plan are approved for implementation. Execute both
Native/inline without another method-selection prompt.
Pause only for a new safety/scope decision, unexpected workspace mutation or
evidence that requires architectural revision, not routine test/build progress.
