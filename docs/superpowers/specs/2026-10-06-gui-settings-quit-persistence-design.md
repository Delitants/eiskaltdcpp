# GUI Settings Persistence During Checked Quit

Status: written design approved by the maintainer on 2026-10-06 after checkpoint
`bcf1c1390`. Native/inline implementation and local settings-specific checks are
recorded at source checkpoint `af796efbf` and fixture checkpoint `612ae2e3c`.
Final acceptance stays open after retained recovery and fresh thread-exit
responsiveness failures.
This repair extends
the approved built-in updater quit contract; it does not authorize installation,
signing, publication, profile changes or production feed activation.

## Intent And Evidence

Quit preparation must save the user's final GUI settings without blocking the
event loop on physical settings I/O. An expired quit attempt must not reach
update handoff or exit when a delayed settings save completes. Settings format,
frozen values and existing fail-closed error behavior must be preserved.

At source checkpoint `0143aad17`, the first settings sync runs synchronously in
`MainWindow::stopWorkersForQuit()`. A later coordinator sync is not the only
write boundary. Disposable ARM and Intel/Rosetta fixtures hold the actual write
or flush for 350ms. Requests with a 100ms budget return after 381-406ms, without
GUI heartbeat progress. Ordinary fixtures then exit; staged-update fixtures
reach the fake install backend with valid consent and exit after its matching
fake completion. These are controlled source-level reproductions, not real
installations or an explanation for earlier Torrent or recovery failures.

One adjacent ARM recovery run also fails prompt visibility and retry completion;
Intel and an unchanged accepted ARM binary pass the same selector, with the ARM
comparison using the same seed. This separate intermittent failure remains open.
Its wait stays at 5000ms; do not lengthen it or change assertions to obtain green.

## Alternatives

1. Keep synchronous settings I/O and add only a post-call deadline check. This
   prevents some late commitment but leaves the GUI stalled; it is insufficient.
2. Create a second same-file settings object or serialize a replacement INI in a
   worker. This risks shared-cache blocking, deferred writes from the original
   object, changed merge/type behavior or loss of sticky errors. Not selected.
3. Transfer the frozen original storage object to an owned settings-only worker,
   serve retained GUI reads from a value snapshot, and require checked physical
   completion. Recommended: retain Qt's writer and avoid a competing GUI writer.

This is an ownership change, not permission to call a live GUI-affine QObject
from an arbitrary thread. `WulforSettings`, windows, contexts and widgets stay
on the GUI thread. Only its private, parentless, frozen storage transfers.

## Storage Ownership And Frozen Reads

Before quit, keep existing settings behavior. Replace the private by-value
`QSettings` storage with explicitly owned parentless storage; do not change the
public getter/setter interfaces, file path, `IniFormat` or atomic-sync policy.

After GUI producers and layout snapshots are retired, freeze exactly once:
capture the readable key/value state and reject every subsequent mutation.
Quiesce queued storage `UpdateRequest` events before capture allocation so a
refused capture or transfer cannot later auto-sync on the GUI. Terminal refusal
also fences explicit save/load/theme/mutation while retaining original intent.
Snapshot capture is GUI-owned and must finish before storage transfer. Getters
then use only the immutable snapshot, including while a physical write is held.
They must preserve default values, invalid variants, conversions, Unicode keys
and Qt's key-normalization/platform case behavior. Legacy XML `hasKey()` behavior
is unchanged. Audit direct storage access in `load()`, `loadTheme()` and other
helpers, not only the four ordinary setter methods.

The job is owned by `WulforSettings` and owns the storage, thread and receipt.
Create all owning state before launching the thread. On the GUI owner, verify
parentless storage and detach its affinity to `nullptr`; no GUI settings event
may flush it after transfer. The worker acquires that affinity-free storage on
its own current Qt thread, performs checked sync, records the original status,
and destroys the storage on that worker. It runs no general Qt event loop and
does not pump application callbacks. Do not transfer storage back to the GUI
while a deferred write or physical teardown could remain.

Qt documents that `sync()` is also invoked by the event loop and destructor,
and that reentrancy permits distinct objects, not unsynchronized calls on one
object. Detaching to `nullptr` stops object event processing and allows the
destination thread to acquire an affinity-free object. These APIs are the
basis for this proposal, not execution proof. Verify the exact deployed Qt
versions and both architectures before retaining the implementation.
[QSettings threading](https://doc.qt.io/qt-6/qsettings.html#accessing-settings-from-multiple-threads-or-processes-simultaneously),
[automatic sync](https://doc.qt.io/qt-6/qsettings.html#sync),
[QObject affinity transfer](https://doc.qt.io/qt-6/qobject.html#moveToThread).

## Completion And Error Contract

Use explicit NotStarted, Pending, Ready and Refused results for the checked quit
save. Start only one job. Repeated polls never launch another writer or recapture
settings. A successful receipt requires `sync()` returning with `NoError`,
storage destruction completed and actual thread-local teardown completed.
Publish the owned future receipt at thread exit, as in the existing persistence
workers; poll it without blocking and join only after physical readiness.

Never detach or terminate the thread. The application retains the settings owner
and its job while Pending or Refused. Forced destruction may need to join; this
does not promise a hard bound for crash/forced teardown or serialization. Worker
code owns storage/job data only, never dereferences a GUI/context owner or
updates GUI state. Destruction order must keep that job data alive through join.

Allocation, affinity validation, launch, sync or receipt failures refuse quit
without granting install consent. A failed thread constructor must not destroy
dirty storage on the GUI as a temporary lambda side effect: the already-owned
job retains it, affinity-free, with the frozen snapshot available. Do not
automatically retry a failed launch or reset an error by constructing a fresh
settings store. This change does not introduce recovery from a terminal GUI
settings failure; it preserves existing fail-closed behavior.

If the worker cannot acquire the required affinity, it must not call settings
methods or destroy storage on the wrong thread. Retain the storage in the owned
job, publish refusal after thread exit and keep the application alive. Validate
affinity by postcondition on Qt versions whose `moveToThread()` returns `void`.

Sticky errors in the original store remain refusal even if destructor flushing
subsequently succeeds. The final coordinator settings check consumes the same
frozen result; it must not reopen storage, reread disk or issue another sync.
Non-quit `saveChecked()` retains its existing synchronous behavior. Legacy
synchronous coordinator callers remain supported: their first frozen save may
run synchronously, but subsequent quit checks consume its cached receipt.

## GUI Retirement And Session Deadline

Split primary GUI retirement into cached producer/layout/Torrent retirement and
settings persistence. Once snapshots are captured, later settings polls must
not replay layout setters, shutdown, Torrent saving or worker retirement. While
settings are Pending, expose that pending state through the existing coordinator
and session poll path, keeping event delivery outside retained GUI roots live.
An asynchronous session explicitly selects asynchronous settings persistence;
direct legacy callers retain their default synchronous contract.
Once GUI producer and snapshot retirement is complete, core preparation must
close admission even if physical settings persistence remains Pending. Preserve
the updater fence before core preparation and do not admit auxiliary writes,
ordinary exit or update commitment until all required receipts are Ready.

Check elapsed time again after a Ready preparation result and immediately before
calling `normalQuitCommitted()`, even if the first call never returned Pending.
If the attempt expired, refuse it and require a new explicit quit request.
Worker completion alone cannot resume the attempt, initiate handoff or emit exit.
That explicit request consumes cached settings completion and continues remaining
retirement work without rewriting successful settings bytes.

A fresh request may use still-valid update consent under existing controller
policy; revalidate it normally. Timeout alone does not permanently revoke a
previously consented update. Existing auxiliary-failure recovery still permanently
disarms installation. A terminal settings error never authorizes an update or
ordinary exit, regardless of whether an archive is Ready.

## Scope And Verification

Implementation is limited to settings storage/read freeze, primary GUI retirement,
coordinator result consumption and session deadline admission, with production
regressions in the existing Qt/GUI tests. No new user preference or translated
message is needed unless the implementation introduces a new displayed label.
Diagnostic settings and Torrent-layout INI files are separate stores and not
silently moved into this repair; inventory same-file consumers before transfer.

Required observable coverage before claiming a repair:

- Actual write and flush holds: timely return, GUI heartbeat and frozen getters.
- Held storage destruction and thread-local exit: no Ready receipt until both
  complete, with owners retained and no late automatic exit or install handoff.
- Deferred settings events queued before freeze: no GUI-side physical flush or
  dirty-data replay after transfer, including when normal GUI events continue.
- Ready, consented update: expired preparation never calls the fake installer;
  explicit retry revalidates consent and proceeds only through matching handoff.
- Late first-call Ready without an earlier Pending: refuse before commitment.
- Launch failure during retained operation: frozen intent remains readable, no
  temporary GUI destructor flush, no detached worker, no installer call and no
  automatic retry. Forced teardown is not this responsiveness guarantee.
- Unwritable/malformed store and pre-existing sticky error: refusal persists;
  no fresh-store reset, partial-success admission or hidden error waiver.
- Explicit retry after timeout: mark already saved bytes to detect physical
  rewrite; ensure core, auxiliary, layout and Torrent receipts are not replayed.
- Reopened values and getter parity for scalar/variant/byte-array/string-list
  values, unknown keys/defaults, empty values, normalized keys and Unicode.
- Foreign callers, owner lifetimes, no-Torrent builds and synchronous legacy
  callers remain fail-closed or retain their existing successful behavior.

Use tests that fail against the unchanged implementation before repairing it.
Keep the controlled syscall holds, exact failure-set verification, original
recovery deadline and failed evidence. Build and run serially in disposable,
external-network-denied profiles on ARM and Intel/Rosetta, with the full relevant
Qt/GUI suites and all applicable layouts after implementation. Report unexpected
failures by name; green reruns do not erase them.

## Review And Remaining Gates

The maintainer approved this written design after checkpoint `bcf1c1390`.
The implemented plan and local execution evidence are summarized in
[`2026-10-06-gui-settings-quit-persistence.md`](../plans/2026-10-06-gui-settings-quit-persistence.md)
at checkpoints `e68f50545`, `09b83854b` and `af796efbf`. This is not authorization
or acceptance for a release.

The one fresh-context review found two Important issues (late core admission
closure and queued auto-sync after pre-transfer refusal), no Critical or Minor
findings. Both have exact RED-to-GREEN regressions from the one fix pass; no
second-review approval is claimed. The first complete matrix has 30 runs, 4,281 executed
case instances and 443,362 passing assertions, with zero failures/errors/skips,
on ARM and Intel/Rosetta using Qt 6.11.1. It includes all three applicable GUI
layouts, held write/flush/teardown, timeout/retry/Ready admission and a separately
executed no-Torrent configuration. Private broken controls detect their exact
seven/four defects; parent and snapshot-allocation refusal checks retain dirty
intent without deferred GUI writes. Protected live state, historical binaries,
providers, bundles, material and failed evidence pass readback checks.

A later post-commit check caught a test observation before physical completion.
A private delayed-fsync control proves Pending bytes become correct at Ready;
both affected fixtures now await the checked receipt before their unchanged byte
assertions. The 100ms budget, 350ms hold and 5000ms waits are unchanged. The
controlled correction passes three cases / 301 assertions. The latest complete
matrix has 4,281 case instances / 443,781 passing assertions and four failures
in the previously retained recovery-button test: hidden prompt and incomplete
retry, on ARM multiline and Intel sidebar. All settings-specific checks pass,
but overall acceptance and plan Task 3 remain open. These failed outputs are
retained, not waived by the earlier green matrix. Protected-state readbacks pass.

The next fresh post-commit Ready-update check also fails timely request and
heartbeat assertions in thread-exit mode 6: 354ms, no heartbeat progress, writer
off the GUI, no exit or install. Bytes and refusal checks still pass. A private
same-seed join-tracing comparison passes without identifying the cause; that
green diagnostic does not waive the failed check. Responsiveness acceptance
therefore remains incomplete within this design, not merely a release gate.

This is isolated source-level acceptance for the bounded repair, not native
Windows/other-Qt/minimum-OS, real-GOST interop, forced/crash teardown or installed
update acceptance. Historical intermittent failures remain open independently.

The retained natural recovery and Torrent/bootstrap/publication/bind/peer-counter
causes remain separate investigations. Localization/native-language quality,
provenance/reproducibility, independent final-candidate review, native Intel
macOS-14 and ARM macOS-26/live/lifecycle/benchmark/soak acceptance, final material
rebuild, numeric production build allocation, signing/notarization, signed-v3
updater acceptance, guarded closed-app installation/readback and authenticated
wiki/prerelease publication/readback remain pending. Signed feeds are last.
