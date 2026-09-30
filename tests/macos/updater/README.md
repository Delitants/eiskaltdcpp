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
  process exits; this was reproduced against a disposable copy. The fixture
  now replies Skip, awaits update-cycle completion, and checks the old app's
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
Route revocation, restart and stop invalidate the session. Files must be private
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
