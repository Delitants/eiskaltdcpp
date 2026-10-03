# Offline Updater Release Tools

These tools prepare and verify one release's Intel and Apple Silicon feeds.
They do not download, upload, publish, install or enable the runtime updater.
Runtime integration, production signing-key setup and native installer recovery
acceptance remain separate release gates. The release presets require an
allocated `EISKALT_MACOS_BUILD_NUMBER`, and `macos/release.py --build-number`
checks the numeric bundle version independently from the displayed release
version. A generated feed alone is not proof that the package has matching
metadata: audit the actual final archive and use the same build for both CPUs.

## Validate Bundle Trust Metadata

`EISKALT_MAC_UPDATER_METADATA` defaults to OFF. Opt-in packaging development
requires CMake 3.23+ and an explicit `EISKALT_MAC_UPDATER_PUBLIC_KEY` (canonical nonzero 32-byte
base64 public key), one `CMAKE_OSX_ARCHITECTURES` value (`x86_64` or `arm64`),
and `EISKALT_MAC_UPDATER_FEED_URL`. The URL must be in the existing maintained
`https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/`
namespace: `intel.xml` for Intel or `apple-silicon.xml` for Apple Silicon.
Only an optional `?revision=N` with a canonical positive decimal value is allowed
for public cache invalidation. Credentials, other queries/hosts, fragments and
wrong-CPU filenames are rejected. No key is generated or implicitly trusted.

Validated fields enter the actual bundle template before its metadata hash.
Key-only and feed-only changes therefore relink and re-sign under Make and Xcode.
Build-cache source generations and an ordered link gate handle equal timestamps
and returning to an older metadata value, without forcing no-op Make links.
The gate only invalidates the owned build executable and rejects outside-build
or symlinked paths; its receipt records linking, not final signature acceptance.
This internal generation is not the allocated release build number.
Turning the option OFF removes all updater and local-network exception keys.
Signed-feed and pre-extraction verification are required, with zero signed-feed
failure expiration; Sparkle's scheduler, automatic updating, profiling and remote
downloader are disabled. Qt remains the policy and remote transport owner.
The only ATS exception allows the app-owned local verification bridge.

This flag does **not** package Sparkle, select the production backend, prepare
an installer or activate updates. The native backend's maintained default URL
selection is unchanged; consuming configured feed metadata belongs to later
gated runtime integration. Do not publish a metadata-only updater bundle.
Audited framework/helper packaging, final signing and native recovery/quit
acceptance remain required. Test with disposable bundles and public fixture keys:

```sh
python3 tests/macos/updater/test_bundle_metadata.py
```

## Stage The Framework Offline

`stage-framework.py` prepares a separate dependency tree; it never modifies or
launches an app, updater, helper or installer. Supply the exact SDK and matching
source archives pinned by `sparkle.lock.json`. It snapshots and checks both
archive hashes before extraction, compares their original license bytes, thins
every framework/helper Mach-O to the requested CPU and enforces its OS ceiling.
No downloads or production key generation occur.
Input archives are limited to 64 MiB each; only checksum-matching snapshots are
extracted. Tools have finite timeouts and their signing diagnostics are not
forwarded.

```sh
python3 macos/updater/stage-framework.py \
  --sdk-archive /path/to/Sparkle-2.10.0.tar.xz \
  --source-archive /path/to/pinned-Sparkle-source.tar.gz \
  --profile intel --output /path/to/new-intel-updater-stage

python3 macos/updater/stage-framework.py \
  --sdk-archive /path/to/Sparkle-2.10.0.tar.xz \
  --source-archive /path/to/pinned-Sparkle-source.tar.gz \
  --profile apple-silicon --output /path/to/new-arm-updater-stage
```

The output parent must exist; an existing destination, including a symlink, is
never overwritten. The complete result is promoted exclusively after successful
inside-out signing and strict signature checks. Partial failures leave no output.
The tree contains only `Contents/Frameworks/Sparkle.framework` and
`Contents/Resources/ThirdPartyNotices/Sparkle/{LICENSE,provenance.json}`. SDK test
apps, developer tools and symbol files are not distributed. Framework links stay
inside the framework; helper identifiers and entitlements are preserved. Each
signed Mach-O hash, CPU/OS ceiling, source pin and preparation are recorded.

Default code signing is explicitly ad-hoc (`--identity -`), without a timestamp.
An existing identity may be supplied with `--identity`; the tool does not create
or export it and still disables timestamp network access. This is not Developer
ID/notarization acceptance or an authenticated update-feed signing identity.
Production key setup remains a distinct recorded step.

Staging is not production CMake integration or updater activation. The final app
must retain its patched Cocoa plugin, validated feed/public-key metadata, nested
signatures and notices, and be sealed/audited after merging dependencies. Keep
those gates open until the actual two release bundles pass; do not copy a stage
over the installed app. Matching source archives must accompany publication.

## Package An Audited Build Bundle

`EISKALT_MAC_UPDATER_PACKAGE` is a separate CMake option, OFF by default. It
requires the validated metadata option above, Python 3.9+, a single CPU, and an
explicit `EISKALT_MAC_UPDATER_STAGE` and `EISKALT_MAC_UPDATER_STAGE_SHA256`.
The checksum is selected from an independently audited local stage, not accepted
from an untrusted downloaded receipt. This is a local build-input trust anchor;
it is not a feed signature or production key setup. `package-framework.py`
defines the canonical digest: sorted relative entries including directory types,
link targets, executable-bit status and SHA256 file bytes, encoded as compact
ASCII JSON and SHA256 hashed. Caller paths never enter this digest.

```sh
# After independently auditing the selected stage, record its complete digest.
PYTHONPATH=macos/updater python3 - <<'PY'
import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('packaging', 'macos/updater/package-framework.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
print(module.digest_tree(Path('/path/to/audited-stage')))
PY

cmake -S . -B /path/to/fresh-build \
  -DEISKALT_MAC_UPDATER_METADATA=ON \
  -DEISKALT_MAC_UPDATER_PUBLIC_KEY="$AUDITED_UPDATE_PUBLIC_KEY" \
  -DEISKALT_MAC_UPDATER_FEED_URL="$MATCHING_MAINTAINED_FEED" \
  -DEISKALT_MAC_UPDATER_PACKAGE=ON \
  -DEISKALT_MAC_UPDATER_STAGE=/path/to/audited-stage \
  -DEISKALT_MAC_UPDATER_STAGE_SHA256="$AUDITED_STAGE_SHA256" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
```

The stage is checked during configuration and before every build, including
no-op builds. Copying uses a private re-verified snapshot after all Qt deployment,
resource copying and library normalization. Invalid stage checksums, license or
source pins, missing helpers/notices, wrong CPU/OS, unsafe links or unexpected
content are rejected. Only owned build bundles beneath the supplied build root
can be modified. Existing different updater destinations are not overwritten;
use a fresh build tree for a new stage or when changing packaging back to OFF.

The confined copied Cocoa leaf is explicitly signed after normalization and
input/path validation; its source is never changed. Signature-stripped private
copies prove that its loadable code bytes are unchanged. The consumer then
retains its final signed bytes, all original
Sparkle nested signatures, license and provenance. Other Mach-O leaves and nested
bundles are signed inside out, followed by the outer application. `--deep` is
used only for strict verification, never signature overwriting on this path.
The final app is checked for thin CPU/OS metadata and forbidden absolute external
library dependencies. This does not prove native minimum-OS execution or resolve
every relative dynamic-link path. Default builds retain their existing signing
path. Enabled packaging skips install-time Qt redeployment over sealed helpers;
installation/CPack acceptance is still a separate gate.

`EISKALT_MAC_UPDATER_CODESIGN_IDENTITY` defaults to explicit ad-hoc `-`, with
timestamps disabled. An existing identity may be supplied; no key is generated.
The app's `ThirdPartyNotices/EiskaltUpdaterPackaging.json` records the public
stage digest, signed and non-signature Cocoa hashes and signing mode, not successful installation or
runtime enablement. Recipe/stage/identity changes enter the metadata generation
gate and force relinking/resealing rather than relying on coarse timestamps.
Failed signing makes the build fail; a mutated build output must not be packaged
until strict final validation succeeds. No updater/helper is launched by these
tools. The production backend remains unsupported until recovery and normal-quit
acceptance; do not publish an inert dependency-only build as a working updater.

```sh
EISKALT_SPARKLE_INTEL_STAGE=/path/to/audited-intel-stage \
EISKALT_SPARKLE_ARM_STAGE=/path/to/audited-arm-stage \
  python3 tests/macos/updater/test_app_packaging.py
```

## Actual Stage Tests

Actual-SDK staging and ZIP-round-trip tests are opt-in when both archive paths
are provided; otherwise the suite explicitly skips them:

```sh
EISKALT_SPARKLE_ARCHIVE=/path/to/Sparkle-2.10.0.tar.xz \
EISKALT_SPARKLE_SOURCE_ARCHIVE=/path/to/pinned-Sparkle-source.tar.gz \
  python3 tests/macos/updater/test_framework_staging.py
```

## Inputs

Place `manifest.json` alongside the two final, immutable release ZIPs. Schema 1
accepts exactly these fields; unknown and duplicate JSON fields are rejected:

| Manifest field | Required value |
| --- | --- |
| `schema_version` | Integer `1` |
| `version` | Visible version, for example `3.0.0-pre.1` |
| `build_number` | Positive integer, at most `9223372036854775807`, shared by both flavors |
| `channel` | `stable` or `preview` |
| `bundle_id` | `com.github.eiskaltdcpp` |
| `notes` | Plain UTF-8 text, at most 16 KiB; markup is escaped, not rendered |
| `assets` | Exactly two objects, one per flavor below |

Each asset has exactly `flavor`, `arch`, `minimum_os`, `filename`, `url`, `size`,
`sha256` and `ed_signature`:

| Flavor | Architecture | Minimum OS | Filename suffix |
| --- | --- | --- | --- |
| `intel` | `x86_64` | `14.0` | `macOS14-plus-Intel-x86_64.zip` |
| `apple-silicon` | `arm64` | `26.0` | `macOS26-plus-AppleSilicon-arm64.zip` |

The complete filename is `EiskaltDCpp-<version>-<suffix>`. The exact asset URL is
`https://github.com/Delitants/eiskaltdcpp/releases/download/v<version>/<filename>`;
other origins, paths, queries or insecure URLs are rejected. `size` is the ZIP's
positive byte length (at most 512 MiB), `sha256` its lowercase hexadecimal digest,
and `ed_signature` the canonical Base64 Ed25519 signature of the complete ZIP.
Finalize signing and packaging before measuring or signing these archive bytes.

Feed metadata is a declaration, not an architecture or Mach-O audit. Run the
separate [package audits](../RELEASE.md) and minimum-OS/native acceptance gates.
Archive signatures do not replace bundle code signing or installation consent.

## Prepare And Verify

Use Python 3.9+ and the verified `sign_update` executable from the SDK pinned in
`sparkle.lock.json`. The caller must verify that SDK before selecting the signer;
the tool does not download or establish executable provenance. A signer path is
a trusted CLI argument, never a command from the manifest. The standard signer
uses its Keychain identity; these tools neither create nor export private keys.
Production key setup/backup/rotation requires a distinct recorded release step.

```sh
python3 macos/updater/make-feeds.py \
  --manifest /path/to/release-assets/manifest.json \
  --output /path/to/new-offline-feeds \
  --signer /path/to/verified-sparkle/bin/sign_update \
  --previous-manifest /path/to/last-published/manifest.json

python3 macos/updater/verify-feeds.py \
  --manifest /path/to/release-assets/manifest.json \
  --feeds /path/to/new-offline-feeds \
  --public-key "$PINNED_UPDATE_PUBLIC_KEY" \
  --openssl /path/to/openssl-3 \
  --previous-manifest /path/to/last-published/manifest.json
```

Generation currently requires macOS for exclusive directory promotion. Its
output parent must already exist, and the destination must not exist. The pair
`intel.xml` and `apple-silicon.xml` is staged privately, signed and independently
verified by Sparkle, then promoted atomically without overwriting another
owner's directory. Failure does not expose a partial pair. Stable feeds omit
the preview-channel marker. Displayed versions stay separate from numeric
update-ordering versions.
The native consumer maps `x86_64` to `intel.xml` and `arm64` to
`apple-silicon.xml` in the existing maintained feed namespace. Unknown CPUs
are unsupported; there is no synthesized filename or alternate-host fallback.
The generator disables Sparkle's optional warning-insertion XML rewrite so
carriage-return references in plain-text notes survive unchanged. This does
not disable signing or either cryptographic verification step; any subsequent
feed edit requires re-signing.

Verification needs OpenSSL 3 and only the application's pinned public key,
obtained independently of the downloaded manifest/feed. It authenticates both
archives and the complete bounded feed bytes before parsing XML and comparing
all metadata. It rejects symlink input leaves, changed archives, missing or
extra feeds, noncanonical signatures, DTD/entity/processing instructions and
conflicting metadata. Feeds are limited to 1 MiB including their signature.
External tool output is not forwarded on signing/verification failures.

`--previous-manifest` checks strictly increasing build numbers against the
caller-selected last published manifest. Omit it only for initial preparation;
omission does not establish that a build is newer than publication history.
Neither command contacts GitHub or verifies public asset availability. After
all acceptance gates, publish packages first, read back their exact public
bytes, and publish authenticated feeds last as a separate authorized operation.

## Isolated Tests

```sh
OPENSSL=/path/to/openssl-3 \
EISKALT_SPARKLE_SIGNER=/path/to/verified-sparkle/bin/sign_update \
PYTHONDONTWRITEBYTECODE=1 python3 tests/macos/updater/test_release_feeds.py
```

Without a signer the suite explicitly skips. With one, it uses ephemeral file
keys through a temporary wrapper around the actual pinned SDK; it does not use
or write the production Keychain identity. Tests cover valid preview/stable
pairs, deterministic output, exclusive promotion, partial signing failures,
ordering, malformed manifests, path injection, symlinks and altered archives
or feeds, including cryptographically re-signed conflicting metadata.

For opt-in native consumer acceptance, set `EISKALT_PUBLICATION_QA` to a fresh,
owned, private (`0700`) empty directory during the Python run. Only public test
keys, manifests, signed feeds and small dummy ZIPs are retained there; private
keys are deleted. Then use that same directory with the standalone native test:

```sh
QT_QPA_PLATFORM=offscreen EISKALT_PUBLICATION_QA=/path/to/private-qa \
  build-updater-probe/eiskalt-updater-native-tests '[feed-publication]'
```

Use the isolated HOME/XDG/TMPDIR runner described in the
[native test instructions](../../tests/macos/updater/README.md). This filter
exercises the production feed/policy/archive consumer without creating a
Sparkle updater or installer. Dummy ZIPs prove crypto and metadata compatibility,
not release-bundle inspection, real network routing or installation recovery.
