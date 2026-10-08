# Torrent Shutdown Refusal Diagnostics

Status: proposed focused diagnostic design; not a runtime repair or acceptance.

## Evidence And Problem

The retained Intel 10,000-file/500,000-piece test failed only its checked shutdown
assertion. Its whole-case time was 9.169 seconds and owner admission passed at
35 milliseconds. The original result contains no failing shutdown stage. Later
successful traces do not identify that original cause and must not waive it.
Do not repeat the already-passing buffered or post-exit diagnostic setups.

Current `drainFileChecks()` checks its three-second deadline after polling.
It can therefore refuse even when that poll clears the last active check.
That is consistent with the strict work deadline, not evidence that the deadline
should be relaxed. When the final poll finishes at 2,999 milliseconds, the later
20-millisecond sleep does not itself cause refusal: the next loop condition sees
no active check. An inactive check with a retained journal record also refuses.

A private fixture extracts this exact branch body, replacing only the sleep
callee and supplying scripted clock/record/alert/poll boundaries. Ten cases pass
per CPU. Removing the deadline condition fails exactly two terminal-at/after-
deadline controls per CPU. ARM executes on the ARM host; x86_64 executes under
Rosetta. These are control-flow checks, not libtorrent, real disk, full-engine,
native Intel/minimum-OS or original-failure reproductions.

## Proposed Ownership And Data

Keep an allocation-free `ShutdownObservation` inside `TorrentEngine::Impl`:
fixed-size stage flags, elapsed microseconds and aggregate active/check-record/
save-pending counts. No strings, lists, paths, endpoints, peer IDs, credentials,
raw exceptions or raw alerts. Write it only on the engine worker.

Record separate flags at existing refusal branches: selected-check deadline,
retained selected-check journal, pre-final resume drain, final resume drain,
resume checkpoint rejection, close-state commit, final-state commit and worker
exception. Multiple failures can coexist; do not collapse them to one guessed
cause. Explicitly record deadline refusal with active-check count zero rather
than calling that successful drain. Store a current stage before potentially
throwing operations so exception diagnostics retain the last attempted boundary.

Limit the observation to the committed quit attempt: ordinary configuration
`closeSession()` calls must not contaminate it. Activate/reset stage observation
at the worker's final shutdown boundary. Independently, the outer worker wrapper
must finalize a distinct `PreFinalWorkerExit` when `run()` escapes before that
boundary, including an exception from operation-error delivery while queued
commands are draining. That classification is not attributed to a drain stage
that never ran. Initialize the observation before worker entry, retain final-
boundary flags if that boundary was entered, and freeze before the existing
thread-exit receipt. Counts and timing are observations, not new policy inputs.

The owner reads only after the ready future and physical worker join. Preserve
the public `QuitRetirementResult` shape, `component == "engine"`, cached terminal
status and boolean `shutdownChecked()` contract. Cache the authoritative result
before delivering a diagnostic. Produce at most one sanitized, localized summary
per refused attempt through the existing `diagnostic` signal. Catch formatting/
direct-delivery exceptions; lost telemetry must not change the stored result.
Queued receiver exceptions cannot be caught by the emitter. Require
receiver-side containment for any owned queued diagnostic dispatch/receiver:
its callback catches before returning to Qt's event loop. Distinguish safe
enqueue/formatting failures from exceptions raised later by a receiver. The
engine does not promise to contain arbitrary external queued subscribers;
those must obey Qt's no-escape callback contract. Tests must separately cover
direct emission and owned queued receiver containment, not infer one from the
other. Audit the existing engine/runtime/main-window diagnostic chain before
choosing the smallest owned callback boundary to change.
No polling caller may read the live observation while status is Pending.

## Invariants

- Keep both three-second drain budgets and every existing comparison/order.
- Keep cancellation, journal/state atomic commits, resource destruction,
  thread-exit receipts, timeout refusal, explicit retry and no late update handoff.
- No owner-thread filesystem access, session destruction or waiting for a worker
  that has not issued its exit receipt; retain existing nonblocking retirement.
- Add no synchronous per-poll logging, allocations or timing-driven policy.
- Keep all original tests, helper durations, assertion thresholds and negative
  controls unchanged. Add tests, never replace original failure evidence.
- No installation, live profile/service/VM/network changes, build-number
  allocation, signing, publication or production feed activation in this scope.

## Acceptance

First prove missing stage detail with focused failing tests, then add diagnostics
without changing the refused/ready/pending outcomes. Real worker tests must cover
state-commit failure, checkpoint rejection and held-worker retirement. A test-only
scripted clock boundary may cover precise deadline classification but must remain
explicitly separate from real disk or original-failure evidence. Compare original
assertions/limits and sanitized diagnostic delivery in both CPU consumer builds.

Independently review ownership, exception/delivery safety, stale observations,
repeat collection, privacy and no-late-install behavior. Preserve all inherited
repairs and prior evidence. The original Intel refusal remains held until an
actual failed run has stage evidence and a causal regression/repair is approved;
diagnostic success alone does not satisfy that gate.

## Approved Implementation Readback

The focused design was approved and implemented locally, without causal repair
or release acceptance. The worker records fixed stage flags and samples counts
and elapsed time once per distinct failure stage; duplicate alerts do not
rescan the job table. The owner caches the checked receipt before one best-effort
summary after physical join. Runtime delivery contains its owned callback's
exceptions, forwards the engine collection after fencing, and checks that a
reentrant callback has not changed the dispatched retirement phase before
advancing it. Pending proxy ownership cannot be bypassed by the new summary.

Both-CPU affected consumers pass the new diagnostics and unchanged retirement
controls, with exact RED/GREEN evidence for the two independent review findings.
Original tests, three native three-second windows and checked outcomes are
unchanged. This is private affected-TU/Rosetta QA, not a full app or native
minimum-OS acceptance. Actual native failed-resume-alert producer coverage and
historical Intel causal evidence remain missing. See the implementation plan's
local checkpoint for exact counts, excluded exploratory receipts and open gates.

## Native Producer Coverage Readback

The native failed-resume-alert gap recorded above now has controlled real
libtorrent 2.1.1 producer coverage. A test-only native session plugin retains an
opaque removed torrent until its abort callback, allowing the unchanged Engine
save flags to receive actual `torrent_removed` failed-resume alerts. The fixture
does not inspect native object layout, fabricate alerts or modify product hooks.
It proves this removed-object branch, not every rejection variant, an application
user-path race or the cause of the historical Intel shutdown refusal.

The appended case checks prior encoded resume/generation and payload preservation,
physical native object/session release, sticky Refused status and exactly one
sanitized cached rejection summary, without reporting drain deadlines. Both-CPU
baseline and private stage-omission controls fail only their exact expected
diagnostic assertions; current candidate GREEN passes. Fresh backend controls and
unchanged hash-pinned Qt consumers pass 852 assertions total, zero errors/failures/
skips. Independent fixture review found no actionable issues; GREEN readback was
performed separately. Original tests, production bytes and prior sealed evidence
are preserved. No budgets, checked outcomes or quit/update policies changed.

This closes only controlled native-producer coverage. The affected-TU/Rosetta
matrix is not a complete current app build or native/minimum-OS acceptance.
Task 1 final integration remains held by inherited NOT_ACCEPTED Task 5 QA;
source is uncommitted. Historical Intel causal capture and all broader provenance,
translation, review/build/signing/update/install/publication gates remain open.
See the plan's latest checkpoint for counts and scope. Nothing installed or published.
