# Shadowsocks 2022, Download-To Controls, and Proxy DHT Design

## Scope

This change has three independent workstreams:

1. Add native Shadowsocks 2022 (SIP022) client support for TCP and UDP.
2. Add visible Add and Remove controls to Downloads > Download to.
3. Complete DHT startup and bootstrapping through SOCKS5 proxy mode.

The workstreams share release verification but otherwise remain isolated. Existing legacy Shadowsocks methods and direct-connect behavior must not change.

## Shadowsocks 2022

### Supported Methods

- `2022-blake3-aes-128-gcm`
- `2022-blake3-aes-256-gcm`
- `2022-blake3-chacha20-poly1305`

The methods are added to the Shadowsocks cipher selector. The existing methods remain available.

### Key Input

The password field accepts either:

- One standard base64-encoded PSK.
- An EIH chain formatted as colon-separated base64 PSKs, with zero or more identity PSKs followed by the user PSK.

Decoded keys must be exactly 16 bytes for `2022-blake3-aes-128-gcm` and 32 bytes for the other two methods. Invalid base64, empty chain components, and incorrect key lengths fail validation with a specific error before any network connection is attempted. Legacy methods continue accepting ordinary passwords and deriving keys with their existing behavior.

### Protocol Boundary

AEAD-2022 is implemented as a separate protocol state path inside the socket transport rather than adding conditionals to the legacy AEAD state machine. Shared low-level address parsing and OpenSSL AEAD helpers may be reused.

The implementation includes:

- Portable BLAKE3 key derivation using the protocol-defined contexts.
- TCP request and response headers, timestamps, request-salt validation, random nonzero request padding, and 16-bit payload chunk lengths.
- UDP client and server session IDs, packet counters, timestamp validation, and replay windows.
- AES-GCM UDP separate-header encryption for the AES methods.
- XChaCha20-Poly1305 UDP packets for the ChaCha method.
- EIH generation for TCP and UDP request packets.
- Response validation before plaintext is exposed to callers.

The portable upstream BLAKE3 C implementation is vendored and built into the existing `extra` library so macOS, Linux, and Windows builds do not gain a new system dependency.

### Error Handling

Configuration errors identify whether base64 decoding, chain structure, or key length is invalid. Protocol errors reject bad message types, timestamps outside the permitted window, mismatched request salts, duplicate or out-of-window UDP packet IDs, malformed lengths, and failed authentication tags.

No AEAD-2022 key material is written to logs.

### Verification

Deterministic tests cover key parsing, BLAKE3 derivation, TCP framing, UDP framing, EIH generation, timestamp validation, request-salt validation, and replay-window behavior. Integration tests connect through all three methods to a current `shadowsocks-rust` server on `192.168.4.71`, including one EIH configuration and both TCP and UDP traffic.

## Download-To Controls

The Download to tab receives a horizontal control row below the table. A stretch occupies the left side; Add and Remove `QPushButton` controls are aligned on the right. They use the existing `eiEDITADD` and `eiEDITDELETE` icon identifiers and translated `Add` and `Remove` labels, matching the Sharing settings page.

Add opens the existing alias prompt and directory chooser. Remove deletes all selected rows and is disabled when the selection is empty. The existing context menu remains and calls the same add/remove handlers so behavior and persistence are identical from either entry point.

Tests verify selection-dependent button state and list persistence independently from file-dialog interaction.

## DHT Through SOCKS5

The DHT datagram socket receives `DCContext`, so its traffic uses the configured outgoing proxy. SOCKS5 UDP datagrams retain a persistent UDP ASSOCIATE control connection, include correct destination framing, and unwrap relay responses before DHT parsing.

When SOCKS5 makes the general client mode passive, `ConnectivityManager` still starts DHT independently of the TCP transfer listener whenever DHT is enabled. DHT bootstrapping registers the SOCKS5 UDP relay port as `u4`; the HTTPS bootstrap connection is also routed through the configured SOCKS5 proxy. Direct passive mode retains its existing behavior.

Shadowsocks UDP remains firewalled for DHT bootstrapping because Shadowsocks does not expose a stable public relay endpoint that can be registered safely.

Runtime verification requires:

- A live SOCKS5 UDP round trip.
- A successful bootstrap HTTP response through the proxy.
- A nonzero DHT node count in the installed application.
- No direct DHT packet leakage when proxy mode is selected.

## Delivery Order

1. Complete and verify the DHT startup fix already under development.
2. Add and verify Download-to controls.
3. Implement AEAD-2022 key parsing and cryptographic primitives.
4. Implement AEAD-2022 TCP, then UDP and EIH.
5. Run unit, integration, release-build, signing, installation, and installed-app runtime checks.

## Non-Goals

- Running an external `sslocal` helper.
- Supporting reduced-round ChaCha8 or ChaCha12 methods.
- Adding Shadowsocks plugins or configuration-URL import.
- Changing existing proxy selection or credential persistence behavior.
