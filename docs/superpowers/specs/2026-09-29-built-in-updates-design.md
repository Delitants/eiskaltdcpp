# Built-In Updates For The v3 macOS Prerelease

Status: written specification approved, not an implemented updater.
The maintainer approved the conversational design on 2026-09-29, including the
recommended opt-in automatic installation. The maintainer subsequently approved
this written specification at commit `8740d09d`. The native/inline implementation
plan is the next review boundary.

## Intent And Scope

Users should discover new EiskaltDC++ releases from inside the application and
be able to install an authentic compatible update without replacing the app by
hand. Checks and notifications are configurable and enabled by default. Updating
must preserve profiles, downloads, shares and proxy privacy, and must not quit
an active application without the user's normal exit decision.

The first installation backend covers the two approved macOS flavors: Intel
x86_64/macOS14+ and Apple Silicon arm64/macOS26+. Shared settings and release
selection should not depend on Torrent support. Windows/Linux must still build;
self-installation there is explicitly outside this prerelease, not silently
advertised as supported. Neither existing staged Mac bundle includes this
feature; both must be rebuilt after integration.

## User Experience And Defaults

- Add an Updates settings section using the app's native Qt styling and translated
  strings, plus Check for Updates in the application/help menu.
- Automatically check for updates: ON by default. Check after startup settles
  when due, then at most once per 24 hours. Do not block startup or the UI thread.
- Notify when an update is available: ON by default. Disabling notifications does
  not hide manual check results, errors from an explicit action, or install consent.
- Automatically download and install on normal quit: OFF by default. Opt-in is
  persistent. Explain that enabling this also requires automatic checks.
- Manual checks remain usable with automatic checking disabled. Disabling checks
  suspends automatic downloads/installation; it never grants consent to a pending
  update. Recheck policy immediately before any installation handoff.
- Offer Download Update, Install on Next Quit, Later and Skip This Version as
  appropriate. Do not offer an automatic restart timer. Installation on an
  explicitly requested quit does not relaunch unless the user requested it.
- Stable builds default to stable releases; prerelease builds default to stable
  plus prereleases so pre.1 can receive pre.2. Make this a visible setting. A
  later stable release is eligible on either channel; never downgrade implicitly.
- Show last successful check, available version, progress/cancel and concise
  diagnostic errors. Avoid repeated notifications for the same dismissed version.
  Rate-limit background failures in Live Log. No repeated modal failure dialogs.
- Save preferences once in the existing user settings system. The Qt controller
  owns policy; the platform adapter must not create a conflicting second scheduler
  or resurrect defaults over an explicit saved OFF choice.

## Approach

Use Sparkle 2 for macOS archive verification and installation, behind an
Objective-C++ adapter and a shared Qt update controller. Pin a reviewed release
with signed-feed support (2.9 or later), record its source/checksum/license, and
verify every bundled framework/helper against each release flavor's OS/CPU gate.

Alternatives considered:

- A fully custom installer would reuse networking directly but adds privileged
  replacement, crash recovery and signature-validation code to maintain. Rejected.
- GitHub release notifications with browser-only downloads are smaller but do
  not implement the requested automatic update mechanism. Rejected as the target.
- Sparkle with its default remote networking is simpler but cannot be presumed
  to inherit Eiskalt's private global proxy settings. Rejected without routing QA.

The recommended design retains Sparkle's installer while explicitly controlling
all remote update traffic. Public APIs must suffice; do not fork private Sparkle
installer interfaces or weaken signing requirements to make integration work.

## Network And Trust Boundaries

Release discovery uses a fixed, public, HTTPS appcast location under the
maintainer's GitHub repository, with a separate feed for each CPU flavor.
No arbitrary update-server field is exposed in production settings. Feed and
release notes are signed, and each full ZIP has an Ed25519 archive signature.
Checksums remain useful publication artifacts but are not signature substitutes.

The public verification key is embedded in the app. Generate a new update-signing
identity only during the approved implementation/release setup; retain its private
key in the maintainer's local Keychain or explicitly configured signing service.
Never put private keys, proxy endpoints, credentials or profile data in Git,
release feeds, screenshots, logs or build artifacts. Do not reuse a GOST CA key.
Key rotation must follow the pinned updater's documented trust transition; an
untrusted key in a downloaded manifest cannot authorize its own replacement.

Use an updater-specific transport over the existing core proxy-capable sockets.
Inspect `HttpConnection` and `BufferedSocket` rather than assuming their generic
defaults are suitable: update HTTPS must strictly verify certificate chains and
destination hostnames, regardless of relaxed hub TLS settings. Do not inherit
the separate public-hub-list HTTP proxy. Snapshot the global outgoing route for
each operation, cancel on route changes, and never retry directly when a selected
proxy fails. Downloads and DNS follow that same route, not the Torrent override.

### Sparkle Transport Integration

Prototype and test a private loopback content bridge using public Sparkle feed
and mutable-download-request delegate hooks before completing the product UI:

1. Bind only loopback on an ephemeral port with an unguessable per-session
   capability. The bridge is not a general HTTP/CONNECT proxy.
2. Fetch the fixed upstream appcast through the updater transport. Serve its
   original signed bytes unchanged to Sparkle through the private endpoint.
3. Map only the selected, validated full-archive request to a capability-scoped
   loopback endpoint. Fetch the upstream archive through that same transport;
   Sparkle verifies the original signed archive before extraction/installation.
4. Disable delta updates, external release-note rendering and system profiling
   for this first integration. Plain-text release notes may come from verified
   feed content. Do not allow remote images/scripts/automatic browser requests.
5. Reject unknown routes, methods, redirects to the bridge, path traversal and
   invalid capabilities. Use finite feed/body limits, connection timeouts and
   cancellation. Redact capabilities and proxy credentials from diagnostics.
6. Constrain remote redirects to HTTPS GitHub release hosting/CDN destinations;
   reject HTTP downgrade and local/private/link-local destinations. Never forward
   proxy credentials as origin headers or across a redirected origin.

This bridge is a proposed integration, not a proven Sparkle capability in this
repository. Acceptance requires the pinned framework to fetch all feed/archive
bytes through the bridge, accept signatures without rewriting signed content,
and produce no direct remote traffic when such traffic is denied. If a public
API cannot satisfy that test, stop installation integration and revise this
design; do not silently enable a direct-network mode or substitute a custom
privileged installer. Local IPC must remain usable until download completion.

## Selection And Installation

- Feed signatures authenticate metadata before actionable notifications. Archive
  signatures are mandatory before extraction. Enable signed-feed and
  verify-before-extraction controls in the pinned Sparkle framework.
- Reject unsupported OS versions, the wrong CPU flavor, mismatching app identity,
  duplicate/conflicting entries and versions not strictly newer than installed.
  Test ordering of pre.1, pre.2, pre.10 and final 3.0.0 with the real comparator.
  An Intel app running under Rosetta stays on the Intel update feed.
- Validate the installed bundle and downloaded archive agree with the advertised
  version, CPU and minimum OS. Preserve the existing explicit ZIP naming contract.
- Only complete verified downloads may be queued for installation. A crash,
  forced kill, Hide, minimize-to-tray or canceled quit is not installation consent.
- Integrate with `MainWindow::closeEvent`, `slotExit` and the existing shutdown
  sequence. Torrent resume data, DC queues and settings must be saved, and runtime
  objects stopped, before the final process-exit/install handoff. Never interpret
  `aboutToQuit` alone as permission to initiate downloads or bypass a canceled quit.
- Recheck the automatic-install preference and pending version at the handoff.
  Explicit manual Install on Next Quit grants consent only for that version.
- Delegate replacement/authorization to Sparkle, not shell commands or privileged
  scripts. Do not remove quarantine or bypass Gatekeeper. Permission denial,
  disk-full, invalid signatures or canceled authorization leave the old app usable.
- Verify replacement recovery with interrupted-install fixtures. Do not claim
  rollback or minimum-OS compatibility solely because library tests pass.
- Settings, torrent data, certificates and shared files stay outside the app
  replacement transaction and must remain byte-identical in isolated QA profiles.

## Publication Contract

Keep separate signed feeds for Intel/macOS14+ and AppleSilicon/macOS26+. Include
version, channel, minimum OS, exact archive URL/length and archive signature.
Upload and read back both final ZIPs and their matching sources/notices before
updating feeds. Verify public bytes and signatures; publish feeds last so checks
cannot select missing or partial assets. Do not publish an available update for
the same version as the installed prerelease.

Keep update signing separate from Apple Developer ID signing/notarization.
Document the actual signing state; ad-hoc local signatures do not establish
notarization or successful installation on another Mac. Existing pre-updater
staged ZIPs are historical QA artifacts, not final prerelease assets.

## Acceptance Tests

- Defaults and restart persistence; saved OFF choices; manual checks when disabled;
  notification suppression; channel and skip-version behavior; no duplicate checks.
- Actual feed/version selection, malformed/truncated/oversized bodies, invalid
  signatures, stale/replayed releases, wrong architecture and too-new minimum OS.
- Direct and configured global proxy routes, authenticated SOCKS5/GOST, remote DNS,
  invalid proxy credentials/certificates, canceled requests and changed profiles.
  Packet/socket evidence with direct destination access denied must establish no
  feed, archive, DNS or release-note bypass. TCP success alone is insufficient.
- Loopback bridge capability isolation and no arbitrary fetch/file-serving routes.
- Complete download, interrupted download, cancel, permission denial, full disk,
  bad signed archive and interrupted replacement against disposable app copies.
- Active transfer while checking/downloading; close-to-tray; canceled exit;
  ordinary quit with automatic installs OFF and ON; explicit manual consent;
  saved resume state and profile/data integrity after replacement and restart.
- Real old/new signed application fixture updates for both CPU flavors. Keep
  Rosetta evidence separate from native Intel/macOS14 and ARM/macOS26 acceptance.
- Both flavor audits, Cocoa regressions, translations, applicable application
  tests, strict signatures and final release source/notice inventory. DC-only
  and non-macOS builds must not acquire a mandatory Sparkle dependency.

## Implementation Boundaries

Expected areas: a shared Qt update controller/transport and settings view;
`WulforSettings` persistence; main-window menu/shutdown integration; a macOS-only
Sparkle adapter; CMake/bundle metadata and dependency packaging; signed-feed
release tooling; isolated test fixtures and user documentation. Keep updater
changes separate from the Intel dependency build and unrelated UI work.

No installed-app/profile modifications, signing identity generation, new
third-party dependency installation or updater publishing were performed when
writing this specification. Next stage is review of the implementation plan
for the already preferred native/inline execution.

## Primary References

- [Sparkle programmatic integration](https://sparkle-project.org/documentation/programmatic-setup/)
- [Sparkle settings and signature controls](https://sparkle-project.org/documentation/customization/)
- [Sparkle public updater delegate](https://sparkle-project.org/documentation/api-reference/Protocols/SPUUpdaterDelegate.html)
- [Publishing Sparkle updates](https://sparkle-project.org/documentation/publishing/)

References checked 2026-09-29. Public API documentation guides the integration;
the network bridge and application lifecycle behavior still require proof.
