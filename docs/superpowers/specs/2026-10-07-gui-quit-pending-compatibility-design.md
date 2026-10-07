# GUI Quit Pending Compatibility

Status: draft for maintainer review, 2026-10-07. The maintainer approved
preparing this focused design after the Task5 failed integrated checkpoint.
This document is not yet approved for implementation. Native/inline execution
remains the selected method; no new delegation or worktree is requested.

## Intent And Boundaries

Finish the existing nonblocking Torrent quit contract without making the GUI
wait for unfinished workers, confusing admission with completion, or weakening
the original persistence/refusal/update-consent tests. Adapt only observations
whose immediate-terminal assumption conflicts with the approved physical
retirement path. Preserve all final assertions about saved bytes, retained
owners, route revocation, notification suppression, cached failure, explicit
retry and installer consent.

This extends the approved nonblocking Torrent retirement and settings-freeze
designs. It does not redesign the updater, add a public setting, change storage
formats, fix the unresolved Torrent bind cause, or accept the broader release.
The installed app, profile, transfers, servers, keys, VM, artifacts and feeds
remain untouched. Unrelated work and all earlier failed receipts stay intact.

## Observed Evidence

The sealed Task5 checkpoint is at source HEAD `a7ef3944`, with three local
review repairs and 93 frozen source/helper/binary/control pins. Its separate
audit is NOT_ACCEPTED: 6,392 case instances / 603,596 assertions, 94
failures/errors. ARM GUI runs abort at the same direct notification callback;
Intel completes the three layouts with 28 failures/errors each. ARM GUI
inventories are partial and cannot be treated as passing suites.

Source and receipt tracing identify four distinct observation problems in
`tests/qt/test_mainwindowtoggle.cpp`:

1. Immediate `stopWorkersForQuit()` / `guiFence()` success and coordinator
   Ready/Refused expectations observe legitimate Pending. The failed-core
   child receipt explicitly reports enum value 3 (Pending), not a terminal
   save failure. Some existing tests already poll; the one-shot helper itself
   must not silently become a blocking helper.
2. `REQUIRE(window->stopWorkersForQuit())` inside the direct notification
   callback throws through Qt event delivery. ARM aborts; Intel reports a
   caught exception. Catch assertions belong on the test thread outside the
   callback invocation, not inside the signal delivery boundary.
3. Actual-window-close tests require immediate proxy-route revocation. The
   route must instead remain valid until physical producer retirement, with
   updater admission already fenced. Later route-revoked checks remain
   mandatory before commitment/deferred exit.
4. The failed Torrent checkpoint section removes its blocked `jobs.json`
   directory immediately after the first false GUI result. False can mean
   Pending, so the filesystem repair may precede the worker's attempted save.
   The receipt subsequently observes `shutdownChecked() == true` where false
   was expected. Static ordering explains this race but is not a new isolated
   proof: the implementation phase must pin the failure before removing the
   blocker and verify that refusal stays cached afterward.

The uninitialized-file-browser child also uses a one-shot terminal expectation.
Its pre-/post-fence empty-view checks must remain true while events are pumped;
unexpected initialization is a product failure, not permission to drop a check.

Production `ApplicationQuitSession` already polls Pending with a 10ms timer and
checks its original elapsed deadline before commitment. Coordinator Pending
does not grant install consent; the coordinator's timeout argument is the core
drain budget, not a fresh global consent deadline on every poll. These properties
must not be changed to make legacy test observations pass.

## Alternatives

1. Restore synchronous GUI waits for legacy calls. This violates the approved
   responsiveness/physical-retirement contract and is rejected.
2. Accept false as success, accept Pending as Refused, remove terminal checks,
   or wait solely for a desired Ready value. This can hide refusal or premature
   readiness and is rejected.
3. Keep the production polling semantics, explicitly separate admission,
   Pending and terminal observations in affected tests, and document the
   existing API contract. Recommended; no new shutdown worker or runtime
   state machine is needed.

## Contract

`MainWindow::stopWorkersForQuit(bool asynchronousPersistence = false)` remains
one owner-thread poll. True means a successful terminal receipt. False can be
an admission rejection, Pending, or terminal failure; admitted tests distinguish
Pending through `quitWorkersPending()`. Admission rejection remains directly
tested on foreign/modal calls and must not consume the valid owner attempt.
`quitProducersRetired()` is the physical producer milestone, not a settings
completion or installation receipt.

`ApplicationQuitCoordinator::prepare(uint32_t, bool = false)` keeps its four
existing statuses. An admitted caller may repoll Pending with the same arguments
and retained coordinator/context. Tests stop on any non-Pending result, then
assert the originally intended exact status/component/core outcome. They must
not filter out Refused until a timeout or manufacture a Ready result.

The production session remains the only existing deadline/commitment driver.
No event pumping, unfinished join, synchronous Torrent wrapper, detached thread,
timeout reset or new consent path is added to production. Existing synchronous
Torrent shutdown wrappers stay synchronous for their explicit legacy consumers;
tests use them only after physical terminal collection when checking cached
results, never as a way to bypass Pending.

## Focused Fixture Migration

Keep the existing one-shot `guiFence()`, the shared `waitFor()` helper in
`ApplicationQuitTestFixture.h`, interposers and request/hold values unchanged.
Add clearly named, test-local
terminal-observation helpers in `test_mainwindowtoggle.cpp` which use the
existing 5000ms event-pumped helper. They return whether a terminal result was
observed and retain the actual result separately. They do not mean the save
succeeded. Use them only after valid GUI-owner admission; rejection tests remain
direct. Every migration must list its moved assertion and preserved postconditions.

- Notification cases: callbacks capture whether the fence was requested, its
  result and Pending state without Catch assertions or nested event pumping.
  After signal delivery returns, assert fencing and suppression during Pending,
  collect terminal completion outside the callback, and keep all final
  notification-policy/stale-offer checks. Controller-only callback checks are
  not changed merely because they are adjacent.
- GUI terminal cases: retain the first prompt request and immediate input/core
  ownership checks; collect Ready or Refused explicitly before the existing
  terminal assertions. Preserve cached repeat checks after terminal receipt,
  foreign/modal rejection, deferred-owner retention and empty file-browser UI.
- Coordinator cases: retain all Ready/Refused/component/byte/owner/route and
  recovery assertions. Only the observation of the first terminal result moves
  behind explicit polling; repeated terminal calls stay direct to verify caching.
  The 3000ms/25ms core drain arguments and 30000ms child watchdogs stay unchanged.
- Route cases: preserve immediate updater/input fencing. In a controlled real
  producer hold, assert the route is not revoked while producers are unfinished;
  retain the original route-revoked assertion after physical producer completion
  and before handoff/exit readiness. Unheld tests may complete immediately and
  must not require a scheduling-dependent first Pending result.
- Failure fixtures: keep blocked settings/checkpoint paths until the actual
  failed receipt has been collected. Only then repair the filesystem and assert
  the same refusal remains cached. Keep auxiliary retry/disarm assertions and
  saved bytes unchanged; no implicit producer retry is introduced.

The observed affected groups are notification delivery, actual close/handoff,
direct coordinator aggregation/refusal/recovery and failed-core children,
hub/GUI terminal fences, checkpoint failure, modal/deferred-owner cases and
uninitialized-browser child. Audit the remaining same-pattern call sites too,
including currently green one-shot refusal tests, without widening scope to
unrelated GUI features. Correctly polling and admission-rejection tests are
not rewritten.

## Source Scope

Expected executable changes are fixture changes only in
`tests/qt/test_mainwindowtoggle.cpp`. Clarify comments for the existing
MainWindow/coordinator declarations if needed; preserve all signatures/defaults.
`ApplicationQuitTestFixture.h`, `ApplicationQuitSession.cpp`, MainWindow/runtime
implementation, persistence owners, production worker lifetimes and updater
consent logic are unchanged by this design.

If explicit terminal observation reveals a genuine product defect, preserve its
exact failure and stop that repair path for a focused design revision. Do not
turn this specification into broad permission to patch production behavior.

## Verification Before Acceptance

Preserve the existing 519-file partial evidence seal, 93 frozen inputs and all
earlier failures. Create fresh, uniquely named receipts for subsequent work.
First run the unchanged focused affected cases from the frozen consumers to
confirm the existing RED; never overwrite a failed full suite. After the narrow
test migration, require all original terminal checks plus explicit Pending
observations to pass. New observations can already be green against the correct
production path; that is contract coverage, not an invented product RED/GREEN.

Use the existing real held-checkpoint/physical-exit fixtures to require prompt
return (<100ms), GUI heartbeat through a 350ms hold, retained owner/route before
physical readiness, eventual correct terminal status, deadline refusal and zero
late exit/install without explicit retry. Keep the original 100ms request,
350ms holds and 5000ms helper. Existing blocking-engine, done-based proxy reap,
failed-retraction acceptance and premature-core-drain controls remain sensitive;
do not replace physical proof with a test double or a done flag.

Rebuild consistent ARM/Intel consumers and the whole applicable no-Torrent
configuration, then run focused cases, ordinary and hidden composed/settings
selectors, all three GUI layouts and the full serial core/Qt/Torrent matrix.
No heavy compilation/hash work overlaps timing tests. Compare complete GUI
inventories across architectures, with only the existing sidebar navigation
exclusion; reject fatal exits, errors, skipped cases or disappeared tests.

Recheck final source/binary/control pins, protected historical/live state and
earlier seals. Even a green GUI migration does not accept Task5 while its
unresolved ARM bind failure remains. Twelve private green bind repeats did not
waive it. Native minimum-OS/live/soak, historical 102/180/354ms causes, broader
Torrent/peer/bootstrap/selected-check, localization/provenance, final rebuild,
signing/notarization, guarded install and publication stay separate. Overall
GUI-settings Task3 and updater Tasks7/8 remain open; signed feeds stay last.

## Handoff

This written specification needs maintainer review. Once approved, write the
focused implementation plan with an exact call-site migration inventory and
verification commands, preserving Native execution. Review that plan before
implementation. No fixture/product behavior has been changed in this design
task, and no completion or release gate is closed by writing it.
