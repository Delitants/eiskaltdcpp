# GUI Quit Pending Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Native/inline execution is already selected. This plan needs maintainer review before implementation.

**Goal:** Make GUI quit fixtures observe physical terminal outcomes explicitly without changing production shutdown behavior or weakening existing failure checks.

**Architecture:** Keep one-shot polling APIs and the existing event-pumped test helper. Add named terminal observers in the full-GUI test translation unit, capture callback results without throwing through Qt, and retain blocked persistence fixtures until their failed receipt is collected. Preserve all existing terminal, ownership, route and consent checks.

**Tech Stack:** C++20, Qt 6, Catch2, isolated macOS ARM and Intel/Rosetta test consumers.

**Spec:** `docs/superpowers/specs/2026-10-07-gui-quit-pending-compatibility-design.md`, approved after `cb7da115` on 2026-10-07.

## Global Constraints

- Keep 100ms request budgets, 350ms holds, the original 5000ms helper, 3000ms/25ms core drain arguments and 30000ms child watchdogs unchanged.
- Keep `guiFence()` one-shot and `ApplicationQuitTestFixture.h` byte-for-byte unchanged. Do not rewrite already-correct polling or admission-rejection tests.
- Executable edits are confined to `tests/qt/test_mainwindowtoggle.cpp`; optional declaration comments only in `eiskaltdcpp-qt/src/MainWindow.h` and `eiskaltdcpp-qt/src/updates/ApplicationQuitCoordinator.h`.
- No MainWindow/runtime/session/persistence/updater behavior changes, new public setting, event pumping or unfinished join in production.
- Preserve the three local reviewed repairs and unrelated work. Any new genuine product defect requires retained evidence and a focused design revision, not an opportunistic patch.
- No app/profile/transfer/server/key/VM/artifact/feed changes; no production build allocation, signing, installation, push or publication.
- Preserve Task5's 519-file failed-checkpoint seal, all 93 frozen inputs and earlier seals. Green repeats do not waive the bind failure or historical 102/180/354ms causes.
- Compile/hash and timing runs are serial. Host ARM/Intel-Rosetta is not native minimum-OS/live acceptance. Signed feeds remain last.

## Review Focus

1. A terminal observer can report successful observation of Refused without reporting a successful save; Task1's checkpoint case must keep both facts separate.
2. A notification callback can fence input during Pending without throwing a Catch assertion through Qt; Task1 captures the result and checks suppression outside delivery.
3. Event pumping can deliver unrelated events while retained owners' queued initialization/deletion remain fenced; Task1 preserves the browser/deferred-owner checks and existing real-worker test.
4. A valid immediate Ready or admission rejection must not be forced into Pending; Task1 preserves direct rejection checks and makes unheld first-result checks conditional, not scheduling-dependent.
5. Repairing a blocked path or revoking a route before physical completion can invalidate failure/consent coverage; Task1 waits for real refusal before repair and Task2 reruns the original held-checkpoint/early-core controls.

## QA Recipe

At implementation, create fresh `QA=Work/qa/gui-quit-pending-compatibility-20261007`,
with a unique suffix if occupied. Do not create or execute these helpers during
plan preparation. Set `OLD=Work/qa/nonblocking-torrent-quit-repair-20261006/task5`.
Copy its `run-case.sh`, `build-tests.sh`, `build-no-torrent.sh`, `build-green.sh`
and `run-matrix.sh`, replacing only the literal output-root references with QA.
Keep presets, provider/plugin paths, dependency/stage pins, test build number 1
and target inventories. Record the original/copied helper hashes and commands.
The inner Seatbelt runner needs escalation outside the outer sandbox; it still
uses disposable HOME and denies external traffic. No live services are used.

Before any edits, verify the 519-file seal and all current 93 input pins. After
authorized migration, compare old frozen copies/seals, not current edited files
against old hashes. Pin fresh current inputs separately and require all
unmigrated source/test files to match the baseline. Save an explicit assertion
move inventory and exact diff, not merely a count of passing tests.

Fresh post-edit builds:

```sh
bash "$QA/build-green.sh"
```

This builds all five targets on each CPU and both test-only interposers, plus
whole ARM no-Torrent core/Qt/settings. No old partial object substitution. Use
`run-case.sh CPU TARGET UNIQUE_LABEL SELECTOR` for every run; it rejects reused
labels and retains XML/log/exit. Keep seed `594128903`. Matching ordinary and
interposed selector/provider rules come from the copied serial matrix.

## Task 1: Explicit Terminal Observation And Scoped Fixture Migration

**Files:** modify `tests/qt/test_mainwindowtoggle.cpp`; optional comments in
`eiskaltdcpp-qt/src/MainWindow.h` and
`eiskaltdcpp-qt/src/updates/ApplicationQuitCoordinator.h`. Do not modify the
shared fixture/header, production implementations or existing component tests.

**Interfaces:** consumes existing `MainWindow::stopWorkersForQuit()`,
`quitWorkersPending()`, `quitProducersRetired()`, coordinator `prepare(uint32_t)`
and the unchanged shared `waitFor(const std::function<bool()>&)`.
Produces test-local `bool waitForGuiQuitTerminal(MainWindow&, bool& saved)` and
`bool waitForCoordinatorQuitTerminal(ApplicationQuitCoordinator&, uint32_t,
ApplicationQuitCoordinator::Result& terminal)`. True means a terminal result
was observed, never that it was Ready. Both use default persistence behavior
and the existing helper, preserve the actual result, and stop repolling after
terminal observation. Admission tests do not use these observers.

- [ ] **Step 1: Preserve and confirm unchanged RED.** Save current source/header/shared-helper hashes and verify `$OLD/evidence-partial.sha256`. Predeclare the exact chosen failure expressions from `$OLD/matrix-audit.json`, then run each named sample separately from the matching frozen Task5 consumer via `EISKALT_QA_BINARY`. Samples: queued notification boundary, real coordinator aggregation, Torrent checkpoint refusal and actual window-close handoff. Intel retained RED is respectively 1 callback failure + 1 error, 1 Ready failure, 2 GUI-value failures + 1 cached-shutdown failure, and 3 route-revocation failures. The ARM callback has 1 failure + SIGABRT error/exit134; its absent later cases are not passes. If fresh reruns differ or pass, retain/investigate instead of manufacturing matching RED. Setup failures and unrelated diagnostics are never accepted as behavioral evidence.

- [ ] **Step 2: Add explicit observers and migrate only the table below.** Use the existing 5000ms helper, returning observation separately from saved/status. Capture GUI callback request/result/Pending in local values with no Catch assertions or nested event pumping in the callback. Make the smallest moved-observation changes; preserve case names, sections, fixtures and every terminal/byte/owner/consent assertion. Keep filesystem blockers until collection of the intended Refused receipt. Optional declaration comments clarify false/Pending/rejection and the coordinator's per-call core budget, without changing signatures/defaults.

### Authorized Observation Inventory

Line anchors refer to source before this migration. The table lists first
terminal observations only; direct cached repeat and rejection checks remain.

| Existing Case | Anchor | Exact Change And Preserved Checks |
| --- | --- | --- |
| Automatic update notifications obey preferences and retained GUI fencing | 354 | Collect GUI terminal result before the existing success assertion; retain notification suppression and action/controller rejection checks. |
| Queued update notices revalidate policy at the notification module boundary | 411 | Capture the GUI callback result; assert outside Qt delivery and collect terminal success. Keep dispatch, stale-offer and policy checks. Leave the controller-only section at414 unchanged. |
| Actual window close waits for the committed matching update receipt | 479 | Move route-revoked observation after producer retirement and before handoff checks; retain all three section outcomes and consent/owner checks. |
| Actual no-update window quit prepares core before deferred exit readiness | 516 | Keep immediate input fencing; assert revocation after producer retirement before final deferred-readiness checks. |
| Direct coordinator checkpoint recovery cannot preserve installer consent | 895 | Observe terminal Refused before repairing the blocked auxiliary path; preserve explicit retry and zero install. |
| Application quit aggregates real GUI hub Torrent and core saves without install | 952 | Observe terminal Ready; keep foreign/modal rejection and cached repeat at979 direct, all byte/route/queue/owner checks intact. |
| Prepared application owners remain alive through a matching update handoff receipt | 999 | Observe Ready before handoff; preserve all prepared/refused/no-update variants and retained owners. |
| Application quit retains an earlier updater or GUI refusal after core succeeds | 1036 | Observe Refused before changing blocker/backend outcome; keep first component and cached Refused checks. |
| GUI refusal delays auxiliary snapshot reads until core callback drain | 1076 | Observe Refused before repairing config; preserve save-observer ordering and actual auxiliary-file checks. |
| Application quit refuses auxiliary save failure even after a ready core | 1128 | Observe Refused before repair; preserve exact component, core Ready, cached refusal and explicit recovery. |
| Auxiliary retry rejects foreign and modal callers without replaying core saves | 1165 | Observe initial Refused only; keep rejection, save-count and explicit retry checks direct. |
| Auxiliary retry cannot replace lost frozen intent with a new owner | 1186 | Collect refusal before replacing the owner; keep rejection and no-new-file checks. |
| Application quit failed-core child fixture | 1227 | Poll with the unchanged conditional3000/25 core budget; retain socket/snapshot/both refusal components, owner retention, cached refusal and process-exit policy. Parent exit-code checks at1151 stay unchanged. |
| GUI quit preparation retires connected and inactive hub clients but retains tabs | 1291 | Observe successful GUI terminal receipt; retain inactive-client, tab/owner and cached-repeat checks. |
| GUI quit preparation saves history intent and caches Torrent persistence refusal | 1417 | Observe terminal separately from success; keep jobs.json blocker until failed receipt, then repair and assert cached failure in GUI/runtime. Preserve the success section and cleared history. |
| Uninitialized file browser quit child | 1674 | Observe GUI terminal completion without admitting browser initialization; retain empty views before/after. Parent watchdog/marker/exit assertions stay unchanged. |
| GUI settings sync refusal remains sticky while retaining the core | 1707 | Retain first immediate fencing checks; collect terminal Refused before deleting config blocker, then keep cached GUI refusal and successful runtime-only cached result. This currently green race-prone case is explicitly in scope. |
| GUI quit preparation rejects nested modal input before consuming its owner attempt | 1726 | Keep rejection at1724 direct; after modal close observe terminal success and preserve original success check. |
| Prepared GUI retains deferred owners and rejects the legacy window-close path | 1807 | Observe terminal success while retaining deferred-delete fencing; keep owner, visible-window, legacy-close and cached-repeat checks. |

Unlisted correctly polling cases, foreign/modal rejection and repeated terminal
checks remain unchanged. If source moved, resolve anchors by exact case name
and expected expression; reject unknown layouts instead of broad replacement.

- [ ] **Step 3: Pin observer semantics with the existing cases.** Add the following assertions to the checkpoint success/failure sections before blocker removal. Then keep the original cached GUI/runtime checks after repair. For coordinator cases require the observer true, then the original exact Ready/Refused/component assertions; terminal Refused must not become observation failure or an implicit retry. In callback tests require captured request, admissible success-or-Pending observation and immediate fenced window, followed by terminal success and original zero-delivery checks outside the callback. Unheld tests must not require initial Pending. Preserve direct admission rejection, browser empty-view and deferred-owner assertions. Compare the moved-expression inventory and shared-helper bytes against the baseline.

```cpp
REQUIRE(waitForGuiQuitTerminal(*window, saved));
CHECK(saved == expected);
CHECK_FALSE(window->quitWorkersPending());
CHECK(runtime->shutdownChecked() == expected);
```

- [ ] **Step 4: Rebuild and run focused GREEN.** Run the fresh three-configuration build recipe before timing runs. For each CPU execute the following selector in each GUI layout, with only the existing sidebar navigation exclusion where applicable. Require no failures/errors/skips or fatal exits; check original selected cases are retained. Then rerun the existing real-worker/foreign/modal cases through the same selector rather than changing their bodies.

```sh
FOCUS='[update-ui-owner],[update-notification-delivery],[window-quit],[application-quit],[gui-quit-fence],[gui-layout-review]'
bash "$QA/run-case.sh" arm64 gui focus-singleline-arm64 "$FOCUS"
```

Set `EISKALT_TEST_TAB_LAYOUT` explicitly for every layout and use matching CPU
labels/provider paths. Do not invoke all layouts concurrently. The subset
includes existing correctly polling tests and their opposite-boundary positives.

- [ ] **Step 5: Curated fixture checkpoint.** Review the exact diff against the observation table, shared-helper hash and untouched executable-source inventory. Commit only the migrated full-GUI fixture and optional comment-only headers as `test(quit): observe retained GUI retirement terminals explicitly`. This is an isolated fixture checkpoint, not Task5 acceptance; keep the local review fixes and status work separate and never push here.

## Task 2: Integrated Verification And Remaining-Gate Readback

**Files:** fresh QA helpers/receipts only; update this plan, parent nonblocking
quit/settings/updater plans and `docs/wiki/Development-Status.md` with actual
results. No new executable product/test changes in this task.

**Interfaces:** consumes Task1 observers/migration, the fresh build consumers
and unchanged component/settings/physical-hold contracts. Produces explicit
complete-inventory results, immutable current pins and an acceptance or
NOT_ACCEPTED remaining-failure report, never release readiness.

- [ ] **Step 1: Rerun the current-source private broken controls.** Copy `$OLD/build-final-controls.cjs`, `run-final-controls.sh`, `check-red.py` and `final-control-contracts.json` to QA, replacing the output root only. Build against fresh consumers, then require the exact predeclared engine14/proxy9/retraction14 failures and unaffected positives, zero errors/skips. Rebuild the premature-core control using Task4's corrected `build-early-core-v2.cjs`: change only QA/build roots to current fresh ARM settings consumer, keep its production-header include correction and sole guarded mutation, and require exactly `{'!(route->revoked->load())': 6}` failures from `[.torrent-settings-freeze-retirement]`. Run it through the copied runner with `EISKALT_QA_BINARY` pointing to QA's `early-core-control-v2/eiskalt-settings-quit-tests` and the original ARM `io-hold-v2.dylib`. Use the same exact-multiset checker with a separately predeclared early-core contract. Never execute the sealed old control as evidence of a new current-source build.

```sh
/opt/homebrew/opt/node@22/bin/node "$QA/build-final-controls.cjs"
bash "$QA/run-final-controls.sh"
```

- [ ] **Step 2: Run the complete serial matrix.** Run the copied `run-matrix.sh`: ordinary core/Qt/Torrent `~[interop]`, ordinary settings, explicit physical component/new-review selectors, all three GUI layouts, hidden allocation/launch/core/settings/auxiliary and composed cases, whole no-Torrent full/freeze/settings selectors. Existing original I/O interposers are enabled only for their original hidden cases. Keep held100/350, saved bytes, route-before-physical-readiness, deadline refusal, zero late install/exit and explicit retry assertions in the original composed tests unchanged.

```sh
bash "$QA/run-matrix.sh"
```

- [ ] **Step 3: Validate complete inventories and truthful failure status.** Build a QA checker from `$OLD/check-matrix.py` using current paths and matching executed labels; do not retain prerequisites for old Task5 setup scripts which were not run in this QA. Keep zero-error/skip/fatal-exit strict acceptance. Compare unchanged ordinary and complete GUI case-name multisets against the frozen Intel inventories, matching both CPUs with the original sidebar exclusion; no case/section disappears through the migration. Check assertion counts independently, documenting intentional added observations rather than requiring an identical count. Maintain a separate explicitly NOT_ACCEPTED audit for any failure/abort. The old ARM Qt bind failure remains unwaived even if this run passes; no new bind repair is authorized here.

- [ ] **Step 4: Freeze and read back final tested inputs and protected state.** Pin current migrated fixture/comments, all unchanged component/session/settings sources, actual binary CPU/minOS/Qt metadata, compile inventories and current private controls. Require all unchanged files to match old pins and all new copies to match current hashes. Use the Task5 protection helper with a fresh output name and the installed Python3.14.7, then verify earlier seals plus the519-file partial checkpoint and4-file design readback seal. Keep historical reports/artifacts/signatures/live state intact. Host/Rosetta is not native/live evidence.

- [ ] **Step 5: Record actual outcome and hand off.** Inline-review the scoped migration/pin/inventory report against the spec; no additional reviewer delegation is authorized by this plan. Update parent status with exact focused/full results and remaining gates. If GUI passes, record that bounded result without accepting overall Task5 while the bind cause or another required gate remains. Checkpoint curated documentation only after readback. No install/sign/push/publication/feed action. If a real product failure appears, retain it and request its focused design instead of extending this fixture task.

## Self-Review And Approval Handoff

Spec coverage: notification/GUI/coordinator/route/failure-fixture migration and
all five Review Focus inputs are owned by Task1; physical sensitivity, full
matrix, inventories, protected state and remaining gates are owned by Task2.
Observers return terminal observation separately from success and keep the
existing 5000ms helper. The19-row table names the authorized migrations;
already-correct polls and rejection/cached-repeat checks are preserved.

This plan needs maintainer review before implementation. Native execution is
already selected and will not be asked again. Broader bind/lifecycle/native/
live/language/provenance/soak and final build/sign/install/GitHub/wiki/prerelease
gates remain pending; overall GUI-settings Task3 and updater Tasks7/8 stay open.
Signed feeds remain last.
