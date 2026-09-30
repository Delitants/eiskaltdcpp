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

### Task 7: Reproducible Signed Feeds And Release Packaging

**Files:** Create `macos/updater/{make-feeds.py,verify-feeds.py,README.md}`, `tests/macos/updater/test_release_feeds.py`; modify `macos/release.py`, `macos/RELEASE.md`, `eiskaltdcpp-qt/Info.plist.in`, `cmake/MacUpdater.cmake`, `docs/third-party-release-inventory.md`, relevant presets/CMake and `tests/macos/bundle/release-profiles.py`.

**Interfaces:** `make-feeds.py --manifest FILE --output DIR --signer PATH` prepares Intel/ARM feeds offline and refuses missing/invalid inputs; `verify-feeds.py --manifest FILE --feeds DIR --public-key KEY` validates them. Manifest schema v1 records version/channel, flavor/arch/minOS, bundle ID, HTTPS asset URL, byte length, SHA256 and Ed25519 signature. The signer is the pinned Sparkle signing tool configured to read Keychain, never a shell command embedded in metadata. All outputs are offline by default; publication is a separate explicit operation.

Amendment: schema v1 also records numeric `build_number`, identical for both
flavors and increasing across published releases. CFBundleVersion and appcast
version use this number; CFBundleShortVersionString and archive filenames retain
the visible release version. Update the existing packaging equality checks and
their tests rather than leaving conflicting version contracts.

- [ ] RED release tests use ephemeral keys and literal fixtures: tampered/truncated feed/archive, absent signature, mismatched length/hash, wrong flavor/minOS/name, conflicting versions, path injection and missing assets all fail; two valid flavor feeds round-trip against actual Sparkle verification. No production private key enters fixture logs.
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
