# Development Status

## Current Preview

[v3.0.0-pre.3](https://github.com/Delitants/eiskaltdcpp/releases/tag/v3.0.0-pre.3)
is published with internal macOS build **30000010**. Publication, local
installation and signed updater-feed activation are complete.

- Intel x86_64 only: macOS 14 or newer.
- Apple Silicon arm64 only: macOS 26 or newer.
- Both builds include the native updater, integrated Torrents and DC sharing.
- Compact native settings/toolbar layouts, a separate default Superdesign icon
  set, consolidated proxy configuration and visual BBCode composing are included.
- Legacy short ADC password challenges require an explicit, off-by-default
  per-hub compatibility option. Other hubs remain strict.
- All 74 translation catalogs are complete. The current interface adds 950
  entries across the 25 Qt and 25 core catalogs; all 126 translation checks pass.
  Automated coverage is not native-speaker certification.
- Apps are ad-hoc signed, **not Developer ID signed or notarized**. Sparkle
  archives and feeds have separate authenticated update signatures.

See [Compact Native GUI](../compact-native-gui.md),
[Legacy ADC authentication](../adc-legacy-authentication.md) and the
[pre.3 release qualification](../releases/3.0.0-pre.3-readiness.md).

## Release Verification

Both CPU candidates passed **2,083 fresh local regression cases each**, with
eight opt-in network cases skipped per CPU. An unchanged Torrent test binary
explicitly reuses its prior **169 cases / 18,733 assertions** per CPU; that result
is not described as a fresh run. Production-object updater capability probes,
isolated bundled startup, thin CPU/minimum-OS metadata, dependency closure and
strict ad-hoc signature checks passed.

Both exact release ZIPs passed production-object archive/feed/policy verification
and five negative controls per CPU. Six current-object full-application fixtures
passed: normal quit, canceled quit and forced exit before commit, on each CPU.
Only normal committed quit replaced the disposable signed fixture. These used
unique test bundle IDs, not the live application's identity or profile.

All eight published assets were downloaded anonymously and matched the qualified
lengths and SHA-256 digests. Both public ZIPs passed authenticated verification.
Signed CPU-specific feeds and their manifest were activated last and verified at
their public branch and immutable commit URLs. The installed build30000010 then
completed a manual Preview-channel check and reported no newer compatible version.
No update download or replacement was requested by that check.

Testing ran on macOS 27, with Intel code under Rosetta. Metadata and host testing
do not establish physical Intel or native macOS 14/26 runtime acceptance. No VM
is a release requirement. See the qualification page for materials provenance,
retained failures and precise installation/routing limits.

## Post-Release Test Coverage

Two local test-only commits follow the published application source:
`6f5c80ead` and `95c93d75a`. They attach existing sanitized shutdown-stage evidence
to a failed checked-shutdown assertion, retain its disposable fixture and cover
missing summaries, successful cleanup and rejection of worker-thread noise.
The latter control has deterministic connection/barrier synchronization and
fails when the owner-thread filter is removed in a private test copy.

Both current backend consumers passed **172 cases / 18,765 assertions per CPU**,
with no failures or skips, explicitly excluding the historical large
`[selected-recheck-responsive]` case. Four focused controls passed 39 assertions
per CPU and are included in the broader counts. The first ARM RED wrapper's
XML-expectation error was retained and reconciled read-only without repeating
its native workload. These test changes do not modify production shutdown
behavior, the installed app, published assets or signed feeds. They do not fix
or identify the original intermittent checked-shutdown refusal.

## Remaining Work

No pre.3 packaging, publication, local-installation or feed-activation task
remains. Follow-up acceptance is separate:

- Diagnose the historical intermittent checked-shutdown refusal from an actual
  recurrence with stage evidence. Preserve the original failure; do not relax
  budgets or repeat an exhausted baseline campaign merely to obtain green runs.
- Same-ID real update replacement/recovery. Unique-ID fixtures and verification
  of exact release ZIPs are complementary evidence, not this transaction.
- Packet-level product updater proxy/DNS routing qualification. Anonymous curl
  delivery and the installed Check Now observation do not establish it.
- Native minimum-OS and physical Intel runtime coverage.
- Native NSColorPanel automation; the Cocoa color-handler test uses a scoped
  nonnative dialog and verifies restoration of the previous policy.
- Native-speaker translation review and broader Windows/Linux runtime QA.
  The [Linux QA workflow](../linux-torrent-qa.md) remains distinct from actual
  hosted runtime acceptance.
- Further dependency reproducibility/provenance work. Consumed older OpenSSL
  headers are supplied, but their full 3.4.1 upstream archive was not retained;
  source/section mapping is not whole-build reproducibility or legal certification.
- Developer ID signing and notarization when an identity is available.

The hosted wiki provides user documentation; its maintained source is `docs/wiki`
in the main repository. Local documentation corrections do not by themselves
synchronize the hosted wiki or change binaries and updater feeds.

## Historical Evidence

The [pre.2 qualification](../releases/3.0.0-pre.2-readiness.md) and
[pre-publication documentation history](https://github.com/Delitants/eiskaltdcpp/blob/a507e625f/docs/wiki/Development-Status.md#historical-development-evidence)
retain earlier checkpoints and their then-pending items. They do not supersede
the current release qualification or turn a later successful run into a causal
fix for an earlier failure.
