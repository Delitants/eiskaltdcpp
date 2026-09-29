# Opt-In Real GOST Acceptance

These fixtures use generated payloads and disposable loopback identities only.
They do not load an application profile or accept a production endpoint, supplied
credentials, certificate, torrent, or server configuration.

Requirements: macOS with Seatbelt, Python 3.9+, OpenSSL CLI, an independently
checksum-verified GOST **3.3.0** binary, and the existing Qt+torrent test build.
The upstream protocol references are `go-gost/x v0.16.0` and
`go-gost/gosocks5 v0.5.0`, not their default branches.

```sh
umask 077
QA=$(mktemp -d /private/tmp/eiskalt-gost-qa.XXXXXX)
export GOST_BIN=/absolute/path/to/verified/gost
export GOST_QA_DIR="$QA"
export GOST_CAPTURE=1 # dedicated loopback fixture ports only; requires BPF access
QT_QPA_PLATFORM=offscreen QT_ACCESSIBILITY=0 \
  build-torrent/tests/qt/eiskaltdcpp-qt-tests '[gost][interop]'
build-torrent/tests/torrent/eiskalt-torrent-tests '[gost]'
```

Without `GOST_BIN`, public interop cases explicitly skip. When it is set, missing
binaries, unsupported isolation, IPv6 failure, and actual transport failure are
failures, never skips. Hidden child cases require private fixture state and are
not standalone acceptance commands.

`gost-fixture.sh start|stop PRIVATE_TEMP_DIR` accepts an empty, caller-owned,
mode-0700 directory below the system temporary directory. Start emits only
nonsecret paths/ports. It generates mode-0600 CA/key/auth/config files and starts
GOST under a loopback-only Seatbelt policy. Stop requests supervised shutdown
through a private Unix socket. The supervisor has a 15-minute inactivity limit.
Do not remove a running fixture directory; stop it first. Evidence and disposable
identities are intentionally retained after testing. Treat the whole QA tree as
private; GOST logs and packet evidence can contain the throwaway identities.

## Application Process Boundary

The hidden peer helper runs a real TorrentEngine peer plus HTTP tracker and a
real TorrentProxyAdapter, all confined to loopback. Echo/DNS/UDP tracker/DHT and
GOST run in separate supervised processes/threads outside the engine client.
Peer metadata has no trackers: service counters cannot be produced by the peer.

The real adapter creates a fresh UDP port per association. A static Seatbelt
allowlist containing only its initial TCP port therefore blocks its UDP path.
`PinnedGostAdapter.h` is a test-only port-forwarder, not a product transport. It
prebinds twelve UDP slots and forwards authentication, CONNECT, and datagram
payloads to the real adapter. Only UDP ASSOCIATE source/BND ports are translated.
It cannot dial torrent destinations. The client allows only those exact ports,
the fixed forwarder TCP port, and GOST's loopback TCP port before constructing
TorrentEngine. Direct TCP and UDP attempts to both IPv4/IPv6 peer and DNS ports
must return EPERM. Client access to mDNSResponder's named Mach service is denied.

A separate real-adapter test (without the port-forwarder) validates UDP source
port pinning, control-EOF invalidation, IPv6 delivery, and bad/anonymous auth.
The standard GOST listener's client-IP filtering cannot be proven by sending
from a second IPv4 source on macOS without an interface alias; no alias is added.
The configured standard relay range is 48000..48063, checked on live replies.

Generated two-way transfers separately select TCP-only/uTP-only and IPv4/IPv6
peers. A separate generated UDP-only torrent forces an actual tracker announce.
A named controlled DHT bootstrap requires a matching KRPC response through the
real SOCKS/adapter/GOST path. File byte comparisons, job counters, tracker/DHT/DNS
counters, private child logs, and real GOST logs form the evidence. Session resume
currently covers completed jobs and a new CONNECT after GOST restart, not partial
piece recovery after a process crash. Offscreen acceptance is not Cocoa UI QA.
