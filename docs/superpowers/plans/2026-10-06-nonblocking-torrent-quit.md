# Nonblocking Torrent Quit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Preserve the maintainer's Native/inline method; do not delegate implementation or invent review results. One independent review of the completed repair range precedes acceptance.

**Goal:** Keep checked normal quit responsive while preserving final Torrent checkpoints, retained owners and refusal of late exit or updater installation.

**Architecture:** GUI-owned components fence admission once and poll owned thread-exit receipts. Runtime retires publisher, engine and proxy in dependency order; cached GUI layouts are separate from completed producer retirement. No GUI-affine shutdown is moved to a coordinator thread.

**Tech Stack:** C++20, Qt 6, owned `std::thread`/future receipts, existing libtorrent backend, Catch2/QtTest, disposable write/flush/TLS-exit holds.

**Spec:** `docs/superpowers/specs/2026-10-06-nonblocking-torrent-quit-design.md`, approved for planning by the maintainer after `bdbd06c2bd4dc375130101306e0fba9533d6fd5a`. The maintainer approved this implementation plan after checkpoint `dd088f2b`; Native/inline execution began on 2026-10-06.

## Global Constraints

- Use the existing `/Users/neolo/ollama_eiskaltdcpp` checkout and `codex/v3.0.0-pre.1` branch. Starting source is `bdbd06c2bd4dc375130101306e0fba9533d6fd5a` plus this documentation checkpoint. Preserve unrelated files; no new worktree.
- Keep the installed app, profile, transfers, credentials, proxy/server configuration, signing keys, VM, historical artifacts and feeds unchanged.
- GUI owners, server, timers and mutating APIs stay on GUI. Never detach or terminate workers, drop unfinished owners, or pump GUI events on a worker.
- The original request budget is 100ms, controlled hold 350ms and `waitFor` helper 5000ms. No widening, removed assertion, skip or green-rerun waiver.
- Preserve settings/job/resume formats, selected verification, sharing cache/exclusion, authentication/TLS/UDP and no-direct-fallback behavior. Existing synchronous APIs retain compatibility; no new user setting or protocol.
- Runtime order is Fence -> Publisher -> Engine -> Proxy -> Complete. Core/route/engine lifetime outlasts cancelled publication and retraction. Unfinished workers yield Pending, not success or owner destruction.
- Keep physical failure sticky; explicit retry may collect successful receipts after session timeout, but late completion alone cannot authorize exit/install. Checkpoint-recovery disarm remains permanent.
- Run disposable profiles and the existing external-network-denied/loopback-only runner. Build and test serially; heavy hashing/compilation must not overlap timing tests.
- ARM macOS 26+ and Intel macOS 14+ remain the approved split; host/Rosetta is not native minimum-OS proof. No-Torrent must still compile and execute.
- No app/material release rebuild, numeric production build allocation, signing, notarization, install, push, wiki/prerelease publication or feed activation. Signed feeds last.

## Review Focus

- Launch/allocation failure after acquiring a socket must not discard a running thread or join unfinished work on GUI: Task 3 owns pre-launch container ownership and failure tests.
- An apparently finished proxy worker can still be in capture/TLS destruction: Task 3 owns reaper sensitivity and physical-tail tests.
- A void share callback can silently reject producer admission: Task 2 owns checked removal, idempotent absence and closed-admission tests.
- A component can fail while a later owner is still running: Task 4 owns continued physical retirement with sticky failure and no premature core preparation.
- A queued mutation or explicit retry can repeat layout/checkpoint work or revive installation: Tasks 1/3 fence queued admission; Task 4 owns marker preservation and consent tests.

## Files And Interfaces

- Create `torrent/QuitRetirement.h`: value-only common result, no QObject/thread ownership or settings dependency.
- Modify `torrent/TorrentEngine.{h,cpp}` and `tests/torrent/test_torrent.cpp`: terminal admission fence, existing worker exit receipt and engine regressions.
- Modify `dcpp/ShareManager.{h,cpp}`, `tests/test_managedshare.cpp`, `eiskaltdcpp-qt/src/TorrentSharePublisher.{h,cpp}` and `tests/qt/test_torrentsharepublisher.cpp`: checked core retraction and publisher retirement.
- Modify `eiskaltdcpp-qt/src/TorrentProxyAdapter.{h,cpp}`, `tests/qt/test_torrentproxyadapter.cpp` and `tests/qt/CMakeLists.txt`: admission fence, physical per-worker receipts, safe reaper and test-only Impl access.
- Modify `eiskaltdcpp-qt/src/TorrentRuntime.{h,cpp}`, `eiskaltdcpp-qt/src/MainWindow.cpp`, `tests/qt/test_torrentruntime.cpp` and `tests/qt/test_settingsquit.cpp`: phase composition, cached snapshots and composed quit tests. No session/coordinator interface change is planned.
- Reuse `tests/qt/ApplicationQuitTestFixture.h`, `tests/macos/updater/CoreWriteProbe.h` and `worker_io_hold.cpp`; do not change their original helpers/holds or add product injection methods.
- Update this plan, the approved spec, parent updater/settings plans and `docs/wiki/Development-Status.md` with observed results and remaining gates.

The shared contract is defined once in Task 1:

```cpp
namespace eiskalt::torrent {
enum class QuitRetirementStatus { Pending, Ready, Refused };
struct QuitRetirementResult {
    QuitRetirementStatus status;
    const char* component; // Static credential-free identifier.
};
}
```

All four owners produce `bool fenceForQuitChecked() noexcept` and
`eiskalt::torrent::QuitRetirementResult retireForQuitChecked() noexcept`.
Both are owner-thread-only; a foreign call fails without modifying the valid
owner attempt. Fence performs no long operation or worker wait; retire requests
its phase once, polls with zero wait, joins only physically ready threads and
caches its result. Internal idle/admission/phase flags are not new public states.

## Build And Evidence Recipe

At execution, create fresh `Work/qa/nonblocking-torrent-quit-repair-20261006`
(`QA`), adding a unique suffix if it exists. Record BASE, source pins, build and
test commands, exact failures and rulings in this plan's private ledger. Never
overwrite prior reports, binaries, seals or controlled failures.

Copy `Work/qa/gui-settings-write-repair-20261006/build-tests.sh` into `$QA` and
change only its `qa` output root. Keep presets, selected-recheck requirement,
local Catch2 source, dependency/provider/stage pins and ad-hoc test build number
`1`. Build all five test targets afresh, including resources/MOC/header consumers:

```sh
bash "$QA/build-tests.sh" arm64
bash "$QA/build-tests.sh" x86_64
```

For no-Torrent copy the ARM configure command, use `$QA/arm64-no-torrent`, append
`-DUSE_TORRENT=OFF -DEISKALT_REQUIRE_SELECTED_RECHECK=OFF`, and build core, Qt and
standalone settings targets. Do not enable the unavailable toggle target.

Reuse unchanged `Work/qa/core-write-repair-20261005/run-tests.sh` (`RUN`). Set
explicit matching provider/plugin paths from the parent settings plan. ARM hold
is `Work/qa/core-write-repair-20261005/io-hold-v2.dylib`; Intel hold is the adjacent
`io-hold-x86_64-v2.dylib`. Set `EISKALT_QA_INTERPOSER` only for hidden physical-I/O
tests. Ordinary portable controls use fixture-owned release guards/watchdogs,
not interposition or external services. Each run gets unique listing/XML/log/exit
paths and strict case/failure inventories. Use retained seed `594128903` for
composed timing comparisons. A sandbox setup failure is not a product failure.

### Task 1: Checked Engine Retirement

**Files:** new `torrent/QuitRetirement.h`; engine header/source and existing engine test translation unit.

**Interfaces:** Produces the common contract and engine fence/retire methods above. Existing `shutdownChecked()` stays `bool`, synchronous and idempotent.

- [x] **Step 1: Add `TEST_CASE("Engine checked retirement retains the worker through checkpoint and thread exit", "[torrent-quit-engine]")`.** Reuse `TorrentEngineTestAccess` and real queued work to hold the worker for 350ms. Define a test-local API adapter that reports a missing checked API instead of a compile error and falls back to existing synchronous shutdown for the baseline. Capture original assertion values before diagnostics. Required checks:

```cpp
CHECK(requestMs < 100);
CHECK(result.status == QuitRetirementStatus::Pending);
CHECK(heartbeatAdvanced);
CHECK_FALSE(threadExitReleased); // No Ready during the controlled physical tail.
CHECK(finalResult.status == QuitRetirementStatus::Ready);
CHECK(checkpointBytesMatch);
CHECK(repeatedResult.status == finalResult.status);
```

Also test actual checkpoint refusal, a thread-local exit guard on the existing
worker, a foreign caller, all mutation paths after fence, cancelled selected
verification and unchanged snapshot reads. Never let assertion failure strand
a held thread: fixture guards release before any owner destructor.

- [x] **Step 2: Build and run strict RED.** Run `$RUN "$B/tests/torrent/eiskalt-torrent-tests" '[torrent-quit-engine]'` under ordinary isolated routing. Preserve missing-API and exact duration/Pending/heartbeat failures separately from setup errors. Keep current synchronous persistence cases as opposite-boundary positives.
- [x] **Step 3: Implement the contract and engine receipts.** Add a terminal admission flag checked by `enqueue` and every direct cached-mutating API, including configure, observation selection, file priority/pause/remove/recheck/cancel. Allocate the terminal promise/future before launching the existing worker; publish persistence outcome at thread exit, catch worker/receipt errors and keep Impl owned. Request stop/wake once. Cache the collected result only after readiness and join; do not read worker-written `shutdownSaved` concurrently. Keep the existing queue, local-check/session drain and atomic checkpoint policy.
- [x] **Step 4: Run GREEN and complete ordinary engine suite.** Require focused controls and `$RUN "$B/tests/torrent/eiskalt-torrent-tests" '~[interop]'` to pass on both CPUs. No unobserved assertion is waived; any unrelated regression stays named/open. Verify no extra checkpoint after collection.
- [x] **Step 5: Local curated checkpoint.** Diff/read back only header, engine and tests; commit `fix(torrent): poll owned engine retirement receipts`. No push/install.

Task1 checkpoint `1ccb0131`: finalized baseline has eight case instances and 24
exact assertion failures, not compiler/setup failures. Final focused GREEN has
eight instances / 90 assertions on each CPU, including a poll during held TLS
cleanup. Each full ordinary `~[interop]` suite has 241 instances / 18,520
assertions and no failures/errors/skips, with identical case-name inventories.
Task completion reruns the focused selectors and validates the unchanged
source/binary hashes against those full-suite receipts; the final integrated
matrix remains mandatory. Earlier partial/stale-binary runs are retained but
excluded from acceptance. Historical bundles/signatures, materials/providers,
protected live files and prior evidence seals pass readback. Engine only:
runtime and MainWindow still call synchronous legacy wrappers. Tasks2-5 and
independent repair-range review remain open; no app installation or activation.

### Task 2: Checked Share Retraction And Publisher Retirement

**Files:** ShareManager header/source and managed-share tests; publisher header/source and publisher tests.

**Interfaces:** Consumes Task 1 result. Produces publisher fence/retire methods, `bool ShareManager::removeManagedFilesChecked(const string& owner)`, and `using CheckedRetract = std::function<bool(const QString&)>`. Append optional `CheckedRetract checkedRetract = {}` after the existing constructor `QObject* parent = nullptr` parameter; preserve all existing calls and void `Retract`/invalidate APIs. Runtime will explicitly supply the checked callback in Task 4.

- [x] **Step 1: Add `TEST_CASE("Checked managed-share retraction distinguishes absence from closed admission", "[managedshare-checked-retract]")`.** Real isolated manager: `CHECK(removeChecked(owner))`, list/TTH lookup absent, `CHECK(removeChecked(owner))` again; after `stopForShutdown()`, `CHECK_FALSE(removeChecked(owner))`. Existing void removal stays compatible. Missing API uses an explicit test adapter, not compilation failure.
- [x] **Step 2: Add `TEST_CASE("Publisher checked retirement preserves cancellation and physical retraction", "[torrent-quit-publisher]")`.** Hold a real Publish callback, a checked retraction and worker TLS exit in separate controls. Set tokens cancelled before releasing publication. Require `requestMs < 100`, Pending, heartbeat, retraction thread not GUI, core admission still open, zero post-fence enqueue/late status, Ready only after physical exit and `retractions == 1`. Callback false/throw or missing checked callback with nonempty IDs must yield cached Refused, never infer success from void. Zero-ID retirement can succeed. Check foreign caller does not poison owner results.
- [x] **Step 3: Run exact RED for both components.** Core selector runs without interposer; publisher controls use release guards and 350ms/100ms limits. Preserve current `[managedshare]` and `[publisher]` positives. Match actual expected expression sets; reject arbitrary nonzero exit acceptance.
- [x] **Step 4: Implement checked removal and publisher receipt.** ShareManager adds the producer-guard/locked bool counterpart, preserving void behavior. GUI fence freezes IDs and cancels tokens without synchronous core retraction; existing publisher worker drains cancellation and checked retractions before its thread-exit promise. Reject new enqueue, suppress cancelled queued callbacks and aggregate failures. Missing/throwing callback marks failure but must not abandon worker lifetime. Legacy synchronous stop retains its void-callback behavior; shared terminal state prevents replay after checked retirement.
- [x] **Step 5: Verify and checkpoint.** Both-CPU focused selectors, complete core and Qt suites pass or report exact retained failures. Verify real retraction under retained core lifetime, original callback compatibility and physically held cleanup. Commit curated paths as `fix(sharing): check retained publisher retirement and retraction`.

Task2 checkpoint `ff835364` (2026-10-07): checked managed removal distinguishes
absence from closed admission. Publisher fencing cancels tokens and freezes IDs
without GUI core retraction; its existing worker attempts all checked retractions
and supplies the outcome only at physical thread exit. Failures remain Refused,
and legacy callbacks are not replayed. A fresh component review found an early
callback-copy exception could skip later retractions; exact six-failure RED and
a retained-worker repair resolve it. Deterministic queued cancellation and
separate legacy counters close both Minor review gaps. The independent focused
followup has no remaining findings, but is not Task5 integrated-range review.

Final focused selectors pass core40/504 and Qt12/166 on each CPU. Complete
ordinary core/Qt suites pass 3,718 case instances / 368,874 assertions total,
with matching case-name inventories and no failures/errors/skips. Completion
reruns the focused selectors and validates unchanged nine source/four binary
pins against the complete-suite receipts instead of repeating the twelve-minute
matrix. Protected state and prior 74/14/105-file seals pass readback. The earlier
mechanical formatting build failure is retained and excluded; acceptance uses
fresh completed builds. No runtime/MainWindow checked-path integration, installed
app/profile change, push or release activation. Tasks3-5 remain open.

### Task 3: Proxy Physical Receipts And Safe Reaping

**Files:** adapter header/source, existing adapter tests and Qt test CMake.

**Interfaces:** Consumes Task 1 result. Produces adapter fence/retire methods. `start()` and ordinary `stop()` stay restartable unless checked quit has established its terminal fence; `endpoint()` cannot advertise a newly closed listener.

- [x] **Step 1: Enable private test access without a product hook.** Add only a `friend struct TorrentProxyAdapterTestAccess` declaration. In the Qt test target, omit the separately compiled adapter `.cpp` and include that same production source from `test_torrentproxyadapter.cpp`, as the existing engine tests do. Other app/full-GUI targets still compile their normal adapter object. Keep one implementation/MOC definition per target; first run unchanged `[adapter]` tests to detect extraction/ODR regressions.
- [x] **Step 2: Add `TEST_CASE("Proxy retirement and reaper retain workers through physical exit", "[torrent-quit-proxy]")`.** A real accepted local connection uses its existing copied cancellation callback as a fixture gate on the actual adapter worker; hold it 350ms while GUI requests retirement. A separate private Worker control sets its original done flag and holds actual thread-local teardown, then calls the real reaper. Both require `requestMs < 100`, Pending, heartbeat and unchanged retained worker count while held. Release guards always finish the thread before destruction. Also assert listener rejects new accepts, no queued DNS resurrection, retained connection/resources until receipt, Ready after physical exit, zero-worker fast path and unchanged restartable ordinary stop.
- [x] **Step 3: Add launch/queued-resolution failure RED.** Reserve owner/container state before launch; inject thread-constructor failure only in the private test build. Check no joinable thread or reservation escapes, accepted socket closes and route credentials are not logged. Foreign fence/poll cannot mutate the valid owner. Cancellation of a queued resolver callback cannot create a new lookup or direct connection.
- [x] **Step 4: Implement per-worker receipts and checked aggregation.** Create each future before thread launch and place the Worker under container ownership before launch can succeed; rollback a failed launch without joining unfinished work. Publish at thread exit after relay/capture/TLS cleanup. Reaper erases only physically ready workers; never use `done` as its lifetime proof. GUI fence closes the server/reaper to new admission; retirement later sets stopping, wakes backoff, cancels resolvers and polls all retained workers. Only after collection reset limits and route data. Preserve existing ordinary stop/start and no-direct-fallback behavior.
- [x] **Step 5: Verify and checkpoint.** Both-CPU focused and complete Qt suites, plus existing ordinary proxy/socket routing tests. Audit cache/limit/resolver mutex scope and Qt abort calls; if actual GUI cancellation blocks, retain the failing evidence and revise the design rather than bypass it. Commit `fix(proxy): reap Torrent workers only after physical exit`.

Task3 checkpoint `1561ca2f9` (2026-10-07): adapter admission is terminally
fenced for checked quit, while ordinary stop/start remains compatible. Workers
and reservations enter container ownership before launch; failed construction
closes the accepted descriptor and rolls back admission. Both ordinary reaping
and checked retirement collect only thread-exit receipts, not the body-end
flag. Route/limits reset only after all physical owners are collected; foreign
calls cannot poison the owner attempt, and closed listeners are not advertised.

Unchanged extraction tests pass 21 cases / 562 assertions. The finalized RED
has exactly 31 behavioral failures, with no setup errors/skips. Both CPU focus
and completion reruns pass six JUnit case nodes / 94 assertions; ordinary
socket/proxy selectors pass 92 / 1,012 each. Complete Qt suites pass 753 nodes
on each CPU, ARM 34,029 and Intel/Rosetta 33,970 assertions, with no failures,
errors or skips and matching inventories. All 748 prior ordinary nodes remain;
the hidden launch-failure control is selected explicitly on both architectures.
Thirteen source/binary pins, retained RED inputs, protected candidate/material/
provider/live/audit/negative readbacks and prior 74/133/14/105-file seals pass.
Completion validates these unchanged full-suite receipts rather than repeating
the eleven-minute matrix; Task5 still requires final integrated verification.

The launch interposer is a separate test-only target, never linked into the app.
The Qt implementation/MOC is linked once per test target. Ownership, resolver,
cache/limit mutex and native abort audit is retained privately. This is isolated
offscreen/loopback component QA, not native minimum-OS or installed-app quit
acceptance. Runtime/MainWindow still call synchronous legacy APIs. Tasks4-5,
independent integrated review and every earlier acceptance/release gate remain
open. Installed app/profile, servers, keys, VM and feeds are unchanged.

### Task 4: Compose Runtime Phases And Quit Milestones

**Files:** runtime header/source, MainWindow source, runtime tests and settings quit tests.

**Interfaces:** Consumes Tasks 1-3 fences/receipts and checked core callback. Produces runtime fence/retire methods and cached runtime terminal outcome. Existing `MainWindow::stopWorkersForQuit(bool)`, `quitWorkersPending()` and `quitProducersRetired()` signatures stay unchanged.

- [x] **Step 1: Promote the actual checkpoint diagnostic.** Append public composed tests under `#if defined(__APPLE__) && defined(USE_TORRENT)`, tagged `[.torrent-quit-retirement]`. Reuse the retained private case structure, original mode1/3 holds, 100ms request and 350ms watchdog. Add mode6 physical exit. Test ordinary/Ready update and explicit retry. Keep every pre-existing settings test body/assertion and helper unchanged.
- [x] **Step 2: Add `TEST_CASE("Runtime checked retirement preserves phase order and sticky failures", "[torrent-quit-runtime]")`.** Check all admission fences before first Pending; publisher -> engine -> proxy order; `CHECK_FALSE(route->revoked->load())` while publisher/engine work remains; one cached failure survives while later owners physically drain. Foreign calls and late reload/publication/DNS callbacks do not mutate the owner attempt. Final Refused requires physical retirement of all retained phases, not an abandoned later worker. `CHECK(savedBytes == marker)` and unchanged resume checkpoint generation pin repeated poll/retry.
- [x] **Step 3: Run strict RED.** Full-GUI hidden selector under matching original interposer; portable runtime tests under the ordinary runner. Confirm existing blocking-boundary failures without widening holds. Any earlier source changes may improve individual components, but baseline MainWindow composition still calls synchronous retirement and must remain sensitive.
- [x] **Step 4: Implement phased runtime composition.** On first fence set runtime stopped/disarm publication and fence all three child admissions. Configure the publisher's checked core callback. Retire each dependency in order; accumulate the first credential-free error but continue physically draining later phases. While any worker remains, report Pending with its actual component; terminal Refused only after all required retirement is collected. Retain all owners/context/route. Existing synchronous runtime shutdown uses the same outcomes and preserves legacy semantics.
- [x] **Step 5: Implement separate cached layout milestone.** Add `quitLayoutsComplete` and `quitLayoutsSaved` to MainWindow's private state. Save retained layouts/transfer view and detach observation once; poll runtime outside that one-shot block. Keep `quitSnapshotsComplete` false and `quitWorkersWaiting` true while Torrent is Pending. After terminal physical retirement cache the aggregate snapshot outcome and then continue the existing async settings/core/auxiliary path. Do not change updater-first fencing, session deadlines, auxiliary recovery/disarm or serialization formats.
- [x] **Step 6: Run composed GREEN and original hidden selectors.** Under holds check request<100ms, GUI heartbeat, off-GUI writer, correct final bytes, timeout Refused and zero late exit/installs. After physical completion alone retain Refused; explicit request3000 reuses markers and permits only the correctly revalidated fake handoff. Repeat all four original settings selectors and no-Torrent settings/freezing cases on both applicable configurations. Refused physical outcome remains cached on retry.
- [x] **Step 7: Curated checkpoint.** Commit `fix(quit): compose nonblocking Torrent retirement before core drain` after readback; no product installation or activation.

**Task4 initial candidate status, 2026-10-07:** Runtime/MainWindow composition
was implemented locally but was not accepted or committed. The final blocking
baseline has exactly 22 portable and 18 composed failures, with no setup errors
or skips. New focus passes 144 portable and 534 composed assertions on each
CPU; the unchanged no-Torrent original suite passes 12 JUnit nodes / 654
assertions. Its GUI/test translation units are compiled from current source,
with unchanged retained accepted no-Torrent dependencies, not a final whole
candidate rebuild.

The original Torrent-enabled settings suite fails 23 assertions across held
write, Ready-update, admission and retry cases. Settings freezing/persistence
is deferred while Torrent is Pending, exposing GUI auto-sync and late writes.
Two immediate route observations also conflict with asynchronous physical
producer retirement. Both failed phase-detector runs are retained; the final
detector follows actual proxy retirement start and the unchanged 350ms TLS
hold. No original test/helper body or deadline has been changed.

The approved narrow revision is
`docs/superpowers/specs/2026-10-07-torrent-quit-settings-freeze-compatibility.md`.
Maintainer approval on 2026-10-07 permits only the specified route-observation
timing adjustment; all other original assertions/helpers/budgets remain.
It supersedes Step5's deferred settings start: begin the existing owned writer
after layouts, then poll settings and Torrent independently without advancing
the producer milestone or core drain while Torrent remains Pending.
The approved revision is now verified: both CPU runtime144, composed648 and
original settings688 assertions pass; complete Qt754 cases each pass with
ARM34,163/Intel34,138 assertions and matching unchanged legacy inventories.
The applicable retained-dependency no-Torrent consumer passes12/656. Deferred
freeze and premature core-drain controls each fail exactly6 intended assertions,
with original RED23 and blocking RED22/18 preserved. Thirty-six input pins and
protected/prior evidence readbacks pass. Original test bytes differ only by
the authorized producer-readiness wait before the unchanged route assertion.
Task4's local curated checkpoint follows these results. Task5's fresh complete
integrated/no-Torrent matrix, remaining broken controls and independent review
stay open; no installation, push or release is authorized by isolated QA.

Task4 source checkpoint: `ee6d707b`. The completion gate reruns both CPU runtime
and composed selectors and validates unchanged full-suite receipts against all
36 pinned inputs. All pass. Accepted sources, helpers and test/control binaries
are frozen before further mutable rebuilds. Task4 is complete; Task5 is next.

### Task 5: Final Regression, Review And Acceptance Evidence

**Files:** tests/controls only if review finds a scoped defect; this plan/spec and existing public status docs. No new runtime feature.

**Interfaces:** Consumes all final components and the unchanged checked-quit/update consent contract. Produces an evidence-backed acceptance or explicit remaining-failure report, not release readiness.

- [x] **Step 1: Rebuild final consistent test consumers.** Fresh both-CPU source/MOC/resource builds with pinned inputs; no old partial source-object substitution for acceptance. Build/run no-Torrent core, Qt and settings. Verify actual Qt, CPU/deployment metadata and complete test inventories. The current consistent checkpoint below passes this gate; it is not a final application/material rebuild.
- [x] **Step 2: Create bounded broken-path controls privately.** One control restores engine blocking join; another treats proxy done as physical readiness; another accepts failed share retraction. Preserve all original bodies/timers and reject unknown source layout. Require each control's exact contract failures and unaffected positives; no public production edits create controls.
- [x] **Step 3: Run full serial matrix.** Core, Qt and Torrent `~[interop]`, ordinary settings, GUI singleline/multiline/sidebar with only the existing sidebar navigation exclusion, original hidden settings/storage/admission/core/auxiliary retirements, plus new composed selector. Add explicit new component selectors for inventories that complete suites omit. No-Torrent freeze/normal/hidden settings selectors remain. Do not claim new real-GOST interop or native/minOS proof from these offline runs. Current 47-run checkpoint passes; historical failures remain unwaived.
- [x] **Step 4: One independent immutable-range review.** Review source range from plan BASE through Task4 for owner lifetime, receipt failure, affinity, retraction admission, callbacks, ordering and consent. If no fresh reviewer tool is available, record that gate as pending; do not manufacture independence or message another task. Fix Important/Critical findings in one focused RED/GREEN pass, rerun the affected and complete suites; retain Minor deferrals explicitly. Current composite follow-up is recorded below; this closes source review only, not the integrated acceptance gates.
- [x] **Step 5: Protected-state and historical-failure readback.** Reuse the latest diagnostic preservation helper with fresh output names; require whole historical bundles/signatures, material/provider/live-file/audit/negative pins and previous evidence seals unchanged. Retain original 102/180/354ms reports and Torrent causes even if current selectors pass. Recheck final source/binary/control hashes after tests.
- [ ] **Step 6: Record and checkpoint actual status.** Mark this repair accepted only if its exact regressions and required reviews are satisfied. Record every remaining gate in parent settings/updater plans and Development Status. Local curated docs checkpoint/readback only; do not close broader Task3 or parent7/8 merely from this bounded repair. No signed feeds, build allocation, final assets or installation.

### Task 5 Partial Checkpoint: 2026-10-07

**Not accepted.** Fresh ARM/Intel five-target builds and a whole ARM no-Torrent
build pass. Independent immutable-range review found three Important defects:
allocating publisher fencing, retained engine sessions after exceptional worker
exit, and proxy DNS/setup admission during the Fence-only interval. The single
focused repair pass has exact baseline RED (3/1/2 failures) and both-CPU GREEN
(6 case instances / 88 assertions). Final-source broken-path controls preserve
their exact 14/9/14 failure sets over 18 instances / 433 assertions.

The serial matrix audit records 6,392 case instances / 603,596 assertions and
94 failures/errors, including three ARM GUI aborts. This is not a complete
passing inventory: each ARM GUI layout stops at 11 cases / 284 assertions;
Intel runs 126/126/124 cases and records 28 failures/errors in each layout.
Original one-shot GUI/coordinator terminal expectations encounter Pending;
an assertion thrown from a direct notification callback aborts the ARM test
process. Production uses ApplicationQuitSession polling; this fixture diagnosis
is not evidence of an installed-app crash or authorization to rewrite checks.
The ARM Qt suite also retains one bind failure in the created-source-seed
branch of the unchanged-settings runtime test. Its cause remains unresolved.
All no-Torrent matrix selectors pass. Timing budgets and original bodies remain
unchanged; neither green diagnostic repeats nor a separate audit waive failures.

The strict gate fails. The separate audit explicitly says NOT_ACCEPTED and
records fatal exits and missing inventories. Protected historical/live state and
earlier seals pass readback. All 93 final source/helper/binary/control pins
are frozen and match; original test bodies/helpers/timers are preserved.
Twelve private bind-telemetry repetitions pass (24 cases / 5,118 assertions)
with successful SDK listener alerts only. The original failure is not reproduced
or waived, and the diagnostic consumer is not acceptance evidence. The initial
protection-helper run used Apple's older Python without hashlib.file_digest;
its failed setup log remains intact, and a fresh v2 receipt using the installed
Python 3.14.7 passes without changing protected inputs. Step 1's inventory gate, integrated Steps
3/4, and final acceptance remain open. The three repairs remain local; no
completion commit, installation, publication, signing or feed activation.

The maintainer subsequently approved preparing a focused GUI compatibility
specification. Draft `docs/superpowers/specs/2026-10-07-gui-quit-pending-compatibility-design.md`
documents explicit terminal observation, safe callback result capture, physical
route ordering and retaining filesystem blockers through failed receipts.
The maintainer approved that specification after `cb7da115`. The focused
implementation plan `docs/superpowers/plans/2026-10-07-gui-quit-pending-compatibility.md`
was subsequently approved and executed in Native mode. Its fixture-only
checkpoint and bounded results follow; production shutdown behavior is unchanged.

### Subsequent GUI Compatibility Verification: 2026-10-07

Fixture checkpoint `be8f21da` migrates only the 19 approved observations to
explicit terminal collection, captures callback results outside Qt delivery,
retains blockers through Refused and observes route revocation after physical
producer retirement. Original terminal/cached/rejection/ownership/consent checks,
case/section inventories, shared helper and timing budgets remain intact.

Fresh ARM/Intel five-target/interposer and whole ARM no-Torrent builds pass.
Focused verification passes 312 case instances / 9,270 assertions; all 47 complete
matrix runs pass 6,729 instances / 621,195 assertions, zero failures/errors/skips/
fatal exits. Full GUI inventories are 126/126/124 on both CPUs; all 37 comparisons
match. Current-source controls retain exact 14/9/14 and early-core six failures.
The 101 tested pins, protected historical/live state and earlier seals verify,
including the 519-file failed checkpoint and design/plan readback seals.

This closes the bounded GUI observation compatibility work, not Task5 acceptance.
The prior ARM Qt bind cause did not recur but remains unwaived; historical
102/180/354ms causes, broader review/lifecycle/native/minOS/live/soak, language/
provenance and release gates remain open. The three runtime repairs remain local;
no completion/release acceptance, signing, install, push or publication. Overall
GUI-settings Task3 and updater Tasks7/8 remain open; signed feeds last.

### Current Composite Source Review: 2026-10-07

The [independent composite review](../../research/2026-10-07-quit-composite-candidate-review.md)
freezes `dd088f2b1..db94c22e3` plus the existing seven-file local repair patch
and allocation helper. One fresh reviewer finds no Critical/Important issue;
the conditional source verdict is supported by parent snapshot/pin/receipt
checks. A Minor established TCP/UDP relay-across-Fence regression is explicitly
deferred, not a demonstrated source defect. All four set-aside categories have
recorded dispositions and remain bounded acceptance gates where applicable.

The 3,486-file snapshot, 101 earlier pins, 275 incremental link inputs and prior
seals verify. The 47 GUI matrix receipts reparse as 6,729/621,195; swarm ordinary
Qt receipts remain 1,520/68,566. No tests were rerun. Three post-GUI-matrix native
diagnostic source deltas are explicit, so a consistent current-source full
matrix remains required. Historical failures/timing causes are unwaived.
Step4 source review is complete; Steps1/3/6 and overall Task5 remain open.
No local runtime-repair completion commit, deployment, signing or publication;
GUI-settings Task3 and updater Tasks7/8 stay open. Signed feeds last.

### Current Consistent Matrix Checkpoint: 2026-10-07

The [current consistent rebuild/matrix](../../research/2026-10-07-current-quit-matrix.md)
uses `b9338079f` plus unchanged local repairs. Fresh ARM/Intel five-target and
whole ARM no-Torrent configurations pass: 13 consumers and both-CPU test-only
interposers, without prior public-consumer object substitution. Exact current
controls retain 14/9/14 plus six premature-core failures.

All 47 matrix runs pass 6,743 instances / 621,713 assertions, zero failures/
errors/skips/fatal exits. All 37 comparisons retain earlier inventories plus
the declared diagnostic/paired-port cases; GUI remains 126/126/124 per CPU.
Actual CPU/minOS/Qt, 1,896 direct object inventories, 2,867 captured input hashes,
97 frozen pins, source/repair-patch and historical/protected-state readbacks
pass. Host/Rosetta is not native/minimum-OS/live or release proof.

Steps1/3 now pass; source review remains complete. Step6 overall acceptance
stays open while historical bind/timing/recovery and broader lifecycle gates
remain unwaived. Earlier 94 failures/errors and all seals remain intact.
GUI-settings Task3/updater Tasks7/8, deferred established-relay coverage,
native/language/provenance/reproducibility/peer/bootstrap/selected-check/soak and
final review/build/sign/install/publication gates stay open. Local source repairs
remain separate; no installation or publication. Signed feeds last.

## Self-Review And Handoff

Spec coverage: intent/compatibility/engine -> Task1; checked retraction/publisher ->
Task2; proxy physical lifetime/reaper -> Task3; dependency phases, GUI milestones,
deadline/retry/consent -> Task4; verification/protected state/remaining gates ->
Task5. The five Review Focus conditions each have an explicit owning test step.
All planned consumers use the Task1 result and identical fence/retire signatures.

Planning clarification: a failed phase is sticky, but runtime keeps returning
Pending while another required owner is unfinished; only physical aggregate
completion permits terminal Refused. This realizes the approved retained-owner
contract without allowing a failed publisher to abandon engine/proxy work.
No current production behavior is changed by the clarification or this plan.

This is one coupled retirement contract, not three independent feature projects.
Each component has its own baseline/green cycle; runtime integration consumes
their exact interfaces. Existing Native execution is preserved. The written
plan is approved; continue from Task4 without asking for the execution method
again. Keep component verification distinct from integrated acceptance.

Remaining beyond this plan: retained natural timing/Torrent causes, broader
pending-owner/per-file AntiSpam coverage, localization/native-language quality,
provenance/reproducibility, whole-candidate review, native/minOS/live/lifecycle/
benchmark/soak, final reviewed app/material rebuild and numeric production build,
signing/notarization/signed-v3 acceptance, guarded closed-app install/readback,
authenticated wiki/screenshots/prerelease publication/readback. Signed feeds last.

### Established GOST Relay Fence Coverage: 2026-10-07

The subsequently approved [test-only relay regression](../../research/2026-10-07-established-relay-fence.md)
addresses the independent review's deferred established TCP/UDP recommendation.
Real authenticated TLS GOST loopback payloads survive Fence/repeated Fence while
new accepts fail; later physical retirement drains ownership. Private omission
of the setup-complete flag causes exactly two post-Fence payload failures on
each CPU, with no unrelated failures/errors/skips. Both CPU focus passes 7
JUnit nodes / 166 assertions; complete ordinary Qt passes 761 per CPU / 69,095
total assertions, retaining all 760 earlier cases and adding one generated case.
Fresh bounded independent source review finds no issues. All 291 current inputs,
2,866 retained inputs, protected-state and historical seal readbacks pass.

This is fresh one-TU incremental Qt verification, not a new full 47-run matrix,
whole-app/native/minimum-OS/live acceptance or historical bind/timing diagnosis.
Step6/overall Task5, GUI-settings Task3/updater7/8 and broader lifecycle/peer/
bootstrap/selected-check/native/language/provenance/reproducibility/soak/release
remain open. Only the appended regression is curated; prior repair hunks stay
local/uncommitted. No installation or publication; signed feeds last.

### Refusal-Only Diagnostic Checkpoint: 2026-10-07

The [one-shot cold-refusal/post-exit observation](../../research/2026-10-07-refusal-only-shutdown-observation.md)
passes the original Intel 79-instance / 1,255-assertion selected-check inventory,
without errors/skips. It does not reproduce or waive the historical large-scope
refusal. Original test/assertion/helper/deadline bodies are reconstructed exactly;
only private copies gain observations, with no per-poll timing or validation edits.
Collected state has no sessions, active checks or journal, and matching saved
generations. Owner-call wall time is not the three-second drain measurement.

The static post-poll deadline guard can refuse even after the final record is
cleared, but this is not established as the historical cause. Preserve the guard,
all failed evidence and Task5's acceptance hold; do not repeat positive receipts
to close it. Proceed with independent dependency provenance/reproducibility while
the causal refusal, wider lifecycle/native/live/language and final release gates
remain open. No installation/publication; production signed feeds remain last.
