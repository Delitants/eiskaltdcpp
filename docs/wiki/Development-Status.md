# Development Status

Checkpoint: 2026-09-29. `v3.0.0-pre.1` is planned, not published.

## Implemented In Development

The integrated Torrent UI and proxy routing, managed DC publication, native UI
updates and selective upstream maintenance are present in the development tree.
The audited Torrent translation batches cover 25 catalogs; compiled-string and
format checks do not replace native-speaker review. The DHT advertisement option
is independently configurable, off by default and takes effect on reconnect.

The UDP search-result queue shutdown race is repaired with deterministic
pre-worker-entry and producer/shutdown regressions. Context-specific protocol
validation and resource limits, authoritative outgoing Date/BaseDate metadata,
and interruptible socket waits with proxy revocation are now integrated in the
development tree. The consolidated functional review found two older Shadowsocks
integration defects: pending final ciphertext could be stranded, and coalesced
reply frames could wait indefinitely for more network input. Both are fixed with
regressions for legacy/2022 routes, buffered commands, file sends, graceful
disconnect and forced shutdown. This review covered the prioritized maintenance
paths, not an exhaustive audit of every source file.

The final macOS regression checkpoint passed 1,427 cases and 61,767 assertions
across core, Qt, Torrent and window suites, with the real-GOST tests enabled.
This is development verification, not an installed or published release.

A versioned macOS ARM64 candidate now builds and passes bundle dependency and
ad-hoc signature checks. A packaging regression also covers extensionless,
non-executable framework binaries, which must not escape the link/OS audits.
Its isolated CLI startup passes with network, real-profile and Homebrew reads
denied. The candidate declares macOS 26 because of its bundled dependencies;
testing was on macOS 27, not macOS 26 or older. The release support-floor choice
remains open. Dependency notices and matching source materials have been staged
for the exact candidate; final distribution checks are still required. Ad-hoc
verification is not Developer ID signing or notarization, and CLI startup is
not live GUI QA.

The eight optional real-GOST interoperability cases have passed on macOS using
disposable loopback identities and servers. Coverage includes authenticated TLS,
TCP/uTP Torrent transfers on IPv4/IPv6, UDP tracker and DHT exchanges, remote DNS,
and completed-session resume after a fixture-server restart. Direct destination
access is denied in the application fixtures. These checks do not contact or
verify a deployed proxy server, and do not establish partial-piece crash recovery.

## Before Prerelease Publication

1. Preserve the reviewed source candidate and completed regression checkpoint
   through the final curated commit and versioned build.
2. Preserve the staged dependency notices and matching source materials through
   final archive verification and publication. Keep profiles, credentials,
   private infrastructure configuration and QA artifacts out of release packages.
3. Finalize the candidate's supported OS floor and distribution materials;
   preserve the verified build through final packaging, installation and
   prerelease publication.
4. Publish the maintained wiki pages. They remain available in `docs/wiki` while
   the GitHub wiki's initial repository setup is unresolved.

## Acceptance Still Separate

Native-speaker review, Windows/Linux Torrent packaging, older macOS compatibility,
live proxy reconnection and DHT behavior, sharing-cache restart acceptance and
extended crash/freeze soak must be reported as tested or untested individually.
A green macOS test suite is not proof for other platforms or a live deployment.

## Later Roadmap

Mixed DC/Torrent pieces, SQLite share storage, parallel TTH hashing, MCN1,
HBRI/SUDP, temporary sharing, richer chat/retry policies and compressed/indexed
lists are separate work, not promised features of the first prerelease.
