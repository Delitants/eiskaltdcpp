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

## Remaining Work

- Native minimum-OS and physical Intel compatibility coverage.
- Broader Windows/Linux runtime QA.
- Native-speaker translation review.
- Further dependency reproducibility tooling.
- Investigation of an intermittent loopback seeding-deadline miss under load;
  three isolated repetitions and the full unchanged suite subsequently passed.
- Developer ID signing and notarization when an identity is available.

The hosted wiki now provides user documentation. Its maintained source is
`docs/wiki` in the main repository. Documentation synchronization does not change
published binaries or signed update feeds.

## Historical Evidence

Earlier development checkpoints and their then-pending items are preserved in
[the pre-publication documentation history](https://github.com/Delitants/eiskaltdcpp/blob/a507e625f/docs/wiki/Development-Status.md#historical-development-evidence).
They do not supersede the current release qualification above.
