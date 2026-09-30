# Selected-File Torrent Rechecking

Status: written specification approved by the maintainer's subsequent Proceed
on 2026-09-30. Implementation plan awaiting review. Not implemented.

The maintainer requested individual-file rechecking after visible whole-torrent
recheck progress and Finder access. Those UI prerequisites exist in local commit
`e72f7fec0`; they are not installed. On 2026-09-30 the maintainer said to proceed
after the recommendation to prepare an app-bundled engine-extension design.
The subsequent Proceed approved this written design at commit `92e5aa1fe`;
it does not approve an implementation plan that had not yet been written.

## Intent And Scope

Users can select one or more files in one Torrent and verify their existing
payload without scanning every file in the Torrent. Verification updates the
engine's piece state, so missing or corrupt pieces can be downloaded again when
the user's existing activity policy permits it. It must not be a read-only report
disguised as repair, nor a whole-Torrent scan disguised as selected-file checking.

Preserve file priorities, wanted selections, proxy routes, DC sharing exclusions,
and paused/stopped state. Other jobs must continue independently. Original files
used by read-only created-Torrent seeding must never be repaired or written.
The system Homebrew installation, production profile and installed app remain
unchanged during development and isolated acceptance.

## Approach And Alternatives

Use a narrowly scoped, version-pinned libtorrent extension built privately for
Eiskalt. It will expose supported selected-piece verification operations to the
adapter while keeping storage, hashing and piece-state transitions inside the
engine. Eiskalt must not call private internals through `native_handle()`.

Current `torrent/CMakeLists.txt` accepts LibtorrentRasterbar 2.1; the local
dependency is 2.1.1. Its public `force_recheck()` has no file/piece selection
argument. Internal checking entry points alone do not prove a safe selective
implementation. The first implementation milestone must prove the proposed
extension on disposable fixtures before UI enablement or release integration.

Alternatives considered:

- A separate read-only file auditor cannot repair engine availability state and
  does not meet the requested normal-Recheck semantics.
- Editing resume bitfields around a custom hasher would duplicate storage,
  partial-file and v2 hash-tree logic. Reject this as the primary mechanism.
- Temporarily changing priorities, feeding bytes back through `add_piece()`, or
  invoking whole-Torrent `force_recheck()` are not acceptable substitutes.

Maintaining a dependency patch increases build/review cost. Keep it small,
documented and suitable for upstream submission. If the feasibility milestone
cannot satisfy the contracts below, stop and report the limitation; do not
silently reduce scope or ship an unsafe approximation.

## User Experience

- Add `Recheck selected files` to the Files table context menu; support ordinary
  multi-row selection independently of the download checkboxes.
- Enable only with metadata, valid storage, a nonempty supported selection, no
  conflicting operation, no unapplied selection/priority edits, and engine
  capability. Explain unavailable reasons rather than silently doing nothing.
- Before starting, confirm the selected-file count and the affected piece scope.
  For shared v1 boundary pieces, identify neighboring files whose fragments must
  be read; the dialog must not imply those entire files will be scanned.
- Show `Rechecking selected files... N%`, a Cancel action and per-file outcomes
  in the existing Torrent details area using native controls and translations.
  Keep downloaded-byte totals separate from verification progress.
- Outcomes distinguish verified, missing/corrupt, could not verify, and canceled.
  A file whose boundary piece fails is not described as independently corrupt
  unless the hash format supplies enough evidence to attribute the failure.
- Completion reports checked files/pieces and affected pieces needing download.
  No automatic file deletion, priority change, or switch from stopped to running.
- Existing whole-Torrent Recheck remains a separate action.

## Scope Planning And Storage

Build an immutable operation plan from validated metadata, the current storage
generation and selected stable file indices. Deduplicate indices, reject invalid
indices, and compute the union of affected pieces without counting overlaps twice.
The plan also identifies all real files touched by those pieces for DC cache
invalidation and user disclosure. Padding is engine-managed; zero-length files
have no payload pieces and require an explicit, non-dividing-by-zero outcome.

Use libtorrent's actual storage mapping, including skipped-file partfiles,
current incomplete/completed location and legitimate mapped offsets. Do not read
guessed final filenames or create missing files/directories during checking.
Normal verification reads no unrelated payload pieces. Reading small metadata or
resume records is not considered scanning unrelated payload.

Validate v1, v2 and hybrid Torrents with the engine's native hash rules. For
hybrids, validate the applicable v1 and v2 information, not just whichever hash
first happens to pass. Missing required authenticated v2 hashes must yield an
explicit unavailable outcome, never fabricated success or an unapproved network
lookup. Preserve existing path/symlink/storage-collision protections; fail closed
on changed storage identity rather than publish a stale result.

## Engine Operation Contract

The extension API and alerts have a capability/version identifier. Proposed
operations are start-selected-check and cancel-selected-check, with an operation
token, immutable piece set, progress and terminal result. Exact C++ signatures
are decided in the implementation plan after upstream source review.

Eiskalt mutations remain owner-thread calls; libtorrent work runs on its own
appropriate executor and disk workers. No hashing, disk drain or joins on the GUI
thread. Progress is coalesced into existing snapshots rather than repainting per
block. Tokens include job/storage generations so late alerts cannot update a
removed, moved, reconfigured or newer checking operation.

Per-job lifecycle:

1. Validate the request and reserve its generation. Reject conflicting moves,
   selection changes, full checks or another selected check.
2. Retract managed DC publication and drain/cancel publication work for this job
   before verification can produce or publish a result.
3. Quiesce this job's peer I/O and drain its outstanding writes. Do not pause the
   entire libtorrent session or disturb other jobs. No payload upload/download
   for this job occurs during verification; do not start trackers or DHT merely
   to perform a stopped-job local check.
4. Persist an atomic pending-check record, then invalidate affected engine
   availability and cached verification assumptions before scheduling reads.
5. Hash only planned pieces using native storage and hashing. Passing pieces
   become valid; failed/missing pieces become unavailable. I/O errors remain
   explicit errors, not unexplained corruption. Do not blame peers for local
   disk corruption discovered by a manual check.
6. Drain reads, persist updated resume state, then clear the pending record.
   Only after successful persistence may the operation be finalized and prior
   permitted activity restored. Honor any newer Pause/Stop command instead.

A stopped job without a live handle requires a local-only checking context. The
feasibility gate must show this neither starts a session route nor advertises
pieces. A paused job may run the explicitly requested disk check while staying
network-paused. Check completion cannot undo stop, pause or sharing exclusions.

For source-read-only jobs, failed pieces remain unavailable for upload; no repair
download or source write is permitted. If the native storage path cannot enforce
that protection, refuse that operation with an explanation.

## Cancellation, Conflicts And Recovery

Cancel stops further scheduling and drains admitted disk work. Keep verified
results already obtained; conservatively mark unchecked affected pieces
unavailable. Explain this consequence in the cancellation UI. Never revive the
old availability bitmap for unchecked data. Preserve prior activity after safe
finalization, except where the user subsequently paused or stopped the job.

Pause changes the post-check activity intent; Stop cancels the check and leaves
the job stopped. Remove waits for this job's verification I/O to drain before
deletion. Route reconfiguration and application shutdown cancel/drain the check
through existing lifecycle ownership. Moves, priority changes and second recheck
requests are rejected while the operation is active, with a visible reason.

The pending record contains an operation token, infohash identity, storage
generation, selected indices and affected-piece set, with bounded parsing. Write
it atomically before any state change that could otherwise restore stale trust.
Bind the record and resume snapshot to a generation so an old asynchronous
save-resume reply cannot overwrite final verification results.

On restart, reconcile an unfinished record before any peers, DC publication or
completion status are enabled. Treat its affected pieces as unverified even if
an older resume snapshot claims them. Leave the interrupted job paused with an
explicit choice to recheck those files or resume and redownload unavailable
pieces. Read-only-source jobs offer recheck only, never redownload or repair.
Do not silently trigger a whole-Torrent scan. A malformed record blocks
that job for recovery rather than trusting stale availability. Unrelated jobs
and pieces retain their existing valid state.

## DC++ Sharing And Hash Cache

Current `TorrentRuntime` retracts publication via `storageInvalidating` and
forgets an entire owner's TTH cache via `hashRecheckRequested`. Retain that
whole-job behavior for the existing full Recheck action.

For selected checks, add a validated affected-file invalidation path to
`ShareManager`. Forget TTH records only for real files touched by checked pieces,
including boundary neighbors. Fence in-flight replacements that captured those
records, and persist the resulting cache change. Unrelated valid hash records
must remain reusable, including after restart.

Publication can remain retracted for the whole job during verification because
the current publisher is job-oriented. This does not justify dropping all its
hash-cache records. Republish only through existing completed/wanted/private/
excluded-file rules and revision checks. A valid BitTorrent hash is not a TTH;
affected files still need appropriate DC hash validation before publication.

## Dependency And Packaging Boundary

Pin an immutable upstream source revision/archive checksum, patch checksum and
license notices in the repository's dependency tooling. Resolve the exact base
revision during implementation; do not depend on a moving branch or quietly
patch the system Homebrew library. Build in project-owned staging prefixes.

Feature capability must be checked at configuration/link time and runtime where
applicable. An unpatched system dependency may support ordinary Torrent builds,
but selected rechecking must be visibly unavailable. Release builds advertising
the feature fail packaging if capability is absent or the wrong library is
bundled. Prevent accidental fallback to a Homebrew dylib with a matching name.

Preserve the existing assert/layout ABI matching safeguards. Validate matching
headers, binary, feature identifier, architecture, deployment target and bundled
library install names. Independently test Intel macOS14+ and Apple Silicon
macOS26+ packages. DC-only builds remain independent of this dependency; other
platforms must still build without pretending unsupported capabilities exist.

## Acceptance Gates

- Instrumented disk fixtures prove the exact payload-piece read set, including
  shared boundaries and partfiles; unrelated pieces are never read.
- v1/v2/hybrid fixtures cover correct data, same-size corruption, missing data,
  missing authenticated hashes, padding, zero-length and multi-selected files.
- Loopback peers prove corrected availability drives redownload only when
  allowed, and prior seed state is revoked after corruption without peer blame.
- Paused, stopped, active and read-only-source jobs retain their safety rules;
  priorities and wanted selections are unchanged; other jobs keep transferring.
- Cancellation and crashes at every persistence boundary cannot restore stale
  trust. Exercise delayed resume alerts, stop/remove/configure/shutdown, storage
  changes, permission denial and disk-full conditions.
- Managed-sharing fixtures prove temporary retraction, selective hash-cache
  invalidation, unrelated cache reuse and correct private/excluded behavior.
- Native Qt QA checks selection versus wanted checkboxes, menu availability,
  progress/cancel, sorting/selection persistence, light/dark styles and strings.
- Existing backend/core/Qt suites, DC-only build and both CPU package gates pass;
  source tests are not substitutes for native/minimum-OS/live acceptance.

## Delivery And Remaining Work

This specification must be reviewed before a written implementation plan is
prepared. The first plan milestone is engine feasibility; UI rollout follows
only after storage/state/recovery contracts are demonstrated. Native inline
execution is the established preference, subject to plan review.

No installation, push, release publication, profile change or updater activation
is part of writing this document. Separate pending work remains: updater core
shutdown/installer handoff and recovery; proxy DNS decision and routed acceptance;
runtime/backend integration; updater UI/localization; signing, numeric builds
and split-CPU packages; final review, installation and prerelease/wiki publication.

## Reference Points

- Local: `torrent/TorrentEngine.{h,cpp}`, `torrent/TorrentTypes.h`,
  `torrent/CMakeLists.txt`, `eiskaltdcpp-qt/src/TorrentRuntime.cpp`,
  `eiskaltdcpp-qt/src/TorrentWindow.cpp`, `dcpp/ShareManager.{h,cpp}`.
- Upstream public API: https://libtorrent.org/reference-Torrent_Handle.html
- Resume parameter semantics: https://www.libtorrent.org/reference-Add_Torrent.html
- Upstream implementation research: https://github.com/arvidn/libtorrent

Self-review: scope, activity preservation, storage boundaries, selective cache
invalidation, cancellation/recovery, dependency isolation and acceptance gates
are specified. Exact patch feasibility is deliberately an implementation gate,
not a claim that the extension already exists or has passed tests.
