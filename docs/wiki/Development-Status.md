# Development Status

## Current Preview

[v3.0.0-pre.2](https://github.com/Delitants/eiskaltdcpp/releases/tag/v3.0.0-pre.2)
is published with internal macOS build **30000002**.

- Intel x86_64 only: macOS 14 or newer.
- Apple Silicon arm64 only: macOS 26 or newer.
- Both builds include the native updater, integrated Torrents and DC sharing.
- All 74 translation catalogs have zero unfinished entries; 19,006 entries were
  completed, with 124 translation checks passing. Static screening covered 480
  settings views; this is not native-speaker certification.
- Apps are ad-hoc signed, **not Developer ID signed or notarized**. Sparkle
  archives and feeds have separate authenticated update signatures.

## Verification

Both CPU builds passed 1,973 regression cases each, eight real-GOST
interoperability cases each and five disposable full-application update/quit
scenarios each. Tests cover committed-quit installation and refusal to install
after canceled quit or a forced precommit exit. Isolated bundled startup passed.
Runtime C++ is unchanged from pre.1, whose live routing checks covered
Cloudflare/Google DoH through GOST.

All eight release assets matched their uploaded SHA-256 digests. Both app ZIPs
were downloaded from the public release and compared with the signed local
bytes. Signed updater feeds were published last and independently verified.
See the [release qualification](../releases/3.0.0-pre.2-readiness.md) for details,
including retained failures and their subsequent verification.

Testing ran on macOS 27, with Intel code under Rosetta. Minimum-OS metadata was
checked throughout both bundles; physical Intel and native macOS 14/26 runtime
testing are not established. Whole-archive dependency reproducibility is not
claimed. A VM is not a release requirement.

## Post-Release Test Repair

The intermittent loopback seeding timeout was reproduced as a test-helper port
collision, including with concurrent ARM/Intel release test binaries. The helper
now gives each native session an OS-assigned listener without changing the
engine's configured settings or its production port policy. The transfer test
also checks listener readiness before connecting; its 10-second payload deadline
is unchanged.

A real reserved-port regression failed before the repair and passed afterward.
Three concurrent ARM/Intel pairs passed the regression and original transfer
case. Both complete updated Torrent backend suites then passed **168 cases and
18,703 assertions per CPU**, without failures or skips. Review found no material
issues. This is a source/test-only follow-up; the published and installed pre.2
application, updater feeds and release assets are unchanged. The rest of the
application suites were not rerun for this test-only repair.

## Remaining Work

Post-pre.2 development is not yet a published prerelease. It includes the
compact native GUI and separate default Superdesign icon set, consolidated
proxy settings, visual BBCode composition, scoped legacy ADC authentication compatibility, qualified
production-updater installation admission, and improved quit diagnostics.
See [Compact Native GUI](../compact-native-gui.md) and
[Legacy ADC authentication](../adc-legacy-authentication.md).

Before publishing that successor:

- Rebuild both architecture packages with the visual composer and a new internal
  build number. The preceding pre-composer build30000009 candidates passed local
  qualification on both CPUs; they are not this new feature's release packages.
  Source synchronization added 27 GUI message/context
  entries and 11 core/DHT messages per language: 950 entries across the 25 Qt
  and 25 core catalogs. All 126 translation checks pass without skips, covering
  compilation of all 74 catalogs, placeholders and fresh source extraction
  (2,077 Qt keys and 225 core/DHT message IDs). Existing translations and all
  24 GTK catalogs are unchanged. This is automated and reviewed source coverage,
  not native-speaker certification or rebuilt-package acceptance.
- Complete the final dependency/source/notice mapping for both release bundles,
  preserve the new Lucide/Feather notices, and qualify the actual signed ZIPs
  and feeds. Development-bundle tests are not public-asset readback.
- Preserve the unresolved intermittent private shutdown-refusal evidence.
  Subsequent successful tests and a normal installed-app quit did not establish
  its cause or a fix.

Longer-term coverage remains separate:

- Native minimum-OS and physical Intel compatibility coverage.
- Broader Windows/Linux runtime QA. A focused manual Linux Torrent workflow is
  now available with required selected-recheck ABI 2, loopback-only test
  networking and explicit test-inventory/results checks. Its offline contract
  tests are not hosted runtime acceptance; see [Linux QA](../linux-torrent-qa.md).
- Native-speaker translation review.
- Further dependency reproducibility tooling.
- Developer ID signing and notarization when an identity is available.

The hosted wiki now provides user documentation. Its maintained source is
`docs/wiki` in the main repository. Documentation synchronization does not change
published binaries or signed update feeds.

## Historical Evidence

Earlier development checkpoints and their then-pending items are preserved in
[the pre-publication documentation history](https://github.com/Delitants/eiskaltdcpp/blob/a507e625f/docs/wiki/Development-Status.md#historical-development-evidence).
They do not supersede the current release qualification above.
