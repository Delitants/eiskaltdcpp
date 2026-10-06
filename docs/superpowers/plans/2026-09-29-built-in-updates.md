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

Signed physical-route checkpoint (2026-10-04): actual native Sparkle discovery
and Eiskalt-owned Ed25519/digest staging now run through authenticated loopback
SOCKS5 and GOST relays with external destinations denied. Production URL/DNS
admission, immutable global route and strict HTTPS remain intact; only disposable
CA roots and responses are injected. Cases cover approved redirects, verified
provider outage, failed proxy/auth/trust, tampered feed/archive, invalid archive
signature with matching digest, inert image notes, refused external notes,
cancelled offer and revocation before readiness. Archive bytes are signed staging
fixtures, not installable release bundles; no installer is started.

Read-only live diagnostics also verified Cloudflare and Google A/AAAA DoH answers
through the saved global GOST route using production strict HTTPS and bounded DNS
parsing. Direct HTTPS/DNS destinations were denied; the macOS sandbox constrained
the proxy port and the helper admitted only the saved numeric proxy endpoint.
No live profile, installed app, server or production signing key was changed.
This is live provider proof, not live signed feed/archive or release acceptance.
Native scheduler/installation activation remains disabled until those remaining
routing, lifecycle and release gates pass.

Native component paint fixtures explicitly establish Qt logical activation before
comparing idle and keyboard-focus images, so a background Cocoa process does not
invalidate the component setup. Focus and pixel assertions remain intact. This
does not prove OS keyboard-focus or notification delivery.

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

Application UI ownership checkpoint (2026-10-03): after the retained native and
committed-quit fixture gates, bind Preferences and Help to the application-owned
controller without enabling production native capability or its scheduler.
Append Updates after all existing pages in both DC-only and Torrent builds;
standalone Preferences has an explicitly unsupported fallback, not another
native factory. Apply preferences only on dialog acceptance. Borrowed controller
bindings expire on teardown and reject duplicate, foreign-thread or fenced
attachment. Manual invalid-runtime checks report Unsupported asynchronously
without network or attempt/success writes. Manual errors enter red status and
System logs using category-redacted text; automatic errors are diagnostic-only.
Automatic offers use the existing Notification dispatch and therefore inherit
global/OS notification suppression. Dispatch evidence is not OS balloon delivery.
Current native routing/activation, end-to-end update notification delivery,
independent linguistic review and release/minimum-OS acceptance remain pending.

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

### Installed Runtime Identity Checkpoint

The application owner now derives identity only from structurally validated
current-main-bundle metadata. The reader binds canonical executable provenance
and the validator binds product ID, canonical numeric build, SemVer channel,
compiled CPU/runtime OS and approved flavor floors. Maintained matching-CPU feed,
canonical nonzero public key, strict typed native trust switches and local-only
ATS are mandatory. CF conversion preserves embedded NULs; booleans, integers and
strings are distinct. No path/environment/profile override is introduced.

Portable adversarial tests and a small native probe cover actual CF XML/binary
plists and executable aliases. The fixture-only applicationVersion override
bypasses Qt's pre-reader coercion of malformed CFBundleVersion; no product startup
repair is claimed. Structural eligibility is not signature/vendor authenticity,
dependency/minimum-OS acceptance or live routing proof. The production backend
is still unsupported with no scheduler or native capability. Next is gated native
factory/config consumption and scheduler/UI ownership after routing acceptance.

### Native Factory And Configuration Binding Checkpoint

The native backend factory now admits only structurally validated current-main-
bundle metadata, otherwise returning an asynchronous unsupported backend before
route selection. Native construction also refuses missing runtime metadata. The
factory owns an unparented backend and starts no scheduler or installer.

The admitted feed (including its revision), signing key and installed identity
are copied as an immutable snapshot. Discovery checks the host bundle key,
build, product and compiled CPU against that snapshot before Sparkle; feed,
archive and cached-stage verification use the same pinned key. Private fixture
constructors remain separate from the public current-bundle path. Production
ApplicationUpdateOwner still selects the unsupported backend: capability,
scheduler, settings/menu/notification integration and live proxy/provider-DNS/
HTTPS acceptance remain gates, not consequences of factory eligibility.

Native tests observe the exact revision-bearing request, mutate the caller's
copy without changing admitted behavior, refuse mismatched key/build/product,
and exercise both valid actual-bundle and non-bundled factory selection.
These disposable development-host checks do not establish vendor authenticity,
release minimum-OS compatibility, live routing or production installation.

### Correlated Native Selection Checkpoint

The factory returns a move-only backend and controller identity from one
validated current-bundle snapshot. The application owner consumes this pair;
it does not independently reread installed identity. An explicit
`EISKALT_MAC_UPDATER_NATIVE` build option defaults OFF and requires macOS,
validated updater metadata and audited framework packaging. Opting in compiles
the real adapters with separate per-target MOC outputs, but does not start the
scheduler or enable installation capability.

Packaging validates a private prospective bundle containing the exact audited
Sparkle stage before modifying the owned build output. This permits an executable
already linked to Sparkle without skipping dependency-closure validation. Other
missing dependencies still reject the input before mutation; the final sealed
bundle receives the same metadata, closure and strict-signature checks.
Bundle executables are deferred to their container signing pass so standalone
main verification cannot run ahead of repairs to invalidated Qt plugin signatures.
Executable bindings are confined and validated on the prospective copy first.

Route-aware DNS remains the next activation gate: Cloudflare DNS over HTTPS
primary and Google DNS over HTTPS fallback, both over the same global proxy.
No direct fallback, OS DNS change or security-validation bypass is permitted.
Development-host selection/packaging QA is not supported-minimum-OS evidence,
live proxy acceptance or permission to enable automatic installation.

### 2026-10-04 Routed DNS Checkpoint

`UpdateDns` now supplies the worker's proxied origin resolution through the same
strict synchronous HTTP backend and immutable global route. Cloudflare DoH is
primary, Google DoH fallback for validated SERVFAIL/REFUSED and verified,
fully framed HTTP 500/502/503/504 provider responses. Provider-host
TLS is independent of proxy TLS trust; fixed numeric bootstrap, DNS wire GET,
A+AAAA question/CNAME/full-address validation, shared bounded deadline, fixed
request/response MIME and no provider redirects/cache prevent local-origin DNS
or direct retry. Typed outage reporting is strict-DNS-only; error bodies are
bounded and discarded, partial primary answers discarded, and the same snapshot/
deadline retained. Generic transport failures remain terminal so authentication
or certificate failure cannot silently select another provider. Numeric SOCKS
stream targets encode binary IPv4/IPv6 before hostname
branches; hostname resolution behavior is otherwise unchanged.

Isolated authenticated SOCKS5/GOST nested-TLS tests exercise actual DNS HTTP
bytes, provider failover, wrong auth/proxy CA/origin CA/hostname/MIME, cancellation,
route revocation and an IPv6 pin with all external network access denied.
Production worker tests bind the same backend/route to DNS and the origin fetch.
Physical GOST worker tests retain production preparation and strict HTTP handling
for feed/archive bytes and approved archive redirects, with loopback-only relays;
private DNS, unapproved redirects and origin trust failures cannot commit output.
This does not establish live provider/feed/archive or signed native whole-route
acceptance. Scheduler/install capability remain disabled; Task 3 remains open.
The follow-up native hover checkpoint traced the tab-close hover-image failure
to a flat native `Midlight` colour equal to the tab surface, not a lost mouse
flag. Close buttons now use a low-alpha `ButtonText` tint for hover, preserving
the pressed highlight, transparent idle surface and keyboard focus ring. A new
flat-palette regression reproduces the previous failure on Cocoa and offscreen;
all 468 combined Cocoa/native Qt cases now pass. This resolves that focused
rendering failure, not the remaining live routing or installation gates.

### 2026-10-04 Live Transport And Torrent Regression Checkpoint

The production update transport fetched a 3,886,463-byte legacy release archive
through the saved global GOST route, routed DoH, strict HTTPS and approved GitHub
redirects. Its SHA256 matched independently retrieved GitHub asset metadata. Both
fixed v3 feeds remain unpublished and were refused without staged output. The
legacy archive was not mounted, executed or installed; this is transport evidence,
not v3 Ed25519 verification or signed-release acceptance. Native activation and
installation capability remain disabled.

The current Torrent Qt target rebuilt successfully. The alternate-source UDP
case passed 80 assertions, and the broad offline suite passed 609 cases with
33,362 assertions using the original failing order seed 1053519413. All eight
opt-in real-GOST cases passed 448 assertions against a checksum-verified upstream
3.3.0 executable, disposable authentication/CA and guarded loopback services.
These cover peer TCP/uTP, tracker/DHT, DNS, reconnect and separate global-core
traffic. They are protocol/runtime QA, not Cocoa UI delivery or production
Torrent acceptance. The old intermittent bind failure did not reproduce; its
root cause remains unproven, not fixed.

The second previously failing order seed 2081490689 also passed all 609 cases
(33,351 assertions). Neither replay reproduced the old bind failure.

The GOST instructions now select `[interop]`, including the eighth global-core
case omitted by `[gost][interop]`. An already sandboxed coordinator cannot apply
the child Seatbelt profiles on macOS; that setup failure is distinct from proxy
failure. The documented private coordinator retains the child restrictions and
direct-connection rejection assertions. No live profile, installed application,
server, production signing key or VM was changed.

Remaining gates: live v3 signed-feed/archive acceptance and guarded activation;
lifecycle/logout/notification/DC-only runtime QA and the unproven Torrent bind
root cause; thin Intel/macOS 14 and Apple Silicon/macOS 26 release/minimum-OS QA
and independent linguistic/whole-branch review; closed-app installation,
README/wiki/screenshots and pre.1 publication. Signed feeds remain last.

### 2026-10-04 Native Termination And DC-Only Entrypoint Checkpoint

The disposable actual-main fixture now calls AppKit's `NSApplication` termination
API without replacing Qt's delegate or synthesizing a Qt Quit event. Receipts
require real Quit delivery, native-request return before checked preparation,
normal main return and retained IPC ownership through GUI/core teardown. Native
confirmation refusal also proves that the event loop and borrowed owners remain
alive before a later consent-revoked cleanup quit.

The signed full-application matrix now includes seven modes: normal quit, no
consent, Hide, canceled confirmation, owned precommit kill, native termination
and canceled native termination. Confirmed ordinary/native quit require the exact
signed new bundle; the other modes require the exact old bundle. No relaunch and
unchanged protected fixture data remain mandatory. The production updater remains
inactive; the signed bytes, credentials and signing identities are disposable.

The fixture also supports Torrent-disabled applications without seeding Torrent
settings. A compiled flavor receipt must match the actual CMake cache. All three
ordinary entrypoint modes (window, Qt Quit and AppKit termination) passed in both
DC-only and Torrent builds, with valid DC/GUI snapshots and unchanged protected
bytes. DC-only profiles contain no Torrent settings/state; Torrent-enabled ones
retain the valid empty job checkpoint. The focused Cocoa quit-composition suite
passed 12 cases with 446 assertions.

Only acceptance fixtures and documentation changed. The old fixture's unguarded
TorrentRuntime reference was a DC-only compile limitation, not a product failure.
Native Cocoa callback acceptance is not actual OS logout/shutdown, OS notification
delivery, live DC transfers, production update activation or minimum-OS proof.
Development dependencies still carry newer-OS linker warnings. These remaining
gates, the unproven intermittent Torrent bind cause, independent review and final
thin-build/install/publication work remain open; signed feeds remain last.

### 2026-10-04 Queued Notification Delivery Checkpoint

An actual controller/MainWindow/Notification integration test, replacing only
the platform notification module with a recorder, exposed four late-delivery
failures: disabling updater notices, destroying the controller, fencing its
workers for quit, and shutting it down after dispatch still delivered the queued
notice. A further review regression also reproduced late delivery after Later,
Skip and Cancel. The existing GUI quit fence already suppressed queued delivery.

Updater notices now carry their offer identity on a dedicated queued signal. The
final notification slot checks the borrowed controller's lifetime, GUI/controller
quit fences, current notification preference, matching offer and deferred/skipped
policy before using the existing Other-event category. The global enabled flag,
text suppression, activity/visibility filters,
Other-event mask and sound policy are unchanged. Generic Other notifications
remain independent of updater ownership. This does not reset user filters or
turn on the system tray, updater scheduler or native installation capability.

The final Cocoa GUI suite passed 81 cases with 5,039 assertions. Current-source
updater/widget tests passed 113 cases in each build: 6,695 assertions with Torrent
and 6,694 without. Ordinary window quit and AppKit termination passed in both
current-source applications, preserving the expected checkpoints and protected
fixture data. Signed disposable normal quit installed the exact verified bundle;
canceled native termination retained the exact old bundle. Both rejected tampered
feed/archive bytes, preserved data and required no relaunch. Nine protected live
file hashes remained unchanged. These are bounded author-reviewed receipts, not
independent release review or minimum-OS acceptance.

The recording-module receipt is not an operating-system banner receipt. Actual
OS notification delivery, live signed v3 release acceptance, guarded activation,
logout/live-transfer and release/minimum-OS gates remain open. The installed
application and live profile are not changed by this checkpoint.

### 2026-10-04 Current Thin Dependency-Consumption Checkpoint

Fresh Intel/macOS14 and Apple Silicon/macOS26 application bundles now compile
from the current notification-fix source, with the maintained selected-file
recheck provider required in both configurations. These are private dependency
acceptance candidates: native updater activation, trust metadata and Sparkle
packaging remain OFF. Their numeric internal build is a QA value, not a production
allocation; these bundles must not be used as final updater-enabled release assets.

The ARM app consumes the controlled static c-ares and MaxMind prefix. Its final
bundle passed strict signatures, architecture/deployment checks across 84 images,
703 dependency-closure edges and a selected-recheck ABI1 copied-bundle probe on
the macOS27 Apple Silicon host. Intel's copied universal Qt payloads were thinned
without modifying the input prefix; its final bundle passed strict signatures,
static ABI/import checks across 40 images and 420 dependency-closure edges.

Actual linked-provider regressions passed 17 cases with 586 assertions per flavor
using temporary profiles and loopback-only networking. ARM execution was native;
Intel execution was through Rosetta, not native Intel acceptance. Loader receipts
confirm the private providers. Copied recheck-provider and patched Cocoa executable
sections match their inputs in both bundles. Before/after tracked-source and
recorded private dependency file-content checks passed, as did nine protected live
file hashes. Complete SDK/header/symlink provenance is not inferred from these
bounded inventories.

Two private runner failures were retained: macOS Bash3.2 rejected an empty-array
expansion before auditing, and the standalone ARM test initially resolved stock
Homebrew libtorrent before running cases. Corrected wrapper arguments and explicit
post-sandbox test-loader inputs produced the receipts above; neither is claimed as
a product bundle fix. No live app launch, profile change, installation, signing-key
operation, push or publication occurred.

Tasks 7/8 remain open for final updater-enabled builds, matching complete materials,
full suites, independent/linguistic review, native minimum-OS/platform and actual
OS/live-transfer acceptance. Live signed v3 acceptance and guarded activation also
remain open. Safe installation and README/wiki/screenshots/prerelease publication
follow those gates; signed feeds remain last.

### 2026-10-04 Updater-Enabled Split Packaging Checkpoint

Fresh, unlaunched Intel and Apple Silicon QA app candidates now compile with
native updater, trust metadata, audited Sparkle packaging and the maintained
selected-file recheck provider required. Their exact source snapshot is
`e325cb6cd`; numeric build 1 remains private QA, not a production allocation.
Only the recorded production PUBLIC key was used; no private signing-key
operation, network update check, installation or publication occurred.

The final ARM bundle passes strict signatures and architecture/deployment checks
across 89 thin images with macOS declarations <=26.0 and 748 closure edges. Intel
passes across 45 thin images with declarations <=14.0 and 465 closure edges. The
ARM copied-bundle tiny ABI probe returns 1 on the native macOS27 host; Intel's ABI
and extension references are statically verified, not native Intel acceptance.
Actual app-target compile/import and trust-metadata readbacks match both profiles.

Both preserve exact signed Sparkle stages and patched Cocoa while staging matching
package-level notices (ARM: 39 packages/2697 files; Intel: 17 packages/502 files) and
resealing only the enclosing app. Fresh project source and controlled ARM static
build receipts accompany the current image/source mapping. All dependency code/data
sections match retained provider mappings; this is not reproducible-build proof.
The current input archive passes byte-exact readback across 2983 files. Package-level
notices remain conservatively inclusive; resource/file-level attribution is open.

Both private QA ZIPs pass extraction, strict signatures, closure and checksum
readback, each below the feed asset limit. Focused regressions pass 24 tests with
no skips: metadata 9, native wiring 3, packaging 12. Restricted-cache Xcode failures
were retained and resolved by running isolated fixtures with normal build-cache
access. Incorrect private wrapper path/SDK/fixture selections were preserved and
corrected without weakening guards or changing product code. The dummy-key plugin
fixture is never executed and proves packaging only, not runtime trust identity.
Tracked-source, captured input/recipe and nine protected live-file hashes pass.
Fresh-context read-only checkpoint review found no actionable findings and checked
all mapped image/notice hashes, both strict signatures and archive checksums. This
review does not replace whole-branch release acceptance.

This advances the packaging/materials subset, not completion of Tasks 7/8. Full
current suites, whole-branch/linguistic review, native minimum-OS/platform and actual
OS/live-transfer acceptance remain open. Final reviewed-source rebuild/materials,
production numeric allocation, signed v3 acceptance and guarded activation precede
safe installation and README/wiki/screenshots/prerelease publication. Signed feeds
remain last, after accepted exact public assets. No live profile, installed app,
dependency prefix, production key, VM or server was changed.

### 2026-10-04 Current Host Suite Checkpoint

Fresh test-only builds use the matching verified ARM and thin Intel dependencies,
native updater configuration and required selected-recheck provider. Both profiles
pass core 1023 cases, local Qt 609, real GOST interoperability 8, Torrent engine
152, GUI multiline/singleline 81 each, and sidebar-applicable GUI 79, without
skips in those runs. GOST uses its documented disposable coordinator and confined
children; the other suites use isolated profiles and external-network denial.
Actual loader receipts confirm private providers and matching Qt. ARM is native
on macOS27; x86_64 is Rosetta, not native Intel or minimum-OS acceptance.

The expanded Torrent suite takes 235-237 seconds, longer than its former aggregate
CTest limit of 180 seconds. That registration now permits 600 seconds, without
changing individual fixture deadlines, responsiveness assertions or product code.
The generated-budget check fails against the original configuration and passes
both final profiles. Actual ARM CTest execution passes in 234.27 seconds.

Two failures from an overbroad sidebar probe are retained: tab-strip-only cases
expect controls absent by design in sidebar mode. They pass in their registered
multiline/singleline modes; sidebar's applicable 79 cases pass. Private harness
launch, parser and shell bookkeeping errors are separately retained and corrected,
not claimed as product fixes. Fresh bounded patch/scope review finds no actionable
findings, but does not replace whole-branch release review.

Final readbacks preserve all134 candidate images/3199 notices, both strict seals
and archives, 12591 recorded dependency contents and nine protected live hashes.
The accepted packaging candidates remain immutable at source e325cb6cd; this
checkpoint does not allocate a production build or produce a new release asset.

Remaining: whole-branch/linguistic and resource/provenance review, native/platform
and minimum-OS acceptance, separately scoped native/live lifecycle gates, final
reviewed-source rebuild/material refresh, production signed-v3 acceptance and
guarded activation. Safe installation, README/wiki/screenshots and pre-release
publication follow those gates. Signed feeds remain last. No live app/profile,
production key, server, VM, installation, push or publication was changed here.

### 2026-10-04 Current Resource Attribution Checkpoint

Read-only mapping of both accepted updater-enabled QA candidates records all1215
copied project resources per CPU and694 app-compiled QRC inputs. Current TS bytes
and retained QM/MO build outputs are distinguished from the earlier historical
catalog proof. External default QRC83 inputs and the separate test-only declaration
are not counted as app-compiled resources. Dependency-framework resources retain
their earlier package-level mapping; this is not new file-level proof for them.

The inherited copyright inventory incorrectly applied legacy LGPL to new
Phosphor assets. A scoped MIT/Expat override now covers688 generated SVGs, eight
notice/attribution files,240 pinned vendor SVGs and the vendor notice. A regression
first reproduces the incorrect classification, then passes with the correction;
the full14-test icon suite passes. Legacy Faenza/Haiku/raster license declarations,
the generator and provenance metadata are not relicensed. Exact MIT notices already
present in the accepted bundles remain untouched; final rebuilding must refresh
their older aggregate inventory.

Country/language flags use platform-font glyphs; neither audited bundle contains
font payloads outside retained notice documents.440 copied legacy icon files per
CPU still lack original upstream file-level author/revision evidence. Current-byte
mapping and aggregate notices are not legal or reproducible-build certification.
Complete resource/dependency attribution, whole-branch/linguistic review and native
platform/minimum-OS acceptance therefore remain open. Final reviewed-source build,
production numeric/signature acceptance and guarded activation precede installation
and README/wiki/screenshots/prerelease publication; signed feeds remain last.

### 2026-10-04 Independent Pending-Release Review

Fresh read-only domain reviews cover the pending local delta from cached branch
base18ac3795 to790ee402b:97commits/305assigned paths. Core/network, updater/release
and Torrent/GUI production paths and contracts were traced independently; test
fixtures received focused, not exhaustive, inspection. Current catalog checks
pass18tests, and13relative documentation links resolve in that frozen source.
Native-speaker, runtime and minimum-OS acceptance are not implied.

Six concrete source findings remain open. First repair is a P1 maintained-engine
part-file mapping loss after persistent metadata flush failure, across all three
storage backends. Five P2 findings concern folder-download intent lost at shutdown,
integrity failures incorrectly latched as physical flush failures, unbounded GUI
worker joins, absent checkpoint-refusal recovery, and blocking filesystem probes
during Torrent selection. Each needs a failing regression, bounded repair and
applicable re-review; this checkpoint does not claim runtime reproduction or fixes.

The review is complete with findings, not approval to publish. Existing QA apps,
archives, providers and live profile remain unchanged. Start with part-file
durability in separately owned providers, update the dependency pin and refresh
final materials only after verification. Remaining attribution, native/platform,
production signing, installation and documentation/publication gates still apply.
Signed feeds remain last; no release-ready claim follows from earlier passing tests.

### 2026-10-04 Part-File Durability Repair Checkpoint

The maintained engine retains dirty part-file slot maps when metadata saves fail
on mmap, pread and POSIX storage. Selected preparation now receives the native
storage error through a stronger disk fence, stays paused and admits no hashes.
Successful saves still reload persisted maps. The POSIX path checks buffered
metadata flushes before marking the map clean, and native open failures retain
their original error code. Custom disk backends reject preparation unless they
implement the stronger fence. The private extension is explicitly ABI2; existing
ABI1 release candidates remain immutable and final applications must be rebuilt.

Native fault regressions first failed on permission denial, EIO and ENOSPC across
all three backends, including buffered POSIX writes. Fresh matching ARM and Intel
providers pass630 fault assertions and the full19-case/1234-assertion dependency
suite, also through the public test target. ARM's152-case/18430-assertion engine
suite and20 bundle-policy tests pass. All1653 staged source files match the new
patch pin, and loader readbacks confirm matching providers and test-only injection.

Intel aggregate acceptance is NOT clean: two152-case runs each had one failure,
first the stopped-local-check1500ms deadline and then large-scope checked shutdown.
The stopped case passed three isolated repetitions; both affected cases passed
two isolated comparisons each with the accepted and repaired providers, unchanged
deadlines. The full-run failures are retained, not waived or attributed to the
repair without evidence. Rosetta execution and declared macOS14/26 floors are
not native Intel or minimum-OS acceptance. This is a verified native data-loss
repair checkpoint, not whole-application release approval.

The next folder-intent regression reproduces lost full and partial list requests
after producer-fenced completion, queue snapshot and restart. It remains unfixed;
the repair must persist intent without reopening shutdown producer admission.
The other four reviewed P2 repairs, Intel aggregate investigation, linguistic and
resource/provenance review, native/platform acceptance, final-source rebuilding,
production signing/activation, safe installation and README/wiki/screenshots/
prerelease publication remain pending. Protected live records, accepted providers,
candidate images/notices, strict seals and archives are unchanged. No installation,
push, publication, key operation, server or VM mutation occurred. Signed feeds last.

### 2026-10-04 Folder-Intent Persistence Repair Checkpoint

Pending folder downloads now persist independently of transient file-list queue
items and the Keep file lists setting. Optional escaped Directory records retain
source CID, hub hint, remote folder, destination and requested priority. Startup
restores and deduplicates valid requests, then requests their file lists within
ordinary producer admission. Shutdown admission remains closed; no new connection
or directory expansion is authorized by this persistence change. Consumed intent
marks the queue dirty, and teardown releases remaining owned requests. Existing
file-expansion priority policy is unchanged; the saved request retains its value.

A real full/partial list-completion regression first failed four assertions after
producer closure, checked snapshot and reload. An additional regression caught
an overly strict one-letter-folder guard before committing. Fresh consistent
DC-only core builds pass88 assertions in3 folder tests and738 assertions in48
snapshot/passive-queue tests on both architectures. Full non-hidden core suites
pass1026cases,150402assertions on ARM and150403 under Intel/Rosetta, with no skips.
Coverage includes both file-list retention settings, resumed folder expansion,
escaped destinations, retained hub hints/priority, duplicate/malformed records,
short names and refused loading after the shutdown fence. The initial RED reused
unchanged baseline objects read-only; final builds do not mix old DirectoryItem
layouts with new headers. This is host core verification, not complete app or
minimum-OS/native Intel acceptance.

Four reviewed P2 repairs remain: integrity-vs-physical-flush error classification,
bounded GUI worker retirement, safe checkpoint-refusal recovery, and asynchronous
Torrent location probes. The retained Intel aggregate Torrent failures from the
prior checkpoint also remain unresolved. Linguistic/resource/provenance review,
native/platform acceptance, final-source application/material rebuilding,
production signing/activation, safe installation and documentation/prerelease
publication still follow. All protected live records and accepted dependency,
candidate-image/notice, seal and archive contents remain unchanged. No push,
installation, signing-key operation, server or VM change. Signed feeds remain last.

### 2026-10-04 Integrity Error Classification Repair Checkpoint

TTH mismatches now have a typed, FileException-compatible integrity error. Download
completion and disconnect cleanup reset the corrupt current segment without
setting the permanent physical-flush veto. Previously verified queue segments
remain intact. The Merkle stream explicitly checks its underlying storage even
when rejecting a hash: an actual write or durability failure takes precedence and
still blocks checked shutdown, including after a later successful transfer. No
latched physical failure is cleared, and no translated error-string matching is
used. Producer admission, cleanup leases and ordinary retry policy are unchanged.

Real completion/disconnect regressions first failed 6 of 48 assertions: valid retry
still vetoed shutdown, and corruption hid a read-only File write until teardown.
A separate registered-peer Data regression first failed checked core preparation
after successful retry. With the repair, both consistent DC-only architectures
pass 68 assertions in 3 focused cases, 1000 in 104 related cleanup/stream/Merkle/
snapshot/core-owner cases, and all 1029 non-hidden core tests. Full suites report
150471 assertions on ARM and 150473 under Intel/Rosetta, with no skips. Coverage
includes actual recovered target contents, physical-error veto retention, checked
core snapshots and disposal. This is host core verification, not installed-app,
native Intel or minimum-OS acceptance. Original RED and intermediate evidence is
retained; final builds use matching current headers and sources.

Three reviewed source repairs remain: bounded asynchronous GUI worker retirement,
safe recovery from a refused checkpoint, and nonblocking Torrent location probes.
The prior Intel aggregate Torrent failures, linguistic/resource/provenance review,
native/platform acceptance, final-source application/material rebuilding,
production signing/activation, safe installation and documentation/prerelease
publication also remain pending. Protected live records and accepted dependencies,
candidate images/notices, strict seals and archives remain unchanged. No push,
installation, signing-key operation, server or VM mutation. Signed feeds last.

### 2026-10-05 Asynchronous GUI Worker Retirement Checkpoint

Checked quit now polls retained GUI threads and futures without blocking their
owner event loop. An independently owned quit-session timer bounds this worker
phase. Pending work prevents core retirement, process exit and update commitment;
timeout retains owners and permits only an explicit worker-phase quit retry.
Late completion, including completion before a delayed timer delivery, does not
grant installation consent. Physical thread receipts include thread-local teardown.
Queued input, results and deferred owner deletion remain fenced while an unrelated
heartbeat can run. Proxy probes, file hashers and separately owned hub/PM shell
workers participate. Parser reads/rows and ADL traversal cooperate with cancellation;
cancelled provisional ADL trees are discarded without publishing partial results.

Real held-worker/timeout regressions first failed 19 of 100 assertions against the
unchanged baseline; parser/ADL cancellation failed 7 of 9, and pre-interrupted
runner cancellation failed 1 of 2. Both fresh matching-ABI2 architectures now pass
the focused parser/ADL suite (32 cases, 276 assertions), proxy suite (23, 588) and
GUI quit suite (29, 881), with no skips. Full core suites pass 1032 cases with
150482 assertions on ARM and 150483 under Intel/Rosetta. Both Torrent suites pass
152 cases/18430 assertions. All three applicable GUI layouts pass on both CPUs:
multiline 85/5227, single-line 85/5319, sidebar 83/5133 (cases/assertions).

The first full ARM Qt run failed 8 assertions in 6 updater-callback cases. Its
complete negative log is retained. Reduced tests, an owned tracing comparison and
unchanged-baseline/current prefix controls passed. The final unmodified full ARM
repeat with the original failing seed passes 609 cases/33571 assertions; the full
Intel Qt suite passes 609/33562. These passes do not explain or waive the initial
aggregate failure. Prior intermittent Intel Torrent aggregate failures likewise
remain unresolved despite this passing run. No test deadline was relaxed.

This checkpoint covers GUI-owned worker retirement, not a hard deadline for every
quit operation. Native updater transport/verification still uses its existing
synchronous checked fence; core/checkpoint/filesystem I/O can still block. Ordinary
manual probe/hasher destruction retains safety joins. Host offscreen/Rosetta tests
are not native Intel, minimum-OS, live notification/logout or installed-app proof.

Next reviewed source repairs: safe recovery from a refused checkpoint, then
nonblocking Torrent location probes. Aggregate-failure investigation and broader
native retirement responsiveness, linguistic/resource/provenance/reproducibility
review, platform/live acceptance and final whole-branch review remain open.
Final-source application/material rebuilding, production numeric allocation and
signing/activation, safe installation/readback, README/wiki/approved unredacted
screenshots and prerelease publication still follow. Protected live records,
accepted dependencies and prior candidate images/notices/seals/archives are
unchanged. No push, installation, key operation, server or VM mutation. Signed
feeds remain last; Tasks 7/8 are not complete.

### 2026-10-05 Retained Auxiliary Checkpoint Recovery

An explicit quit retry or an independently owned, modeless "Retry Save and Quit"
button can now recover a failed search-blacklist or anti-spam snapshot. The main
window stays fenced and disabled; removing an obstruction alone does not advance
quit. Retries write only frozen intent, retain the original owners and reuse
successful primary GUI, Torrent, core, settings-sync and updater-fence receipts.
They do not restart producers, replay primary shutdown or provide an unsaved exit.
Foreign/modal callers and lost or replaced snapshot owners cannot certify Ready.

The coordinator, not merely the recovery dialog, permanently disarms update
consent before admitting this recovery. Disarm is limited to the pre-handoff phase;
a void cancellation request is not a terminal native-installer receipt. A failed
or unknown disarm remains blocked. Recovered quit can only select NoUpdate. The
separate worker-timeout retry and ordinary matching updater handoff remain intact.
Core, Torrent, physical-durability, primary GUI/settings-sync and native-installer
failures intentionally remain terminal: their existing receipts cannot establish
safe retry scope. This repairs the reviewed auxiliary-save obstruction, not every
possible refused checkpoint or a total filesystem/native quit deadline.

The unchanged baseline failed 11 of 174 assertions in five focused cases. Two
additional coordinator/foreign-thread regressions failed three of 259 assertions
before their guards were repaired. Both fresh, matching-ABI2 architecture builds
pass the focused quit suite (20 cases/799 assertions). Full ARM core passes
1032/150486, Qt 609/33569 and Torrent 152/18430; Intel/Rosetta passes 1032/150483,
609/33626 and 152/18430 respectively. Both architectures pass all applicable GUI
layouts: multiline 90/5397, single-line 90/5489 and sidebar 88/5303. No skips.
An owned coordinator copy without the retained-owner guard fails exactly one of
14 assertions; the unmodified single case passes all 14. This extra guard check
does not replace independent whole-branch review.

Next reviewed source repair: nonblocking Torrent location probes. Earlier
intermittent Qt/Intel Torrent aggregate failures remain unexplained and are not
waived by this passing run. New recovery strings still need catalog coverage and
native-language/control acceptance. Broader native retirement responsiveness,
linguistic/resource/provenance/reproducibility review, native minimum-OS/platform
and live acceptance, final whole-branch review and final-source application/material
rebuilding remain open. Production numeric allocation, signing/signed-v3 acceptance
and guarded activation, safe closed-app installation/readback, README/wiki/approved
unredacted screenshots and prerelease publication still follow. Protected live
records, accepted dependencies and prior candidates remain unchanged. No push,
installation, private-key, server, VM or Docker change. Signed feeds remain last;
Tasks 7/8 are not complete.

### 2026-10-05 Staged Nonblocking Torrent Location Probes

Torrent selection and context-menu refresh now admit the actual save directory
lexically, without querying filesystem availability. Explicit Open probes the
directory on a worker using only copied path/dependency values. A future watcher
on the stable top-level window retains pending work after its requesting child
disappears, integrating with the existing checked-quit future fence. One request
per requester is admitted; stale job/path/engine/attachment, lost or disabled
owners and foreign-thread admission cannot open Finder or show a late error.
Closing the retained singleton invalidates its request generation, including a
close/reopen before completion. No Torrent activity, priorities or storage paths
are changed. Existing translated titles/errors are reused.

The corrected frozen baseline fails exactly eight of 42 Qt assertions in four
cases and three of 37 GUI assertions in three cases. The initial missing-folder
fixture had overlooked metadata admission creating an empty directory; that run
is retained but not credited as semantic RED evidence. Before the close-generation
guard, a held probe reproduced exactly two late opens in 69 assertions: hidden
and hidden/reopened tabs. Both fresh, matching-ABI2 architecture builds pass the
focused repair checks: Qt five cases/131 assertions and GUI three cases/61
assertions each. These are isolated offscreen/Intel-Rosetta checks with held real
read-only probes and intercepted file URLs, not native Finder or a slow mounted
filesystem acceptance test.

Full ARM core passes 1032 cases/150482 assertions. GUI multiline passes 92/5437,
single-line 92/5529 and applicable sidebar 90/5343. Full Qt is NOT green: 612 of
613 cases pass, with one initial publication deadline failure in the runtime
stop/resume preservation case (Rechecking 0.0%, no publication or runtime error;
seed 3982279739). Full Torrent is NOT green: 151 of 152 cases pass, with one
initial SOCKS5-bootstrap connection deadline failure (seed 1446322059). Same-seed
isolated controls pass on current and unchanged 2a8bc4866 binaries, but do not
explain or waive either aggregate failure. No deadline was relaxed. New location
cases precede runtime cases in Qt linker order, so absence of interference has
not been established by a passing isolated control. Full Intel regression and
same-seed aggregate/prefix controls remain pending; earlier intermittent failures
also remain open. This is a working-source checkpoint, not repair acceptance or
a release-ready verdict. No commit, installation or publication at this stage.

Next: investigate retained aggregate failures and complete cross-architecture
regression, then fresh whole-branch review. Broader native quit responsiveness,
catalog/native-language, resource/dependency attribution, provenance and
reproducibility work, native Intel/minimum-OS and live notification/logout/DC/
Torrent/benchmark acceptance remain. Final reviewed source/provider/application/
material rebuilding, production numeric allocation, signing/signed-v3 acceptance,
guarded activation and closed-app installation/readback still follow. README,
wiki, approved unredacted screenshots, prerelease assets and public readback remain
pending. Signed feeds remain last; Tasks 7/8 are not complete.

### 2026-10-05 Cross-Architecture And Fresh Review Checkpoint

The staged folder-probe repair now has full Intel/Rosetta regression: core
1032/150482, Qt 613/33662, Torrent 152/18430, GUI multiline 92/5437,
single-line 92/5529 and applicable sidebar 90/5343 all pass without skips.
Current ARM full-order controls at the original failure seeds also pass:
Qt 613/33869 and Torrent 152/18430. These do not waive the original failures.
The unchanged Qt baseline instead reproduced one listener-bind failure in the
source-seed unchanged-settings case; its original stop/resume case passed.
Both unchanged/current backend controls passed. A diagnostic-only traced baseline
Qt run passed without reproducing the runtime listener failure. Port selection
uses Qt randomness independently of Catch seeds; no cause is established and no
deadline or production networking policy was relaxed.

A fresh read-only reviewer inspected the frozen 312-path committed release delta
plus the working repair. The prior six source findings were not reissued. Two
Important/P2 findings remain: native updater transport/verification retirement
still performs blocking GUI-thread joins, and the mandatory ABI2 configure probe
compiles but does not execute its library ABI comparison. The latter must reject
ABI2 headers paired with an ABI1 binary, retaining independent runtime/package
checks and an explicit cross-compilation policy. Both need behavioral regression
evidence and repairs. Tests/catalogs received focused, not exhaustive inspection.
The verdict is with fixes, not whole-source acceptance or release approval.

Protected profiles, installed application, dependencies and prior packaging
candidates remain unchanged. This is a local bounded source checkpoint only;
no installation, push, signing, activation or publication. Retained aggregate
failure causes, broader native quit responsiveness, linguistic/resource/
provenance/reproducibility and native/minimum-OS/live acceptance remain open.
Final reviewed rebuilding/material refresh, numeric allocation/signing/signed-v3
acceptance, guarded activation/install, README/wiki/unredacted screenshots and
prerelease public readback still follow. Signed feeds last; Tasks 7/8 remain open.

### 2026-10-05 ABI2 Configure Admission Checkpoint

The reviewed configure-admission finding is repaired locally. The probe now
executes the linked library's ABI marker after checking the ABI-2 header, rather
than merely compiling its return comparison. Probe/run caches are cleared on
every configure. Independent application-runtime and bundle ABI checks remain
unchanged; the maintained provider patch and ABI were not changed.

Cross-builds require an explicit runnable emulator to enable selected rechecking.
Without one, optional builds leave the feature unavailable and required builds
refuse configuration. Preseeded success cannot replace the actual run. Failed
execution is normalized in both normal and cache variables, including the newer
CMake policy under which a normal `FAILED_TO_RUN` value can shadow cached zero.
This fail-closed policy costs cross-builders an explicit working runner; it does
not claim native minimum-OS acceptance or validate arbitrary untrusted providers.

Real Mach-O fixtures first reproduced exactly ten intended failures in 33 cases
against the unchanged compile-only check, with no unrelated errors. Aligning the
fixture with the application's CMake policy range exposed two additional
normal/cache-shadow failures in the first repair. Both were reproduced before
normalization was fixed. Final native fixtures pass 34/34, thin x86_64/Rosetta
configure fixtures 16/16, and all three macOS packaging suites pass. Both actual
maintained-provider configurations execute the marker successfully. All 686
non-generated source compile commands per architecture match the prior
consistent build after normalizing owned build paths; generated version headers
also match. Existing compiled application-test binaries were reused, not rebuilt
or represented as new release bundles.

Both architectures pass all six applicable regular regression phases. ARM:
core 1032/150484, Qt 613/33702, Torrent 152/18430; Intel/Rosetta:
core 1032/150482, Qt 613/33766, Torrent 152/18430. Both pass GUI multiline
92/5437, single-line 92/5529 and applicable sidebar 90/5343. Explicit native
hidden fixtures, interop and benchmarks remain separate from these selections.
Earlier aggregate negatives are retained and unexplained, not waived by this
passing run. Original and interrupted QA evidence is retained privately.

The other reviewed P2, nonblocking native updater transport/verification
retirement, still needs held-filesystem, physical TLS-teardown, retained-owner
timeout and explicit-retry regressions. Broader core/staging/filesystem latency,
linguistic/resource/provenance/reproducibility, native/minimum-OS/live and benchmark
acceptance remain open. Final reviewed application/material rebuilding, production
numeric allocation/signing/signed-v3 acceptance, guarded activation and closed-app
installation, README/wiki/approved unredacted screenshots and prerelease public
readback still follow. This is a local source checkpoint only, not whole-source
or release acceptance. No installation, push, signing or live-state mutation.
Signed feeds remain last; Tasks 7/8 are not complete.

### 2026-10-05 Native Updater Retirement Checkpoint

The other reviewed Important/P2 finding is repaired locally. Transport and
archive-verification workers are owned QThreads. Normal completion and checked
quit poll physical `wait(0)` receipts rather than joining live workers on the
GUI thread or treating body completion as thread-local teardown completion.
Admission closes once; Pending retains the worker, context and staging owners.
Controller and coordinator distinguish Pending from terminal refusal. The
existing finite quit-session timer now covers updater workers too. Timeout
refuses quit; later retirement does not grant consent or automatically exit.
An explicit retry can proceed after physical retirement. No detach, forced
termination or nested event-loop pump was introduced.

Before production changes, unchanged product objects reproduced exactly 22
intended assertions: transport eight, controller five, session six and native
archive verification three. Real staging writes, archive mapping and C++
thread-local teardown were held in disposable test processes. Launcher-stripped
interposer and read-versus-mapping setup failures were retained, not accepted as
behavioral RED evidence. Additional combined transport/session I/O coverage was
added before its final compilation; it is not claimed as a separate baseline RED.

Fresh consistent two-CPU builds completed for all four test targets on ARM and
thin x86_64. Focused Qt, GUI and native selections pass 225, 701 and 1949 assertions
respectively on each architecture. All twelve applicable regular phases pass.
ARM: core 1032/150485, Qt 615/33700, Torrent 152/18430; Intel/Rosetta:
core 1032/150484, Qt 615/33887, Torrent 152/18430. Both pass GUI multiline
93/5462, single-line 93/5554 and applicable sidebar 91/5368. Explicit native
hidden fixtures remain separate from the regular selections. Matching provider,
loopback-only network denial and isolated profile receipts were checked; the
test I/O interposer is absent from the regular aggregate and production targets.

The maintained ABI2 provider and protected live state remain unchanged. Source,
dependency, prior binary and accepted candidate preservation readbacks accompany
the private evidence. Passing runs do not explain or waive retained aggregate
negatives. Destructor waits remain final ownership fallbacks; arbitrary core,
staging and filesystem latency and a hard whole-quit deadline remain open.
Injected wire/signature fixtures are not live update or installation acceptance.
Thin x86_64/Rosetta is not native Intel/macOS-14 acceptance; the current ARM host
does not establish exact minimum macOS-26 acceptance.

Both concrete reviewed P2 source findings now have repairs and regression
evidence, not whole-source or release acceptance. Linguistic/resource/provenance/
reproducibility, native/minimum-OS/live and benchmark acceptance remain open.
Final reviewed application/material rebuilding, production numeric allocation,
signing/signed-v3 acceptance, guarded activation and closed-app installation,
README/wiki/approved unredacted screenshots and prerelease public readback still
follow. No installation, push, signing, activation or live-state mutation occurred.
Signed feeds remain last; Tasks 7/8 are not complete.

### 2026-10-05 Retained Aggregate Boundary Observations

Private, observation-only copies now timestamp engine commands, bootstrap/socket
admission, check-state alerts, completion snapshots and DC publication. Exact
observation reversal was checked before compiling owned outputs against the
consistent current ARM test objects. Original operations, assertions, deadlines,
selection filters and loopback-only network policy were retained. Compiler and
application test processes ran sequentially; no production source changed.

At the original seeds, the bootstrap control passes 1/4 and full Torrent
152/18430; the publication control passes both original sections, 1/519, and
full applicable Qt 615/33689. These are current binaries with newer updater tests,
not byte-identical replays of the older failing binaries. Counts in the existing
timed stability loop vary with scheduling. Complete XML, no errors/skips, matching
ABI2 loader and direct-network denial were checked. Rejected launcher/anchor/
bookkeeping setup attempts remain privately retained, not test results.

The aggregate bootstrap wait is 22.429ms against its unchanged 2000ms limit.
Bootstrap precedes this fresh session's listener construction, so its own
libtorrent listen-port collision alone cannot explain the missing first connection.
This does not exclude previous-session, socket/resource or scheduling problems.
Aggregate publication waits are 844.947ms and 630.052ms against the unchanged
10000ms limit; both show check completion followed by successful DC publication.
Neither original failure reproduced or has an established cause. Observation can
perturb timing. No fix, failure waiver, native/minimum-OS, live proxy or release
acceptance is claimed; every earlier negative log remains retained.

All frozen source, protected state, dependencies, prior binary/log and accepted
candidate readbacks pass. Next independent work is held staging-cleanup
responsiveness: GUI-side file removal and temporary-directory teardown after
worker retirement, before any repair. Broader core/filesystem responsiveness,
linguistic/resource/provenance/reproducibility, native/minimum-OS/live/benchmark
acceptance, final app/material rebuilding, numeric allocation/signing/signed-v3
acceptance, guarded activation/closed-app install, README/wiki/approved unredacted
screenshots and prerelease/public readback remain open. No push, install, signing,
activation, provider or live-state changes. Signed feeds last; Tasks 7/8 stay open.

### 2026-10-05 Held Staging Cleanup Boundary Audit

Private append-only probes linked against the consistent current ARM production
objects hold the actual deletion syscall for 350ms with an owned watchdog.
Transparent QFile/QTemporaryDir control passes 1/31; the independent descriptor-
relative control passes 2/54, preserving unlink/unlinkat/rmdir results, flags and
ENOENT and excluding a component-prefix sibling. Test hooks never load into the
installed app. Every launcher uses a disposable profile and loopback-only sandbox.

Exact behavioral RED is now retained: transport 1/41 with 11 anticipated failures,
native staging 1/162 with 10, checked session 1/35 with 10. A strict validator checks
complete XML, exact cases/counts/failed-expression multisets, no errors/skips,
explicit process exits and matching hook/provider/direct-network-denial receipts.
These intentionally failing probes are diagnosis, not green regression acceptance.

Actual GUI-thread unlink blocks cancelled transport completion for 359ms and quit
retraction for 361ms. Actual GUI-thread rmdir blocks native Ready cancellation for
368ms and revoked-Ready quit cleanup for 361ms. Valid prior Ready retains its
archive without entering directory removal. These concrete cleanup operations
still block after the earlier physical-worker-retirement repair.

The checked session's 50ms request blocks for 425ms, then reaches exit readiness
and retires its core route rather than refusing while retained. This isolated
fixture has no selected update and invokes no installer: it establishes late exit
readiness, not actual update consent, crash installation or a live app exit.

Next repair must own file/directory cleanup through physical retirement, preserve
terminal removal failure and delivered ownership, and keep checked quit Pending
while held so its finite timeout refuses without late Ready/consent/automatic exit.
Explicit post-drain retry remains necessary. Native owner-release paths, including
installer refusal and destructor fallback, need the same lifetime audit. No
detach/terminate/nested event pump or arbitrary filesystem hard deadline is implied.

All frozen source/protected state, prior negative logs/binaries, dependencies and
accepted candidate readbacks match. No production repair, full regression rerun,
installation, push, signing, activation or live-state mutation in this audit.
Cleanup repair is next; intermittent causes and broader core/filesystem latency,
linguistic/resource/provenance/reproducibility, native/minimum-OS/live/benchmarks,
final app/material rebuild, numeric allocation/signing/signed-v3 acceptance,
guarded activation/closed-app install and docs/prerelease/public readback remain.
Signed feeds last; Tasks 7/8 remain open.

### 2026-10-05 Owned Cleanup Repair And Socket-Boundary Continuation

Transport file retraction and native staging-directory deletion now use owned
cleanup workers. Results and last-reference release require physical `wait(0)`
receipts; held cleanup stays Pending, and removal failure remains a sticky quit
refusal. Prior valid Ready/delivered ownership is preserved. Native temporary
directories no longer auto-delete on GUI-side reference release. Cleanup-only
polling also drains refused installer handoffs after the quit fence without
reopening discovery, downloads or installation. Destructor fallback is joined,
not detached; arbitrary staging/core/filesystem hard deadlines remain unproven.

Public regression probes first retain exactly 33 anticipated baseline failures:
transport 1/41 with 11, native 1/162 with 10, real session 1/35 with 10, and native
permanent removal refusal 1/42 with two. Strict case/count/expression-multiset,
exit, loader and sandbox validation rejects unrelated failures or skips. A real
post-fence cleanup polling defect found in the first repair iteration is fixed;
older sleep-based checks now await physical retirement and separate error facts.
No production timeout is extended. A final test-only review permits an immediate
completed fence when unheld cleanup has already retired; held probes still require
Pending. Earlier failing repair/setup evidence remains retained.

Fresh four-target ARM/Intel builds and final affected Qt rebuilds pass serially
at j2. Eight final focused phases pass: per CPU control 1/31, transport 25/314,
native 24/2723 and real session 3/88. Actual held deletion returns in 0-1ms off GUI;
the finite quit request refuses before core retirement, never exits late, and
requires explicit post-drain retry. The final twelve ordinary phases pass without
interposer: ten unchanged phases plus two freshly rebuilt Qt reruns, 4152 case
executions. Both full Qt reruns have 615 cases. Complete XML/output, matching ABI2
providers and inner loopback-only network denial are verified. This is host ARM
and Rosetta Intel QA, not native Intel/macOS14 or separate minimum-OS acceptance.

The independent Torrent diagnostic adds private transparent kernel socket
observations, not production networking changes. Real success/refusal/invalid-fd
controls verify syscall results and errno before original-seed aggregate runs.
Bootstrap control passes 1/4 and full Torrent 152/18430 at seed1446322059;
publication control passes both original sections, 1/511, and full applicable Qt
615/33900 at seed3982279739. Within the target bootstrap scope, listen succeeds,
connect reports EINPROGRESS, SO_ERROR is zero, the fixture accepts and Qt delivers
connected; its repeated EISCONN probe is completion, not refusal. Aggregate wait
is 41.547ms under the unchanged 2000ms deadline. Publication waits are
850.846/613.981ms under unchanged 10000ms limits and both publish successfully.
Instrumentation can perturb timing. Neither retained original cause is established
or waived; these coherent observer binaries are not byte-identical old failures.

Frozen unaffected source, guarded profile/installed executable, dependencies,
providers, prior negative logs/binaries and old accepted packaging candidates,
seals and archives all read back unchanged. Old candidates are not the final
rebuilt release. No push, install, signing, activation or live-state mutation.
Remaining: intermittent causes; broader core/staging/filesystem responsiveness;
language/resources/dependency provenance/reproducibility; native/minimum-OS/live
notification/logout/Gatekeeper/notarization/DC/Torrent/benchmarks; final reviewed
app/material rebuild; numeric allocation/signing/signed-v3 acceptance/guarded
activation; closed-app install/readback; README/wiki/approved unredacted
screenshots/prerelease/public readback. Signed feeds last; Tasks 7/8 remain open.

### 2026-10-05 Core Write Boundary And Catalog Completeness

The next bounded diagnostic holds an actual `write()` to a disposable profile's
`DCPlusPlus.xml.tmp` after GUI worker retirement. It reuses coherent current
production objects and adds observation only in private fixture/interposer copies.
The watchdog releases the syscall after 350ms; the successful write control
preserves its return value and contents. Matching ARM and Rosetta Intel runs under
the unchanged loopback-only child policy each reproduce exactly four anticipated
failures, with no unrelated exceptions or skips. Quit takes 404/360ms, the writer
is the GUI owner, the heartbeat cannot advance during the hold, and the session
queues exit rather than refusing at the requested 50ms worker/socket budget.

This establishes a remaining core checkpoint responsiveness problem, not an
updater cleanup regression or a repaired hard deadline. Core preparation remains
startup-owner-bound, with terminal persistence receipts and retained failure
ownership. Moving the whole method to a worker would violate those contracts.
The next repair needs an explicit split between owner-thread lifecycle work,
frozen persistence intent, asynchronous owned writes and checked completion.
No production core/session behavior or existing timeout was changed here.

Fresh extraction finds four missing quit-recovery messages in all 25 catalogs.
The baseline runtime audit fails exactly 100 message checks; independent baseline
QM compilation reproduces the same 100 omissions. All four messages are now
translated. Ten additional proxy-test and magnet-sharing captions already have
completed translations in other contexts in every language. Exactly 250 texts
are reused unchanged in the actual Connection/Transfers runtime contexts, after
a regression reproduces 250 missing entries. The compiled baseline control also
reproduces exactly those 250 omissions. No pre-existing message content changes.

The final full catalog suite passes 20 tests without skips, including extraction,
Qt placeholders, gettext formats and all 350 additions surviving QM compilation
with their intended contexts. All 14 icon tests pass. Five legacy QRC declarations
reference 415 existing inputs; this is not the complete package resource inventory.
The broader project-manifest extraction finds 2065 keys and 32 further missing
entries per language, plus inherited unfinished translations. An Objective-C++
parser warning in the installer helper is retained; this extraction is not a
guarantee that every possible runtime literal was discovered. Native-speaker
review and complete resource/dependency provenance remain open.

This checkpoint changes catalogs/tests/documentation only. The installed app,
profile, providers, signing material, servers and VM are not modified. No new
release app, native/minimum-OS acceptance, production signature or publication is
claimed. Remaining: the demonstrated core write latency, earlier intermittent
Torrent causes, other filesystem/staging responsiveness, the 32 catalog entries
and linguistic review, resource/dependency provenance/reproducibility, native
Intel/minimum-OS/live lifecycle and benchmark acceptance, final reviewed-source
app/material rebuild, numeric allocation/signing/signed-v3 acceptance and guarded
activation, closed-app installation/readback, README/wiki/approved unredacted
screenshots/prerelease/public readback. Signed feeds last; Tasks 7/8 stay open.

### 2026-10-05 Owned Core Persistence Repair

The normal-quit session now opts into an owned asynchronous core persistence
path. Existing startup-owner lifecycle barriers remain in place. After they
retire, the first worker writes independent quiescent snapshots in their original
order, including Queue's CID tail before Users. Its thread-exit receipt is
consumed and joined before the owner serializes Settings and invokes Save
listeners. A second worker writes only frozen Settings bytes. Native flush must
succeed before close/replacement. Pending or failed contexts retain their owners;
first failure is sticky, independent snapshots still run, and late completion
after timeout cannot authorize exit without an explicit retry.

Auxiliary rules freeze after successful GUI worker retirement and before core
background persistence, preventing late edits from corrupting recovery intent.
Their physical writes remain after core retirement. Legacy synchronous callers
and result enum ordinals are preserved. Thread launch failure is terminal Failed,
not a cached Pending state. Opt-in interposers are disposable test-process tools,
never application dependencies.

Behavioral baseline failures were observed before the repair for owner admission,
actual Settings write latency, C++ thread-local retirement, late rule mutation
and worker launch failure. The first full suite additionally caught a missing
native flush in the new frozen writer; explicit flush was restored without
relaxing the existing failure/replacement test. Final ARM and Intel/Rosetta focused
runs each pass 43 case executions and 1273 assertions, including held Queue/Settings writes,
native flush and thread-local exit, both worker-launch failure phases, retained
recovery and normal quit. All four holds preserve the GUI heartbeat and require
explicit retry after refusal.

The final ARM regular core, applicable Qt and three window layouts pass. The
Torrent aggregate has one rejection-counter failure in its country/unidentified
peer-policy test; its no-payload assertions did not fail. A same-seed focused
rerun passes, but does not establish the cause or waive the original negative.
Fresh four-target Intel builds and its core, applicable Qt and Torrent suites
pass, as do all three final Intel window layouts. Intel/Rosetta is not native
Intel/macOS 14 or exact ARM minimum-OS acceptance.

An earlier Intel multiline run also exposed a GUI test-fixture race: its independent
200ms worker release could precede the post-heartbeat held-state assertion after
delayed scheduling. A private 250ms delay reproduces exactly four route assertion
failures with observed release/retirement and no other failures. Explicit release
after the held-state checks fixes that control (110 assertions), with a 2s deadlock
watchdog. The 50ms close assertion and heartbeat/input checks are unchanged, and
two explicit unreleased checks strengthen the fixture. All six final layouts pass.
The natural negative and its unrecorded precise scheduling duration are retained;
the controlled result does not retrospectively measure that duration. Only the
affected GUI harnesses were rebuilt for this test-only refinement; six backend
binaries and their compiled production sources remain unchanged.

Fresh catalog checks pass 20 tests and icon checks pass 14, without skips.

This is not a total hard quit deadline: owner lifecycle joins, Settings XML/Save
listeners, GUI auxiliary settings and other filesystem operations remain separate
responsiveness gates. Author delta review does not replace independent whole-
branch review. Remaining catalog omissions, language/resources/dependency
provenance, native/minimum-OS/live lifecycle and benchmark checks, final reviewed
app/material rebuild, production numbering/signing/signed-v3 acceptance, guarded
activation, closed-app install/readback and documentation/prerelease publication
remain open. Profile, installed app, providers, keys, services and prior release
candidates are unchanged. No push/install/signing/activation. Signed feeds last;
Tasks 7/8 remain open.

### 2026-10-05 Framework File-Level Provenance Checkpoint

The retained historical updater QA candidates now have complete inventories of
non-code framework files:114ARM and96Intel, including shipped headers, module
maps and helper metadata. Their previous package-only receipts reproduce exactly
210 missing mapping checks. Completed file-level coverage and four disposable
rejection controls pass with no skips; omission and changed-hash controls each
produce exactly one intended rejection. Inside-framework link confinement is
verified. No candidate is mutated for negative controls.

All210 copied files match the pinned Sparkle SDK or exact Qt deployment-provider
bytes. Combined source evidence:93byte-exact files,72localization dictionaries
with identical source values,12generated resources associated with source inputs.
A renamed private Sparkle module map is verified against its actual source member.
Source-generation linkage remains unresolved for33files:29Qt framework Info.plist
files, two public Sparkle module maps and two helper PkgInfo files. SDK/provider
matches do not establish original generator recipes or file-level legal licensing.

Both whole candidate trees retain bytes/types/link targets/executable bits and
pass strict signature readback. Five archive/notice pins,58Qt provider resource
files and nine protected live-file hashes pass preservation checks. The bundles
remain historical sourcee325cb6cd; this audit is not a final current-source rebuild
or reproducibility/native minimum-OS acceptance. No code/dependency changes,
application launch, signing, installation, push or feed activation occurred.

Legacy icon origin evidence, generated-resource/toolchain linkage, broader input
provenance/reproducibility, inherited unfinished/native-language review, retained
Torrent/quit causes, independent whole-branch and native/minimum-OS/live/benchmark
acceptance remain open. Final-source application/material rebuild, numeric build
allocation, signing/updater acceptance, guarded installation and public release
readback still follow. Signed feeds remain last.
