# Nonblocking Torrent Retirement During Checked Quit

Status: proposed written design for maintainer review. The maintainer approved
preparing this design after the isolated checkpoint diagnosis, not its
implementation. A reviewed implementation plan and Native/inline execution
selection follow written-design approval. No installation or release approval
is implied.

## Intent And Evidence

Checked normal quit must keep the GUI responsive while Torrent work retires,
save the final resume checkpoint, and refuse an expired request. Neither worker
completion nor an old update consent may independently authorize late exit or
installation. Keep the installed app, profile, transfers, credentials, proxy
configuration, signing keys, VM, historical artifacts and feeds unchanged during
development and isolated QA.

At source `e3dab31ac`, a private fixture holds actual final `jobs.json` writes
and flushes with the existing 350ms interposer. Against the unchanged 100ms
request budget, 28 ordinary/Ready-update observations take 365-401ms without
GUI heartbeat. Four observed join stacks run through MainWindow, TorrentRuntime
and TorrentEngine::Impl::stop while the checkpoint writer is held off GUI.
Fourteen matched unheld observations take 6-25ms. Saved bytes and timeout refusal
still pass. Six unchanged current-selector runs pass but do not identify or waive
the historical 102/180/354ms failures. This design addresses the confirmed
blocking boundary, not every retained freeze or Torrent regression.

Current runtime shutdown also synchronously joins the sharing publisher and
clears proxy workers whose destructors join. Proxy `done` is stored before
lambda captures and thread-local state finish destruction; it is not a physical
exit receipt. Those adjacent boundaries must not replace the engine join with
another GUI wait. They have source evidence, not this diagnostic's held-runtime
reproduction; each needs its own failing regression before implementation.

## Alternatives

1. Retain blocking shutdown and check the deadline afterward. Existing checks
   already refuse commitment, but the GUI still freezes. Not sufficient.
2. Run runtime shutdown on a new coordinator thread. Its QObject owners, server,
   timers, mutation APIs and callbacks are GUI-affine; moving this call is unsafe.
   Detaching the existing workers or treating `done` as physical exit is unsafe
   for borrowed engine/core state and worker cleanup. Not selected.
3. Keep owners on GUI, close admission once, request cancellation, and poll owned
   thread-exit receipts. Collect/join only completed workers and preserve final
   checkpoints. Recommended; no replacement Torrent worker or GUI event pumping
   inside a worker is introduced.

## Scope And Compatibility

Add a checked, nonblocking retirement path to TorrentEngine, TorrentSharePublisher,
TorrentProxyAdapter and TorrentRuntime. Reuse a small value-only result contract
with Pending, Ready and Refused plus a credential-free component identifier.
Ready means physical retirement and a successful required checkpoint, not just
an issued cancellation. Unknown owners, foreign-thread calls and missing/error
receipts refuse admission without mutating a valid owner-thread attempt.

Keep existing synchronous `stop()`, `shutdown()` and `shutdownChecked()` behavior
for explicit legacy consumers and final destruction. They can request the same
retirement state and block only in their existing synchronous context. The new
MainWindow checked path must never call those blocking wrappers while Pending.
No destructor joins may be triggered by dropping an unfinished owner. Forced
or crash teardown does not acquire a new hard responsiveness guarantee.

No settings format, job identity, resume-generation, selected-file verification,
DC-sharing exclusion/cache, proxy authentication/TLS/UDP, direct-fallback policy,
updater consent or existing deadline changes. No new user-facing setting or
network service is needed. Compile guards preserve the no-Torrent path.

## Admission And Runtime Phases

TorrentRuntime owns a terminal quit state machine and retains all three child
owners. The first GUI-owner request sets `stopped`, disarms publication and
fences engine mutation/configuration, sharing enqueue and proxy connection
admission before returning Pending. Every entry point, including cached-mutation
APIs that do not use `enqueue`, must obey this fence. Late queued configuration,
DNS and publication callbacks cannot create new work or revive status.

Admission fencing is distinct from physical retirement. Preserve the dependency
order: cancelled sharing work and its retractions finish before engine teardown;
engine resume/local-verification/session teardown finishes before existing proxy
workers are retired. Close the GUI-owned proxy listener to new accepts at the
first fence, but retain existing relay workers and route data until their phase.
No owner, core context or proxy route is destroyed between phases.

Use explicit phases: Fence, Publisher, Engine, Proxy, Complete. Polls are
idempotent and do not reconfigure, rehash, re-save completed checkpoints, recreate
listeners or repeat retractions. A physical failure is sticky Refused; timeout
alone is a session refusal and leaves the runtime Pending until its worker
finishes. A new explicit quit request may collect that same eventual receipt.
Do not silently restart a stopped phase or reset a failed checkpoint.

## Engine Retirement

Fence public engine mutations on its QObject owner without performing I/O or
waiting on a worker-owned long operation. Request worker stop once, wake the
existing loop, and retain Impl and the thread. Already accepted work follows the
existing queue/drain policy; selected checks, resume alerts, session destruction
and final atomic `jobs.json` persistence remain on that worker.

Give Impl one owned terminal future created before thread launch. The worker
publishes its checked persistence outcome at thread exit, including thread-local
cleanup. Worker exception and launch paths must publish or retain explicit
failure, never leave a false Ready or a discarded joinable thread. Future
readiness is polled with zero wait; after readiness, the owner collects the
receipt and joins before returning Ready/Refused. `shutdownSaved` may not be read
concurrently merely because stop was requested. Never detach or terminate.

The retirement path rejects new commands without invalidating lifetime-safe
cache readers needed by the cancelled publisher. No receipt is accepted before
local verification/session destruction, required checkpoint commit and worker
thread-local teardown complete. Existing atomic checkpoint failures still fail
shutdown. Unselected files and user data are not changed or deleted.

## Sharing Publisher Retirement

On GUI, cancel all publication tokens before yielding. Freeze the IDs requiring
retraction into value-owned data, reject enqueue, and signal the existing worker.
Do not call a potentially contended ShareManager retraction on GUI as part of
checked retirement. The publisher worker finishes cancellation of its current
publication, then performs the frozen retractions against the retained core
before publishing its thread-exit receipt. Core admission must still be open
through that phase; no new hashing or publication is admitted.

ShareManager::removeManagedFiles currently uses its producer guard and lock,
but silently returns when producer admission is closed. Add a thread-safe
`bool removeManagedFilesChecked(const string& owner)` counterpart: reject closed
admission, perform the same guarded removal/rebuild, and return true when the
owner is removed or already absent. Keep the existing void API and ordinary
invalidate/exclusion semantics compatible. The quit publisher uses a checked
retraction callback and aggregates failures; other callers need no migration.
Do not call a guard rejection success. Exceptions refuse shutdown while
preserving owners and intent. This narrow core receipt addition is part of the
written design, not authorization for a broader ShareManager refactor.

Suppressed queued hashing/finished callbacks must remain harmless throughout
Pending and Refused. Token cancellation, not disappearance of the QObject or
callback delivery, fences an in-flight publication commit. No worker invokes
GUI-affine mutation directly, and no retraction runs after core drain begins.

## Proxy Worker Retirement

Listener close and timer control stay on GUI. At the proxy retirement phase,
set stopping, wake the existing backoff condition and cancel retained resolver
requests. Preserve existing no-fallback and encrypted-route behavior. Accepted
local sockets, upstream sockets, UDP relays, reservations and resolver state
remain owned until their worker completion is collected.

Each Worker has an owned thread-exit receipt, initialized before launch. It
must become ready only after relay teardown, lambda-capture/resource destruction
and thread-local exit; the existing `done` flag is not an acceptance signal.
The ordinary reaper must also use physical readiness before erasing/joining a
worker, so an event-loop tick cannot recreate the same stall outside quit.
Keep unfinished workers in the vector and aggregate Pending until all receipts
are collected. Reset limits/route data only afterward; no `workers.clear()` on
unfinished workers. Launch failure and cancellation must retain resource safety.

Queued DNS callbacks check cancellation and retirement before creating a lookup;
DNS result delivery cannot resurrect a relay. Audit short cache/limit/resolver
mutex critical sections and Qt cancellation calls for GUI waits. If an existing
native abort call blocks on unfinished resolver work, reproduce it and revise
this design rather than claim thread receipts cover that separate wait.

## MainWindow And Checked Quit Composition

Keep ordinary GUI-worker retirement and retained layout capture on GUI. Cache
layout/transfer snapshots once in a separate milestone so polling Pending
Torrent retirement cannot save them repeatedly. Preserve the existing
most-recent-layout ordering, failure aggregation and frozen settings values.
Detach the transfer view's engine observation once before retirement begins.

While any Torrent phase is Pending, report `quitWorkersPending()` true and
`quitProducersRetired()` false. Return promptly without starting core drain or
discarding runtime owners. Only complete Torrent retirement permits the producer
milestone that currently admits core preparation while GUI settings persist.
Keep updater fencing before intentional core route revocation. Preserve existing
settings, auxiliary checkpoints and deadline admission checks.

The session's original 100ms test budget is not widened. While a physical hold
lasts 350ms, GUI events continue and the session becomes Refused by its existing
deadline logic. Later worker completion alone never emits exitReady or starts
an installer. Explicit retry reuses successful snapshots/checkpoints and
revalidates existing update consent under the current rules. Checkpoint-recovery
disarm remains permanent; runtime failure cannot become success on polling.

## Verification Before Acceptance

Promote the diagnostic into bounded regressions only after the plan is approved.
Require failing baseline then passing repair for each affected runtime boundary;
keep the 100ms budget, 350ms holds and original 5000ms helper unchanged.

- Actual engine checkpoint write and flush: prompt return, GUI heartbeat,
  off-GUI writer, Pending through physical hold, correct final bytes and receipt.
- Engine TLS exit/session/check cancellation: no early disposal or Ready.
- Publisher held publication/retraction/TLS exit: cancellation before commit,
  no GUI retraction wait, checked removal while core remains live, no late status.
- Proxy held connection/resolution/TLS exit and ordinary reaper: no premature
  vector erase/join, no replacement lookup/relay and no direct fallback.
- Ordinary and Ready-update quit: deadline refusal, no late exit/install,
  explicit retry without duplicate checkpoints, and cached failure remains red.
- Foreign caller, launch/receipt/persistence/retraction failure, zero workers,
  repeat calls, queued callbacks, storage-marker and retained-owner checks.
- Complete serial core, Qt, Torrent, settings, all GUI layouts and hidden
  retirement matrices on ARM and Intel/Rosetta; no-Torrent selection as before.

Validate exact case inventories, error sets, exit codes, source/object/binary
inputs and sandbox denial. Preserve original failing reports and controls.
Run matching broken-path controls and one independent review of the completed
repair range. Host/Rosetta execution is not native/minimum-OS proof. Recheck
historical bundles/signatures, providers, notices, live files and prior evidence
without changing them. No heavy build/hash work overlaps timing tests.

## Remaining Gates

This proposed repair is not overall GUI settings Task 3 or parent Tasks 7/8
acceptance. Historical natural timing, Torrent publication/bootstrap/bind/peer
counter/selected-check causes, broader pending-owner and per-file AntiSpam
coverage remain open. Localization/native-language quality, provenance and
reproducibility, whole-candidate independent review, native/minimum-OS/live GUI,
lifecycle/benchmark/soak acceptance, final reviewed app/material rebuild, numeric
production build allocation, signing/notarization, signed-v3 update acceptance,
guarded closed-app installation/readback and authenticated wiki/screenshots/
prerelease publication/readback remain separate. Signed feeds are last.
