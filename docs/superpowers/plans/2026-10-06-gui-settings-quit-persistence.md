# GUI Settings Quit Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. The maintainer's previously selected Native/inline method is preserved; no implementer delegation or per-task reviewer. One fresh-context review of the completed change range precedes acceptance.

**Goal:** Save frozen GUI settings without blocking the event loop on physical I/O, and prevent an expired quit attempt from authorizing exit or updater handoff.

**Architecture:** Keep `WulforSettings` GUI-owned with frozen value-based reads. An owned job transfers its original parentless settings storage through affinity-free state to a settings-only worker; checked sync, storage destruction and thread exit precede readiness. Cache GUI retirement separately from settings persistence and enforce the quit deadline again before commitment.

**Tech Stack:** C++20, Qt, `QSettings::IniFormat`, owned `std::thread`/future receipts, Catch2/QtTest, disposable macOS syscall interposition.

**Spec:** `docs/superpowers/specs/2026-10-06-gui-settings-quit-persistence-design.md`, approved by the maintainer after checkpoint `bcf1c1390` on 2026-10-06. Tasks 1/2 and review fixes are implemented. Task 3 final acceptance remains open after retained recovery and fresh held-thread-exit responsiveness failures; installation and release gates remain separate.

## Global Constraints

- Use the existing `/Users/neolo/ollama_eiskaltdcpp` checkout, branch `codex/v3.0.0-pre.1`; preserve unrelated work. Starting source checkpoint: `bcf1c1390cadbcacc5aa708c0cfb25146f9a1879` plus this documentation checkpoint.
- Keep the installed application, live profile/downloads/shares, private credentials/server settings, signing keys, VM, Docker, historical bundles/providers and prior failed evidence unchanged.
- Preserve the settings path, `IniFormat`, atomic-sync policy, public getter/setter behavior and legacy XML `hasKey()` semantics.
- `WulforSettings`, contexts and GUI owners stay on the GUI thread. Only frozen parentless private storage transfers; no competing same-file settings writer or synthetic replacement INI.
- Start one owned settings job; no detached/terminated workers, no GUI-side join while Pending, and no success before storage destruction and physical thread-local teardown.
- Terminal GUI settings/launch/affinity failures refuse quit and retain intent. Do not reset sticky errors by reopening storage or add automatic recovery from terminal GUI settings failure.
- Timeout requires a new explicit quit request. Existing valid update consent can be revalidated on that request; auxiliary-failure recovery still permanently disarms installation.
- Preserve the original 5000ms natural-recovery wait and assertions. Controlled holds remain 350ms against a 100ms quit budget; no deadline widening, skip or green-rerun waiver.
- Use disposable profiles and verified external-network-denied/loopback-only test processes. Compile and run application tests serially, without compiler/test overlap.
- Verification is not installation, real signed-updater acceptance or native minimum-OS acceptance. Approved split remains Intel-only macOS 14+ and Apple-Silicon-only macOS 26+; host/Rosetta is not a substitute.
- No push, production signing/notarization/build allocation, installation, wiki/prerelease publication or feed activation. Signed feeds remain last.

## Review Focus

- Pending Qt auto-sync events must not write on the GUI after transfer; Task 1 tests continued event delivery and physical file markers.
- Thread-constructor or affinity failure must not destroy dirty storage on the GUI; Task 1 checks retained values, unchanged physical bytes and terminal refusal.
- A first Ready result can arrive after the budget without any earlier Pending; Task 2 tests a fully pre-retired session with an immediately expired zero budget.
- Cached reads must preserve normalized keys, invalid/empty variants and platform case behavior; Task 1 compares literal values and native Qt lookups before/after freeze.
- Polling/explicit retry must not repeat settings/layout/Torrent/core writes or revive auxiliary-failure install consent; Task 2 uses completed-file markers and existing recovery regressions.

## Files And Boundaries

- Modify `eiskaltdcpp-qt/src/WulforSettings.{h,cpp}`: owned storage, frozen reads, checked quit job and cached receipt. Keep implementation-private job in the `.cpp`.
- Modify `eiskaltdcpp-qt/src/MainWindow.{h,cpp}`: separate cached GUI snapshots/producer retirement from settings polling.
- Modify `eiskaltdcpp-qt/src/updates/ApplicationQuitCoordinator.cpp`: pass the existing asynchronous-persistence selection into GUI retirement and consume the settings receipt.
- Modify `eiskaltdcpp-qt/src/updates/ApplicationQuitSession.cpp`: post-Ready deadline check before ordinary exit or updater commitment.
- Modify `tests/qt/test_wulforsettings.cpp`: real settings and cached-read regressions.
- Create `tests/qt/ApplicationQuitTestFixture.h`: extract the existing `IsolatedMainWindow`, fake `QuitBackend` and `waitFor` helpers from `test_mainwindowtoggle.cpp` without changing their behavior or 5000ms wait. Guard only the Torrent settings seeding with `USE_TORRENT`; existing tests reuse this header.
- Create `tests/qt/test_settingsquit.cpp`: generic real-I/O and composed settings/quit regressions, without unconditional Torrent headers or fixtures.
- Modify `eiskaltdcpp-qt/CMakeLists.txt`: compile the new source once into the existing toggle target, and add `eiskalt-settings-quit-tests` whenever `BUILD_TESTS` is enabled, independently of `USE_TORRENT`. Reuse full-GUI sources excluding `main.cpp`, Qt resources/MOC and platform libraries; link `eiskalt-torrent` and Torrent resources only when enabled. Do not relax the existing toggle target's Torrent guard.
- Create `tests/qt/WulforSettingsTestAccess.h`: fixture-only inspection of storage identity/affinity for real destructor and failure observations; no production cleanup/injection method. Add only a friend declaration if needed.
- Reuse `tests/macos/updater/CoreWriteProbe.h` and `worker_io_hold.cpp`; modify them only if an observation cannot be made with existing modes 1 (write), 3 (flush), 6 (TLS teardown), 7 (launch failure).
- Update the approved spec status, parent updater plan and `docs/wiki/Development-Status.md` with actual results/remaining gates, not predictions.

## Private Build And Test Commands

During execution create a new, non-existing `Work/qa/gui-settings-write-repair-20261006` directory (`QA`). If it already exists, append a unique suffix; never overwrite it. Record the chosen absolute path in the ledger.

Copy `Work/qa/release-continuation-20261005/review-fixes/build-tests.sh` to `$QA/build-tests.sh`; change its `qa` output root to `$QA` and add `eiskalt-settings-quit-tests` to its test-target lists after introducing that target. Keep all other provider/toolchain/stage hashes, local Catch2 source and ad-hoc QA build number `1`; verify those exact copied-script differences before execution. This performs fresh CMake configuration and complete source/MOC/resource builds into `$QA/{arm64,x86_64}-build-final`, not partial old-object relinks. Native metadata/packaging inputs are needed by the existing native test targets; historical stages remain read-only. Build only test targets, not the distributable application or install target.

```bash
bash "$QA/build-tests.sh" arm64
bash "$QA/build-tests.sh" x86_64
```

Build/run serially; each command must finish before the next application test or compiler phase. Retain child exit codes, configure/build logs and source/binary hashes. No network FetchContent: the recipe already sets `FETCHCONTENT_SOURCE_DIR_CATCH2` to the local source. Verify deployment targets and CPU before trusting each binary.

Use the retained `Work/qa/core-write-repair-20261005/run-tests.sh` runner, without altering its inner sandbox. Run outside the outer Seatbelt only when approved; its external-network EPERM/EACCES assertion remains mandatory. Set these explicit environments per CPU:

| CPU | `EISKALT_QA_PROVIDER_LIB` | `EISKALT_QA_QT_PLUGINS` | Interposer for held tests only |
| --- | --- | --- | --- |
| ARM | `Work/qa/partfile-fence-repair-20261004/provider/arm64-complete/lib` | `/opt/homebrew/share/qt/plugins` | `Work/qa/core-write-repair-20261005/io-hold-v2.dylib` |
| Intel/Rosetta | `Work/qa/partfile-fence-repair-20261004/provider/x86_64-complete/lib` | `Work/qa/updater-split-packaging-20261004/intel-qt-thin/plugins` | `Work/qa/core-write-repair-20261005/io-hold-x86_64-v2.dylib` |

Resolve table paths against the repository. Set `EISKALT_QA_INTERPOSER` only for held tests and unset it for ordinary suites. `B=$QA/<cpu>-build-final`; `RUN=Work/qa/core-write-repair-20261005/run-tests.sh`. Each invocation gets a unique XML/log/exit output path:

```bash
sh "$RUN" "$B/tests/qt/eiskaltdcpp-qt-tests" '[gui-settings-freeze]'
sh "$RUN" "$B/eiskaltdcpp-qt/eiskalt-settings-quit-tests" '[.gui-settings-write-retirement]'
sh "$RUN" "$B/eiskaltdcpp-qt/eiskalt-settings-quit-tests" '[.gui-settings-ready-retirement]'
```

Use `--reporter xml --out <unique-path>` for evidence. Strictly validate complete XML, selected case names/counts, actual assertion failure sets, child exits, no exceptions/fatal errors/skips and verified sandbox trace. Expected RED is not a blanket acceptance of a nonzero process.

### Task 1: Owned Frozen Settings Persistence

**Files:** `WulforSettings.{h,cpp}`, `test_wulforsettings.cpp`, new `WulforSettingsTestAccess.h`, `ApplicationQuitTestFixture.h` and `test_settingsquit.cpp`; existing `test_mainwindowtoggle.cpp` and Qt CMake for shared fixture/target wiring, as mapped above.

**Interfaces:**
- Produce `WulforSettings::QuitSaveStatus { NotStarted, Pending, Ready, Refused }` and `QuitSaveStatus saveForQuitChecked(bool asynchronous = false) noexcept`.
- Keep `void freezeForQuit()`, `bool saveChecked()` and public getters/setters. `freezeForQuit()` is idempotent; publish the frozen cache only after complete capture.
- Add private `QVariant value(const QString& key, const QVariant& fallback) const`, `std::unique_ptr<QSettings> settings`, `QMap<QString, QVariant> quitValues`, forward-declared `struct QuitWrite`, owned `std::unique_ptr<QuitWrite> quitWrite` and cached `QuitSaveStatus quitSaveStatus`.
- Foreign calls to the checked quit API return Refused without changing the owner-thread attempt. No new updater/controller interface.

- [x] **Step 1: Add real-store failing regressions.** Extract the shared fixture and wire the standalone target first; run the unchanged existing recovery selector to detect extraction regressions. Use isolated config paths in `test_wulforsettings.cpp` tagged `[gui-settings-freeze]`, and hidden real-I/O tests in `test_settingsquit.cpp` tagged `[.gui-settings-write-retirement]`. Use a test-local template adapter that reports an explicit missing-API assertion against the baseline, not a compiler error.

```cpp
// Literal cached values/defaults, also checked while the worker is held:
store.setStr("qa/value", "before");
store.setVar("qa/bytes", QByteArray("\0x", 2));
store.freezeForQuit();
store.setStr("qa/value", "late");
CHECK(store.getStr("qa/value") == "before");
CHECK(store.getStr("qa/missing", "fallback") == "fallback");
CHECK(store.getVar("qa/bytes").toByteArray() == QByteArray("\0x", 2));
// Owned asynchronous save under real hold:
CHECK(elapsed.elapsed() < 100);
CHECK(writerThread != guiThread);
CHECK(heartbeatAdvanced);
CHECK(statusWhileHeld == Pending);
CHECK(statusAfterPhysicalExit == Ready);
```

Also cover integer/bool/string-list values, invalid versus empty variants,
Unicode and `/qa//value/` normalization; test native platform case behavior with
literal expectations and a separate real Qt reference before transfer. Capture
all pre-existing auto-sync events without processing them until after launch;
GUI event processing during the hold must not block or flush frozen storage.

- [x] **Step 2: Run and seal RED.** Build current-source baseline test consumers and run both selectors with modes 1/3; verify the exact real-thread/heartbeat/status failures and preserve baseline source/object inputs. The existing private diagnostic reports remain historical evidence, not a fresh RED for a different test body.
- [x] **Step 3: Implement storage ownership and frozen readers.** In `WulforSettings.{h,cpp}`, retain the original writer, normalized read semantics and all direct mutator guards, including `loadTheme()`. Freeze values on the GUI, detach only parentless storage to null affinity, then acquire on the worker and check affinity by postcondition (also supports `void`-return Qt APIs). Do not use a second same-file object for frozen reads.
- [x] **Step 4: Implement checked job lifetime.** `QuitWrite` owns storage/thread/future; construct owning state before thread launch. Worker code operates on job/storage only, checks `sync()` status, destroys correctly-affine storage and publishes at thread exit. Nonblocking polling consumes the cached result; join only after readiness. Keep failed-launch/affinity storage retained, with no temporary-lambda GUI destructor flush. Never clear sticky errors. `saveChecked()` delegates to the cached quit result once frozen; before freeze its synchronous behavior stays unchanged.
- [x] **Step 5: Extend boundary regressions before finalizing.** Observe actual storage `QObject::destroyed` with a DirectConnection held by a watchdog to prove destructor completion is required; mode 6 separately holds TLS teardown. Mode 7 proves constructor failure retains intent and disk markers while GUI events continue. Include unwritable/malformed INI, prior sticky error, foreign caller, repeat polling/no new thread and real-storage affinity validation refusal. TestAccess only exposes fixture inspection, not new production behavior.
- [x] **Step 6: Run GREEN and ordinary settings suite.** Run the selectors above plus `[wulforsettings]` without an interposer on both CPUs; require no unrelated failures/errors/skips. Verify reopened physical values after Ready and marked bytes unchanged on subsequent checks. Physical holds must retain owner/job until watchdog release; no test pumps application callbacks inside the worker.
- [x] **Step 7: Commit only owned storage/tests paths.** Use `git diff --check`, curated staging, source/hash readback and a local `fix(settings): own frozen quit persistence off the GUI thread` checkpoint. No push or deployment.

### Task 2: Compose GUI Retirement And Deadline Admission

**Files:** `MainWindow.{h,cpp}`, `updates/ApplicationQuitCoordinator.cpp`, `updates/ApplicationQuitSession.cpp` and `tests/qt/test_settingsquit.cpp`; retain existing recovery cases in `test_mainwindowtoggle.cpp` unchanged.

**Interfaces:**
- Consume Task 1's `WulforSettings::saveForQuitChecked(bool)` and cached status.
- Change `MainWindow::stopWorkersForQuit(bool asynchronousPersistence = false)`; preserve existing bool return and `quitWorkersPending() const`.
- Add private `quitSnapshotsComplete` and `quitSnapshotsSaved` fields alongside existing drain receipts. They distinguish completed producer/layout/Torrent retirement from Pending settings.
- `ApplicationQuitCoordinator::prepare(uint32_t, bool)` stays unchanged externally; pass its bool to primary GUI retirement. No new session API or installer capability.

- [x] **Step 1: Promote composed failing tests.** Adapt the two retained private diagnostic bodies into `test_settingsquit.cpp` cases tagged `[.gui-settings-write-retirement]` and `[.gui-settings-ready-retirement]`. Core Ready before arming hold isolates GUI settings. Keep 350ms holds, 100ms budget, actual bytes and fake installer only; assertions require timely request, live heartbeat, Refused after deadline, zero handoffs/consent and no late exit. Tag ordinary first-Ready/retry cases `[settings-quit]` for full-suite and no-Torrent selection.
- [x] **Step 2: Add immediate-expiry and retry-marker tests.** Prepare GUI/core receipts synchronously before constructing/requesting the session; test both no-update and Ready-consented update branches:

```cpp
session.request(0); // First preparation can return Ready; budget is already over.
CHECK(session.state() == ApplicationQuitSession::State::Refused);
CHECK(backend.installs == 0);
CHECK(ready.isEmpty());
// A separate explicit request may consume existing receipts and valid consent.
session.request(3000);
```

For a timed-out held save, let its worker physically finish, then mark the
completed settings file before explicit retry; assert the marker survives.
Also mark completed core/auxiliary files and verify layout/Torrent state is not
saved again. Revalidate existing consent and require matching fake handoff only
after the fresh request. Existing auxiliary-failure tests must still disarm it.

- [x] **Step 3: Run strict composed RED.** Task 1 alone leaves the coordinator's primary settings call synchronous; validate exact failures under modes 1/3 and zero-budget first-Ready behavior. Retain unexpected failures separately rather than counting them as expected RED.
- [x] **Step 4: Integrate cached GUI stages.** In MainWindow cache physical producer/layout/Torrent outcomes once; preserve failures rather than overwriting them on later settings polls. Return false/Pending through `quitWorkersPending()` while settings run, and final bool only after their terminal receipt. Do not recapture settings or call frozen setters on polling/retry. In the coordinator pass the async flag and make its final `saveChecked()` consume that same receipt.
- [x] **Step 5: Add post-Ready admission check.** In `ApplicationQuitSession::advance()`, after a Ready coordinator result and before both `queueExit()` and `normalQuitCommitted()`, refuse when `workerDeadline.elapsed() >= drainTimeout`, even if the first call never returned Pending. Use existing worker-timeout refusal/retry state and message. Preserve terminal-error refusal, fresh-request consent revalidation and permanent auxiliary-recovery disarm.
- [x] **Step 6: Run composed GREEN and legacy comparisons.** Execute both hidden selectors with modes 1/3/6 as applicable, then `[checkpoint-recovery],[application-quit],[gui-worker-drain]` without holds on both CPUs. Verify direct synchronous coordinator callers, foreign/modal rejection and lost-owner retention remain valid. Retain/report the existing anti-spam prompt/retry intermittent failure if it recurs; do not modify its 5000ms wait.
- [x] **Step 7: Commit only owned composition/tests paths.** Local `fix(quit): await settings receipts and reject expired commitment`; verify staged/committed/current bytes. No production installation or updater activation.

### Task 3: Full Regression, Compatibility And Preservation Checkpoint

**Files:** existing parent updater plan, Development Status and approved spec; new private QA outputs only. No new production interface.

**Interfaces:** Consume Tasks 1/2's current-source binaries and evidence. Produce a sealed verification manifest, updated truthful status and final independent review disposition; not release assets.

- [x] **Step 1: Rebuild final current-source test targets.** Fresh/consistent CMake dependency rebuild of all five targets for both CPUs, including MOC/resource consumers of changed headers. Hash source and binaries before tests. Copy the ARM configuration invocation into a separate private script, change its build output to `$QA/arm64-no-torrent`, and append `-DUSE_TORRENT=OFF -DEISKALT_REQUIRE_SELECTED_RECHECK=OFF` after existing arguments. Build `eiskaltdcpp-qt-tests` and `eiskalt-settings-quit-tests` there; the existing toggle target is deliberately unavailable without Torrent. Run `[gui-settings-freeze]`, `[settings-quit]` and both new hidden selectors with the same ARM runner/provider environment. A configure-only check is not no-Torrent execution proof.
- [ ] **Step 2: Run the ordinary complete suites serially.** For each main CPU build run `eiskaltdcpp-tests`, `eiskaltdcpp-qt-tests` and `eiskalt-torrent-tests` with `~[interop]`, the standalone `[settings-quit]` selector, then complete GUI single-line, multiline and sidebar layouts. Sidebar uses existing `~[tab-navigation-mainwindow]` exclusion only because tab-bar navigation is not that layout. Interposer must be unset; fresh XML/exit evidence for every run.
- [x] **Step 3: Run explicit hidden retirement suites.** With matching interposer run the two new selectors and existing `[.core-write-retirement]`, `[.auxiliary-write-retirement]`. Preserve controlled failure binaries/inputs for bypassed async persistence and removed post-Ready guard, built only in private output paths, and validate that each fails exactly the checks it should. Never mutate public production sources to produce controls.
- [x] **Step 4: Verify compatibility boundaries.** Confirm CPU/deployment metadata and frozen type/key semantics under the actual Qt versions. Record native Windows/case-insensitive and any untested Qt-version paths as unverified, not inferred from Rosetta. No-Torrent fixture selection must reject empty case sets; no hidden skips to pass this repair.
- [x] **Step 5: Read back protected state.** Copy the diagnostic `preserve.py` into the new QA root and run with `python3 -B` and fresh output names. Require both historical full bundle trees/strict seals, five material pins, 58 providers, nine live files, 17 old audit outputs and eleven negatives unchanged. Verify eight old original and four latest accepted Qt/GUI binary pins plus new RED evidence before/after final checkpoint. Never overwrite prior readback reports.
- [x] **Step 6: Perform one fresh-context final review.** Review the complete immutable Tasks 1/2 commit range against the approved spec: affinity and error paths, deferred Qt sync, thread lifetime, storage/read cache semantics, receipt caching, timeout and consent. Resolve findings with tests; preserve explicit residual risks. This is not the broader prerelease's complete final-candidate/native review gate.
- [ ] **Step 7: Update and checkpoint status.** Record actual tests/failures/review/readbacks in the parent updater plan, Development Status and private ledger. Local curated docs checkpoint only after evidence readback. Do not mark this repair fixed if its exact regressions remain red, or claim all suites green if the natural recovery/Torrent failures recur.

## Handoff And Remaining Tasks

The maintainer approved this plan and its Native/inline execution. Local source checkpoints are `e68f50545` (owned storage), `09b83854b` (composition/deadline admission) and `af796efbf` (final-review fixes). No push, installation or release activation occurred.

## Execution And Review Checkpoint (2026-10-06)

Tasks 1/2 have fresh both-CPU post-commit checks. The single fresh-context review of `0f2487459..09b83854b` found two Important issues: settings Pending postponed core admission closure, and pre-transfer refusal retained queued Qt auto-sync. There were no Critical or Minor findings. Both issues were reproduced before the one fix pass; no second review or post-fix reviewer approval is claimed.

The fix exposes completed GUI producer/snapshot retirement separately from physical settings persistence. Core preparation closes admission after that safe milestone even while settings remain Pending; updater-before-core order, auxiliary-save and commitment gates remain intact. Before snapshot allocation, settings freeze removes queued `UpdateRequest` events. Terminal refusal also fences mutation, loading and explicit save while retaining the original store and readable intent. Parent validation and a private real `std::bad_alloc` injection verify unchanged physical bytes while events continue.

All final test consumers were consistently rebuilt. The first complete post-fix matrix executed 30 nonempty runs and 4,281 case instances, with 443,362 passing assertions and no failures, expected failures, errors or skips. This earlier result is retained but superseded for acceptance by the later failed matrix below. Exact case inventories match the binaries. Per main CPU: core 1,035 cases, Qt 618, Torrent 152, ordinary settings 5, and GUI single-line/multiline/sidebar 106/106/104. Explicit held settings, Ready-update, unprepared-core admission, core and auxiliary retirement checks pass. A separately built `USE_TORRENT=OFF` configuration executes six selections comprising 13 case instances. Ordinary suites exclude the eight opt-in real-GOST `[interop]` cases; this checkpoint is not new GOST interop proof.

Baseline full layouts retained 24 route-admission failures across six runs before the fix. Private no-async and no-post-Ready-guard controls retain exactly seven and four expected failures, respectively, with opposite-boundary positives. The strengthened parent refusal, snapshot-allocation refusal and held unprepared-core cases pass after their exact RED observations. Original recovery waits/assertions remain unchanged; passing these runs does not identify or waive historical intermittent causes.

Actual Qt is 6.11.1 for both CPUs. Binary metadata confirms ARM minimum macOS 26 and Intel minimum macOS 14, not native minimum-OS acceptance. Windows/case-insensitive behavior and other Qt versions remain unverified. Whole historical bundle trees and strict signatures, five material pins, 58 provider resources, nine live files, 17 audit outputs, eleven negative outputs, eight original and four latest accepted binary pins, and sealed RED/current-source/control inputs all match their hashes. Original failed runner outputs and source/binary evidence remain retained.

Forced/crash teardown responsiveness is outside this bounded checked-quit guarantee. The review and local tests do not constitute real updater, installed-app, whole-candidate or production acceptance.

### Physical-Receipt Fixture Correction And Acceptance Hold

A fresh post-commit contract run failed one settings-flush byte observation: the test read the file 30ms after releasing a syscall, without awaiting physical completion. A private delayed-fsync diagnostic preserves the original assertion and reproduces the same boundary: the marker is absent with a Pending receipt, then present after Ready. The Ready-update fixture has the same flaw. Checkpoint `612ae2e3c` adds checked completion before both byte observations; original byte assertions, 100ms quit budget, 350ms hold and 5000ms wait remain unchanged. The private control passes three cases / 301 assertions after correction, and rebuilt broken controls retain exact seven/four failure sensitivity.

The latest rebuilt matrix executes all 30 exact inventories / 4,281 case instances, with 443,781 passing assertions and four failures, no errors or skips. The only failing test is `Checkpoint recovery button retains frozen rules and permanently disarms a ready update`, once in ARM multiline and once in Intel sidebar. Each fails `prompt->isVisible()` and `recovered`. All settings-specific, core, Qt, Torrent and no-Torrent checks pass. This is a recurrence of the retained natural recovery failure, not a settings-byte failure; its cause remains unresolved. No rerun waives it and Task 3 is NOT complete. First-green inputs, failed contract output, private diagnostics and both full matrices remain sealed/retained. Protected-state and hash readbacks still pass.

A subsequent fresh post-commit contract passes ordinary settings, adjacent recovery and held settings writes on ARM, but its Ready-update selector fails `duration < 100` and `advanced.load()` in thread-exit mode 6. The request takes 354ms with no GUI heartbeat progress; the observed writer is not the GUI, exit and installation remain refused, and saved-byte checks pass. This is an unresolved responsiveness failure within the repair's acceptance scope. The original budgets and assertions are unchanged. A private join-tracing run using the same seed passes (35ms/heartbeat progress) without observing a join during the hold; it neither identifies nor waives the failing cause. Task 3 was again NOT recorded complete. No further production ownership change was guessed from this comparison; retained failed evidence and protected state remain intact.

The next bounded diagnostic at source `6364345bd` keeps the original test bodies, interposer, seed 594128903, runner, holds and deadlines. Eight private stage-probe runs pass without a >=40ms span. Four unchanged-binary runs reproduce two failures in the explicit retry case: initial request 180ms against 100ms and no heartbeat progress during the held write. Two read-only exact-PID sampled runs pass; one has a useful call graph showing synchronous Torrent-engine shutdown joining on the GUI. That is a concrete diagnostic lead, not the established cause of either retained natural failure. Aggregate: 14 nonempty runs / 28 case instances / 2,658 passing assertions and two failures, no expected failures or skips. No green rerun waives the new retry failure or the prior 354ms/recovery failures. Public target, 262 original objects, instrumented public sources, protected-state and retained failure pins are unchanged. No production repair is made from these observations; Task 3 remains incomplete.

The subsequent wider private wait probe at `d62ef62d2` covers armed GUI joins before hold entry and is validated by two standalone held-worker controls. Twelve original/passively observed runs / 24 case instances have 2,279 passing assertions and one flush-section heartbeat failure (68ms request). That run captures a 63ms Torrent-engine GUI join before settings hold entry; the join does not establish the later heartbeat failure's cause. A private late-watchdog scheduling control separately proves a fixture false-negative: GUI ticks advance before blocking `watchdog.get()`, then a late snapshot observes no further progress because collection blocks the event loop. RED fails exactly three heartbeat assertions; adding event-pumped physical watchdog readiness before the unchanged get/check passes 117 assertions. Original request deadline, assertions and 350ms watchdog sleep remain; the scheduling gate intentionally starts the controlled observation later and is not natural acceptance. Four refusal-path collectors are proposed for a bounded fixture-only correction; two storage/destructor collectors already await release and remain unchanged. No production owner/API change or merged fixture correction is made here, and natural latency/recovery failures remain unwaived. Task 3 stays open.

### Approved Watchdog Collector Correction

After checkpoint `918bf54b5`, the maintainer approved event-pumped physical
watchdog readiness at the four refusal-path collectors. The source delta adds
only four requirements before unchanged `watchdog.get()` and heartbeat checks.
Both storage/destructor collectors remain unchanged, as do the 100ms request
budget, 350ms watchdog sleep, 5000ms wait helper and all byte, thread, consent,
Refused and exit assertions. No production ownership or updater API changes.

Fresh controlled late-snapshot RED reproduces exactly three heartbeat failures
with 111 other assertions passing. GREEN against the edited fixture passes all
117 assertions. This deliberately delayed observation is sensitivity evidence,
not natural latency acceptance. All five fixture consumers are rebuilt into
new private outputs using their original full-target compile/link inputs:
ARM settings/toggle, Intel settings/toggle and ARM no-Torrent settings. Original
targets and all 262/272/262/272/261 link objects are retained unchanged. The
edited source's historical pins resolve to its exact archived before-copy;
no historical manifest is rewritten. The serial matrix matches all 32 exact
nonempty inventories: 4,287 case instances, 443,977 passing assertions and six
failures across five runs, with one fatal abort and no skips or expected failures.
ARM Qt fails initial DC publication (`ready`, published file/magnet present but
status Preparing); ARM sidebar repeats recovery visibility/retry failures;
ARM Ready-update flush takes 102ms against 100ms while heartbeat progresses;
ARM admission passes 64 ordinary assertions then aborts during teardown with
`Checked shutdown must retain its context`; Intel Torrent fails the stopped
selected-check terminal-state wait at its original 1500ms bound. The core
ownership guard rejects non-Ready, pending or wrong-owner disposal; its exact
failed teardown precondition still needs diagnosis, not a guessed guard change.
Intel held settings and all no-Torrent selections pass. Original Qt/Torrent
suite binaries are unchanged by this fixture correction. No passing rerun
waives these failures or the retained 180/354ms stalls. Task 3 remains incomplete.
No installation, signing, push or publication occurred.

#### Checked-Context Teardown Diagnostic After `d16c183a9`

The next approved bounded diagnostic preserves all test bodies, the four
watchdog waits, 100ms request budget, 350ms hold, 5000ms wait helper, original
interposer and loopback-only runner. Three unchanged-binary and eight private
state-probe runs use the retained abort seed 1936105996. All eleven pass the
exact admission inventory with 704 assertions; the eight probes observe both
core persistence receipts collected and Finished/Ready before fixture disposal.
This does not reproduce or waive the historical natural abort.

Two private controls delay only a core thread-exit receipt by 250ms, separately
at SnapshotWriting and SettingsWriting. Ordinary quit and Ready-update branches
are run independently for each. All four reach their original final assertions
(128 passing assertions total) then abort with the exact guarded logic_error.
At disposal each captures Pending, the correct owner thread, a valid physically
ready receipt and a still-joinable worker. Each worker returned Ready; no final
receipt poll occurred after the delayed completion. The fixture unconditionally
calls shutdown after a refused session stops polling. This proves an uncovered
fixture cleanup contract path, not a failed write, wrong-owner disposal or an
established attribution of the earlier natural abort to either exact phase.
The production guard remains correct and unchanged.

Recommended next bounded change: in this admission test only, after the existing
refusal/heartbeat/byte/no-exit/no-install assertions, collect core receipts on
the startup owner through the checked async API and require Ready before
cleanup. Do not retry ApplicationQuitSession, grant update consent, weaken the
guard or alter the request deadline. Validate the controlled RED/cleanup GREEN
and natural ARM/Intel/no-Torrent consumers separately. This recommendation is
not implemented; its design approval is requested separately.

Private evidence is retained under
`Work/qa/checked-context-teardown-diagnosis-20261006`. Exact inventories, child
exit codes, fatal expression location, phase/owner/receipt fields and original
input hashes are checked. An initial private control-build wrapper rejected an
ambiguous path replacement before compilation; its script/error remain retained,
and the corrected build uses new outputs. This is not an application failure.
No public source, original binary/object/archive, guard, live profile, installed
app, server, VM, signing key or feed is changed. Task 3 remains incomplete; the
102/180/354ms stalls and other retained matrix failures remain unwaived.

#### Approved Admission Cleanup After `d335cb88b`

The maintainer approved the bounded test-only receipt collection. Eight lines
are appended only to the admission test: after every original assertion, poll
the checked core API on the startup owner, require Ready, pump queued events,
and reassert Refused/no exit/no install. The session is never retried; the
100ms request budget, 350ms watchdog hold, 5000ms helper, shared destructor and
production ownership/guard remain unchanged. Failed/unsupported receipts are
not accepted as Ready or swallowed to permit disposal.

Fresh controlled RED independently verifies both phases and both quit branches:
128 passing original assertions followed by exactly four guarded SIGABRTs.
GREEN changes only the fixture object while reusing the same private delayed
core objects, and passes 148 assertions. All four disposal traces are now
Finished/Ready on the correct owner, without an outstanding receipt or worker.
Five fixture consumers are compiled/relinked serially into new ARM,
Intel/Rosetta and no-Torrent outputs. Exact source scope, CPU/deployment metadata,
original target/object/archive hashes and current control pins pass. Historical
fixture source pins resolve to retained exact-byte copies; no old manifest or
failed output is rewritten. Private evidence is under
`Work/qa/admission-receipt-cleanup-repair-20261006`. This corrects the demonstrated
fixture cleanup contract gap, not the unresolved exact phase of the earlier
natural abort, natural quit stalls, or final release acceptance.

The complete serial matrix matches all 32 exact nonempty inventories: 4,287
case instances, 444,190 passing assertions, two failures in one ARM sidebar run,
and zero fatal errors, expected failures or skips. Both failures are the retained
checkpoint recovery case (`prompt->isVisible()` and `recovered`, seed 2124193600).
ARM/Intel admission pass 74 assertions each; no-Torrent admission passes 72.
All core, Qt, Torrent, other GUI layouts and explicit retirement selections pass.
Those passes do not waive the sidebar failure or earlier natural latency and
Torrent failures. Before/after preservation checks pass for both historical
bundle trees/signatures, five archive/notice pins, 58 providers, nine live files,
17 prior audits and eleven negative outputs. An inline scope review finds no
ownership/guard/deadline expansion; this is not a new independent final-candidate
review. No installation, push, signing or publication occurs. Task 3 remains
incomplete; the bounded admission cleanup correction is independently verified.

This plan does not close retained natural recovery/Torrent/bootstrap/publication/bind/peer-counter causes, pending-owner/Ready-update coverage or per-file AntiSpam recovery, translation/native-language quality, provenance/reproducibility, whole-candidate independent review, native minimum-OS/live/lifecycle/benchmark/soak acceptance, final app/material rebuild, numeric production build allocation, signing/notarization/signed-v3 acceptance, guarded installation/readback or authenticated wiki/prerelease publication/readback. Signed feeds remain last; parent Tasks 7/8 stay open.

#### Sidebar Recovery Diagnostic After `fd605c919`

Focused ARM sidebar diagnosis retains seed 2124193600 and all public code,
guards, deadlines and assertions. Six unmodified runs reproduce the exact
`prompt->isVisible()` / `recovered` failure once. Eighteen private trace runs
reproduce it once without a forced delay. That trace observes the Anti-spam
retry click at 36ms while the session remains Preparing and its prompt hidden;
the failed auxiliary receipt is collected at 48ms, reopening the recovery
prompt. The queued click was correctly ignored during Preparing. No Ready
notification or installation follows. The test removes the filesystem blocker
before the preceding blocked retry has a terminal receipt, relying on a 30ms
wait instead of synchronizing on renewed refusal.

Two private 250ms failed-receipt controls reproduce that exact ordering in
both Search blacklist and Anti-spam branches. All three nonempty inventories
match the single intended case. Across 26 runs, 2,210 assertions pass and twelve
fail: two each in the two natural reproductions, four each in the two controlled
runs. There are no fatal errors, expected failures or skips. Passing reruns do
not waive either reproduction. This establishes the test scheduling gap, not
the precise historical matrix timings or the other retained quit stalls.

Recommended bounded correction, not implemented: keep the blocked path intact
after the deliberately unsuccessful retry, then use the existing 5000ms helper
to require the renewed visible recovery prompt before removing the blocker.
Keep every original assertion, the later mouse click, frozen rule checks,
single producer fence, no-install checks and 3000ms session budget unchanged.
Validate against the same retained delayed-receipt objects before broader
ARM/Intel/layout coverage. No production session change or queued retry consent
is proposed. Private diagnostic evidence is retained under
`Work/qa/sidebar-checkpoint-recovery-diagnosis-20261006`; Task 3, final review,
native/live acceptance, installation and release gates remain open.

#### Approved Recovery Test Synchronization After `702e529ce`

Five lines are inserted only in the recovery-button test: while the filesystem
blocker remains intact, require the renewed visible recovery prompt through
the unchanged 5000ms helper. Only then remove the blocker and exercise the
original click. All original assertions, 30ms waits, 3000ms session budget,
frozen rule checks, producer fences and no-install checks remain unchanged.
No production retry, consent, ownership or retention behavior changes.

Fresh controlled RED uses the retained delayed-receipt binary: two exact case
instances / 124 passing assertions / eight failures, only the invisible prompt
and missing recovery in both branches. GREEN changes only the test object and
reuses the identical session/coordinator control objects: two case instances /
180 assertions / zero failures. Fresh ARM and Intel/Rosetta GUI consumers
preserve original binaries and objects; exact edit scope, source pins and CPU/
minimum-OS metadata pass. This is not native Intel/macOS14 acceptance.

The complete serial matrix matches all 32 exact nonempty inventories: 4,287
case instances, 444,046 passing assertions, one assertion failure, zero fatal
errors, exceptions, expected failures or skips. All six GUI layouts and every
other selection pass except Intel/Rosetta core. Seed 130004532 fails
`BufferedSocket drains the final Shadowsocks frame before send completion`,
`2022-blake3-aes-256-gcm / disconnect`, at `listener.completed.load()`
(`tests/test_bufferedsocket.cpp:129`). The final pending-output and received-byte
assertions do not fail. This original core target is unchanged; cause remains
unestablished and is not waived by other passes. The matrix and its checker
correctly return FAIL. Evidence is retained under
`Work/qa/sidebar-checkpoint-recovery-repair-20261006`.

The bounded test synchronization correction is verified; overall Task 3 is not
accepted. Earlier exact historical timings, quit latency/Torrent causes,
pending-owner/Ready-update and per-file recovery coverage, localization,
provenance/reproducibility, independent whole-candidate review, native/live/
lifecycle/benchmark/soak acceptance and all final rebuild/signing/installation/
publication gates remain open. Signed feeds stay last; parent Tasks 7/8 stay
open. No installation, push, production build allocation or feed change.

#### Shadowsocks Completion Diagnostic After `50b88b0b3`

Nine isolated unchanged-binary runs at retained seed 130004532 pass; this does
not waive the earlier Intel/Rosetta 2022 disconnect completion failure. Across
24 private no-forced-delay trace runs, one legacy-cipher disconnect run freezes
the original completion load false immediately after peer EOF, with all 33312
expected encrypted bytes received. Its Failed callback publishes completion
about 0.35ms later, well inside the unchanged three-second deadline. The
production fail path closes the socket before firing Failed; peer EOF is not a
listener receipt. Trace timestamps/string copying can perturb scheduling; this
identifies the observed gap, not the original historical 2022 timing. The Failed
callback's default pending flag alone is not independent drain proof.

Four private 150ms callback-publication controls reproduce the same gap for
both ciphers on ARM and Intel/Rosetta. Six exact inventories / 37 case instances
/ 296 sections validate 1674 passing assertions and nine failures: one traced
and eight controlled. No exception, fatal error, expected failure or skip. All
failed checks follow EOF/all received bytes and precede callback completion;
completion is true after checked cleanup and publishes within the original
deadline. Diagnostic contract PASS is not green suite acceptance or a fix.
Only private test objects differ; production, originals, runner/policy and
protected state remain unchanged. Evidence is retained under
`Work/qa/shadowsocks-completion-diagnosis-20261006`.

Recommended bounded test-only correction, pending approval: after the read
loop, wait for listener completion using the same shared three-second deadline
and one-millisecond yield pattern, then retain every original assertion and
cancellation/byte bound. No socket behavior, callback order or timeout change.
Validate against retained held-callback behavior before the full current matrix.
Task 3, earlier latency/Torrent causes, coverage/review/native/live acceptance
and final rebuild/signing/install/publication gates remain open. Signed feeds
last; no app/profile, VM/Docker/server, credential/key or GitHub changes.

#### Approved Shadowsocks Completion Synchronization After `541ddf322`

Three test-only lines now wait for listener completion after the read loop,
using the same shared three-second deadline and existing one-millisecond yield
pattern. Every original assertion, byte/pending-output check and 750ms cancel
bound remains unchanged; cancellation continues before the wait. No production
transport, callback order, ownership, interface or timeout changes.

Fresh controlled RED verifies four case instances / 173 passing assertions /
eight failures only in disconnect completion for both ciphers/CPUs. GREEN
reuses the same 150ms callback-publication behavior, changing only these three
test lines: four case instances / 183 assertions / zero failures. Twelve fresh
uninstrumented focused runs pass 555 assertions at seed 130004532. Four private
consumers preserve 98 original input pins each; exact public/control/link scope,
source/object/binary pins and CPU/minOS metadata pass. Intel remains Rosetta,
not native macOS14 acceptance.

The complete serial offline matrix passes all 32 selections / 4,287 exact case
instances / 444,190 assertions, with no failures, test exceptions, fatal errors,
expected failures or skips. All preceding case-name inventories are unchanged.
Both core/Qt/Torrent suites, six GUI layouts, retirement checks and no-Torrent
selections pass. Optional real-GOST interop is not included. Prior failures,
including the original unobserved historical 2022 timing, remain retained;
earlier latency/Torrent causes are not waived by this green matrix.

Before/after protected-state and unchanged-source/input checks pass, including
whole historical bundles/signatures, providers/live files and prior 188/318-file
seals. Inline scope review finds no guard/budget expansion, not independent
whole-candidate review. Evidence is retained under
`Work/qa/shadowsocks-completion-repair-20261006`. The bounded test repair is
verified; overall Task 3, broader coverage/review/native/live acceptance and all
final rebuild/signing/install/publication gates remain open. Signed feeds last;
no app/profile, VM/Docker/server, credential/key, push or feed change.
