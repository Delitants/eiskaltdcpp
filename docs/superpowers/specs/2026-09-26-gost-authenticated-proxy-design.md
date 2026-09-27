# Authenticated GOST Proxy Support

Date: 2026-09-26
Status: Design for review; implementation and deployment have not started.

## Intent And Approved Connection Policy

Add native GOST support to Eiskalt's torrent proxy, without requiring an
external client process. Peers (downloads and uploads on established proxy
connections), trackers, and DHT use one authenticated encrypted route. Existing
plain SOCKS5 applications can use the same server listener.

The user selected mixed standard SOCKS5 and GOST-negotiated TLS on one port.
Eiskalt's new GOST mode requires TLS and authentication; accepting plain clients
on the server must never cause Eiskalt to downgrade. Ordinary clients' SOCKS5
credentials and relay traffic are not protected by the proxy transport.

Deployment identity, port, initial source-address allowlist, credentials, and
certificate material belong only in private deployment records outside the
checkout. No endpoint is shipped as an application default. Nothing is pushed
or released to GitHub as part of this task.

## Scope

- Add a distinct `GOST (authenticated TLS + UDP)` torrent proxy type. Preserve
  the meaning and saved values of Direct, SOCKS5, SOCKS5-over-TLS, and Shadowsocks.
- Add username, masked password, and optional private-CA certificate selection
  to the new profile, with validation and asynchronous TCP/UDP diagnostics.
- Configure only this Mac's torrent-specific profile after successful testing.
  Do not change its global DC proxy, shares, jobs, priorities, or security rules.
- Deploy a dedicated GOST service on the authorized host, alongside existing
  services, with mandatory authentication and a default-deny client allowlist.
- Provide a private operational runbook for credentials, certificate renewal,
  adding/removing allowed IPs, verification, and rollback.

Out of scope: routing the whole Mac, an external GOST client, global DC transport
changes, arbitrary inbound peer port forwarding, and a public release.

## Existing Architecture

`TorrentRuntime` chooses an upstream profile and uses `TorrentProxyAdapter` for
encrypted transports. Libtorrent sees an authenticated loopback SOCKS5 endpoint.
The adapter currently handles CONNECT only, rejects UDP ASSOCIATE, and reports
UDP unavailable. `dcpp::Socket` provides cancellable stream proxy connections and
TLS identity verification. `ProxyTestRunner` provides background diagnostics.

Extend these boundaries rather than embedding a GOST binary or rewriting the
torrent engine. Keep protocol framing and handshake helpers independently
testable and separate from Qt widgets.

## Wire Protocol And Trust

1. Dial the configured proxy and offer only GOST TLS-AUTH (`0x82`). Reject other
   selected methods, malformed replies, or an unsupported-method response.
2. Complete TLS 1.2 or newer, verifying the certificate chain, validity period,
   and configured hostname or IP identity before sending credentials.
3. Perform GOST's username/password exchange inside TLS. Require nonempty
   credentials and enforce the protocol's byte-length limits before connecting.
4. Use CONNECT for stream traffic and GOST UDP-TUN (`0xF3`) for datagrams. Match
   the exact selected upstream release's handshake and framing, not assumptions
   based on the method numbers alone.

This is distinct from immediate TLS followed by standard SOCKS5. Keep the
existing SOCKS5-over-TLS option unchanged. Do not implement automatic detection,
plain fallback, anonymous fallback, or trust-on-first-use acceptance.

System-trusted server certificates work by default. For this IP-address-based
deployment, generate a private CA and a server certificate with the exact IP SAN
and server-authentication usage. Transfer only the public CA certificate to the
Mac over verified SSH. Trust it only in the selected proxy profile, not in the
system trust store. Private keys stay on the server with restricted permissions.
Use a one-year leaf validity and document replacement before expiry. Missing,
expired, wrong-identity, or untrusted certificates stop the connection.

## UDP Adapter And Failure Behavior

The loopback adapter accepts authenticated UDP ASSOCIATE for GOST profiles and
returns a loopback relay address. Bind each association to its authenticated
control connection and expected local sender; do not create a LAN-accessible
relay. Convert standard SOCKS5 datagrams to/from GOST UDP-TUN frames on a separate
verified, authenticated TLS connection. Support IPv4, IPv6, and domain address
forms, preserving datagram boundaries and binary payloads.

Validate lengths and addresses before allocation or forwarding; reject unsupported
SOCKS fragmentation. Bound associations, buffers, workers, and queued datagrams.
Use cancellable background I/O and backpressure, never GUI-thread network work
or busy polling. Control closure and application shutdown release sockets and
workers. A failed tunnel invalidates its association; reconnect with bounded
backoff and never replay stale queued datagrams onto a new association.

The adapter advertises UDP capability for GOST, allowing the existing torrent
session policy to enable DHT, UDP trackers, and uTP when the user enables them.
If UDP fails, report it separately from TCP and keep all traffic fail-closed.
Other healthy proxied connections may continue. Local discovery, direct peer
connections, local destination DNS, and direct port mapping must remain disabled
where required by the existing strict proxy policy. IPv6 destinations must use
the proxy too; an IPv4 proxy connection must not cause direct IPv6 leakage.

UDP-over-TCP has head-of-line blocking under packet loss. This is a compatibility
tradeoff, not a claim of equal performance to a native encrypted UDP transport.

## Settings And Diagnostics

Store a separate GOST profile with stable serialized type values and preserve
all other profiles when switching. Include the trust configuration in runtime
configuration equality so a genuine trust change reconnects, while applying
unchanged settings does not restart sessions or unnecessarily rehash shared data.

Use the existing application-styled controls and translation workflow. Show a
clear explanation that GOST mode encrypts TCP and tunneled UDP. The diagnostic
button uses the same transport and trust rules as real transfers, the existing
user-editable destinations, and bounded asynchronous cancellation. Distinguish
negotiation, certificate, authentication, CONNECT, UDP-TUN, and destination
failures. Redact passwords and secret-bearing URLs from logs and exceptions.

Do not alter existing credential storage for unrelated profiles. Restrict the
local profile and private deployment files to the current user, and never put
live credentials into tests, screenshots, command arguments, source, or build
artifacts. Public tests use disposable loopback-only identities.

## Server Deployment

Use a pinned upstream GOST release, verify its artifact integrity and exact
protocol behavior, and run it as an unprivileged dedicated systemd service.
Authenticate both standard and TLS-AUTH clients. Enable standard UDP ASSOCIATE
and UDP-TUN, but leave BIND, multiplex BIND, remote forwarding, and management
APIs disabled. Disable debug/traffic recording in production.

Apply the authorized source IP allowlist before exposing the listener. Use GOST
admission control and narrowly scoped persistent host firewall rules. Do not
flush the host ruleset, change SSH access, or touch existing DHT, tracker, or web
listeners. Bind the service's client listener to IPv4 initially: no IPv6 client
address has been authorized. Outbound IPv6 destinations remain supported.

Standard UDP requires a client-facing relay port in addition to the TCP control
port. Reserve a bounded unused relay range for this service, restrict its ingress
to the same allowed client addresses, and retain authenticated association/source
checks. Verify the pinned release supports this configuration and rejects UDP
from a different source before production exposure. If those checks fail, do
not widen firewall access or silently deploy a different security policy.

Generate unique high-entropy credentials. Keep service configuration, keys, and
private deployment records outside the checkout with owner/group-only access.
Limit service privileges and resource use. Prevent proxying to loopback,
link-local/metadata, and private management destinations, including after DNS
resolution, while retaining public Internet TCP/UDP and required DNS resolution.

Maintain one private allowlist source and an atomic validate/apply operation that
updates admission and firewall rules consistently. It must not temporarily allow
everyone on reload or an empty list. The runbook gives the actual file paths and
commands to add an IP, apply changes, test admission, and revert. Adding an IP
does not replace the authentication requirement.

## Verification And Acceptance

- Start with failing tests for negotiation, authentication, UDP framing, profile
  persistence, and transport capability selection.
- Test valid and invalid credentials; refused/changed negotiation; malformed,
  fragmented, truncated, and oversized frames; and certificate identity, trust,
  expiry, and private-CA handling. A downgrade must send no credentials.
- Test IPv4, IPv6, and domain destinations, interleaved datagrams, binary payloads,
  sender isolation, cancellation, reconnect, bounded resource use, and idle CPU.
- Run compatibility tests against the pinned real GOST binary, not only mocks.
  Verify plain authenticated TCP and UDP clients use the same listener.
- Run torrent integration tests for proxied download/upload, DHT, UDP trackers,
  and uTP. Verify failure does not produce direct traffic or local target DNS.
- Exercise native macOS settings and diagnostics, plus existing core, Qt, torrent,
  publication lifecycle, and no-op-settings regressions. Build Windows support
  where the existing build environment permits; report any untested platform.
- Verify the live listener from the authorized Mac, wrong-password rejection,
  and denial from a non-allowed source under controlled tests. A firewall listing
  alone is not end-to-end proof. If no second source is available, record that
  specific acceptance gate as unverified instead of claiming it passed.
- Verify existing server services and firewall persistence remain healthy. Keep
  remote setup reversible and preserve a private pre-change snapshot.
- Back up the installed Mac app and profile. Replace only while Eiskalt is closed;
  never force quit. Configure the torrent profile only after the new app is
  installed. No active torrent payload transfer is needed to modify settings.
- Scan all newly publishable changes and packaged artifacts for deployment
  identifiers and secrets before any future publication. Do not change historical
  unrelated files or publish anything in this task.

Completion requires separately reporting application implementation, installed
build, server deployment, local configuration, tested traffic, and any remaining
verification gaps. Spec approval is followed by an implementation plan; this
document itself does not claim those stages are complete.

## References

- GOST SOCKS5, negotiated encryption, and UDP-TUN:
  https://gost.run/en/tutorials/protocols/socks/
- GOST admission control: https://gost.run/en/concepts/admission/
- Reference implementation (pin matching release revisions before development):
  https://github.com/go-gost/x/tree/master/connector/socks/v5
- Server UDP relay and source filtering:
  https://github.com/go-gost/x/blob/master/handler/socks/v5/udp.go
