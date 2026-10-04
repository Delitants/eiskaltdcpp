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

The verifier is linked into the opt-in native harness and native Qt test target.
Production installation remains disabled. Normal macOS quit now requires checked
preparation and returns through GUI/core teardown; an unsolicited event-loop exit
still uses the emergency `_Exit` fallback. Installation must not be enabled until
the actual pending-update application handoff is accepted as well.

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

The twenty-six adapter modes are `normal`, `hide`, `cancel-quit`,
`kill-before-commit`, `cancel-prepared`, `bad-identity`, `bad-minimum-os`,
`cancel-before-native`, `bad-feed`, `bad-archive`, `cancel-verifier`,
`destroy-verifier`, `cancel-check`, `cancel-download`, `cancel-extraction`,
`cancel-ready`, `deadline-verifier`, `deadline-extraction`, `cancel-after-handoff`,
`deadline-after-handoff`, `worker-start-failure`, `cancel-worker-start` and
`deadline-worker-start`, `resume-installing`, `replacement-denied` and
`authorization-refused`.
For `bad-identity` or `bad-minimum-os`, add the corresponding
`--wrong-identity` or `--wrong-minimum-os` flag to `create`.
Normal and the two post-handoff refusal cases install; every other mode must
retain the exact old signed bundle. Successful handoffs retain the prepared
adapter until process exit. The additional consent
and corrupt-byte cases assert refusal before creating a native installer. Tests
first require private `0600` regular single-link staging in a private owned parent,
so corrupt-byte rejection cannot silently be a permissions refusal.
The adapter owns a separate Qt bridge, so it must not contact the runner's
legacy Python server, except the explicit retained-session seed described below.
The native parent asserts external socket denial; this
does not prove installer/XPC-helper confinement. The hide/cancel/crash modes
simulate lifecycle boundaries, not actual MainWindow gestures.

### Retained Installer And Replacement Refusal

`resume-installing` uses public Sparkle callbacks to prepare/Dismiss an installer
in the same process and bundle identity, then verifies a second public check
reports `Installing` with no second feed/archive fetch. The production adapter
must refuse that actual retained installer, with no new download/extraction or
Ready/handoff. After process exit the parent requires the exact old signed tree
and unchanged user-data snapshots. A temporary `Skip`-to-`Dismiss` mutation was
caught by post-exit readback even though the child reported refusal, proving that
an update-cycle callback alone is not a disarm receipt. This covers a live
retained installer, not persisted downloaded-cache recovery across a crash.

`replacement-denied` waits for committed native preparation, identifies the
single fixture `Autoupdate` PID by the exact random bundle ID, then makes only
the owned disposable destination directory read-only before allowing child exit.
It requires that native helper to exit and its OS log to report both permission
denial and failure to move this exact old app. Directory permissions are restored
only when no installer for that fixture remains. If one remains, denial is
retained rather than permitting a late replacement. The old signed tree and
user-data snapshots must match after exit. This is an actual failure before
moving the old app; it does not establish administrator cancellation, disk-full,
interrupted replacement or successful restoration after moving the old app aside.

The disposable parent has a separate home. Launchd-owned Sparkle helpers can
still use the real user's cache directory under the unique probe bundle ID, as
confirmed by their logs. Those are not Eiskalt profile paths; they are not proof
of helper filesystem/network confinement. No production keys or installed app
are targeted. These cases do not exercise a real authorization prompt; the
separate VM-only case below does.

### Actual Authorization Cancellation In A Disposable VM

Create a fresh adapter fixture inside a disposable macOS VM with a logged-in
GUI account. Run the following from the guest's GUI Terminal, not a background
SSH session, and click **Cancel** on the actual macOS update authorization prompt:

```sh
python3 tests/macos/updater/fixtures.py install-probe --adapter --disposable-vm \
  --root /absolute/path/to/fresh-guest-fixture --mode authorization-refused
```

The runner requires explicit VM opt-in and a `VirtualMac` hardware model before
reading any fixture files. It verifies a fresh unique authorization right, makes
only the owned immediate fixture destination read-only, and allows two minutes
for cancellation. The native test requires Sparkle cancellation code **4007**,
verified/downloaded bytes, no prepared installer, and no Ready or HandingOff
stage. The parent checks the entire old signed bundle, sentinel user data and
absence of relaunch after child exit. Destination permissions are restored only
after the exact cancellation receipt and if its exact `Autoupdate` process is
absent. An unconfirmed failure retains the read-only destination for inspection,
even if that process is not currently observed. This query is not a complete XPC or
launchd-helper lifetime/confinement proof.

The native helper may create `<fixtureBundleIdentifier>.sparkle2-auth` in the
guest authorization database. The result reports the exact right and whether
cleanup is required. An administrator must remove **only that test-created
right** in the disposable guest and verify that reading it subsequently fails.
Do not reset the authorization database, change authorization rules, or run this
case against the host or an installed application. No password is needed to
cancel the update prompt; cleanup may require the guest administrator password.

A background-session error **4005** or `errAuthorizationInteractionNotAllowed`
(-60007) is not cancellation acceptance. A writable-destination control must
fail the cancellation assertions. VM observations on macOS 27/ARM64 do not
establish either supported platform's minimum-OS behavior.

Admission and mocked cleanup regressions launch no native helper and modify no
authorization right:

```sh
python3 tests/macos/updater/test_fixture_admission.py -v
```

Cancellation is a private owner-thread request, not an immediate terminal
receipt. The monotonic deadline requests the same cancellation path; it never
grants exit or drops the native owner. Verification joins its worker before
refusal. Check/download cancellation invokes only the currently valid public
SDK block. Extraction clears the expired download block, waits for Ready,
replies Skip, and waits for the native update-cycle completion callback. If
that callback never arrives, the active owner and staging must remain retained.
The first cancellation/deadline reason is retained, repeated active requests
are idempotent, and cancellation after approved handoff is refused.
Approval is latched before the SDK reply, not only after cycle completion.
The HandingOff phase is an observation of that irrevocable decision, not an
exit receipt. Direct and queued post-approval requests must both be refused
while the approved update still installs exactly once without relaunch.

The phase cases assert no immediate terminal callback, foreign-thread refusal,
exactly one eventual refusal and unchanged signed bundles after child exit.
`deadline-verifier` uses a real elapsed deadline; `deadline-extraction` requests
expiry at the real extraction callback, not a timed slow-extraction simulation.
`destroy-verifier` destroys the admitted owner before pumping events and checks
that its worker joins without a callback or crash. The worker captures its
joined implementation state, not the owning `unique_ptr` being destroyed.
The three worker-start cases use a private launcher that throws before creating
a thread. They cover queued failure and first-reason retention; they do not
prove behavior under actual OS thread/resource exhaustion.
Fresh-state admission rejects resumed/downloaded/installing offers. The
downloaded-cache crash test below exercises one real SDK restart boundary;
product staging restoration remains a separate acceptance gate.

### Downloaded-Cache Crash Boundary

The disposable `Probe` driver exposes two hooks for an owning parent harness:
`EISKALT_PROBE_MODE=cache-seed` holds inside the public `didDownloadUpdate`
callback, before returning into extraction. It writes the fixture bundle ID to
`EISKALT_PROBE_CACHE_SEED_MARKER` and emits `CACHE_SEED` only for a verified feed,
a fresh NotDownloaded offer, no extraction/Ready state, and a probe bundle ID.
The parent must verify a complete cached archive against the authenticated
fixture bytes, then terminate **only its spawned disposable child** within
30 seconds. The hold expires after 60 seconds; this is not a production pause.

Restart `[bridge-probe]` with the same bundle and separate fixture home using
`EISKALT_PROBE_MODE=cache-restart`. It requires signed feed/archive handling,
Ready followed by Skip/cycle completion, and public state NotDownloaded before
emitting `CACHE_RESTART`. The parent must independently check actual request
counts, old whole-bundle/signature readback, user-data sentinels and no relaunch.
On the tested pinned SDK, cached bytes survived the crash but this fresh process
fetched the signed feed/archive again rather than silently resuming them. A
subsequent production-adapter `cancel-ready` run also leaves the old app intact.

These are native test-driver hooks, not a product cache loader or installation
consent. App-owned staging restoration and committed-quit integration still
need their separate acceptance. Observed SDK cache behavior on one VM is not
a promise of cache recovery on every supported OS.

Disposable VM recovery also covers a final-new-app move failure after the old
app was moved aside: the native installer restored the exact signed old bundle
and left fixture user data unchanged. This is one tested HFS+ rollback path,
not a guarantee for every filesystem or interruption phase.

Fresh normal-install and cancel-at-Ready VM observations identified the exact
fixture-owned Installer XPC, Autoupdate and Updater processes and verified each
was gone after its cycle. No Internet sockets were observed in the helper
samples. Sampling does not prove the absence of every short-lived connection
and is not production proxy/DNS acceptance or an OS isolation boundary.

`MacUpdateBackend::armInstallOnQuit` still reports Unsupported. App-owned staging
restoration, real committed-quit integration, supported-platform acceptance,
helper lifetime and verified update routing remain gates before product
activation. The approved vendor installer is not strictly OS-isolated; parent
socket denial must not be presented as blanket helper sandboxing. Passing
disposable tests is not a successful update of the installed Eiskalt app.

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

### Verified Stage Cache Foundation

`saveVerifiedStage` authenticates the selected signed feed and complete archive
before publishing a bounded pair in an explicitly supplied private directory.
Only `appcast.xml`, `update.zip` and two reserved temporary slots are used. A
directory lock excludes concurrent cache operations. Writes and temporary-slot
cleanup are anchored to opened directories; symlinks, hardlinks, shared roots
and special files are rejected. A crash between the two atomic replacements
can leave a mixed pair, which restoration refuses rather than treating as Ready.

`restoreVerifiedStage` requires a fresh signed feed selecting exactly the same
release under the currently pinned key. It reauthenticates the cached feed,
copies and reverifies the archive into a new private stage without overwriting
an existing destination. It performs no network requests and grants no offer or
install consent. Installed identity/channel/route policy remains the caller's
responsibility. Windows refuses until private staging ACL support is accepted.

Run `[update-stage-cache]` in the Qt or standalone native test target. Actual
`MacUpdateBackend` worker restoration is covered by the tests below. Runtime activation and
committed-quit integration remain pending; this is not an installed-app updater.

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
does not join it on the GUI thread. Ephemeral staging is removed on normal
destruction. An optional persistent cache uses a fixed private leaf beneath the
platform's generic cache location, not the user profile. Only that leaf may be
created, on the worker; unsuitable or absent parent directories disable caching
without changing existing permissions or invalidating a verified download.

Every download first tries cache restoration on the joined verification worker,
after fresh routed, authenticated discovery. Both fresh and cached signed feeds
must bind the exact selection under the currently installed key, and the copied
archive is verified again. A cache miss uses the same immutable connection route
for a normal archive download, never a direct fallback. Cancellation, revoked
routes and the retained quit fence suppress stale Ready results and miss retries.
Successful fresh verification optionally stores the pair on that worker; cache
storage failure does not turn a verified download into an installation failure.
Cache data never supplies an offline offer or installation consent.

The `[backend-cache-native]` group also runs fresh child processes against one
private cache. It verifies restoration after ordinary process exit and after
SIGKILL of only the owned child following a verified-cache receipt. Each restart
starts without an offer or Ready result, fetches the feed again, then reaches
Ready without an archive fetch. This uses injected transport bytes and real
Sparkle discovery/Ed25519 verification, not production HTTP or installer access.

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

## Committed-Quit Controller Contract

`normalQuitCommitted()` consumes a successful updater worker fence and current
version-bound policy. `quitDisposition()` distinguishes no pending installation,
an awaiting handoff, a prepared installation, and refusal. NoUpdate needs no
asynchronous receipt; AwaitingHandoff requires retaining the controller, backend,
staging and application owners while the event loop remains alive.

The new `beginInstallHandoff` backend endpoint receives a unique operation and a
guarded, owner-thread consent callback. It must check consent immediately before
irreversible handoff, and emit exactly one matching `installHandoffFinished`
receipt. Exceptions are permitted only before work is admitted. Unknown
implementations refuse; there is no fallback to the legacy arm call. The
controller rechecks current preferences, skip/defer settings and exact ready
version, latches observed revocation, rejects stale/duplicate receipts, and
invalidates callbacks on shutdown or destruction. A claimed success cannot
override an already observed consent refusal.

Run `[update-handoff-contract]` and `[application-quit]` in the isolated Qt targets.
The latter composes real retained core/GUI shutdown with a fixture backend; it
does not install an update. The native endpoint and real MainWindow checked-quit
session are implemented, but runtime installation activation remains pending.
The public Mac backend still reports installation unsupported.

### Native Backend Handoff

The opt-in Qt target (`EISKALT_TEST_MAC_UPDATE_DISCOVERY=ON`) also exercises the
real MacUpdateBackend endpoint, without enabling installation capability in the
product. `[backend-handoff-native]` checks admission, operation IDs, thread
ownership, stage retention and terminal refusal using the nonbundled test process.
A refusal from that process is not native replacement acceptance.

Use a fresh signed fixture for each real main-bundle case:

```sh
python3 tests/macos/updater/fixtures.py create --adapter --installer-xpc \
  --root /tmp/new-backend-installer-fixture --sparkle "$SPARKLE" \
  --binary build-test/tests/qt/eiskaltdcpp-qt-tests
python3 tests/macos/updater/fixtures.py install-probe --adapter --backend \
  --root /tmp/new-backend-installer-fixture --mode normal
```

Backend modes are `normal`, `cancel-verifier`, `cancel-before-native` and
`cancel-prepared`. The parent preserves the adapter's exact post-exit bundle,
signature, no-relaunch and test-data checks. Backend modes add disposable XDG
paths/TMPDIR and Cocoa rendering; unsupported modes are refused before fixture
access. Each child uses fresh authenticated information-only discovery and archive
verification, then retires the core route after its worker fence. Only private
local staged installer IPC remains; no external transport is restarted.
The normal case retains the prepared owner through process exit. Cancellation
and consent refusals keep the old app. These cases do not simulate an actual
MainWindow close gesture and do not establish installer-helper OS isolation.

### Actual Application Normal Quit

`ApplicationQuitSession` is owned outside the retained GUI QObject tree. It
consumes checked application preparation before committing the updater, retains
all owners and the event loop during a pending handoff, and emits deferred exit
readiness only for NoUpdate or Prepared. Refused remains retained: a controller
can synthesize refusal before native cancellation finishes, so it is not a safe
disarm receipt. Hide, active modal input and canceled confirmation do not commit.

The production macOS entrypoint enables this checked session without an updater
service yet. Its application-owned receiver exits only after readiness, destroys
GUI owners while core managers exist, then consumes checked core shutdown. Quit
events cannot accept termination while preparation is pending. The profile's
single-instance marker remains owned through GUI/core disposal, not merely until
the exit callback runs. Unexpected loop
exit retains the emergency fallback and is not installation consent.

Run `[window-quit]` and `[application-quit]` in the full GUI target, then exercise
the actual entrypoint in disposable profiles:

```sh
python3 tests/macos/updater/run_application_quit.py --build build-torrent --mode window
python3 tests/macos/updater/run_application_quit.py --build build-torrent --mode quit-event
```

This harness reuses production GUI objects and includes the real `main.cpp`,
changing only the fixture IPC identity. It requires FORCE_XDG, fresh private
homes and a localhost-only network policy; protected fixture bytes, hash/queue/
settings snapshots and the Torrent checkpoint are read back after main returns.
The private IPC marker must still be owned at quit and during GUI destruction.
`_Exit` cannot produce the required post-return receipt. This establishes ordinary
no-update quit, not signed pending-update full-app replacement, OS logout,
installer activation, or supported-minimum-OS acceptance.
