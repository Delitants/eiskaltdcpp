# Authenticated GOST Proxy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add authenticated, verified GOST TLS with tunneled UDP to Eiskalt's torrent proxy and deploy a privately configured, allowlisted mixed-mode server.

**Architecture:** Extend the native stream transport and authenticated loopback adapter; do not embed a GOST client. Libtorrent continues speaking standard SOCKS5 locally while the adapter translates UDP to the GOST tunnel. Deploy only after real-server interoperability and failure-path tests pass.

**Tech Stack:** C++20, Qt 6, OpenSSL, libtorrent, Catch2, GOST 3.3.0, Debian systemd/nftables.

**Spec:** `docs/superpowers/specs/2026-09-26-gost-authenticated-proxy-design.md`

**Execution update (2026-09-26):** The user selected Native/inline execution.
Continue remaining work with `superpowers:executing-plans`; completed tasks are
not repeated. The approved DC++ recommendations are tracked in the linked
[selective-port plan](2026-09-26-dcplusplus-selective-ports.md). Independent
maintenance can proceed without changing this plan's deployment/review gates.

## Global Constraints

- Eiskalt's new GOST mode requires TLS and authentication; accepting plain clients on the server must never cause Eiskalt to downgrade.
- Complete TLS 1.2 or newer, verifying the certificate chain, validity period, and configured hostname or IP identity before sending credentials.
- Do not implement automatic detection, plain fallback, anonymous fallback, or trust-on-first-use acceptance.
- Configure only this Mac's torrent-specific profile after successful testing.
- No endpoint is shipped as an application default. Nothing is pushed or released to GitHub as part of this task.
- Preserve existing global DC settings, shares, torrent jobs, firewall rules, and remote services. Never force quit Eiskalt or Docker.
- Current checkout contains extensive pre-existing work, including untracked product files. Capture a private baseline before editing; do not recreate from HEAD or stage whole dirty files indiscriminately. Make task commits only from reviewed new deltas, excluding unrelated baseline work.
- Use one build at a time, with `-j2`. Native Cocoa checks require a real display and `QT_ACCESSIBILITY=0`; offscreen tests are not native acceptance.
- All live endpoints, allowlist entries, credentials, keys, logs, backups, and operations notes stay outside the checkout. Use disposable loopback data in repository tests.

## Review Focus

- A CA file replaced at the same path must invalidate the immutable trust snapshot and reconnect safely (Tasks 1, 4).
- Unicode credentials must be bounded by UTF-8 bytes, not characters; rejected negotiation must send no credentials (Tasks 1, 2).
- Closing or replacing a UDP control connection must not let late packets reach its successor (Task 3).
- Failure during allowlist changes must leave access restricted, not briefly open or inconsistent (Task 6).
- IPv4-mapped IPv6, DNS resolving to private addresses, and same-address peers must not bypass destination or association policy (Tasks 3, 5, 6).

## File Map And Test Commands

Core transport: `dcpp/GostProtocol.{h,cpp}` (new bounded framing), `dcpp/Socket.{h,cpp}` (verified handshake and streams), `dcpp/CMakeLists.txt`, `tests/test_gostprotocol.cpp` (new), `tests/test_streamproxy.cpp`, `tests/CMakeLists.txt`.

Torrent routing: `torrent/TorrentTypes.h`, `torrent/TorrentSettings.cpp`, `eiskaltdcpp-qt/src/TorrentProxyAdapter.{h,cpp}`, new `eiskaltdcpp-qt/src/GostUdpRelay.{h,cpp}`, `eiskaltdcpp-qt/src/TorrentRuntime.cpp`, `torrent/TorrentEngine.cpp`, both Qt CMake source lists.

UI/diagnostics: `eiskaltdcpp-qt/src/SettingsTorrent.cpp`, `eiskaltdcpp-qt/src/ProxyTestRunner.{h,cpp}`, existing `eiskaltdcpp-qt/translations/*.ts`; never replace the app's shared style sheet.

Tests: existing `tests/qt/test_{settingstorrent,torrentproxyadapter,proxytestrunner,torrentruntime}.cpp`, `tests/torrent/test_torrent.cpp`; new disposable fixtures under `tests/proxy/` and `tests/qt/test_gostinterop.cpp`.

Build with `cmake --build build-torrent --target eiskaltdcpp-tests eiskaltdcpp-qt-tests eiskalt-torrent-tests -j2`.
Commands below use `CORE=build-torrent/tests/eiskaltdcpp-tests`, `QT=build-torrent/tests/qt/eiskaltdcpp-qt-tests`, `BT=build-torrent/tests/torrent/eiskalt-torrent-tests`; define these shell variables first. Give every new test a `[gost]` tag plus its task tag. Expected success: exit 0 and all matching tests passed; zero matching tests is failure.

## Task 1: Persist And Validate A Separate GOST Profile

**Interfaces:** Append `ProxyType::Gost = 4` without changing values 0-3. Add `QString caFile` and runtime-only `QByteArray caPem` at the end of `ProxyConfig`; add `Settings::gostProxy`. Existing `loadSettings`, `saveSettings`, `selectedProxy`, and `validateProxy` signatures stay unchanged. Add `bool loadProxyTrust(ProxyConfig&, QString* error)` in new `torrent/ProxyTrust.{h,cpp}`; empty path means system roots, supplied paths must contain valid PEM CA certificates and be at most 1 MiB.

- [ ] Write `[gost][profile]` tests in `tests/torrent/test_torrent.cpp`: `REQUIRE(int(ProxyType::Gost) == 4)`, round-trip GOST credentials/CA path, legacy JSON without a GOST profile unchanged, unknown type rejected, `caPem` never serialized, and inactive profiles preserved. Assert empty credentials and 256-byte UTF-8 fields fail; 255-byte valid fields pass.
- [ ] Run `$BT '[gost][profile]'` and record the expected missing-interface/failing-assertion result before implementation.
- [ ] Implement the interfaces above and register `ProxyTrust.cpp` in `torrent/CMakeLists.txt`. Build a validated CA snapshot before runtime equality checks; configuration comparison includes CA contents so replacement at the same path is detected. An unreadable supplied CA must not silently select system roots. New GOST defaults have an empty host and no credentials.
- [ ] Rebuild and run `$BT '[gost][profile]'`. Assert settings replacement remains atomic and owner-only on Unix; no-op round trips preserve all unrelated keys supported by the existing schema. Review the delta before a scoped task commit.

## Task 2: Verified TLS-AUTH And Bounded UDP Framing

**Interfaces:** Append `StreamProxyConfig::Gost`; add `std::string caPem`. Add `Socket::gostOpenUdpTunnel(const StreamProxyConfig&, uint32_t timeout)` and private `gostHandshake(const StreamProxyConfig&, uint32_t timeout)`; `proxyConnect` dispatches GOST CONNECT. Existing stream read/write/wait/cancellation APIs remain authoritative.

New `dcpp::gost::Datagram` contains `std::string host`, `uint16_t port`, `ByteVector payload`. `encodeTunnel(const Datagram&) -> ByteVector` throws on invalid inputs. `decodeTunnel(std::span<const uint8_t>, Datagram&, size_t& consumed) -> DecodeResult` returns `Complete`, `NeedMore`, or `Invalid`; no unbounded allocations. Helpers for standard SOCKS datagrams use the same data type, with ordinary RSV=0/FRAG=0 checks.

- [ ] Write `[gost][wire]` tests with disposable certificates: exact offered method `05 01 82`; selection 00/02/80/FF rejected without credentials; RFC1929 auth only after verified TLS; invalid auth version/status rejected. Assert expired, wrong-IP/name, unknown-CA and missing-CA failures, TLS below 1.2 failure, successful private/system-root paths, trickle deadlines, and cancellation.
- [ ] Add framing tests: binary/empty payload, IPv4/IPv6/domain, fragmented stream reads, two concatenated frames, maximum 65507-byte payload, oversized length, invalid ATYP/port, truncated address, nonzero standard FRAG, and Unicode credential byte boundaries. Match GOST 3.3.0's `x/v0.16.0` and `gosocks5/v0.5.0`: tunneled RSV carries payload length and FRAG is 0xFF, not ordinary SOCKS UDP framing.
- [ ] Run `$CORE '[gost][wire]'` for RED, then implement the interfaces in the mapped core files. Preserve immediate SOCKS5-over-TLS semantics and all non-GOST policy defaults; forbid `verifyTls=false` on GOST. Use one overall handshake deadline, not one timeout per byte.
- [ ] Rebuild; run `$CORE '[gost][wire]'` and `$CORE '[streamproxy]'`. Confirm fail-closed tests have no target connection and no credentials in failure strings. Commit only reviewed task deltas.

## Task 3: Loopback UDP Associations Over GOST

**Interfaces:** `GostUdpRelay` is a worker-owned helper, not a GUI object: constructor takes an immutable `Socket::StreamProxyConfig`; `start(const QHostAddress& localBind, const QHostAddress& expectedSender, quint16 expectedPort, QString* error) -> quint16` returns bound relay port or 0; `requestStop()` cancels, `join()` waits, destructor stops/joins. It shares Task 2's codec and native Socket APIs. `TorrentProxyAdapter::start`, `stop`, and `endpoint` signatures stay unchanged.

- [ ] Write `[gost][adapter]` tests: authenticated local UDP ASSOCIATE returns loopback only; alternate sender IP/port is rejected; control EOF closes the relay; a new association cannot receive late old replies; malformed frames never reach upstream. Assert GOST advertises UDP, legacy encrypted modes still do not, and uploads survive TCP half-close.
- [ ] Run `QT_QPA_PLATFORM=offscreen "$QT" '[gost][adapter]'` for RED.
- [ ] Implement the relay and adapter dispatch. Maximum 64 total adapter workers, at most 8 UDP associations, 512 KiB buffered per association, 65507-byte payload limit, and 10-second setup deadline. Enforce explicit requested sender port; for an unspecified port lock to the first valid loopback datagram and retain the control association boundary. All network work stays in workers; poll waits must block when idle. Reject incoming standard fragments.
- [ ] On tunnel loss discard queued datagrams, close its local association, and let libtorrent re-associate. Rate-limit failed upstream establishment using a shared 1/2/4/8/16/30-second capped backoff, reset after success, with cancellation interrupting waits. Never reconnect a stale association or silently bypass the adapter.
- [ ] Rebuild and run the adapter tests plus existing `[adapter]` tests. Check native idle CPU over 30 seconds, bounded buffers under backpressure, and shutdown within 2 seconds even during TLS/UDP stalls. Add failure output if these gates are missed. Review/commit the task delta.

## Task 4: Settings, Real Diagnostics, And Session Routing

**Interfaces:** Existing `ProxyTestRunner::start(StreamProxyConfig, ProbeTargets, bool, int)` selects Task 2's GOST transport; append diagnostic statuses `NegotiationFailed`, `CertificateFailed`, and `TunnelFailed` without changing old meanings. `TorrentRuntime` snapshots Task 1's trust before starting the adapter. GOST UI object names: `torrentGostCaFile`, `torrentGostCaBrowse`; reuse the existing host/user/password controls and profile-switch pattern.

- [ ] Write `[gost][routing]` and `[gost][ui]` tests for independent profile switching, masked password, CA picker, persistent choices, 1x/2x style, no scrolling three-item type popup, and translated GOST explanatory copy. Assert immutable probe destinations, matching DNS response required for UDP success, no live-session mutation, cancellation, and diagnostic credential redaction.
- [ ] Add runtime assertions: healthy GOST enables configured DHT/UDP trackers/uTP via the adapter; direct peer paths/LSD/UPnP remain disabled by strict policy. Identical settings preserve session and managed hash identities; changed CA contents recreate transport; missing/invalid trust stops routing. Global DC settings remain unchanged.
- [ ] Run `QT_QPA_PLATFORM=offscreen "$QT" '[gost][ui],[gost][routing]'` for RED. Implement settings controls, `ProxyTestRunner`, capability selection in runtime/engine, and source translations using the repository workflow. Do not claim TCP CONNECT alone proves working TLS application traffic or UDP routing.
- [ ] Rebuild, run focused Qt tests and `$BT '[gost]'`, then native Cocoa UI tests at scale 1 and 2 with `QT_ACCESSIBILITY=0`. Save disposable-profile screenshots only; verify timeout does not freeze the UI. Review/commit task changes.

## Task 5: Real GOST And End-To-End Torrent Acceptance

**Interfaces:** New `tests/proxy/gost-fixture.sh start|stop` accepts a private temporary fixture directory and `GOST_BIN`; start outputs only nonsecret paths/ports. Generate throwaway CA/key/auth files with mode 0600. New `tests/qt/test_gostinterop.cpp` uses this fixture and existing libtorrent test peers. No production endpoint or secrets are accepted by the public fixture.

- [ ] Obtain GOST 3.3.0 for the test host and intended server platform only after execution approval, verify upstream checksums/provenance, and record them privately. Verify `gost -V`; reference its pinned `x/v0.16.0`, `gosocks5/v0.5.0`, not master. Missing artifacts or integrity failures block deployment, not justify a silent version change.
- [ ] Write `[gost][interop]` failures for direct Eiskalt TLS-AUTH CONNECT/UDP-TUN against real GOST, plus plain authenticated SOCKS5 TCP/UDP on the same listener. Verify wrong auth and certificate fail, UDP source isolation works, and standard relay ports stay inside the configured test range.
- [ ] Exercise controlled, freely redistributable/generated torrent data: proxy TCP download and upload, uTP, UDP tracker announce, DHT query/response, IPv6 peer/UDP destinations, remote hostname resolution, server restart, and session resume. Restrict the torrent test process's external network to the proxy during acceptance; collect connection/DNS evidence to prove no direct fallback, including after failures.
- [ ] Run `GOST_BIN="$GOST_BIN" QT_QPA_PLATFORM=offscreen "$QT" '[gost][interop]'` and the relevant `$BT '[gost]'` integration cases. A skipped IPv6 route is a reported gap, not a pass. Audit error logs for secrets and preserve all RED evidence before making corrections. Review/commit fixtures and corrections only.

## Task 6: Private, Reversible Server Deployment

**Files:** Outside checkout only: a user-only local deployment directory; server `/etc/eiskalt-gost/{config.yaml,allowed-clients.txt,tls/}`, `/etc/systemd/system/eiskalt-gost.service`, `/usr/local/sbin/eiskalt-gost-allowlist`. Capture existing firewall/services before modifying any path; if a proposed path already exists, stop and reconcile rather than overwrite.

**Interfaces:** Root-only `eiskalt-gost-allowlist check|apply|rollback` reads `allowed-clients.txt` (one canonical IPv4 address or CIDR per line) and renders GOST admission plus a dedicated nftables set. Empty/invalid input is rejected. Private notes store the authorized listener port/addresses and actual reserved UDP range; no values are copied into this plan.

- [ ] Recheck host identity, source address, port availability, firewalls, running services, and free relay range. Preserve complete private snapshots. Use a 64-port unused high UDP range and GOST's `udp.bindRange.min/max`; do not reuse DHT/tracker ports. No live change yet.
- [ ] Rehearse config, credentials, TLS-AUTH, admission, firewall reload, and rollback with a disposable service/network namespace. Test valid/invalid and empty allowlists; process crash between update stages; wrong-source UDP; private/metadata destinations, including DNS names resolving there and IPv4-mapped IPv6. Failures must remain closed.
- [ ] Implement the root-owned allowlist helper under a file lock: validate both candidates, stop only GOST, install the dedicated firewall rules atomically while closed, atomically replace admission config, restart and health-check. On failure restore previous files/rules and restart the previous configuration. Restrict all clients until success; this short proxy-only outage is preferable to inconsistent access.
- [ ] Generate a unique account and 32 random bytes encoded as a password; never pass secrets via argv or print them. Generate private CA plus 365-day IP-SAN server certificate. Service user can read the leaf key/config, not the CA signing key. Create a user-readable private handoff record and public-CA copy via verified SSH; directories 0700, private files 0600 except narrowly required service group reads.
- [ ] Install verified binary/service as an unprivileged dedicated user. Bind IPv4 client ingress only, require auth for plain and TLS-AUTH, disable BIND/remote forwarding/APIs/recording, and enable UDP relay/tunnel. Apply service-scoped resource limits and destination filtering after DNS using UID-scoped egress rules; explicitly accommodate only necessary DNS and replies to allowed clients. Do not flush host firewall or alter other services.
- [ ] Before exposure apply admission plus persistent firewall to the authorized allowlist. Test permitted Mac TCP/UDP, bad credentials, and a controlled unlisted source; if no second external source is available distinguish local policy tests from unverified external denial. Check existing DHT/tracker/web health and persistence configuration without rebooting the shared host.
- [ ] Write the private runbook with exact paths, allowlist edit/check/apply/rollback commands, CA/leaf renewal, credential rotation, restart impact, and acceptance results. Public repository records only generic feature evidence; no deployment commit or GitHub push.

## Task 7: Final Review, Build, And Mac Configuration

**Files:** Generic usage notes in `docs/torrent-building.md`; private QA, application/profile backups, installed app, and local torrent profile outside checkout. No new global defaults.

- [ ] Review all task deltas against the original dirty baseline, then run full `$CORE`, `QT_QPA_PLATFORM=offscreen "$QT"`, and `$BT` suites serially. Include stopped/paused peer counts, no-op settings, managed DC hash reuse, and certificate/UDP failure tests. Require a fresh independent review; resolve substantive findings before installation.
- [ ] Build the existing macOS app target with `cmake --build build-torrent --target eiskaltdcpp-qt -j2`; use the existing self-contained packaging and Cocoa backport workflow. Verify native Cocoa regression tests, library load paths, ad-hoc signature, and protocol/file handlers. Do not claim notarization or an older OS minimum than dependencies support. Attempt the established Windows build only if its toolchain is available; report it otherwise.
- [ ] Ensure Eiskalt is closed; if it is running, stage the build and ask the user to close it. Back up the current app/profile, compare package contents, install atomically with rollback, and verify executable hash and signature. Do not launch a second instance or kill the live app.
- [ ] Configure the installed app's torrent-only GOST profile with the private server endpoint, auth, and CA snapshot. Preserve all global DC settings and torrent data. Verify the real settings page's TCP/UDP tests and live transport logs without exposing secrets. Do not start existing payload transfers just to demonstrate connectivity; use the controlled acceptance fixture.
- [ ] Scan newly publishable source and app contents against private deployment identifiers/secrets without echoing them; keep results outside checkout. Update generic docs and private runbook, then report separately: implemented, tested, server deployed, Mac installed/configured, residual gaps, and the actual future allowlist command. Nothing is pushed.

## Execution Order And Review

Tasks 1-5 precede live deployment. Tasks 1 and 2 have separate test cycles but converge
at Task 3; Tasks 4 and 5 must use their agreed interfaces. Task 6 does not expose a
listener until its policy and real-binary checks pass. Task 7 is the only step that
replaces the user's app or changes their local profile.

Recommended execution: subagent-driven, one implementation task at a time with an
independent review before proceeding. Do not run concurrent builds or concurrent
edits to shared transport/settings files. Native execution is also possible with
one final independent review, but catches cross-boundary mistakes later.

## Versioned References

- https://github.com/go-gost/gost/blob/v3.3.0/go.mod
- https://github.com/go-gost/x/blob/v0.16.0/connector/socks/v5/selector.go
- https://github.com/go-gost/x/blob/v0.16.0/internal/util/socks/conn.go
- https://github.com/go-gost/x/blob/v0.16.0/handler/socks/v5/metadata.go
- https://github.com/go-gost/x/blob/v0.16.0/handler/socks/v5/udp.go
