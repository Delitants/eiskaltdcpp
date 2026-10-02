# Native Updater Feasibility Gate

This is an opt-in, disposable test, **not an installed updater**. It uses the
real pinned Sparkle framework, ephemeral Ed25519 keys, a random bundle ID and
separate fixture homes. No production key, Keychain entry, Eiskalt profile or
installed application is used. The framework license and source archive must
be retained according to `macos/updater/sparkle.lock.json`.

## Run

Download the official archive and matching source from the lock file, verify
their SHA-256 values, then extract the framework to an isolated directory.
Use the existing pinned Catch2 3.5.2 source; do not install shared dependencies.
The standalone harness also needs Qt 6 Core/Widgets/Network, OpenSSL and Intl.

```sh
cmake -S tests/macos/updater -B build-updater-probe \
  -DEISKALT_SPARKLE_ROOT=/absolute/path/to/extracted/sparkle \
  -DCATCH2_SOURCE_DIR=/absolute/path/to/catch2-source \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build build-updater-probe -j2
python3 tests/macos/updater/fixtures.py create --root /tmp/new-updater-fixture \
  --sparkle /absolute/path/to/extracted/sparkle \
  --binary build-updater-probe/eiskalt-updater-native-tests --installer-xpc
python3 tests/macos/updater/fixtures.py verify --root /tmp/new-updater-fixture
for mode in valid bad-feed bad-archive unmapped information-only; do
  python3 tests/macos/updater/fixtures.py probe --root /tmp/new-updater-fixture \
    --mode "$mode" || exit $?
done
```

Alternatively, `EISKALT_BUILD_UPDATER_PROBE=ON` adds the native target to the
full application's tests build. It is OFF by default and does not link Sparkle
into the application. Socket tests require local loopback permission and a GUI
login session for the native helper. Never remove the external-network denial
assertion to make a restricted runner pass.

The fixture runner denies external networking in the updater process, checks
an actual external socket receives EPERM, and leaves Sparkle's downloader in
that process. The installer launcher uses Sparkle's documented XPC helper,
since an in-process launchd submission is forbidden under `sandbox-exec`.
This does not establish a network sandbox for launchd-owned helpers. Full
proxy/DNS/installer-process acceptance remains separate work.

## Findings

- Sparkle's standard comparator ignores the hyphen and subsequent text:
  `3.0.0-pre.1` and `3.0.0-pre.2` compare equal. The maintainer approved a
  separate increasing numeric CFBundleVersion; display versions and release
  filenames remain unchanged. This fixture allocates builds 1, 2, 10 and 11
  to pre.1, pre.2, pre.10 and final respectively, purely as ordering examples.
  Product build-number allocation and packaging checks are not implemented yet.
- The public feed URL and mutable archive request hooks accept original signed
  bytes through loopback, without rewriting the signed appcast.
- An altered feed fails before offering an update. An altered archive produces
  a signature-validation-before-unarchiving failure. The `extracting` UI event
  is emitted before verification and is not evidence that files were extracted.
- **Dismiss is not cancel.** Replying Dismiss at Ready can install when the
  process exits; this was reproduced against a disposable copy. Ordinary probes
  reply Skip, await update-cycle completion, and check the old app's
  executable and metadata again after exit. This cancellation check is not a
  crash-safety proof.

Do not connect Sparkle's eager download/preparation lifecycle to the product
controller before resolving the normal-quit-only requirement. The proposed
revision, approved by the maintainer, is app-owned archive staging/verification,
with Sparkle's installer started only at committed normal quit. Information-only
discovery is tested separately to establish it does not download or prepare an
installer. No production integration is enabled.

## Staged Qt Bridge Acceptance

`UpdateLoopbackBridge` serves already-staged files and has no remote fetch API.
The backend must fetch the feed through `UpdateTransport`, authenticate its
metadata, and verify the archive before mapping that archive at committed normal
quit. Starting a local bridge session is not installation consent.

The bridge binds only IPv4 loopback with a new 256-bit capability for each
session. It accepts exact GET paths and Host headers, with no query strings,
ranges, request bodies, proxy methods, or directory serving. Header size,
concurrent clients, socket write queues and connection lifetimes are bounded.
Route revocation, restart and stop invalidate ordinary discovery sessions. A
separate private installer session serves only local staged bytes after committed
quit, without reviving a retired external route. Stop invalidates both kinds.
Files must be private
regular staging files; they stay open on their original descriptors and changes
invalidate delivery. Windows staging remains unavailable until its private ACL
requirements have a platform implementation.

After creating the pinned signed fixture above, run the actual Qt bridge and
native consumer together (substitute the Qt build and fixture directories):

```sh
QT_QPA_PLATFORM=offscreen EISKALT_UPDATER_SIGNATURE_FIXTURE="$FIXTURE" \
  "$B/tests/qt/eiskaltdcpp-qt-tests" '[update-bridge],[update-bridge-native]'
```

The native case clones the disposable probe app into a private temporary
directory, gives it a separate home, denies its external networking and exercises
information-only discovery, altered-feed rejection, and archive delivery followed
by Skip/cycle completion. Its executable and metadata must remain unchanged after
exit. It does not target the installed Eiskalt app. This proves the production Qt
bridge can serve the pinned Sparkle consumer, not that production proxy DNS,
installer-helper network confinement, or the application shutdown controller is
complete. Those remain separate release gates.

## Committed-Exit Fixture

`install-probe` checks installation after the native child has exited. Create a
fresh fixture for each mode using the `create` command above, then run:

```sh
python3 tests/macos/updater/fixtures.py install-probe \
  --root /tmp/fresh-updater-fixture --mode normal
```

Modes are `normal`, `hide`, `cancel-quit`, `kill-before-commit`,
`cancel-prepared`, and `bad-identity`. For the last mode, pass `--wrong-identity`
to `create`. Only `normal` may reply Dismiss at Ready to install on exit.
`cancel-prepared` replies Skip and awaits cancellation. Pre-commit modes
use information-only discovery, never preparation. `kill-before-commit` kills
only the disposable child spawned by this runner, after discovery completes.
These simulate lifecycle boundaries, not actual MainWindow gestures.

The runner authenticates fixture bytes before preflight and requires exact HTTP
requests, a whole-bundle file/symlink snapshot and strict code-signature readback
after exit, no relaunch
sentinel, and unchanged profile/share/download sentinel files. This does not
exercise Eiskalt's real persistence, authorization failure, disk-full recovery,
an interruption after commit, or installer-helper network confinement.

Native probing found Sparkle can prepare a validly signed archive with the wrong
internal bundle ID when its app filename matches. `BundleArchiveVerifier` therefore
independently checks identity before any installer preparation. It reads bounded
ZIP metadata without extraction, verifies the plist CRC, requires the expected
bundle ID, numeric build and displayed version, and checks a thin Mach-O main
executable's CPU/type. Ambiguous paths and non-directory identity-path ancestors
are rejected. Supported packaging is one app root, stored/deflated entries,
non-ZIP64, a maximum 512 MiB archive and 1 MiB Info.plist. This is not a signature
verifier or general safe-extraction library. The caller must first authenticate
the entire archive and retain exclusive immutable staging until handoff.

`create` also writes **unsigned parser-only** cases under `parser-cases/`.
Run `[bundle-archive]` in the old disposable bundle with a separate HOME and
`EISKALT_PROBE_LOCAL_ARCHIVE` pointing to its signed `update.zip`. The test checks
the real package, both ZIP methods, identity/CPU mismatches, cancellation, path
aliases/traversal, symlink ancestors, malformed headers, CRC failure and bounds.
These unsigned cases must never be offered to Sparkle.

The verifier is linked into the opt-in native harness and native Qt test target. Production
normal-quit handoff remains disabled. In particular, macOS currently leaves the
Qt loop through an emergency `_Exit` path that skips core teardown; installation
must not be enabled there without first establishing successful queue/settings/
Torrent persistence and keeping the bridge event loop alive through preparation.

### Production Adapter In Disposable Bundles

`MacUpdateInstaller` is an opt-in native adapter, not an activated app feature.
Its private start rejects uncommitted and foreign-owner calls before admission.
It reauthenticates the selected signed feed and archive on a joined worker,
checks internal bundle ID/build/version/CPU/minimum OS, then supplies only local
private staging to the public Sparkle installer API. Current consent is checked
before creating Sparkle and again at Ready; consent rejection replies Skip.
The adapter never quits or relaunches the application and must remain owned
outside fenced GUI roots through exit. The caller must retain exclusive immutable
staging throughout the handoff.

Create a fresh fixture for each mode, including the required installer XPC flag:

```sh
python3 tests/macos/updater/fixtures.py create --adapter --installer-xpc \
  --root /tmp/new-native-installer-fixture --sparkle "$SPARKLE" \
  --binary build-updater-probe/eiskalt-updater-native-tests
python3 tests/macos/updater/fixtures.py install-probe --adapter \
  --root /tmp/new-native-installer-fixture --mode normal
```

The ten adapter modes are `normal`, `hide`, `cancel-quit`,
`kill-before-commit`, `cancel-prepared`, `bad-identity`, `bad-minimum-os`,
`cancel-before-native`, `bad-feed` and `bad-archive`.
For the latter two, add `--wrong-identity` or `--wrong-minimum-os` to `create`.
Only normal installs; every other mode must retain the exact old signed bundle.
Normal retains the prepared adapter until process exit. The additional consent
and corrupt-byte cases assert refusal before creating a native installer. Tests
first require private `0600` regular single-link staging in a private owned parent,
so corrupt-byte rejection cannot silently be a permissions refusal.
The adapter owns a separate Qt bridge, so it must not contact the runner's
legacy Python server. The native parent asserts external socket denial; this
does not prove installer/XPC-helper confinement. The hide/cancel/crash modes
simulate lifecycle boundaries, not actual MainWindow gestures.

`MacUpdateBackend::armInstallOnQuit` still reports Unsupported. Authorization
cancellation, disk-full/interrupted replacement and recovery, native session
deadline/abort behavior, real committed-quit integration and helper networking
remain gates before product activation. Passing these disposable tests is not
a successful update of the installed Eiskalt app.

### Shutdown Checkpoint Results

`SettingsManager::saveChecked`, `QueueManager::saveQueueChecked(true)` and
`WulforSettings::saveChecked` expose write/sync failure rather than silently
treating a save attempt as success. The core XML writers flush a sibling temporary
file and atomically replace the previous file without deleting it first or
falling back to a potentially partial copy. Legacy void methods remain wrappers.
This is not a power-loss/filesystem durability guarantee or a process-stop API.

`TorrentEngine::shutdownChecked` joins its worker and retains the final checkpoint
result for repeated calls. It drains an older pending resume request before
requesting a fresh paused checkpoint. A missing/failed resume reply or atomic
state-write failure returns false, even if an older checkpoint was retained.
`TorrentRuntime::shutdownChecked` stops publication and proxy workers and passes
that result through. The existing void shutdown entry points remain compatible.

Run `[shutdown-persistence]` in the core, torrent, and Qt test binaries to exercise
successful writes, failure/retry with previous bytes retained, GUI sync failure,
resume timeout, and repeatable engine/runtime outcomes. Fixtures use isolated
profiles. None of these helpers grants update consent. Core producers and hub
sockets still need a verified non-destructive stop sequence before the real
MainWindow can use these results to authorize installer handoff.

### Hub And Socket Stop Primitives

The core `[hub-shutdown]` cases exercise `Client::stopForShutdown` on local ADC
and NMDC sockets. The terminal stop preserves the client object and its UI
listeners, rejects late reconnect requests even after gate reset/completion,
drains the in-flight timer/socket callback, and detaches further timer ticks.
The final auto-reconnect flag write occurs after the socket callback barrier, so
an already-running callback cannot overwrite it during stop.
Invoke it only from the client lifetime owner, serialized with manual
connect/reconnect/deletion, not from a timer/socket callback. Do not hold a
ClientManager lock across these callback barriers. The barriers themselves are
not timed and require callbacks to return.

`BufferedSocket::waitShutdown(timeoutMillis)` is a bounded resource-drain check.
It returns false for a live socket or a blocked transport destructor, and true
only after destruction finishes. The global count now increments before worker
launch (rolling back on failure) and decrements after deletion rather than in
the destructor body before member cleanup. Neither overload prevents new socket
creation or represents a join of the native thread's final return epilogue.
Producers must already be stopped before a true result authorizes core teardown.

These fixtures do not invoke the real MainWindow or install an update. Complete
core quiescence, client registry ownership, checked persistence, and the normal
quit event-loop handoff remain integration gates. The installed app and its
emergency macOS exit behavior remain unchanged.

The `[searchmanager][shutdown]` cases also verify that truncated UDP input does
not terminate the queue worker before a subsequent packet can be consumed. This
keeps active-producer shutdown tests from passing after an empty fixture packet
has already killed the worker with an out-of-range string access.

## Controller Contract

The independent Qt controller/policy tests use an explicitly injected backend and
clock; they do not claim that the production platform adapter is connected.
Automatic checks and notices default ON, while automatic download/install defaults
OFF. All persisted values use the existing WulforSettings `updates/*` namespace.
The installed identity must provide the allocated numeric build, bundle ID, CPU,
OS and default channel; there is no guessed build-number fallback.

Backend commands and replies carry a process-unique operation ID. A canceled or
previous controller's late replies must not advance current work. The backend may
emit an offer only after authenticating its signed metadata, and `downloadReady`
only after verifying the full archive against that offer. The controller also
checks compatibility, build ordering and the ready build/digest pair, but those
checks are not substitutes for cryptographic verification.

The single controller timer polls after startup settles and applies a persisted
24-hour last-attempt gate. Manual checks bypass scheduling OFF, never backend
verification. Last success is separate from last attempt. Background error reports
are category-only and limited to once per category/hour; manual results remain
visible. Later/Skip and notification deduplication survive controller recreation.

Only `normalQuitCommitted()` may delegate install preparation. Its caller must
first complete the application's queue, settings and Torrent shutdown successfully.
It rereads saved preferences and binds consent to numeric build plus archive hash.
Ordinary `shutdown()`, destruction, and cancellation never grant consent. Settings
and backend must outlive the controller. Production platform wiring, handoff error
recovery, persisted ready-cache verification and actual app shutdown acceptance
remain pending; no application window constructs this controller yet.

## Signed Feed Contract

`UpdateFeedVerifier` authenticates the original bounded bytes before parsing XML.
It accepts the pinned Sparkle signer's single Ed25519 trailer, with canonical
Base64, an exact signed length and no unsigned suffix except the final newline.
The key must come from installed app metadata, never from the feed. The original
signed bytes are retained for Sparkle's independent verification. This helper
does not grant installation consent, extract files or perform network requests.

The intentionally narrow RSS 2.0 schema is part of the forthcoming publisher
contract. It uses the normal Sparkle namespace and the custom namespace
`https://eiskaltdcpp.org/xml-namespaces/updates/1` (an identifier, never fetched):

- Each item has a canonical positive numeric `sparkle:version`, bounded display
  `sparkle:shortVersionString` and `sparkle:minimumSystemVersion`.
- Stable items omit `sparkle:channel`, matching Sparkle's default-channel
  semantics. Preview items use `sparkle:channel` equal to `preview`.
- `eiskalt:bundleIdentifier` and `eiskalt:cpu` bind the exact app and thin CPU
  flavor (`arm64` or `x86_64`). Selection uses the installed flavor, not the host
  CPU, so running under Rosetta cannot silently switch architectures.
- Exactly one full ZIP enclosure carries the allowlisted GitHub release URL,
  positive `length`, `type="application/octet-stream"`, `sparkle:os="macos"`,
  canonical `sparkle:edSignature` and lowercase hexadecimal `eiskalt:sha256`.
- Optional item `title`, `pubDate` and inline `description` are text only. The
  description is returned as literal text, including any escaped markup; UI
  consumers must explicitly use plain-text rendering, not Qt's auto/rich text.

Limits are 1 MiB for the entire signed feed, 128 releases, 4096 XML elements,
five levels, eight attributes per element, 16 KiB of notes and 512 MiB per
archive. Duplicate/conflicting release identities or fields, DTDs/entities,
processing instructions, unknown elements/attributes, external release notes,
deltas and package installers are rejected. A wrong-channel, current/older,
wrong-bundle, wrong-CPU or too-new-OS candidate is never selected. A malformed
signed item rejects the entire feed rather than silently changing selection.

Run the portable tests and optional real signer/consumer interoperability tests:

```sh
QT_QPA_PLATFORM=offscreen \
  EISKALT_SPARKLE_SIGNER="$SPARKLE/bin/sign_update" \
  EISKALT_UPDATER_SIGNATURE_FIXTURE="$FIXTURE" \
  EISKALT_UPDATER_PROBE_BINARY="$PWD/build-updater-probe/eiskalt-updater-native-tests" \
  "$B/tests/qt/eiskaltdcpp-qt-tests" \
  '[update-feed],[update-feed-native],[update-feed-bridge-native]'
```

Rebuild the native probe first. The final test clones the disposable fixture,
uses an ephemeral test identity and a separate home, serves the original signed
feed over the staged-only Qt bridge and denies the consumer external networking.
Both stable and preview selection must match Sparkle, with no download,
extraction, preparation or executable/plist changes. This is signed-metadata
interoperability, not production backend activation, proxy DNS acceptance,
helper confinement or a successful app update. The future adapter must still
require Sparkle's signed-feed success and match its selected build/archive
against the authenticated offer before advancing.

## Origin HTTPS Roots On macOS

The updater's independent `SSLSocket::createVerifiedClient` context imports the
OS default root certificates using Security.framework's local
`SecTrustCopyAnchorCertificates` API. It does not need an OpenSSL/Homebrew CA
file on the user's machine. On macOS, `SSL_CERT_FILE` and `SSL_CERT_DIR` cannot
silently replace this updater root set. Other platforms retain their existing
OpenSSL default-root behavior; explicit isolated fixture roots remain supported.

DER certificates are bounded to 4096 anchors, 64 KiB each and 16 MiB total.
Empty, malformed, trailing-data and non-CA entries fail closed. A replacement
store is installed only after the entire set is valid, never mixed with a prior
store, DC certificates or the GOST CA. The context has no client identity.
OpenSSL still checks the certificate chain, validity period, server purpose and
logical hostname, even when origin TLS is nested inside GOST.

This imports the OS default anchor set; it is not a native SecTrust evaluation
or a promise to honor per-user Keychain exceptions. No native trust evaluation,
AIA fetching or OCSP requests are started, avoiding an additional network path
outside the selected proxy. Existing per-connection cancellation and TLS
deadlines still apply. Fresh Intel/macOS14 and Apple Silicon/macOS26 package
acceptance remains a separate release gate.

`[https-roots]` compares imported certificates to the native default anchor set,
checks atomic failure and bounds, and verifies that an environment-injected CA
does not authorize an origin inside a disposable authenticated GOST tunnel.
Run with local socket permission; no system Keychain or trust setting is changed:

```sh
SSL_CERT_FILE=/nonexistent-eiskalt-qa-ca.pem \
SSL_CERT_DIR=/nonexistent-eiskalt-qa-ca-directory \
  build-gost-core/tests/eiskaltdcpp-tests '[https-roots],[strict-https]'
```

## Native Information-Only Adapter

`MacUpdateDiscovery` has only check/cancel operations, with no archive mapping,
download, consent or installer API. It verifies a bounded signed feed against
the installed bundle's pinned key, selects by numeric build, channel, CPU and
minimum OS, and gives Sparkle a private unchanged copy through the loopback
bridge. An offer requires Sparkle's own successful signature validation and an
exact match of build, version, archive URL, size, signature, channel and minimum
OS. A missing match explicitly opts out of Sparkle's default candidate selection.

The adapter requires explicit fail-closed bundle settings (signed feeds,
verification before extraction, zero signature-failure expiration, no external
downloader, automatic checks/downloads or profiling). It invokes only
`checkForUpdateInformation`, rejects other check types and unexpected user-driver
callbacks, and cancels on route revocation or a 30-second deadline. Generation
guards discard queued callbacks even when an external operation ID is reused.
Cancellation releases the updater and removes the local staged feed; no prepared
archive is retained. The pinned information-only driver stops at discovery even
when probing an existing installer; an unverified resumed result is rejected.

This source is currently opt-in in the Qt test target, not connected to the
application controller. Backend composition, routed acceptance, committed-quit
handoff and product packaging remain pending. Do not treat passing discovery
tests as an installed or fully integrated updater.

With the pinned disposable fixture created above and a Qt test build `$B`:

```sh
cmake -S . -B "$B" -DEISKALT_TEST_MAC_UPDATE_DISCOVERY=ON \
  -DEISKALT_SPARKLE_ROOT="$SPARKLE"
cmake --build "$B" --target eiskaltdcpp-qt-tests -j2
QA_HOME=$(mktemp -d /private/tmp/eiskalt-discovery-home.XXXXXX)
HOME="$QA_HOME" CFFIXED_USER_HOME="$QA_HOME" \
XDG_CONFIG_HOME="$QA_HOME/config" QT_QPA_PLATFORM=cocoa \
EISKALT_UPDATER_SIGNATURE_FIXTURE="$FIXTURE" \
  /usr/bin/sandbox-exec -p '(version 1)(allow default)(deny network*)
    (allow network-outbound (remote ip "localhost:*"))
    (allow network-bind (local ip "localhost:*"))
    (allow network-inbound (local ip "localhost:*"))' \
  "$B/tests/qt/eiskaltdcpp-qt-tests" '[update-discovery-native]'
```

The tests assert external socket denial (`EPERM`), a disposable home and Qt's
Cocoa platform. Offscreen Qt does not dispatch the native callback loop and is
not an equivalent acceptance run. A GUI login session and local socket permission
are required. Tests cover stable/preview selection with a newer wrong-CPU item,
bad signatures/policies, no-update completion, overlap rejection, pre-start and
in-flight cancellation, reused IDs, route revocation and owner destruction.

## Backend Composition

`MacUpdateBackend` connects the worker transport, native discovery adapter and
archive verifier. It captures one immutable connection route, downloads the
fixed CPU-specific feed into private temporary staging, and emits only the
independently verified native offer. A download request must match that offer's
complete metadata. The signed feed is reauthenticated against the installed key;
archive size, SHA-256 and Ed25519 signature are then checked on a worker thread.
Ready means verified app-owned staging, not installer preparation or consent.

Cancel and route changes invalidate the offer immediately. A canceled network or
verification worker retains its staging until it exits; new work is rejected
while that worker drains, so cancellation cannot create unbounded parallel jobs.
Owner destruction cancels and joins its workers. Archive verification is bounded
to 512 MiB; OpenSSL's single Ed25519 verification call cannot be interrupted
mid-call, so final destruction can wait for that call to finish. Ordinary cancel
does not join it on the GUI thread. Staging is temporary and is removed on normal
destruction; crash/restart cache recovery is not implemented by this checkpoint.

The controller discards invalidated offers after cancel/failure and requires a
new check before retrying. `UnsupportedUpdateBackend` reports unsupported
capability asynchronously without network, download or installer access. It is
compiled into the portable app target but is not selected by a runtime factory
yet. The Mac backend remains opt-in in the native Qt tests; neither backend is
activated by MainWindow. Mac installer handoff explicitly reports unsupported
until the committed-normal-quit implementation is completed.

Run `[update-backend-native],[update-discovery-native],[update-backend]` with the
same Cocoa/disposable-HOME/external-network-denied command above. The backend
fixture injects bytes at the worker transport boundary, then exercises real
Sparkle discovery, real archive signature verification and the real controller's
Ready/Later lifecycle. It also checks tampering, changed offers, route revocation,
in-flight cancellation/reuse and destruction. This is composition evidence,
**not production HTTP/proxy DNS acceptance or an installation test**. Those
remain separate gates, together with the quit handoff and release packaging.
