# Development Status

## Current Preview

[v3.0.0-pre.1](https://github.com/Delitants/eiskaltdcpp/releases/tag/v3.0.0-pre.1)
was published on 2026-10-16 with internal macOS build **30000001**.

- Intel x86_64 only: macOS 14 or newer.
- Apple Silicon arm64 only: macOS 26 or newer.
- Both builds include the native updater, integrated Torrents and DC sharing.
- Apps are ad-hoc signed, **not Developer ID signed or notarized**. Sparkle
  archives and feeds have separate authenticated update signatures.

## Verification

Both CPU builds passed 1,973 regression cases each, eight real-GOST
interoperability cases each and five disposable full-application update/quit
scenarios each. Tests cover committed-quit installation and refusal to install
after canceled quit or a forced precommit exit. Live Cloudflare/Google DoH through
GOST and isolated bundled startup also passed.

All eight release assets matched their uploaded SHA-256 digests. Both app ZIPs
were downloaded from the public release and compared with the signed local
bytes. Signed updater feeds were published last and independently verified.
See the [release qualification](../releases/3.0.0-pre.1-readiness.md) for details.

Testing ran on macOS 27, with Intel code under Rosetta. Minimum-OS metadata was
checked throughout both bundles; physical Intel and native macOS 14/26 runtime
testing are not established. Whole-archive dependency reproducibility is not
claimed. A VM is not a release requirement.

## Remaining Work

- Native minimum-OS and physical Intel compatibility coverage.
- Broader Windows/Linux runtime QA.
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
