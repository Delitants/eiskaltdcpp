# Optional Torrent Engine

The native core now requires c-ares 1.18.1 or newer for bounded, cancellable proxy
endpoint lookup. Install `c-ares` with Homebrew/vcpkg or `libc-ares-dev` on Debian
build hosts. A dynamically linked Linux package also needs `libc-ares2` at runtime.
Standalone torrent-backend builds use the shared OpenSSL CA validator but do not
require the core resolver. Global GOST routing is available in connection
preferences; installing this dependency does not configure or deploy a proxy.

The Torrent milestone uses a separate libtorrent-rasterbar engine. It does not
merge DC and Torrent pieces or turn existing DC shares into Torrents.

Torrent-enabled builds start the 3.x application series at 3.0.0. DC-only builds
retain their existing version selection. This major version does not migrate or
reset the existing DC configuration. A local QA install is not a public release.

`USE_TORRENT` is currently OFF by default while integration verification is in
progress. Existing release workflows remain DC-only until explicitly updated and
verified. Enabling it requires the Qt6 frontend, C++20, and libtorrent-rasterbar
2.1 or later with its CMake package. The version tested during development is
2.1.1 (Homebrew formula revision 1). Do not substitute an ABI-incompatible debug
library or mix a different libtorrent version into the final package.

## macOS Development

```sh
brew install libtorrent-rasterbar
cmake -S . -B build-torrent \
  -DUSE_TORRENT=ON -DUSE_QT6=ON -DUSE_GTK3=OFF -DBUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release -DEISKALT_MACOS_DEPS_PREFIX=/opt/homebrew \
  '-DEISKALT_MACDEPLOYQT_LIBRARY_PATHS=/opt/homebrew/lib;/opt/homebrew/opt/webp/lib'
cmake --build build-torrent -j2
QT_QPA_PLATFORM=offscreen ./build-torrent/tests/qt/eiskaltdcpp-qt-tests
./build-torrent/tests/eiskaltdcpp-tests
./build-torrent/tests/torrent/eiskalt-torrent-tests
```

Network tests bind isolated loopback fixtures and need socket permissions.
Retain the project's normal DC dependencies and configuration options. For an
offline rebuild, point `FETCHCONTENT_SOURCE_DIR_CATCH2` at an existing Catch2
checkout instead of downloading another copy.

The currently installed Homebrew libtorrent/OpenSSL dependencies on this
development host target macOS 26. A successful local build is not proof of macOS
13/14 compatibility. Release packages must build all dependencies for their
advertised minimum OS, bundle libtorrent and its transitive dependencies, and
pass signing and clean-machine launch checks. Do not distribute a bundle that
depends on `/opt/homebrew` paths.

## Windows And Ubuntu

Use matching-architecture Qt6 and libtorrent 2.1.x CMake packages built with the
same compiler/runtime as the application. Ubuntu's older distribution
`libtorrent-rasterbar-dev` packages may be 2.0.x and do not satisfy this API.
Keep `USE_TORRENT=OFF` rather than silently building against 2.0 headers.

Windows installers must include the matching libtorrent DLL and its runtime
dependencies. Ubuntu packages must declare the actual linked library dependency
or ship an independently verified private library; AppImages must include the
library. These platform-specific Torrent packaging gates have not passed yet.

## Routing And Sharing

Torrent management now opens as an internal application tab. Live Torrent
downloads and uploads/seeding appear in the existing bottom Transfers panel
alongside DC transfers, including when the management tab is hidden. The
Protocol column distinguishes DC++ from Torrent; equal filenames are not
merged. Pause/resume/stop/details actions route to the Torrent engine rather than
DC-only peer commands. Actual mixed DC/Torrent piece downloading is not yet
implemented. Stopped jobs remain in the Torrents tab, not in live transfers.
Build/test checkpoints and live acceptance are separate; see the current
[development status](wiki/Development-Status.md).

Application SOCKS5 can be used natively. SOCKS5-over-TLS and Shadowsocks use an
authenticated loopback-only TCP bridge, with no direct fallback. For those
two bridge modes, Torrent DHT, uTP and UDP trackers are unavailable and shown as
disabled. GOST has a separate encrypted UDP tunnel described below.
WebTorrent/WebRTC and WebSocket trackers are not supported in this
milestone; they are disabled regardless of route, including STUN discovery.
This limitation does not change existing DC proxy settings.

The Torrent preferences have a separate Proxy tab. Routing can inherit the
application connection, require its proxy, use direct networking, or use a
Torrent-only profile for peers, DHT and trackers together. Changing a Torrent
profile never writes the DC proxy settings. SOCKS5, Shadowsocks and GOST retain
independent endpoints and credentials when switching types. The local settings
file is owner-readable/writable and stores credentials without encryption;
protect profile backups accordingly. Version-1 Torrent settings remain readable;
new saves use version 2 without resetting existing choices.

### GOST Authenticated TLS And UDP

Connection preferences provide an independent global GOST profile. Selecting it
routes DC hubs, outgoing peer transfers and DC UDP through authenticated TLS,
including remote destination DNS. DC operates effectively in passive mode;
saved incoming/direct-mode preferences are not overwritten. DC DHT can bootstrap
and exchange requests through the outbound tunnel without advertising a local
or public listening port. The separately labeled public hub-list HTTP proxy
remains an explicit exception; this feature is not a whole-application VPN.

Torrent **Follow application** inherits the validated global profile, including
CA bytes and route revocation. It does not independently reread the CA file.
Applying an unchanged profile preserves pending and active Torrent sessions;
a genuine profile/trust change revokes old DC and inherited Torrent traffic.
Torrent-only profiles remain independent. Test buttons use unsaved snapshots
without saving or replacing active routes. Cancel leaves the profile untouched.

The separate GOST profile uses native TLS-AUTH negotiation, followed by verified
TLS and mandatory username/password authentication. No external GOST client or
bundled GOST executable is needed. This is not immediate TLS wrapped around
ordinary SOCKS5: select the matching server protocol rather than reusing the
legacy SOCKS5 TLS checkbox. Eiskalt rejects anonymous negotiation, untrusted
certificates, and downgrade attempts without falling back to a direct route.

Leave the CA certificate field empty to use system trust, or select a PEM CA
file for a private server. The configured server hostname or IP must match its
certificate. Private CA trust applies only to this profile, not the system
trust store. A missing or invalid supplied CA file blocks the route. Changing
the contents of the selected CA file is detected when settings are reapplied;
unchanged settings do not recreate the route or invalidate managed DC hashes.

The GOST UDP option enables authenticated UDP-over-TCP tunneling through the
encrypted route. DHT, UDP trackers and outgoing uTP additionally obey their
individual Torrent settings. Turning this option off preserves TCP-only use;
it never enables direct UDP. Downloads and uploads on established peer
connections use the same proxy. Unsolicited incoming peer connections, local
discovery and direct port mapping remain disabled by strict proxy policy.
UDP-over-TCP can incur head-of-line delays under packet loss.

Test proxy uses `google.com` as the TCP destination and UDP DNS query name;
ports and the numeric DNS resolver remain configurable. It does not start
torrents or save the profile. TCP success verifies the proxy TLS/authentication and
CONNECT response, not the destination's TLS or application protocol. UDP success
requires a matching DNS reply through the tunnel. Negotiation, proxy TLS,
authentication, CONNECT and UDP-TUN failures are reported separately. Tests are
asynchronous and cancellable, with no direct fallback.

The protocol target is GOST 3.3.0. The opt-in
[real-GOST acceptance fixtures](../tests/proxy/README.gost.md) use isolated
loopback servers and generated credentials. They are separate from deployed
server acceptance; source availability or passing fixtures do not establish
the state of a deployed server or installed build.
No production endpoint, credential, certificate key or allowlist is shipped.

Native SOCKS5 enables UDP by default for custom profiles. UDP requires successful
UDP ASSOCIATE support by the server; rejected associations never enable direct
UDP. Peer/HTTP tracker hostname resolution goes through SOCKS5. DHT bootstrap
hostnames are contacted with a KRPC ping through SOCKS5 UDP, and only the numeric
reply endpoints reach libtorrent, avoiding its otherwise direct DNS resolution.
This preparation is bounded and cancellable on the engine worker, not the GUI
thread. Unreachable bootstrap names are omitted from the active session, not
erased from saved preferences. If hostname bootstrap fails completely, it retries
after 30 seconds with bounded backoff up to five minutes and a one-second attempt
budget. Successful retries update the existing session without restarting its
transfers; route changes and shutdown cancel old attempts. Named bootstrap hints
in metadata/resume files are ignored on proxied routes, and retained numeric hints
are canonicalized before they reach libtorrent. Direct routing still uses the
system resolver.

Completed-file DC publication is on by default for new settings; an explicitly
saved OFF choice is preserved. The publisher TTH-hashes exact
selected, verified, non-private files on a background worker. It never shares a
whole download directory. Its virtual roots are separate from user-managed
shares and are revoked before file selection, rechecking, removal or relocation.
Files already placed inside a manually shared DC directory remain subject to
that manual share; the add dialog warns about this independent exposure.
Different jobs cannot claim overlapping payload paths, including unselected
files and relocation destinations. Colliding jobs remain blocked and cannot
delete data owned by another job.

## Network Defaults And Creation

The default bootstrap is `dhtb.hublist.eu:6252`, with IPv4 and IPv6 DNS records.
Only the previous exact numeric bootstrap default is migrated; other custom
bootstrap lists and explicit sharing/discovery choices are preserved.
Randomize port on each start defaults ON. The chosen port remains stable across
settings changes during a run; preferences show last-confirmed successful bind
endpoints reported by libtorrent. This is bind history, not a live socket
inventory; addresses may be stale after interface/IP changes. Disable
randomization to configure a fixed port.
Local peer discovery defaults ON for direct connections and remains disabled
under proxy routing. Peer encryption defaults to Optional and can be Disabled
or Required. This is BitTorrent protocol encryption, not TLS or anonymity.

Create Torrent in the internal Torrent tab accepts a file or folder, trackers,
and a private flag. It creates hybrid v1/v2 metadata using cancellable background
hashing without modifying source files. Choose a new output filename outside
the selected folder; existing output files are never overwritten. Optional
seeding uses the normal engine verification path, not an unchecked seed mode.
Created-source seeding is upload-only, stays in the original directory even
when a completed-download directory is configured, and never deletes source
payloads when removing the job. Its file selection is fixed; pause or remove the
seed to stop sharing originals. This protection is retained in resume state.

The current creator uses descriptor-anchored, no-follow file access on macOS
and POSIX systems. Creation is explicitly unavailable on Windows until an
equivalent native-handle implementation is verified. Windows Torrent management
and transfer support are independent of this creator limitation.

## Separate DHT Infrastructure

DC and BitTorrent discovery use separate protocols and services. A DC HTTP
bootstrap endpoint is not a BitTorrent KRPC endpoint. Server deployment and
health must be verified independently of the application's C++ libtorrent API;
private deployment configuration is not included in this source distribution.

See [Development Status](wiki/Development-Status.md) for public release gates,
tested boundaries and remaining work.
