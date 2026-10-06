# Development Status

Checkpoint: 2026-10-05. `v3.0.0-pre.1` remains this development checkpoint's release target.

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

The earlier September 29 macOS checkpoint passed 1,427 cases and 61,767 assertions
across core, Qt, Torrent and window suites, with the real-GOST tests enabled.
This is development verification, not an installed or published release.

A previous versioned macOS ARM64 candidate passed bundle dependency and
ad-hoc signature checks. A packaging regression also covers extensionless,
non-executable framework binaries, which must not escape the link/OS audits.
Its isolated CLI startup passes with network, real-profile and Homebrew reads
denied. The candidate declares macOS 26 because of its bundled dependencies;
testing was on macOS 27, not macOS 26 or older. The approved release split is an
Intel-only macOS 14+ ZIP and an Apple-Silicon-only macOS 26+ ZIP, with explicit
architecture/minimum-OS filenames. Separate thin Intel and Apple Silicon
verification builds now pass. Intel verification runs through Rosetta, not native
Intel hardware; native minimum-OS acceptance remains pending. The earlier ARM64
candidate is not the final rebuilt release for either architecture.
Dependency notices and matching source materials have been staged
for the exact candidate; final distribution checks are still required. Ad-hoc
verification is not Developer ID signing or notarization, and CLI startup is
not live GUI QA.

The eight optional real-GOST interoperability cases have passed on macOS using
disposable loopback identities and servers. Coverage includes authenticated TLS,
TCP/uTP Torrent transfers on IPv4/IPv6, UDP tracker and DHT exchanges, remote DNS,
and completed-session resume after a fixture-server restart. Direct destination
access is denied in the application fixtures. These checks do not contact or
verify a deployed proxy server, and do not establish partial-piece crash recovery.

## Updater And Quit Checkpoint

Built-in update settings, verified staging and committed-normal-quit installation
are integrated in development. Download routing, signature/archive acceptance
and installer recovery are separate checks; a transport success alone is not an
accepted update or installed release. Production signed-feed activation remains
a release gate.

Core checkpoint writes now use owned background workers after lifecycle
retirement. Settings Save callbacks remain on the startup owner. Pending or failed
receipts retain the application; a timed-out quit requires explicit retry and
cannot install an update merely because its worker finishes later. This is not a
total hard deadline for lifecycle joins, serialization or every filesystem call.

Focused persistence/quit checks and all three applicable window layouts pass on
ARM and Intel/Rosetta. Regular core and Qt suites also pass on both; the Intel
Torrent suite passes. An ARM Torrent peer-policy rejection-counter failure is
retained for investigation despite a passing focused rerun. Earlier intermittent
bootstrap, publication and bind causes are not waived by green reruns.

Peer-policy test failures now include the policy variant, listener ports, proxy
forwarding and available leech peer/alert state. A deliberately invalid loopback
endpoint reproduces the counter failure without payload transfer and verifies
the diagnostic output. It does not establish the cause of the retained natural
failure or justify changing the security policy.
The diagnostic observers were reviewed and corrected to freeze the original
assertion values before collecting extra details. A controlled late-counter
change verifies that boundary. Corrected full Torrent suites pass on ARM and
Intel/Rosetta (152 cases each); the original intermittent failure remains open.

Nine proxy-test and Torrent-menu runtime keys have been added in all 25 catalogs.
Catalog format and compiled-string checks do not establish complete localization:
23 extracted runtime entries per catalog, inherited unfinished translations and
native-speaker review remain pending. Complete resource/dependency provenance,
reproducibility and independent whole-branch review also remain separate gates.

## Before Prerelease Publication

1. Finish retained regression diagnoses, localization, resource/provenance checks
   and independent review of the final source candidate.
2. Complete native/minimum-OS and live GUI/lifecycle acceptance for the approved
   Intel-only macOS 14+ and Apple-Silicon-only macOS 26+ split.
3. Rebuild final reviewed-source assets; preserve matching dependency notices and
   source materials. Complete numeric build allocation, signing, updater acceptance
   and guarded closed-app installation/readback. Keep profiles, credentials,
   private infrastructure configuration and QA artifacts out of release packages.
4. Publish the maintained wiki, approved screenshots and prerelease assets, then
   confirm public readback. Activate signed feeds only after the final gates;
   signed feeds are last. Wiki sources remain available in `docs/wiki` meanwhile.

## Acceptance Still Separate

Native-speaker review, Windows/Linux Torrent packaging, older macOS compatibility,
live proxy reconnection and DHT behavior, sharing-cache restart acceptance and
extended crash/freeze soak must be reported as tested or untested individually.
A green macOS test suite is not proof for other platforms or a live deployment.

## Later Roadmap

Mixed DC/Torrent pieces, SQLite share storage, parallel TTH hashing, MCN1,
HBRI/SUDP, temporary sharing, richer chat/retry policies and compressed/indexed
lists are separate work, not promised features of the first prerelease.
