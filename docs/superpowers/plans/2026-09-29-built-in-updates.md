# Built-In Updates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Native/inline execution was already selected; do not dispatch implementer agents. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship configurable, default-on update checks/notifications and opt-in, verified macOS installation on normal quit in both v3 prerelease flavors.

**Architecture:** A shared Qt controller owns persisted policy and exposes one platform-backend interface. Sparkle handles macOS verification/replacement, while a capability-scoped loopback bridge routes every remote fetch through Eiskalt's strict proxy-aware transport. Prove that public Sparkle APIs can preserve this boundary before integrating settings or shutdown behavior.

**Tech Stack:** C++17, Qt6, existing DCContext/ProxyRoute/BufferedSocket/OpenSSL, Objective-C++, Sparkle 2.10.0, Catch2, Python3 release tooling, CMake.

**Spec:** `docs/superpowers/specs/2026-09-29-built-in-updates-design.md`, committed as `8740d09d`, approved by the maintainer after that commit.

**Execution amendments approved 2026-09-29:** numeric internal build numbers
replace display-version comparison; display versions/filenames stay unchanged.
Discovery is information-only. Download and Ed25519 verification use app-owned
staging, and Sparkle archive preparation starts only after committed normal quit.
These supersede eager-preparation assumptions in the original task details.

## Global Constraints

- Intel x86_64/macOS14+ and Apple Silicon arm64/macOS26+ are separate thin bundles. Keep their explicit filenames from `macos/RELEASE.md`.
- Automatically check for updates: ON. Notify when available: ON. Automatically download/install on normal quit: OFF. Preserve explicit saved OFF choices.
- Checks occur after startup settles when due, at most once per 24 hours automatically. Manual checks work when automatic checking is disabled.
- Stable channel for stable builds; stable plus prereleases for prerelease builds. No implicit downgrades or automatic architecture switching under Rosetta.
- No forced quit/restart, crash-triggered installation, installation on Hide, or settings/profile/data changes inside app replacement.
- Strict origin HTTPS certificate/hostname verification regardless of hub TLS preferences. Use the immutable global outgoing proxy route, not the Torrent override or public-hub-list HTTP proxy. No direct retry after proxy failure.
- Signed feeds and archives; verify archives before extraction. No delta updates, remote release-note rendering, system profiling or private installer APIs.
- Keep signing keys local, separate from GOST trust, and out of Git/logs/artifacts. Do not create a production signing identity during fixture work.
- Keep DC-only and non-macOS builds working without Sparkle. Self-installation on Windows/Linux is outside this prerelease and must not be advertised.
- Preserve the installed app, private profiles, server configuration, unrelated untracked files and existing shared dependency prefixes. Tests use disposable copies.
- One build owner, two build workers (`-j2`), serial application tests; no duplicate Intel dependency builds. Record all failures and distinguish Rosetta from native minimum-OS acceptance.
- Product implementation starts only after review of this plan. No updater code, framework install or signing identity was created while writing it.

## Review Focus

1. A proxy profile changes during download: revoke the old route and pending install without sending any bytes directly (Tasks 2, 3, 5).
2. A clock jumps or the machine wakes after sleep: no duplicate concurrent checks or burst retries (Task 4).
3. Preferences change or quit is canceled after a download completes: old consent must not trigger installation (Tasks 4, 5).
4. A restored cache contains a formerly valid but now stale, wrong-channel or mismatched archive: revalidate identity/version/consent before handoff (Tasks 3, 5).
5. Signed metadata contains active HTML, redirects or oversized fields: no remote rendering, unbounded memory use or credential forwarding (Tasks 2, 3, 6).

## Files And Responsibilities

- `eiskaltdcpp-qt/src/updates/`: focused policy/types, transport, loopback bridge, backend interface, controller, macOS adapter, unsupported-platform adapter and settings widget; do not add these responsibilities to the existing large main-window file.
- `dcpp/HttpConnection.{h,cpp}`: opt-in strict request policy only; preserve legacy callers. Existing `ProxyRoute.h` supplies generation/revocation snapshots.
- `eiskaltdcpp-qt/src/WulforSettings.{h,cpp}`, `Settings.{h,cpp}`, `MainWindow.{h,cpp}`: persistence, page/menu integration and the existing shutdown handoff.
- `cmake/MacUpdater.cmake`, `eiskaltdcpp-qt/Info.plist.in`, app/test `CMakeLists.txt`: optional framework wiring, helper signing and metadata invalidation through existing `MacBundleMetadata.cmake`.
- `tests/qt/test_update*.cpp`, `tests/macos/updater/`: policy/transport/UI tests and real signed old/new app fixtures. No production endpoints/profiles.
- `macos/updater/`, `macos/release.py`, `macos/RELEASE.md`: pinned provenance, offline feed generation/verification, archive checks and publication documentation.

## Execution Setup

Use the existing `codex/v3.0.0-pre.1` checkout after checking its state; do not discard unrelated files. Record task progress under this plan's ignored workspace with the executing-plans helper. Use `build-updater` as a separate app build directory, never an Intel/ARM shared cache. Inspect the existing `CMakeCache.txt` before reusing any build.

Commands below run at the repo root. `B=build-updater` denotes the disposable app build. Configure it through the matching approved macOS preset, with the complete Qt prefix, patched Cocoa hash and local Catch2 source as documented in `macos/RELEASE.md`. The native updater fixture target introduced below is `eiskalt-updater-native-tests` at `$B/tests/macos/updater/`. Core/Qt test binary paths retain their existing names. Use elevated loopback access for socket tests, not relaxed security assertions.

### Task 1: Prove The Pinned Sparkle Public-API Boundary

**Files:** Create `macos/updater/sparkle.lock.json`, `cmake/MacUpdater.cmake`, `tests/macos/updater/{CMakeLists.txt,main.mm,fixtures.py}`; modify `tests/CMakeLists.txt`.

**Interfaces:** This task produces the optional `Sparkle::Sparkle` imported target and the native test target. `fixtures.py create --root PATH` generates disposable signing keys, signed appcast/full ZIP and old/new app copies; `fixtures.py verify --root PATH` verifies fixture integrity. Nothing uses the production bundle ID/profile or generates a production key.

- [x] Record the release pin: tag `2.10.0`, commit `eef1a539a373c1f1a320624b1130fc5de7b2e100`, archive `https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz`, SHA256 `c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c`. Downloaded/verified in isolated QA; matching source/license provenance recorded.
- [x] Add RED tests tagged `[updater-native][bridge-probe]`: original signed bytes, selected archive mapping, tamper rejection and blocked default downloader. The parent downloader has loopback-only networking and proves external socket denial; installer-XPC network evidence remains part of later product acceptance, not a result of this fixture.
- [x] Build/run the standalone native target. Observed RED for missing archive mapping, prerelease ordering, and eager downloads during information-only discovery; logs retained under `Work/qa/updater-20260929/`.
- [x] Implement the fixture delegate with public feed/download hooks, signed-feed controls and no remote notes/profiling. Real comparator tests numeric builds; information-only discovery does not download or prepare installation. Prepared fixtures explicitly cancel and await completion; Dismiss is not cancellation.
- [x] Rerun five fixture modes: valid, bad-feed, bad-archive, unmapped and information-only. Native assertions and original-app readbacks pass; no unsigned feeds, direct fallback or private APIs. Approved design amendments above address the two discovered incompatibilities.
- [x] Commit the pin, optional CMake wiring, passing fixture and approved amendments. No installed app/profile changes or production update behavior.

### Task 2: Strict, Cancelable Update Transport

**Files:** Create `eiskaltdcpp-qt/src/updates/UpdateTransport.{h,cpp}` and `tests/qt/test_updatetransport.cpp`; modify `dcpp/HttpConnection.{h,cpp}`, `tests/qt/CMakeLists.txt` and its existing HTTP tests where appropriate.

**Interfaces:** Define `ResourceKind { Feed, Archive }`; `UpdateTransport(DCContext&, QObject*)`; `quint64 fetch(const QUrl&, ResourceKind, const QString& destination, std::shared_ptr<const dcpp::ProxyRouteSnapshot>)`; `void cancel(quint64)`. Signals: `progress(id, received, total)`, `finished(id, path)`, `failed(id, redactedMessage)`. Destinations are controller-owned private staging files, never server-supplied paths.

Introduce a namespace-level `dcpp::HttpRequestPolicy` and a constructor overload `HttpConnection(DCContext&, const HttpRequestPolicy&, const std::string& userAgent)`. Existing constructors keep legacy behavior. The strict policy supplies HTTPS-only, allowUntrusted=false, exclusion of the public hub-list proxy, a body limit, redirect limit/validator and immutable route snapshot; apply it to every redirect/reconnect. Do not globally change hub TLS defaults.

- [ ] Write `[updates][update-transport]` tests: relaxed hub certificate acceptance does not permit invalid updater certificates/hostnames; origin TLS runs over the selected proxy tunnel; the public-list proxy is ignored; wrong credentials/route revocation have zero direct connections; redirects never carry proxy authorization headers to origins.
- [ ] Add boundary assertions: feed limit 1 MiB, full ZIP limit 512 MiB, response-header limit 64 KiB, maximum five redirects, 15-second connect timeout, 30-second idle timeout and a 30-minute total archive deadline. Fail at limit+1; accept exactly limit. Stream archives to disk with bounded buffering rather than collecting a QByteArray.
- [ ] Run `cmake --build "$B" --target eiskaltdcpp-qt-tests -j2` then `QT_QPA_PLATFORM=offscreen "$B/tests/qt/eiskaltdcpp-qt-tests" '[update-transport]'`. Expected RED for missing strict policy/transport.
- [ ] Implement worker-owned HTTP lifecycle, atomic staging completion and cancellation. Use `ProxyRouteSnapshot::generation/revoked`; never mutate application settings to apply a snapshot. Audit `SSLSocket` SNI/hostname and trust-root selection explicitly; reuse its transport BIO rather than bypassing GOST with the raw file descriptor. Delete incomplete files on failure.
- [ ] Production origin policy: use only `raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/` for feeds and `github.com/Delitants/eiskaltdcpp/releases/download/` for release assets. Asset redirects may use exact hosts `release-assets.githubusercontent.com` and `objects.githubusercontent.com`. HTTPS only, no URL userinfo, literal IPs, fragments or private/link-local destination addresses. Keep destination DNS on the selected route; existing `resolveProxyEndpoint` is only for the proxy endpoint and must not resolve update destinations directly. If a remote resolver cannot prove a required destination constraint, fail closed and treat that route as an unpassed acceptance gate, not as supported proxy updating.
- [ ] Rerun transport and existing HTTP/proxy tests. Expected GREEN, no changed default behavior for other callers. Commit the bounded transport changes.

### Task 3: Integrate The Real Proxy Bridge And Verification Backend

**Files:** Create `updates/UpdateTypes.h`, `updates/UpdateBackend.h`, `updates/UpdateLoopbackBridge.{h,cpp}`, `updates/MacUpdateBackend.mm`, `updates/UnsupportedUpdateBackend.cpp`, `tests/qt/test_updatebridge.cpp`; extend native fixture and app/test CMake wiring. All `updates/` source paths here are under `eiskaltdcpp-qt/src/`.

**Interfaces:** Define `enum class Channel { Stable, Preview }` and `enum class CheckKind { Automatic, Manual }` in `UpdateTypes.h`. `UpdateBackend : QObject` exposes void-returning `check(Channel, CheckKind)`, `download(QString version)`, `cancel()`, `armInstallOnQuit(QString version, bool relaunch)`, `revokeInstallConsent()`. Signals: `available(UpdateOffer)`, `downloadReady(QString version)`, `progress(qint64,qint64)`, `failed(QString)`, `noUpdate()`. `UpdateOffer` contains only verified version/channel, plain-text notes, CPU/minOS, expected size and identity; it exposes no untrusted executable commands.

`UpdateLoopbackBridge(UpdateTransport&, QObject*)` exposes `QUrl beginSession(const QUrl& fixedFeed)`, `QUrl mapArchive(const QUrl& verifiedUrl, qint64 expectedSize)`, `void stop()`. It serves only the current unchanged signed feed and mapped full archive; mapping is backend-owned, not a user-input fetch API.

Amendment: `UpdateOffer` also binds the immutable numeric build ID and archive
digest. `download()` uses Eiskalt-owned staging and OpenSSL Ed25519 verification
against the pinned public key and signature from the verified feed, not Sparkle's
eager archive preparation. Fail closed on missing/invalid signatures. Retain
verified signed feed bytes with the staged archive. `check()` uses information-only
discovery. The normal-quit handoff serves only this revalidated local pair;
it must not fetch remote data or launch an installer before consent commits.

- [ ] RED `[updates][update-bridge]` tests: bind only 127.0.0.1, random 256-bit per-session capability, reject wrong capability/Host/method/path/query, no directory serving/CONNECT, bounded concurrent requests, cancel on route generation change. Cover replay from an earlier session and encoded traversal.
- [ ] RED native `[updater-native][proxy-acceptance]` tests: actual authenticated SOCKS5/GOST relay with direct origin access denied; feed, archive and DNS succeed only via relay. Wrong auth/CA, failed proxy and redirects never use direct origin access. Denied external image/notes requests stay at zero.
- [ ] Implement the bridge using Task 2, then the public adapter proven by Task 1. Require signed-feed status before emitting `available`, verify archive signature before extraction, use `SURequireSignedFeed=YES`, `SUVerifyUpdateBeforeExtraction=YES` and no expiration fallback for bad feed signatures. Validate bundle ID, CPU flavor, minOS, version and full archive selection. Non-Mac backend reports installation unsupported without linking Sparkle.
- [ ] Run the Qt bridge tests and native proxy-acceptance tag serially. Expected GREEN with disposable credentials, destination-denial logs and no skipped proxy cases. Merely getting a proxy handshake does not pass. Commit backend/bridge integration only after this gate.

### Task 4: Persisted Policy And A Single Scheduler

**Files:** Create `updates/UpdatePolicy.{h,cpp}`, `updates/UpdateController.{h,cpp}`, `tests/qt/test_updatepolicy.cpp`, `tests/qt/test_updatecontroller.cpp`; modify `WulforSettings.{h,cpp}` and test CMake.

**Interfaces:** `UpdatePreferences { bool checks=true, notifications=true, autoInstall=false; Channel channel; }`; static `bool UpdatePolicy::mayCheck(const UpdatePreferences&, CheckKind, QDateTime now, QDateTime lastAttempt, bool inFlight)` and `bool mayInstall(const UpdatePreferences&, bool normalQuit, const QString& readyVersion, const QString& manualConsentVersion)`. Invalid `lastAttempt` means never attempted; empty ready/consent versions mean no download/consent. `UpdateController(WulforSettings&, UpdateBackend&, QObject*)` exposes void-returning `start()`, `applyPreferences(UpdatePreferences)`, `checkNow()`, `download()`, `installOnNextQuit()`, `later()`, `skipVersion()`, `cancel()`, `normalQuitCommitted()`, `shutdown()`. Signals carry status and notification requests to UI, not direct widget manipulation from workers.

- [ ] RED tests assert ON/ON/OFF defaults, explicit OFF survives restart, preview-build channel default, stable-build default, manual checks ignore scheduling OFF but never bypass verification, and skipped/current/older/wrong-channel versions produce no actionable notice.
- [ ] Add literal clock cases: attempt at t=0, automatic check at t=86399 denied and t=86400 allowed; a concurrent manual request joins/rejects the existing operation; clock reversal and sleep/wake do not create bursts. Disabling checks cancels automatic activity, while explicit version-bound manual consent remains distinct.
- [ ] Run Qt `[update-policy],[update-controller]` tags, expected RED. Implement one Qt-owned scheduler; disable Sparkle's independent automatic-check/download policies and drive public APIs through the backend/user driver. Persist only in WulforSettings using `updates/*` keys; do not mirror policies into a second user-defaults store. Keep last attempt separate from displayed last success. Use injectable clock/backend for policy tests, real backend tests from Task 3 for trust.
- [ ] Test notification OFF, Later/Skip deduplication, cancellation and error-rate limiting; background failures log at most once per failure category per hour, manual errors remain visible. Revalidate preferences immediately before handoff. Rerun tags plus WulforSettings tests, expected GREEN. Commit.

### Task 5: Safe Installation And Shutdown Handoff

**Files:** Modify `updates/MacUpdateBackend.mm`, `UpdateController.{h,cpp}`, `MainWindow.{h,cpp}`; extend `tests/qt/test_mainwindowtoggle.cpp` and `tests/macos/updater/` fixtures.

**Interfaces:** `normalQuitCommitted()` may arm the backend only after all existing queue/settings/Torrent shutdown steps have completed successfully. `shutdown()` cancels networking and invalidates tokens, without granting install consent. Keep a version-bound consent token, not a global boolean that can authorize a later different download.

Amendment: this boundary starts Sparkle archive preparation for the first time.
Keep the Qt/local bridge event loop alive through handoff, serve only revalidated
staged bytes, and cancel on handoff failure. Do not stage in Sparkle earlier and
attempt to revoke later: native probing showed Dismiss can install at process exit.

Native acceptance checkpoint: a signed ZIP with a matching app filename but a
wrong internal bundle ID reached Sparkle Ready. Independently preflight its
bounded ZIP identity after signature verification and before preparation; preserve
the exclusive immutable stage through handoff. `BundleArchiveVerifier` and the
disposable `install-probe` runner exercise this boundary, not the product window.
The current macOS `_Exit` path skips core teardown. Establish successful queue,
settings and Torrent persistence before wiring the final handoff; do not regard
an accepted close event alone as sufficient. Authorization/disk-full/recovery and
real MainWindow acceptance remain required below.

Persistence checkpoint: checked core/GUI settings and queue saves now expose
failure, with no-delete-first core replacement. Checked torrent engine/runtime
shutdown retains resume/atomic-write failure and repeated-call outcomes. These
are prerequisites, not committed-quit integration: stop core producers and hub
sockets safely before consuming their results, then keep the event loop alive
for the installer handoff. The macOS emergency exit workaround is unchanged.

Hub-stop checkpoint: `Client::stopForShutdown` permanently closes the reconnect
gate, drains/removes its timer listener and detaches the socket without deleting
the UI-owned client. Its caller must own client lifetime and serialize manual
connect/reconnect/deletion; it must not run from a timer/socket callback or under
a ClientManager lock. Socket completion accounting now remains nonzero until
transport/member destruction finishes. A bounded `BufferedSocket::waitShutdown`
overload exposes incomplete drain, but is not a producer fence. Isolated ADC/NMDC,
in-flight callback/timer and delayed transport-destruction cases cover these
primitives. They are not yet wired into the application. Full core producer stop,
client-registry lifetime sequencing, active-transfer and real MainWindow quit
acceptance remain required; do not replace `_Exit` or arm the installer yet.

HTTP-stop checkpoint: `HttpConnection::stopForShutdown` permanently rejects
new requests, waits for admitted legacy callbacks, then detaches the remaining
socket. Callback accounting does not hold a lock across application callbacks
or socket listener barriers. Ordinary `abort` remains reusable. Strict HTTPS
requests are cancelled only; their owner must still join the I/O worker.
`FavoriteManager` and `DynDNS` expose terminal stops that preserve the managers
and prevent late refresh/load/retry requests. Owners must serialize terminal
stop with public request methods and destruction, never call it from a callback,
and still drain asynchronous socket resources afterwards. These are prerequisites,
not a whole-core stop coordinator or application quit integration.

Client-registry checkpoint: `ClientManager::stopForShutdown` closes creation
admission, drains admitted creation/release operations, and pins registered
clients by deferring releases until hub callback barriers finish. Late
`getClient` throws rather than changing existing successful non-null semantics.
Repeat/concurrent stops join the same terminal phase; ordinary client release
afterwards remains available. No registry/lifecycle lock is held across listener
callbacks or hub barriers. The manager's lifetime and manual Client operations
must still be owner-serialized, and stop must not run from a callback or while
holding registry/listener locks. The future coordinator must fence UI, scripts
and search creation first, stop all other producers, then drain socket resources.
This API is not yet activated by MainWindow or DCContext, and is not committed
quit or whole-core quiescence. Preserve `_Exit` and do not arm the installer yet.

Search/share checkpoint: terminal `SearchManager::stopForShutdown` rejects new
search/result/reply work, drains admitted direct callbacks, then joins its UDP
listener and queue even if no listener was opened. Ordinary disconnect retains
the queue for relisten, and the receiver stop flag is atomic. Listener
configuration, terminal stop and destruction remain owner-serialized; do not
stop from a callback or under listener/manager locks.

`ShareManager::stopForShutdown` closes refresh/managed-publication admission,
cooperatively cancels scans and managed hashing, drains admitted producers and
timer/queue/hash callbacks, and joins refresh without holding manager locks.
Existing shares, reusable hash evidence and the Settings Save listener survive
for the final checked persistence checkpoint. Manual mutations/settings reload,
stop and destruction remain owner-serialized; late mutations cannot revive the
manager. File I/O is not forcibly interrupted, so a stalled worker delays quit
rather than granting installation consent. Stop publication/refresh before
destroying their hash, DHT, client and queue dependencies. These APIs are not
yet wired into MainWindow or a non-destructive whole-core coordinator; the
proxy route needed for local handoff must still be preserved.

DHT/bootstrap checkpoint: `DHT::stopForShutdown` closes shared admission for
transport, dispatch, node, search, index and connection producers. It drains the
timer and bootstrap HTTP sources, waits admitted calls, then joins UDP before
closing its socket and clears ephemeral send/bootstrap queues. Routing nodes,
indexes and sub-manager instances remain available for later persistence; the
proxy route is not revoked. Repeated owner-serialized terminal stops are safe;
ordinary stop/start remains reusable. Nullable node creation is checked at all
internal callers, and index scheduling flags are atomic and fenced.
Bootstrap HTTP callbacks are drained and detached; asynchronous BufferedSocket
resource completion still requires the later whole-core resource drain.

Manual configuration/lifecycle, raw manager users, read-only access and
destruction must still be owner-serialized. Do not stop from a producer callback
or under manager/listener locks. Uninterruptible I/O delays the barrier rather
than granting installer consent. The existing DHT save API remains an unchecked
legacy write, not installation authorization. No MainWindow/DCContext updater
coordinator or Sparkle activation is added by this checkpoint. Queue rechecker/
mover and active-transfer barriers, checked final persistence sequencing and actual
committed-normal-quit handoff remain required before removing `_Exit`.

Hash checkpoint: `HashManager::stopForShutdown` closes new hash, rebuild, tree
publication and timer-side-effect admission; cancels queued/current reads,
drains real publication and timer callbacks, then joins the worker. Concurrent
stop callers join the same barrier. Completed cache/tree evidence is retained
for read-only access without invalidating entries or marking new use after stop.
Canceled reads and early EOF are never finalized/published; normal fast/slow
hashing still completes. Partial-work status and buffers are cleared on exit.
This does not save the hash store or revoke the consent-bound proxy route.
Startup/configuration, raw cache pointers and destruction remain owner-serialized;
never stop from callbacks or under manager/listener locks. Legacy HashStore
saving remains unchecked, not installer consent. Remaining queue producers and
active transfers still need their barriers before coordinator/final persistence.

Queue-worker checkpoint: `QueueManager::stopWorkersForShutdown` closes recheck
and physical-move admission, cancels queued/current rechecks, drains admitted
calls and callbacks, joins the rechecker, then drains and joins accepted mover
jobs. Concurrent stop callers join the same terminal barrier. Queue data and
read/save APIs remain available; the destructor stops workers before destroying
queue state. This is deliberately not a whole QueueManager terminal stop.

Rechecking retains previously verified segments until a complete, still-applicable
result is ready; cancellation, read failures and changed queue progress cannot
commit partial or stale results. A failed file read does not strand the next
queued recheck. Accepted completed-file moves are not discarded. Blocking disk
I/O delays shutdown instead of allowing force-exit or installer consent.

The coordinator must first fence other queue producers and drain transfer
completion, and retain hash/client/log/queue dependencies through worker drain.
Manual/raw worker lifecycle and destruction remain owner-serialized; never stop
from a worker/listener callback or under queue/listener locks. Worker quiescence
is not proof a legacy physical move succeeded: `moveFile_` can log rename/fallback
failures without a checked outcome. Whole-queue/active-transfer barriers, checked
final move/persistence outcomes, non-destructive coordination and committed-quit
installer acceptance remain pending. No MainWindow/Sparkle activation or `_Exit`
replacement is authorized by this checkpoint.

Connection-producer checkpoint: `ConnectionManager::stopForShutdown` permanently
closes listener/accept/outbound/expected-connection, connection-queue, force,
timer and handshake/handoff admission. It drains admitted calls and timer
dispatch, joins listening servers, requests existing peer disconnects, then
waits for actual registry retirement and its own cleanup callbacks. Concurrent
stop calls join one terminal phase; failure cleanup remains admitted during the
drain and is closed atomically with completion. Listening-server stop flags are
atomic. Nullable private factories are checked at every internal caller, and
ordinary disconnect/relisten remains reusable. Legacy shutdown delegates to the
same barrier without changing the global proxy route or granting installer consent.

Pending connection/read state and manager dependencies survive. Manual listener
configuration, raw pointer users and destruction remain owner-serialized; never
stop from callbacks or under manager/listener locks. Registry retirement happens
before later UserConnection download/upload listeners and transport destruction,
so this is not whole-transfer/socket quiescence. Real loopback coverage explicitly
proves that distinction. Retain those dependencies, fence remaining download/upload/
queue producers and complete the separate BufferedSocket drain before queue-worker
stop/final checked persistence. Pre-existing allocation/transfer/move failure
outcomes and non-destructive whole-core/committed-quit installer acceptance remain
pending. Do not replace `_Exit` or activate Sparkle based on this barrier alone.

Transfer-producer checkpoint: `DownloadManager::stopProducersForShutdown` and
`UploadManager::stopProducersForShutdown` close new connection/request/file/slot/
timer work, drain admitted setup callbacks and detach their timer/client sources.
Concurrent/repeated calls join the same terminal phase. No lifecycle or transfer
registry lock is held across source detach or condition-variable waits. Manual
raw/configuration/limit mutations and destruction remain owner-serialized; never
call the fence from callbacks or under manager/listener locks. Ordinary transfer
setup and waiting-list controls remain usable before terminal fencing.

These APIs intentionally leave accepted data, completion, failure, queue progress,
stream and slot cleanup operational. A completed download cannot schedule its
next job after closure; its now-idle listener is detached and peer disconnected.
Read/waiting state survives. Destructor-body fencing retains the existing active-
list wait, but this is not a complete transfer destructor/resource barrier: the
blocked failure regression proves active-list removal precedes final queue
bookkeeping. Retain transfer/queue/hash/share/finished/log/CM dependencies through
the later peer/socket drain, and keep queue workers open for accepted completion
moves. Whole-queue fencing, checked flush/move outcomes, final resource/cleanup
completion, non-destructive coordination and committed-quit installer acceptance
remain pending. No MainWindow/DCContext/Sparkle activation or `_Exit` replacement.

Queue-producer checkpoint: `QueueManager::stopProducersForShutdown` closes public
new file/list/directory/source/selection/setup/priority/load/PFS/search/timer work
and joins admitted callbacks and manager-owned background filelist matchers.
Same-thread nested operations finish with their already-admitted parent, so an
admitted folder/list/load pipeline cannot consume intent while refusing its later
mutations. Matcher startup/reaping is owned, cancellation is checked between
physical reads, and startup completion is recorded before a worker can be reaped.
Concurrent/repeated stops join one phase; no manager/listener/lifecycle lock is
held across source detach or worker/operation waits. Read/checkpoint APIs survive.

Accepted `putDownload`, source-error cleanup and worker moves deliberately remain
live. New peer/list-processing tails require admission; late completed-filelist
processing does not consume pending directory intent. Manual remove/clear/raw/
configuration and private loader/source access remain owner-fenced. This is not
accepted cleanup/resource completion or checked flush/move success: dependencies
and worker admission must remain alive through the later peer/socket drain before
worker stop and checked final saves. Normal-quit/coordinator/installer gates remain
pending, with no `_Exit` replacement or Sparkle activation.

Accepted-transfer cleanup checkpoint: both transfer managers expose
`stopCleanupForShutdown()` as a second terminal phase. After producer closure and
owner-fenced peer/socket drain, it closes accepted callback admission, joins whole
operations (including already-admitted synchronous nested cleanup), and returns
one stable result to concurrent/repeated callers. Download failure/completion
queue tails and upload stream/slot retirement finish before that result. Late
entries refuse raw pointers/dependencies. Listener detach and waits occur without
manager/lifecycle locks. Reads remain available; manual/raw/configuration users
and socket lifetime remain the owner's separate prerequisite.

Download flush errors are latched for the manager lifetime and discard only the
current uncommitted segment progress, preserving earlier verified queue segments.
Active downloads/idlers, upload streams/slots, or a flush error yield false, never
installer or destructive-teardown permission. Empty lists or CM registry retirement
alone do not imply resource completion. Destructor cleanup now joins admitted
accepted callbacks after the legacy active-list wait; this does not replace the
owner's socket drain. Queue accepted-cleanup/checked physical move outcomes,
non-destructive whole-core sequencing, final checked saves and normal-quit handoff
remain required. No MainWindow/DCContext/Sparkle activation or `_Exit` replacement.

Interrupted Merkle flushes retain partial hash state and validate complete leaves
without falsely comparing an unfinished leaf with a complete expected leaf. Actual
EOF finalizes once, repeated flushes keep root validation, and excess/post-EOF
payload rejects before hashing/writing. Real-file regressions cover continued
partial writes, corrupt complete/short final blocks and valid short-file cleanup.

Accepted-queue checkpoint: `QueueManager::stopCleanupForShutdown()` closes
external accepted cleanup after producer closure and owner-fenced transfer/socket/
manual/raw callers. It cancels pending rechecks, joins whole admitted queue tails
and synchronous nested mutations, then joins existing worker closure. Previously
queued mover jobs retain trusted internal leases through execution/callbacks;
external threads never inherit an admitted parent's lease. The mover references
its owning manager rather than requiring the context to republish that pointer
during destruction. Late raw/ownership entries reject before dependency effects.

Concurrent/repeated callers share one stable checked result. Retained running
downloads, incomplete mover jobs, accepted exceptions or requested-target move
failures yield false, never installer/destructive-teardown permission. A successful
source-directory fallback preserves data but does not certify the requested
destination. Wrong-order worker closure retains a failed completed-move outcome.
Rechecker cancellation/stale-identity protection and ordinary reusable operation
remain intact. Checked hash/DHT/client persistence, aggregate core/socket drain
and committed-normal-quit handoff remain separate prerequisites. No MainWindow/
DCContext/Sparkle activation or `_Exit` replacement at this checkpoint.

XML-snapshot checkpoint: `HashManager::saveIndexChecked()`,
`ClientManager::saveUsersChecked()` and `DHT::saveDataChecked()` expose retryable
snapshot outcomes while their managers stay live. Existing void saves delegate.
Flushed/closed native replacement preserves the previous file on reported write
or rename failure; hash/DHT dirty state and user save intent survive retry.
Same-file Users writers serialize. Final owner must fence nick mutations and
save the queue first, including its source-CID `saveUser` tail, before Users.

DHT consumes an atomic dirty mark before its locked snapshot and restores it on
failure, without clearing marks arriving during serialization/I/O or acquiring
the DHT lock from an index writer. A fresh disabled/no-state instance succeeds
without writing; a missing bucket with pending state cannot certify success.
The DHT reader accepts the emitted `Files/File` and legacy `Indexes/Index`
formats, correcting persisted foreign-index readback without changing the writer.

This is an XML-file checkpoint, not all hash persistence: coordinated data/index
rebuild failure/recovery remains a gate, as do required Favorites/conditional
writers and mandatory managed publication revocation versus optional performance
caches. Real staging/rename
refusal, child-only buffered-flush write refusal, retry and loader readbacks do
not prove fsync fault, disk-full/power-loss, Windows or minimum-OS acceptance.
Aggregate non-destructive core/socket drain and committed-normal-quit installer
handoff remain unimplemented; no runtime activation or `_Exit` replacement.

Primary HashData insertion checkpoint: `dataInsertionsSucceeded()` reports only
accepted insertions, with a manager-lifetime failure latch. Reported storage
failure denies further new roots in this session; old roots remain readable.
It does not certify constructor, rebuild or whole-store persistence. A transient
failure must withhold installer handoff, not prohibit an ordinary no-update quit.
Leaves flush before the cursor write, and the cursor flushes before publishing a
new tree/file mapping or `TTHDone`. The first append of a loaded/rebuilt generation
validates the cursor against header/file bounds and every indexed leaf extent.
Short reads leave caller output unchanged. Optional unsupported xattrs remain
optional. Accepted queue TREE insertion errors propagate after retiring the
owned download from running/item bookkeeping, retaining the cleanup failure and
prior verified progress rather than leaving a dangling pointer.

Real read-only data-open refusal and child-only partial append refusal are
distinct from virtual File flush-order/fault and matching-stale-tail read tests.
These do not establish native fsync fault, disk-full/power-loss or crash-safe
two-file rebuild. Constructor/rebuild outcomes and coordinated recovery remain
prerequisites before consuming index and insertion results for shutdown consent.

Insertion checkpoint acceptance: 72 focused cases/1,027 assertions and 20
randomized orders pass; full DC-only core 948 cases/148,163 assertions pass;
12 registered persistence/retirement checks pass. The exact accepted-provider
Torrent-enabled Qt suite rebuilds and passes 548 cases/28,550 assertions, with
eight explicit real-GOST opt-in skips. Bounded author review, not independent
whole-updater or live/minimum-OS acceptance. Installed application unchanged.

- [ ] RED `[updates][update-shutdown]` tests assert Hide/minimize/canceled quit/forced kill never install, autoInstall OFF never installs without explicit version consent, toggling preferences or changing the ready version invalidates old automatic consent, and active transfers are unaffected by check/download.
- [ ] Native RED fixture assertions: real old/new signed bundles update only on normal quit, false relaunch flag stays closed, canceled authorization and bad identity leave the old app executable, interruption/disk-full leave a recoverable old installation. Hash disposable profile/share/download trees before and after; require equality except explicitly saved resume/settings state expected from normal shutdown.
- [ ] Integrate at the final successful `closeEvent` cleanup boundary, not an early menu click or generic `aboutToQuit`. Implement deferred public Sparkle install/user-driver callbacks without force-exit or private APIs. Revalidate signed metadata, pending archive and policy after cache restore and immediately before handoff. Keep network bridge alive only through download completion, not as an external process.
- [ ] Run `QT_QPA_PLATFORM=offscreen "$B/eiskaltdcpp-qt/eiskalt-mainwindow-toggle-tests" '[update-shutdown]'` and native `[update-install]`. Expected GREEN on disposable app copies, no production installation. Test an ordinary quit with no pending update for regressions. Commit.

### Task 6: Native Settings, Notifications And Localization

**Files:** Create `updates/SettingsUpdates.{h,cpp}`, `tests/qt/test_settingsupdates.cpp`; modify `Settings.{h,cpp}`, `MainWindow.{h,cpp}`, app/test CMake, and affected `eiskaltdcpp-qt/translations/*.ts` through the existing translation workflow.

**Interfaces:** `SettingsUpdates(UpdateController&, QWidget*)`, `void apply()`; menu action calls `checkNow()`. Append `Settings::Page::Updates` without renumbering existing pages, including the conditional Torrent entry. Construct the page in both Torrent-enabled and DC-only Qt builds.

- [ ] RED widget tests: visible ON/ON/OFF controls, keyboard-accessible labels, auto-install visibly disabled when automatic checks are OFF, saved OFF values retained, manual check always available, notification OFF suppresses unsolicited notices but not manual results.
- [ ] RED UI tests: last success, release version, progress/cancel and appropriate Download/Install on Next Quit/Later/Skip actions reflect controller state; a ready download never changes a quit button into forced installation. Render malicious signed release-note HTML as inert plain text.
- [ ] Implement native Qt layout using existing settings styling, not new global CSS or combobox overrides. Add translated copy and accessible names; use existing Live Log/status infrastructure with redacted errors. Disable unsupported platform install controls with an explanatory label, not a broken action.
- [ ] Run Qt `[settings-updates]` and window `[update-ui]`, expected GREEN. Inspect light/dark and narrow/large settings windows in an isolated profile; compare displayed defaults after restart. Run catalog/placeholder checks and record linguistic review separately. Commit.

Task 6 can advance as an isolated reusable widget while Task 5's destructive
native recovery checks await a disposable macOS test environment. The page uses
the existing controller and native Qt palette/layout, preserves saved preferences,
and renders release notes only as plain text. A backend installation capability
defaults to false; no production backend is enabled by this UI checkpoint.
Do not change Settings/MainWindow ownership, menus, main/_Exit, or installer arming
until the retained native recovery and committed-quit acceptance gates pass.
Widget/extraction tests are not integrated application settings, native Cocoa QA,
linguistic review of language catalogs, or release acceptance. Keep those steps pending.

Catalog checkpoint: all 25 catalogs now contain the updater's extracted runtime
contexts, including Objective-C++ backend errors. All 25 languages have populated
updater captions and diagnostics; existing non-updater content is preserved.
Test-owned compiled catalogs verify actual widget actions, runtime diagnostic
lookups, version substitution and distinct keyboard mnemonics in every language.
The offscreen light/dark layout checks retain the 420-pixel limit for all 25
languages. Detailed consent/error text is tested in English, Ukrainian and Russian.
These tests do not constitute independent linguistic review. This
does not complete translation review, real Settings/menu/Live Log integration,
native Cocoa layout QA or the installer recovery/committed-quit gates.

Native reusable-widget checkpoint: the isolated settings suite now runs with the
copied patched Cocoa plugin on macOS 27. Ukrainian checkbox captions and Georgian
action rows reproduced native-only violations of the existing 420-pixel limit.
Local native-indicator checkbox wrapping and Qt form-row wrapping fix those
violations without changing captions, fonts, global styling or controller policy.
Mouse clicks on wrapped text and keyboard toggling remain tested.

All 25 languages pass native light/dark 420-pixel layout checks; 100 retained
native screenshots cover narrow and 800-pixel-wide variants. Native settings
tests pass 13 cases/2,904 assertions, DC-only settings pass 13/2,804, and the
frozen-candidate full Qt suite passes 578 cases with eight explicit real-GOST
opt-in skips. Original build binaries/catalogs and the installed app/profile
remain unchanged. This is reusable-widget QA and a bounded author review, not
actual Settings/menu/Live Log ownership integration, independent linguistic or
whole-branch review, native minimum-OS acceptance or installer recovery.
Those remaining runtime and release gates are unchanged.

Semantic-review checkpoint: a separate reviewer inspected all 31 updater-settings
messages in each of 25 catalogs (775 source/translation pairs). An author review
covered the remaining 13 diagnostic messages per catalog (325 pairs). No clear
reversal of installation consent, crash exclusion, verification failure or the
no-direct-fallback guarantee was identified within the reviewers' language
confidence limits. The fresh extraction/completion/placeholder audit passes
16 tests; mechanical checks do not prove meaning.

One minor Hungarian confirmation-message issue is deferred: "Update operation
canceled." currently reads as an interruption rather than explicit cancellation.
The Spanish, French and Italian unsupported-backend wording also uses "automatic
updating" for "self-updating"; the settings support label separately distinguishes
unavailable installation from available manual checking. Neither finding changes
the installation policy. Basque, Georgian and Interlingue idiom needs native-speaker
review; this bounded independent model review is not native-speaker certification
or whole-branch acceptance. No catalogs or runtime behavior changed. Actual
application integration, disposable-Mac installer recovery and committed-quit
acceptance remain required before enabling updates.

Wording-polish follow-up: the four deferred messages above are now corrected.
Hungarian explicitly reports cancellation; Spanish, French and Italian describe
unsupported installation by the application rather than unsupported automatic
checking. XML and compiled-catalog regressions reproduced the original wording;
the Hungarian runtime test also exercises the actual Cancel status. The corrected
catalogs pass 17 mechanical tests and 14 settings tests (2,844 assertions) with
both offscreen and native Cocoa rendering. The unsupported-installation tests
retain manual checking and leave installation actions disabled. Only the four
intended catalog messages changed; all other messages in all 25 catalogs remain
unchanged. This does not resolve native-speaker polish, installer recovery,
actual application integration, production signing or release acceptance.

### Task 7: Reproducible Signed Feeds And Release Packaging

**Files:** Create `macos/updater/{make-feeds.py,verify-feeds.py,README.md}`, `tests/macos/updater/test_release_feeds.py`; modify `macos/release.py`, `macos/RELEASE.md`, `eiskaltdcpp-qt/Info.plist.in`, `cmake/MacUpdater.cmake`, `docs/third-party-release-inventory.md`, relevant presets/CMake and `tests/macos/bundle/release-profiles.py`.

**Interfaces:** `make-feeds.py --manifest FILE --output DIR --signer PATH` prepares Intel/ARM feeds offline and refuses missing/invalid inputs; `verify-feeds.py --manifest FILE --feeds DIR --public-key KEY` validates them. Manifest schema v1 records version/channel, flavor/arch/minOS, bundle ID, HTTPS asset URL, byte length, SHA256 and Ed25519 signature. The signer is the pinned Sparkle signing tool configured to read Keychain, never a shell command embedded in metadata. All outputs are offline by default; publication is a separate explicit operation.

Amendment: schema v1 also records numeric `build_number`, identical for both
flavors and increasing across published releases. CFBundleVersion and appcast
version use this number; CFBundleShortVersionString and archive filenames retain
the visible release version. Update the existing packaging equality checks and
their tests rather than leaving conflicting version contracts.

Offline tooling checkpoint (Task 7 remains partial): `release_feeds.py` shares
strict schema/byte/signature validation between the two CLI entry points. Both
flavors stage together and use macOS exclusive promotion; no existing output
is overwritten. An optional previous manifest checks increasing numeric builds
offline, not publication history. Ephemeral-key tests invoke the pinned SDK and
OpenSSL 3; the opt-in native `[feed-publication]` case consumes their public-only
fixtures using production feed/policy/archive verifiers. This does not establish
real bundle contents or enable an installer. Framework/helper signing,
production Keychain setup, both real release packages
and native minimum-OS/recovery acceptance are still pending.

Numeric packaging checkpoint: `MacBundleVersion.cmake` validates a separate
positive canonical signed-64-bit `EISKALT_MACOS_BUILD_NUMBER`. Ordinary builds
default to development identity `1`; both release presets require an explicit
environment allocation. The package CLI requires `--build-number`, checks it
against a string-valued `CFBundleVersion` before packaging and again after ZIP
extraction, and preserves display versions/filenames. Existing metadata hashing
forces relink/post-build signing for a number-only change, including Xcode.
This is not production number allocation, dependency/minimum-OS acceptance or
installer activation; whole Task 7 remains partial.
Full application deployment also exposed read-only linked-provider staging.
The deploy script now rejects an existing staging symlink/directory and makes
only the copied libtorrent file owner-writable before Qt's edits, never the
shared dependency. Regression tests retain original source bytes/permissions.

Framework staging checkpoint: `stage-framework.py` snapshots/verifies both pinned
SDK/source archives before extraction, preserves matching license bytes, and
produces a separate thin dependency tree per flavor. All framework/helper Mach-O
files are OS-floor audited, signed inside out and strictly verified; original
helper identifiers/entitlements and confined framework links are retained.
Source pins, preparation/signing mode and final binary hashes are recorded in
ThirdPartyNotices. Exclusive promotion rejects existing destinations and failed
signing never exposes a partial stage. Actual-SDK fixtures include a signed ZIP
round-trip. This is not production application wiring, feed/public-key CMake
validation, final app signing, key setup or native installation/minimum-OS proof.
The framework/helper packaging checklist item therefore remains open.

Feed path checkpoint: the native consumer uses the producer's flavor filenames,
`intel.xml` for `x86_64` and `apple-silicon.xml` for `arm64`, rather than CPU-named
files. A native discovery-only regression checks the requested URL against a
literal contract; portable policy tests cover both mappings and unsupported
CPUs. The maintained host/repository namespace, routing policy and production
backend activation gates are unchanged. This does not publish or install feeds.

Trust metadata checkpoint: `MacUpdaterMetadata.cmake` is included before the
existing bundle metadata hash. Its separate opt-in development flag defaults
OFF and is not framework packaging or updater activation. ON requires an explicit
canonical nonzero base64 public key and a single CPU's exact maintained flavor
feed URL; only an optional positive decimal revision query is permitted for
public cache invalidation. Signed-feed/archive guards and single-policy-owner
settings are emitted into the real plist; OFF removes them. Actual Make/Xcode
fixture builds exercise key-only/feed-only relink and re-signing. Build-cache
metadata source generations and a confined pre-link gate also exercise equal
timestamps and returning to a prior hash; no-op builds keep their identity.
The internal generation/linked receipt is not release ordering or proof of
successful final signing. Metadata opt-in requires CMake 3.23+.
Configured feed consumption, audited framework/helper copying and whole-app final signing
remain later gated work; the packaging checklist item stays open.

Audited packaging integration checkpoint: the separate default-OFF
`EISKALT_MAC_UPDATER_PACKAGE` requires validated metadata and an explicitly pinned
complete local stage digest. It audits during configure and every build, copies
from a re-verified private snapshot after Qt deployment/normalization, signs only
the confined copied Cocoa leaf while proving unchanged non-signature code,
retains exact Sparkle bytes/notices, and signs other leaves/bundles then
the app explicitly without deep signature rewriting. Strict whole-app signatures,
CPU/OS declarations and forbidden absolute dependencies are checked. Recipe,
stage and identity inputs enter the metadata generation gate. Existing different
destinations or changing ON back to OFF require a fresh build tree. Default
legacy signing remains unchanged; install-time redeployment is skipped on the
enabled path. No runtime/backend/quit activation is implied. Disposable
Intel/ARM and Make/Xcode fixtures do not complete actual split-CPU release,
native minimum-OS, installer recovery or installation acceptance; the checklist
item and whole Task 7 therefore remain open until those gates pass.

Relative closure checkpoint: the legacy external-link entry point now performs
a read-only thin-Mach-O audit of main/helper/plugin contexts, inherited runpaths,
required/weak/re-export/upward imports, confinement and dependency existence.
Install IDs are separate from imports. JSON records per-image hashes and resolved
edges; system-cache paths and detached-image contexts remain explicit assumptions.
The new recipe enters packaging hash/configure dependencies, including equal
timestamps. This does not establish runtime-generated dlopen resolution, symbol
compatibility, static/source/notice completeness or native minimum-OS acceptance.

- [x] RED release tests use ephemeral keys and literal fixtures: tampered/truncated feed/archive, absent signature, mismatched length/hash, wrong flavor/minOS/name, conflicting versions, path injection and missing assets all fail; two valid flavor feeds round-trip against actual Sparkle verification. No production private key enters fixture logs.
- [ ] Add framework/helper packaging with explicit thinning and strict final signing, retaining the patched Qt Cocoa plugin. Make feed URL/public verification key changes invalidate bundle metadata and relink through `MacBundleMetadata.cmake`. A release configure without a valid public key/feed contract must fail rather than emit an updater that trusts unsigned content.
- [ ] Generate the production signing identity only as a distinct, recorded release-setup action, in local Keychain; expose only its public key to source/configuration. Generate full-archive signatures after final signing/packaging. No automatic private-key export; document secure backup/rotation requirements.
- [ ] Run `python3 tests/macos/updater/test_release_feeds.py` and `python3 tests/macos/bundle/release-profiles.py "$PWD"`, plus native `[feed-publication]`; expected GREEN. Add Sparkle's exact source/license/build provenance to each flavor's notices. Preserve the 512 MiB archive gate or revise the plan if the actual complete bundle exceeds it, never silently raise it.
- [ ] Commit release tooling, public metadata and documentation only. Do not commit keys, generated QA apps, private profiles, cache files or unsigned fallback feeds.

### Task 8: Whole-Application Acceptance And Publication

**Files:** Update `README.md`, `docs/wiki/{Getting-Started,Development-Status,Troubleshooting}.md`, `macos/RELEASE.md` and release notes with observed outcomes. Final fixtures/results live in ignored QA evidence.

**Interfaces:** Consumes Tasks 1-7 and the independently completed Intel dependency/plugin/application build. Publishes only verified artifacts from this final source state; earlier pre-updater ARM ZIPs are not release candidates.

- [ ] Build both macOS flavors with their own dependency prefixes/patched Cocoa; run serial core, Qt, Torrent and main-window suites plus the native update fixture. Run the DC-only build/tests. Configure/build the existing Windows/Linux jobs to catch accidental mandatory Sparkle/Objective-C++ dependencies; report unavailable platform execution rather than infer success.
- [ ] Run `ctest --test-dir "$B" --output-on-failure -j1` under isolated HOME/XDG paths with loopback access; expected zero failures with real proxy acceptance enabled, not skipped. Run Cocoa tray/accessibility tests and old/new updater fixtures on native minimum-OS hosts when available; clearly retain unavailable acceptance gates.
- [ ] Independently review the complete updater diff once, then make one bounded fix pass with RED/GREEN evidence for findings. Re-run affected/full suites before claiming completion. Do not repeatedly review the earlier unrelated C++ branch.
- [ ] Audit every final Mach-O and staged source/notice file; run signed ZIP roundtrip, strict signature, external-link, app-identity and isolated Homebrew/profile-denied startup checks. Verify update metadata selects only newer compatible versions and current pre.1 reports no update to itself.
- [ ] Recheck app closure before any approved local installation. Back up the installed bundle; never force-quit it, overwrite its profile or install from an unverified staging directory. Record installed executable/version readback separately from packaging tests.
- [ ] Verify canonical remote is `Delitants/eiskaltdcpp`, finish source/README/wiki publication (wiki still requires web initialization), then upload both final ZIPs, matching sources/notices and checksums to the approved prerelease. Read back public bytes/signatures. Publish the separate signed feeds under the repository's `updates/` directory **last**, only after asset verification. No scheduled updater may advertise missing assets.
- [ ] Report exactly what is installed/published and remaining native OS, live/profile, soak, translation or platform gates. Do not mark the whole prerelease complete while any required release gate remains open.

## Self-Review And Handoff

Coverage: Task 1 proves the otherwise unverified API path; Tasks 2-3 enforce routing/trust; Task 4 owns policy/defaults/channels; Task 5 owns consent/replacement; Task 6 owns UI/localization; Task 7 owns signing/materials; Task 8 owns final acceptance/publication. Review Focus cases have explicit tests in those tasks. Shared method/type names above are the execution contracts; keep them consistent in task briefs.

This plan and its two execution amendments were approved. Execution remains
native/inline; no further execution-mode confirmation is needed. Approval is
not evidence of publication or replacement of the installed application.

Hash creation/rebuild checkpoint (Task 5 remains in progress): construction now
loads/selects the committed cache once; worker startup does not reload it or
clear already-used mappings. Fresh data is staged, cursor/size checked and
physically flushed before replacement. Truncated/indexed data is never silently
recreated. Failed initialization denies new roots and checked index saves, while
independently valid old leaves remain readable.

Rebuild stages a unique data generation and complete flushed XML. One native
same-filesystem XML replacement commits both offsets and data selection, then
no-throw map/path swaps update memory. Generated caches use format v3 with a
validated basename; legacy v2 remains readable/writable. Older clients ignore
v3 rather than combining compacted offsets with legacy data. The original legacy
data is retained as migration evidence. Superseded committed generations are
optionally reclaimed after commit; interrupted unreferenced stages remain safe
orphans. No shared-profile/multi-process or filesystem/power-loss guarantee.

`dataStorageSucceeded()` covers initialization and accepted insertion/rebuild
outcomes, NOT final XML persistence or whole-core shutdown. Rebuild failures are
sticky for the manager lifetime, including after a successful retry. Corrupt
retained leaves/missing retained roots refuse rebuild without dropping their
saved mapping. Filesystem lookup refusals are checked rather than escaping the
worker. All accepted producers/cache writers still must drain before the final
owner combines this outcome with `saveIndexChecked()` and other required saves.
No MainWindow/DCContext/Sparkle/normal-quit installer activation in this slice.

Verification hold (2026-10-01): randomized QA exposed a shared test-path isolation
bug that reached the real hash cache, queue and nickname cache. Development,
commit and release validation were paused pending approved narrow recovery.
Current files and a matching saved-profile candidate are preserved in private
local QA evidence. Shared TestContext now reapplies owned path overrides; future
suite launches must also isolate HOME/XDG defaults. That initial hold is a
historical record, not a claim that the profile was preserved. Do not treat the
preceding initial QA as release acceptance or an installed implementation.

Recovery completed after explicit user approval: the exact four saved-copy
files were restored with closed-app/hash guards and independent Merkle-root
readback; preferences, favorites, DHT state and installed executable hashes
are unchanged. The approved queue/nickname copies are empty. The incident
history remains recorded. A shared-path regression uses only an owned decoy
directory and checks ownership before cache construction; removing the helper
reset reproduces the failure without reaching a real profile. All resumed
acceptance runs use fresh isolated HOME/XDG/TMP roots in addition to fixture
overrides. No installation, release or whole-core acceptance is implied.

Final bounded checkpoint validation after recovery: focused90/1301 and all20
randomized seeds pass; full DC-only core964/148414 passes; registered CTest16/16
passes serially; exact accepted selected-recheck provider Torrent-enabled Qt
passes548 cases/28563 assertions with8 explicit opt-in live-GOST skips. Both
targets rebuilt-j2, suites serial, source/profile/executable hashes unchanged
after resumed QA. Bounded inline author review completed; independent whole-
updater review, mandatory remaining writers and aggregate normal-quit wiring
are still required. Task5 remains IN PROGRESS with original BASE retained.

Required-writer continuation: `FavoriteManager::saveChecked()` serializes the
final snapshot with existing saves, flushes/closes staging and atomically
replaces Favorites.xml without delete-first. It reports loading-time skips
and write/replace failures as false; ordinary void `save()` remains compatible.
Owner must first quiesce UI and accepted callbacks; this is not their barrier.

`ShareManager::managedCacheRevocationsSucceeded()` reports only mandatory
accepted saved-cache invalidations. Unlink precedes optional regeneration as
before; a failed required unlink latches false until manager replacement.
Later successful optional writes/retries cannot erase that failure. Missing
cache and regeneration-only failure are nonfatal. The existing warning is
delivered after releasing cache I/O serialization, with the failure already
latched. Final owner must drain producers before using this outcome for consent.
No aggregate core/conditional-writer/normal-quit/installer activation is implied.

Supplemental final-QA correction: GOST diagnostics now recognize specific native
socket timeout reasons before certificate/auth/tunnel stage classification. The
native socket's independently rounded millisecond budget can expire while the
outer deadline still has more than a millisecond left. A real disposable stalled
TLS regression reproduced the misleading certificate failure before the fix;
cancellation still takes precedence and trust/auth/REP results remain distinct.
No TLS verification, route or direct-fallback policy is relaxed. Temporary trace
removed; full final Qt validation and repeated deadline cases required.

Conditional IPFilter checkpoint: `saveListChecked()` is a non-destructive final
snapshot for initialized/edited rules, including explicit empty state. An unused
manager does no persistence I/O and leaves any disabled saved rule file alone.
Explicit load, valid add, clear or ordinary save initializes its snapshot state;
ordinary `saveList()` retains its throwing caller interface. The existing format
and rule order/action/direction remain unchanged. Staging is flushed/closed before
native atomic replace, never delete-first; checked failure can retry and neither
success nor refusal clears live rules. Internal recursive serialization covers
load/edit/lookup and both writers, not GUI borrowed-pointer ownership. Final owner
must still quiesce those GUI edits and network readers; legacy destructive
shutdown is not the persistence certificate. Regression proofs include actual
loader/native partial-write/replace/stage refusal, explicit empty/unused states
and missing-initialization/missing-ordinary-lock mutations restored exactly.
Whole-core aggregation, ordinary-quit and installer activation remain unwired.

Settings outcome checkpoint (Task 5 remains in progress): a checked final save
now refuses to publish XML if any retained Save listener fails. The shared event
dispatcher exposes an opt-in delivery outcome while ordinary events retain their
notify-all, exception-containment and diagnostic behavior. Callers must still
quiesce settings edits and own listener lifetimes; this is not a shutdown fence.
Tracing the buffer corrected the earlier audit: its flush already delegates to
native File.flush, so no redundant flush was added. Loader/readback, native
staging/replacement/write refusal and test-only inode/thread-bounded fsync EIO
verify old-file retention and retry. The injected syscall outcome is not physical
disk/power-loss acceptance; shared-core macOS explicitly skips that injection.
Whole-core owner ordering and committed normal-quit updater handoff remain pending.

Lua/Throttle/ADL prerequisite checkpoint (Task 5 remains in progress): shared
Lua admission rejects new hooks before its lock, drains admitted nested calls,
detaches timer and client listeners outside callback locks, and retains state
until destruction. Loading is idempotent and cannot revive terminal admission
or replace another manager's runtime; finalizers cannot call native side effects.
The existing macOS ADC bypass remains unchanged. The default Lua formatter's
invalid percent escape was corrected for the configured Lua runtime.

Throttle shutdown now latches before the first tick, joins concurrent stops,
and lets the owning timer release its locks even when shutdown arrives during
refill. This is not a socket-resource drain, and the timer must remain running.
ADL ordinary checked saving stages, flushes, closes and atomically replaces the
existing XML schema; failures retain edits and can retry. Its owner-fenced
terminal save caches either outcome and suppresses later fresh save/destructor
writes, including after refusal. Public collection edits remain caller-fenced.
Focused real Lua, controlled timer, loader and native I/O refusal regressions
and bounded independent source reviews accompany these prerequisites. Full
owner aggregation, GUI normal-quit/installer consent and release remain pending.

Retained transport/offline-tail prerequisite checkpoint (Task 5 remains in
progress): terminal Client cleanup now closes ADC's raw UDP relay after its
timer/socket callback barriers, and the client registry closes its own raw UDP
socket. The lifetime owner must fence manual UDP sends first; these closes are
not admission gates and BufferedSocket's resource count does not cover them.
Shared SOCKS UDP terminal cleanup disconnects all matching-context published or
retired controls, even with borrowed snapshots, outside association/setup locks.
Weak tracking preserves ordinary settings-refresh snapshot behavior and prunes
expired controls; unrelated contexts are not revoked.

ADC/NMDC terminal cleanup also finishes their offline-user transitions while
Favorites and ClientManager remain alive, before the final snapshot phase.
DHT terminal cleanup snapshots its actual ClientManager registrations, not
only routing entries: each per-node online flag and explicit registration
reference is retired once, including routing-rejected nodes. Routing state is
retained and a shared User stays online while another identity remains. Later
hub/routing disposal no longer generates these favorite-user save tails.
Real loopback GOST/SOCKS regressions and retained-state/loader/refcount tests
cover these prerequisites; the checked core coordinator, failure retention and
GUI committed-normal-quit handoff still need implementation and acceptance.

Checked core coordinator checkpoint (Task 5 remains in progress):
`DCContext::prepareShutdownChecked` now aggregates the retained stop, resource
drain, transfer cleanup and checked persistence outcomes. Full startup records
the Timer constructor's thread as the shutdown owner. Unsupported/incomplete,
wrong-owner and reentrant calls do not consume an admissible attempt. An
admissible result is permanent, including a socket timeout or the first failing
component; no retry can turn a previously failed quit into installer permission.

Preparation retains the context object, managers and running flag. It drains
accepted Queue cleanup before closing Hash admission, stops Timer only once,
and attempts independent snapshots after quiescence without short-circuiting;
Queue's source-CID tail precedes Users, and Settings is last with Share attached.
Ready shutdown disposes without replaying raw Timer unlock or primary saves.
Failed/preparing shutdown refuses legacy teardown. Destroying a failed checked
context terminates rather than silently destroying its unique_ptr members:
the application owner must retain the entire object on refusal.

Regression coverage includes native buffered-write refusal, sticky snapshot
staging/listener failures, retained identities, queue-source loader readback,
physical socket-destruction timeout, mapping exceptions, mandatory Hash/Share
receipts, reentrancy and the failed-context destructor guard. The product GUI,
manual callers, Torrent/updater workers and macOS emergency exit are still
unchanged. Their retained input/worker fence and committed-normal-quit handoff
must land before the coordinator can authorize a real updater installation.

Retained updater worker-fence checkpoint (Task 5 remains in progress):
`UpdateTransport::stopWorkersForQuit` terminally closes admission and physically
joins its cancelable network worker without pumping queued callbacks. It removes
undelivered committed output, preserves already-delivered staging, rejects late
fetches and caches removal/join refusal. Wrong-owner calls do not consume the
owner attempt, and a fence from final progress cannot finish the canceled job.

The native backend stops discovery callbacks and polling, joins transport and
archive-verification workers, and retains a prior Ready stage only when its
route is still valid at preparation. Intentional core route retirement after a
successful fence no longer clears that stage. This is a worker-drain receipt,
not an archive/installer identity certificate; refused owners and their context
must remain alive. Unknown backends fail closed, and unsupported backends silence
pending capability replies without claiming installer support.

The controller separately closes scheduling/manual/preferences/notification
inputs while retaining the Ready version and its prior consent. Committed quit
rechecks the sticky worker result and saved channel, skipped/deferred version and
consent rules. Neither this fence nor ordinary shutdown grants installation.
GUI/manual DC/Torrent owner fencing, checked application normal-quit aggregation,
native installer acceptance/failure recovery and macOS emergency exit integration
remain pending; `armInstallOnQuit` is still the native fail-closed stub.

Retained GUI worker-preparation checkpoint (Task 5 remains in progress):
`MainWindow::stopWorkersForQuit` snapshots window/history intent, fences queued
GUI work and deferred owner deletion, disables manual actions, drains owned
threads and futures without pumping GUI events, and consumes checked Torrent
and GUI-settings outcomes. It includes registered tab roots and the separately
owned notification service. Foreign-thread calls and active modal/popup handlers
are rejected before consuming the attempt; an admitted failure stays false and
retains the window and core owners. Script-engine preparation is not supported.
An admitted preparation also rejects the old unchecked window-close path;
deferred GUI owners remain retained until the application coordinator resolves
the outcome. Future updater/native-handoff objects must live outside these
fenced GUI roots so their deliberately preserved event loop can still operate.

Proxy diagnostics now own and explicitly join their worker; cancellation and
late replies cannot restart a terminal runner. File hashing checks interruption
between chunks, retains its worker until joined, and rejects terminal dialog
actions. Shell cancellation is atomic and does not launch a pre-cancelled child.
Qt future completion events are fenced separately from queued signal delivery.

Retained hub lifetime preparation now saves each hub's layout intent, removes
core listeners, terminally stops and releases its registered client with core
dependencies alive, and drains hub-command workers even when they belong to a
separate chat widget. GUI preparation includes inactive and detached hub tabs
before final settings sync. Prepared frames remain disabled and owned; queued
work, deletion, reconnect, redirect and manual command paths stay fenced. Hub
registration is delivered on the GUI thread rather than mutating its registry
from a socket worker. This receipt is not physical core socket-drain proof.

These preparation APIs are not connected to normal quit yet and do not
authorize installation. Native installer acceptance/recovery and replacement
of the macOS emergency exit remain pending. Keep the existing exit path and
fail-closed installer unchanged until those integration gates pass.

Checked application aggregation checkpoint (Task 5 remains in progress):
`ApplicationQuitCoordinator` rejects foreign-owner, modal/popup and unsupported
contexts before admission, fences updater workers before intentional core proxy
retirement, and combines real GUI/Torrent/core persistence receipts. The first
admitted refusal is permanent; a later successful save cannot grant installation
permission. The application must retain the entire context and GUI on refusal.
The coordinator borrows owners and never destroys them or commits update consent.

Search blacklist and anti-spam rules/lists now have checked atomic final saves
and terminal mutation guards. Their separate QtContext-owned roots are covered
by the GUI event fence. After a GUI refusal, auxiliary snapshots wait for a
successful core callback drain; if both barriers fail, those owners remain
untouched. All independent final saves are attempted without replacing an
earlier failure. Legacy destructor saves cannot silently rewrite prepared state.

Isolated tests use a real MainWindow, hub client, active Torrent worker, checked
core and a controlled DownloadManager transfer on an accepted loopback socket.
They verify partial-file and serialized queue progress, retained owners, updater
ordering, terminal failure and unrelated QObject delivery. Core persistence and
socket-drain failures run in child fixtures that deliberately retain failed
owners until test-process exit, not through forbidden failed-context destruction.
This is not live peer payload, native installer, crash/recovery, physical
power-loss, minimum-OS or independent whole-updater review proof. Next is actual
native installer acceptance/handoff and ordinary committed-quit integration,
not another audit of the completed preparation prerequisites.

Native committed-handoff checkpoint (Task 5 remains in progress):
`MacUpdateInstaller` now implements the public native installer protocol in
opt-in disposable signed bundles. Its private entry requires committed normal
quit and the GUI owner thread, reauthenticates the exact signed offer/archive
on a physically joined worker, and binds internal bundle ID/build/version/CPU
and minimum OS before native preparation. A private staged-only bridge remains
usable after intentional route retirement without reopening external networking.
Current consent is checked before Sparkle creation and again at Ready; only
approved committed work can reply Dismiss. The adapter never quits or relaunches
the app and is not connected to `MacUpdateBackend::armInstallOnQuit` yet.

Disposable cases cover normal installation, hide, canceled quit, pre-commit
termination, consent withdrawal at Ready, wrong internal bundle ID and wrong
internal minimum OS, consent already withdrawn before native creation, and
corrupt staged feed/archive bytes. Normal retains the prepared native owner
through process exit. Parent readback checks the exact whole signed bundle,
no relaunch and unchanged user-data sentinels. These are simulated lifecycle
boundaries, not real MainWindow gestures or installer authorization cancellation.
External denial is proved in the native parent, not in launchd/XPC helpers.
Recovery after disk/interruption failure, authorization cancellation, native
session recovery and real committed/no-update quit remain next gates.
The installed app, normal exit path, profile and real signing keys are unchanged.

Native cancellation checkpoint (Task 5 remains in progress):
the private adapter accepts owner-thread cancellation and a monotonic deadline
without treating either request as an exit receipt. Verification joins before
refusal. Native check/download requests use only current public cancellation
blocks; extraction clears the expired block, waits for Ready/Skip and the
update-cycle completion callback. A stalled native cycle retains its owner and
staging rather than fabricating completion. First reason wins, active repeats
are idempotent, and approved prepared handoff refuses cancellation.

Typed phase notifications support future progress UI without activating it.
The verification worker captures the joined implementation state, fixing a
separately reproduced owner-destruction crash. Disposable native cases cover
verifier/check/download/extraction/Ready cancellation, real elapsed verifier
expiry, explicit expiry at extraction, foreign-thread refusal, and destruction
before event pumping. Extraction expiry is not elapsed slow-extraction proof.
Fresh-state admission rejects resumed SDK stages, but real resumed-session
recovery, authorization/disk/interruption recovery and helper confinement are
still unverified. Product backend, MainWindow and emergency exit remain unchanged;
no installed-app update or whole Task 5 completion is claimed.

Scoped review additionally identified the Native-before-cycle approved-handoff
window: accepting cancel there falsely reported refusal while Dismiss remained
armed. Approval now refuses cancellation before the SDK reply, not just after
terminal Prepared. Direct cancellation/deadline cases prove the pre-terminal
window; queued cases may run after completion and also require refusal and exact
approved installation. Worker-start refusal preserves
an already accepted first cancellation/deadline reason; a private launcher seam
throws before creating a thread to exercise that catch/queued-refusal branch,
without claiming real OS resource-exhaustion acceptance. All twenty-three native
modes remain disposable and runtime activation stays disabled.

Native retained-session/replacement-refusal checkpoint (Task 5 remains in progress):
the disposable public SDK driver prepares an installer, replies Dismiss, then
checks again with the same process/bundle identity and requires an actual
`Installing` callback without another feed/archive fetch. The production adapter
refuses that live retained session; exact old whole-bundle/signature and user-data
readback after process exit is required. A temporary Skip-to-Dismiss mutation was
caught by post-exit replacement even though the child still reported refusal.
Update-cycle refusal alone is therefore not accepted as a disarm receipt.

A separate case waits for completed native preparation, identifies the fixture's
single Autoupdate process, and denies writes only to its disposable destination
before child exit. It requires physical exit of that helper and OS-log receipts
for permission denial, finalization failure and failure to move the exact old
app. Permissions are restored only after no fixture installer remains; the old
signed tree and user-data snapshots must match. This exercises failure before
old-app removal, not disk-full, administrator cancellation, interruption after
removal or best-effort rollback. Launchd-owned helpers can use real-user cache
paths under their random probe identities despite parent-home isolation; no
helper filesystem/network confinement is claimed.

Product armInstallOnQuit, MainWindow and the emergency exit remain unchanged.
Cross-crash downloaded-cache recovery, authorization/disk/interruption/rollback
and helper confinement still need an isolated test environment and acceptance
before native activation or real committed/no-update quit integration. Updater
UI/localization, live routed HTTP/DNS, whole-branch/platform QA, signing/thin
packages, install/docs/wiki/screenshots/prerelease and feeds-last remain pending.

### Committed-Quit Receipt Contract Checkpoint

The controller now separates NotCommitted, NoUpdate, AwaitingHandoff, Prepared
and Refused. A successful updater worker fence and current version-bound policy
are required before it invokes the explicit asynchronous beginInstallHandoff
endpoint. The backend receives a unique operation and guarded live consent
callback; unknown implementations refuse without a legacy arm fallback. Backend
exceptions are allowed only before admitting work. Matching terminal receipts
are delivered once on the GUI owner thread; stale/duplicate replies and success
after observed consent revocation cannot certify Prepared. Shutdown/destruction
invalidate callbacks before revocation, and NoUpdate needs no pending receipt.

This is an interface/controller checkpoint, not native or MainWindow activation.
The final caller must consume ApplicationQuitCoordinator Ready first and retain
all owners/event-loop until the backend terminal receipt. Native signed-stage
handoff and actual committed/no-update closeEvent acceptance remain next. The
legacy macOS emergency exit remains unchanged.

### Native Backend Receipt Checkpoint

MacUpdateBackend now connects committed beginInstallHandoff to the real native
installer after a successful worker fence and exact retained Ready offer check.
It preserves exclusive staging through asynchronous cancellation and terminal
receipts. Operation IDs cannot produce duplicate receipts; foreign or competing
calls cannot steal the admitted handoff. Live consent runs on the GUI owner,
with sticky rejection and weak-owner guards. Terminal forwarding is deferred so
refusal observers may release owners safely. A prepared receipt reports actual
native preparation, never a fabricated disarm result after irreversible handoff.

Disposable signed-main-bundle tests exercise real backend discovery, archive
verification, route retirement and native installation. The normal case requires
exact signed replacement after process exit, no relaunch and unchanged test data.
Verifier cancellation and consent refusal before native startup or at final Ready
require the exact old signed bundle after exit. This is not actual MainWindow
closeEvent acceptance or supported-minimum-OS verification. Public installation
capability and runtime selection remain disabled; the emergency exit is unchanged.

### Actual Window And Ordinary Quit Checkpoint

The unparented ApplicationQuitSession now composes retained application
preparation with the controller's committed handoff. Only NoUpdate or Prepared
authorizes deferred exit; AwaitingHandoff retains the event loop and owners.
Refused is sticky because a synthesized controller refusal is not native disarm
completion. MainWindow preserves Hide, modal input and canceled confirmation.

Production macOS main enables checked no-update quit, receives readiness on the
application rather than fenced GUI receivers, destroys GUI owners before core
managers, and returns normally through checked core disposal. Unsolicited
event-loop exits retain the emergency fallback. The single-instance marker stays
owned until GUI/core teardown finishes, so a new instance cannot race final
profile writes. An application Quit event remains
unaccepted while preparation is pending; the legacy session callback also cancels
pending termination, without claiming OS-session integration acceptance.

Actual-entrypoint QA includes real main.cpp with only a fresh IPC identity,
private profiles and a localhost-only network policy. It requires the post-return
receipt, protected bytes and valid hash/queue/settings/Torrent snapshots. This is
ordinary no-update acceptance, not signed pending-update full-app replacement.
Public installation capability/runtime backend selection remain disabled.
Task 5 stays in progress until remaining lifecycle
acceptance; routed HTTP/DNS, UI/localization, intermittent Torrent bind diagnosis,
current dual thin builds/minimum-OS QA/fresh whole-branch review and publication
with signed feeds last remain separate gates.

### Application Owner And Signed Full-App Quit Checkpoint

Production macOS main now owns an unparented ApplicationUpdateOwner outside
fenced GUI roots and passes its controller into checked quit. Its constructor
still selects UnsupportedUpdateBackend with no scheduler or native installation
capability. At aggregate-ready NoUpdate, ownership is disposed before borrowed
GUI/core settings vanish. Prepared retains the sealed owner/native staging
through OS process exit; pending/refused dispositions cannot release it.

Disposable signed full-app acceptance includes actual main.cpp, MainWindow,
controller, MacUpdateBackend and Sparkle. Only the QA translation unit substitutes
a trusted fixture owner with fixture-only capability and injected signed bytes.
Normal quit returns through actual GUI/core teardown, installs the exact signed
new bundle, does not relaunch and preserves download/share data and valid
hash/queue/settings/Torrent snapshots. No consent, Hide, canceled confirmation
and owned-child SIGKILL before commitment keep the exact old bundle. The abnormal
case does not assert final persistence. Normally returning cases retain the
profile IPC lease at quit and through GUI destruction.

This is development-host full-app lifecycle acceptance, not production native
selection, live routed HTTPS/provider DNS, strict helper isolation or minimum-OS
proof. Verified runtime identity/scheduler, settings/menu/notification ownership,
OS logout/additional lifecycle coverage and remaining release gates stay pending.
