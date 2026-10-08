# AntiSpam Per-File Quit Recovery

Status: written specification approved on 2026-10-07 after design-only commit
`6d066c82a`. The implementation plan awaits maintainer review; product code is
not changed by this document. Source inspected at
`9b88fab066fda1a1f19dc34bac199911ea3b0136`.
The seven existing local Torrent retirement/test changes remain outside this
repair's scope and must be preserved.

## Intent And Current Evidence

After a partial AntiSpam save failure, an explicit Retry Save and Quit must write
only files without successful commit receipts. Already saved files must not be
reopened, truncated or replaced. Quit remains refused until all required files
have checked completion and the current quit request is within its deadline.

Current `AntiSpam::quitSaveOperation()` captures four paths and byte arrays and
returns one boolean for the entire batch. It attempts all four files, even after
an earlier failure, but loses individual successes when the aggregate is false.
A subsequent operation therefore physically writes the successful files again.
This is confirmed by source inspection, not a new runtime reproduction.

`ApplicationQuitCoordinator::AuxiliaryWrite` currently owns a thread and a future
with SearchBlacklist and AntiSpam owner-level booleans. Its future is published
at thread exit. The GUI consumes it, validates retained owners and updates their
cached completion. `ApplicationQuitSession` retains timed-out work and requires
an explicit request before polling it again. Preserve those lifetime and
admission boundaries rather than introducing a second persistence subsystem.

The earlier auxiliary stall repair explicitly excluded per-file AntiSpam
receipts. Its passing tests are not evidence that this new contract works.

## Alternatives

1. Retain the current batch boolean. It is simple but cannot prevent successful
   files being rewritten after a partial failure; it does not meet the intent.
2. Share mutable completion bits between AntiSpam and its worker. This requires
   additional synchronization and makes owner destruction and simultaneous
   synchronous calls harder to reason about. Not selected.
3. Retain GUI-owned frozen intent and committed-file bits, copy them into each
   owned job and return updated bits through its thread-exit future. Selected:
   the worker mutates only its private values; GUI state changes only on checked
   receipt collection.

## Scope And Compatibility

The four files remain `antispam`, `blacklist`, `graylist` and `whitelist`, in that
order, under the captured user configuration root. Preserve their existing UTF-8
serialization, newlines, key uppercasing, empty-file handling and `QSaveFile`
atomic replacement policy. Do not change ordinary settings/list saves or load
behavior. This is not a four-file atomic transaction: successful commits remain
successful when another file fails.

Keep public `freezeForQuitChecked()` and `saveForQuitChecked()` behavior and
coordinator/session public signatures. Changes to the private AntiSpam operation
and auxiliary receipt interfaces are allowed. The synchronous save still returns
one boolean, now derived from four cached file receipts. SearchBlacklist keeps
its existing one-file receipt semantics.

Do not replay successful SearchBlacklist, GUI settings, core checkpoint,
layout, Torrent checkpoint, sharing or proxy retirement during recovery. Do not
change timeout budgets, polling intervals, update-consent rules or quit prompt
wording. Additional user-facing per-file error text is outside this repair.

## Frozen Intent And Ownership

On the GUI owner, close mutation admission before serializing the first complete
snapshot. Capture the configuration root, all four paths and all four byte arrays
as one value-owned snapshot. Retain it for that AntiSpam owner's quit lifetime;
retries must not resolve paths again, reread disk or serialize live lists again.
Keep the existing ordering that freezes AntiSpam only after GUI producer fencing;
do not serialize while hub callbacks can still mutate its rules.

Snapshot construction must publish all four entries or none. A capture failure
refuses quit and cannot start a partial job. This design grants per-file retry
only for a complete frozen snapshot; it does not add recovery from an incomplete
capture. Retain the fenced owner rather than rebuilding intent from a new owner.

AntiSpam retains a fixed four-entry committed mask, initially false. This mask is
GUI-owned. The coordinator retains the physical job and its future, not the GUI
object. A submitted operation contains copies of frozen paths/bytes and the mask,
plus a value token identifying its snapshot and attempt. It has no QObject,
QtContext, MainWindow or live list references. Destroying or replacing AntiSpam
while a job is pending cannot invalidate the job's values.

Allow at most one outstanding job for an AntiSpam owner. Mark its GUI-side
in-flight admission before submission. While it is outstanding, a direct
`saveForQuitChecked()` refuses without writing or reading worker progress.
Coordinator polls retain the same job; they must not launch a duplicate writer.
Release admission after checked receipt collection, or after a failed launch
that provably started no worker. Worker completion alone cannot release GUI-side
admission through a callback to AntiSpam.

## Per-File Write And Receipt Contract

Each job starts with a private copy of the previously committed mask. Skip every
true entry without opening its path. Attempt every remaining entry once, even if
an earlier entry fails. A file becomes committed only when open, complete write
and `QSaveFile::commit()` all succeed. Destroy the per-file writer before leaving
that attempt. Keep prior true entries true.

Handle ordinary file failures and exceptions within each attempt, leaving that
entry false and continuing to later entries. The resulting receipt contains the
snapshot/attempt token and the entire updated mask, including prior successes.
Do not discard earlier successes because a later file failed. Return the receipt
at thread exit, after physical thread-local teardown, using the existing owned
future boundary. Do not detach, terminate or add an unowned asynchronous worker.

On the GUI, consume a ready future, retire its physically finished worker and
validate the retained AntiSpam owner and snapshot/attempt token before applying
the receipt. Owner replacement, destruction, a foreign token or a regressing
mask refuses quit and cannot authorize exit or install. Do not adopt completion
into a replacement owner. Apply valid bits even if another auxiliary owner failed.

Only an all-true mask sets `quitSaved` and contributes Ready. A valid partial mask
contributes Refused but remains available for explicit checkpoint recovery.
Repeated synchronous saves after all four commits return true without I/O.

A worker launch failure retains the complete snapshot and prior bits and may be
retried explicitly under existing checkpoint eligibility. Failure to obtain a
trustworthy receipt after a worker ran is different: partial physical commits
are then unknown. Refuse without inferring success from disk, replaying uncertain
entries or granting per-file retry. Such receipt-integrity failure is terminal
for this quit session; retain owners and job data safely. Previously checked bits
remain retained, but they do not establish completion of the missing receipt.

## Synchronous And Asynchronous Integration

Use the same value-only batch writer and receipt validator for both paths.
`saveForQuitChecked()` verifies GUI ownership, obtains the retained snapshot and
prior mask, runs the batch synchronously when no job is outstanding, validates
its result and records the mask. Its legacy synchronous nature is unchanged.

The asynchronous coordinator combines the unchanged SearchBlacklist result with
the detailed AntiSpam receipt. All GUI-affine snapshot capture, pending admission
and receipt acceptance run on the GUI owner; the worker only performs value-owned
file I/O. Join a normal job only after its thread-exit future is ready. Existing
forced coordinator destruction may still join outstanding work; this repair
does not promise nonblocking forced teardown.

Preserve retained-owner, foreign-thread, modal/popup and reentrant-call checks.
Distinguish a valid retryable file failure from capture or receipt-integrity
failure when computing checkpoint retry eligibility. Do not let a generic false
batch boolean accidentally make the latter failures retryable.

## Explicit Retry, Deadlines And Updates

A retry after a valid partial failure submits only the still-false entries from
the same frozen snapshot. Multiple partial failures monotonically accumulate
checked commits. No disk inspection is used to reconstruct a mask.

If a job is pending when the current request expires, retain it and refuse that
request. Late completion must not queue exit, install an update or automatically
start a replacement job. The next explicit request first collects the retained
job's result. It must not run a second writer while that job remains pending.
If collection produces another valid partial failure, surface refusal; a later
explicit retry submits the remaining entries. Cached completion never bypasses
the current request's deadline checks.

After any retryable auxiliary save failure, preserve permanent updater recovery
disarm. A previously Ready, consented update must not install during successful
recovery. This repair does not broaden the existing policy for timeout-only quit
attempts that have not produced a retryable auxiliary failure.

## Verification And Acceptance

Use disposable profiles and retained private evidence. Establish a behavioral
RED result against the current batch-level behavior before retaining production
changes. Assert the exact expected failure set; unrelated failures, skips or
changed deadlines do not qualify. Use real file-open/write/commit failure paths
where possible and existing scoped syscall/worker probes for held I/O and launch
failure. No test hooks belong in release binaries.

Required coverage:

- Parameterize failure of each of the four files, including first and last;
  ensure later writable files are still committed after an earlier failure.
- Mark successful files after collecting a partial receipt and verify explicit
  retry leaves their bytes and filesystem identity unchanged. Probe physical
  writes where needed so equal rewritten bytes cannot hide a replay.
- Fail multiple files across successive retries; verify checked bits never
  regress and only remaining entries receive physical writes.
- Verify exact frozen serialization for keys, Unicode, empty lists and paths;
  reject late GUI mutations and configuration-root changes as new intent.
- Cover direct synchronous saves, asynchronous coordinator saves and attempts
  to call the synchronous API while its asynchronous job is outstanding.
- Hold a remaining write and thread-exit teardown across an expired request;
  require GUI heartbeat progress, no duplicate writer, no late exit/handoff and
  successful explicit polling/retry without replay of committed files.
- Destroy or replace the AntiSpam owner while pending and after partial failure;
  finish the value-owned job safely but refuse receipt adoption and recovery.
- Exercise launch failure and invalid/missing receipt handling without replaying
  primary checkpoints or successful SearchBlacklist/AntiSpam files.
- Combine a partial AntiSpam failure with a Ready-consented fake update; require
  permanent recovery disarm, no installer calls and no late handoff.
- Retain foreign-thread, modal/popup, repeated-poll and ordinary success cases.

Fresh ARM and Intel/Rosetta consumers must run focused recovery, existing hidden
auxiliary write-retirement probes and applicable regular Qt/GUI suites. Capture
source/dependency identities, exact selectors, seeds, assertion inventories and
isolated test routing. Do not alter or relabel earlier accepted binaries or QA
receipts. A bounded independent review must check ownership, admission and
exception paths before this repair is called verified.

Host ARM and Rosetta results do not establish native Intel/minimum-OS, installed
app, full application rebuild or signed updater acceptance. Historical bind,
latency and recovery causes remain unwaived. Final integration, localization,
provenance, native/live QA, build/sign/notarize/install and publication remain
separate gates; production signed feeds remain last.

## Next Gate

Review and approve `docs/superpowers/plans/2026-10-07-antispam-per-file-quit-recovery.md`
before implementation. Preserve the maintainer's existing native/inline execution
preference when the plan is approved. This specification authorizes no installation,
profile mutation, production credentials, remote service changes, publication or
feed activation.
