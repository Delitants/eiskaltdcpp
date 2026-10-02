# Offline Signed Release Feeds

These tools prepare and verify one release's Intel and Apple Silicon feeds.
They do not download, upload, publish, install or enable the runtime updater.
Framework packaging, numeric bundle metadata, production signing-key setup and
native installer recovery acceptance remain separate release gates. In
particular, current `macos/release.py` still uses the earlier bundle-version
contract; a generated feed is not proof that a package has numeric metadata.

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
