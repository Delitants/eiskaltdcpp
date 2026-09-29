# Proxy Routing And TLS

## Scope

Global Connection preferences configure DC outgoing routing. Torrent preferences
can follow the application route, require a proxy, use an explicit direct route,
or use a Torrent-only profile. A Torrent-only profile covers peers, DHT and
trackers together; it does not change DC settings. The public hub-list HTTP proxy
is a separately labeled route. This is not a whole-application VPN.

Strict proxy routes have no direct fallback. Established proxied peer connections
carry downloads and uploads. A proxy does not automatically provide unsolicited
incoming connections; passive-mode limitations still apply.

## SOCKS5 And GOST

Plain SOCKS5 UDP requires server support for UDP ASSOCIATE and a reachable relay.
A working TCP CONNECT alone does not prove UDP support. SOCKS5-over-TLS and
Shadowsocks bridge modes have separate transport limitations; do not assume
that selecting encryption enables UDP.

The GOST profile implements authenticated TLS negotiation and UDP-over-TCP
natively. No separate GOST client is required. Use a compatible server, a username
and password, and a trusted CA certificate. The server protocol must match the
GOST profile; it is not interchangeable with plain SOCKS5 or immediate TLS around
SOCKS5. Enable its UDP option for DHT, UDP trackers and outgoing uTP, subject to
those features' individual settings. UDP tunneling over TCP can add delay under
packet loss.

Proxy passwords are masked by default; the eye control temporarily reveals them.
Local profile credentials are not encrypted at rest, so protect profile files and
backups. No public proxy endpoint, password or private trust material is supplied
by this documentation.

## Certificate Management

DC peer TLS identity and proxy-server trust are different. Connection Advanced
provides management of the application's DC certificate. Fresh configurations
can generate a DC identity automatically, and healthy existing identities should
be retained. The default peer encryption policy allows encryption rather than
requiring every peer to support it.

For GOST, trust the CA supplied by the server administrator through the profile's
CA selection. Generating a new local certificate does not establish trust in an
unrelated server. Do not disable verification or publish private keys to work
around a certificate error.

## Diagnostics

Proxy tests run asynchronously from an unsaved settings snapshot without replacing
the active route. TCP tests use `google.com`; the UDP DNS query also uses
`google.com`. Ports and the numeric DNS resolver remain configurable.

TCP success verifies proxy negotiation and CONNECT, not a completed destination
TLS handshake or download. UDP success requires a matching DNS response through
the selected relay/tunnel. Neither proves every tracker or peer is reachable.
Use Cancel to stop the test without saving settings.

A server can accept numeric IP destinations but reject hostnames because of DNS,
routing or policy. A server rejection is not by itself a reason to fall back to a
direct connection. Inspect service logs and the separate TCP/UDP results.
