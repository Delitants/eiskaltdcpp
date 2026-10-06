# Core Persistence Worker Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement inline.

**Goal:** Remove core checkpoint writes from the GUI quit path without moving
thread-affine lifecycle barriers or granting installation/exit consent early.

**Architecture:** Preserve synchronous `prepareShutdownChecked`. Add opt-in
`prepareShutdownAsyncChecked`, polled only by the startup owner. After existing
barriers, an owned thread writes quiescent non-Settings snapshots in the original
order. `std::promise::set_value_at_thread_exit` makes its completion receipt ready
only after thread-local teardown. The owner consumes that receipt and joins, then
freezes Settings XML with Save listeners on the owner thread. A second owned
thread writes only those frozen bytes. The context stays retained while Pending
or Failed. No worker is detached. First failure remains sticky; independent
snapshots and Settings are still attempted. MainWindow's existing admission fence
must keep all GUI/manual/Torrent/updater inputs closed throughout this interval.
Auxiliary rule mutations freeze after successful GUI retirement and before the
first asynchronous core receipt, without moving their physical writes earlier.
Thread creation failure is terminal Failed, not a cached Pending result.
The frozen Settings writer must flush the native file before close/replacement,
preserving the previous snapshot when that flush fails.

**Compatibility:** Legacy synchronous callers keep their results and ordering.
The coordinator defaults to synchronous behavior; ApplicationQuitSession opts in.
Existing worker timeout/refusal/explicit retry handles core Pending as well.
This does not prove a hard bound for lifecycle joins, frozen-XML serialization,
GUI auxiliary snapshots, or every possible filesystem operation.

**Scope:** DCContext owns threads/receipts; SettingsManager separates frozen XML
from its existing checked writer; coordinator/session integrate Pending. Core and
real-GUI tests cover owner affinity, retention, failure latches and actual writes.
Public interposers remain opt-in test-only code, never application dependencies.

1. Write real held settings-write and thread-local-exit GUI tests. Reproduce the
   current baseline failures before changing core behavior; retain exact evidence.
2. Add the frozen Settings serialization/writer split and core asynchronous path.
   Keep final Settings Save callbacks owner-bound after earlier snapshot writes.
3. Integrate Pending into the quit coordinator/session. Never auto-exit after a
   timed-out request; require explicit retry after physical completion.
4. Add ordinary core tests for caller admission, sticky failure, ordering and
   synchronous compatibility. Run fresh affected/four-target builds at j2 and
   focused/full serial suites with matching ARM and Intel/Rosetta providers.
   Include launch failure in each worker phase, held Queue/Settings/native flush,
   late auxiliary mutation and thread-local-exit probes. Existing native flush
   refusal and replacement tests remain mandatory; do not weaken them for GREEN.
   Hold GUI fixture workers until explicit post-assertion release, with a separate
   deadlock watchdog. Preserve the close/heartbeat budgets under delayed scheduling.
5. Preserve profile, app, providers, old negatives and accepted candidates. Record
   exact limitations and commit only reviewed public files; no push/install/feed
   activation. Continue unrelated approved release work after local verification.

The user requested continuation of remaining tasks without intermediate approval
pauses. This plan is a scoped repair of the approved retained-owner normal-quit
contract, not approval to change live profiles, bypass trust or activate feeds.
