# Shadowsocks 2022, Proxy Reliability, Live Log, and UI Controls Design

## Scope

This change has six independent workstreams:

1. Add native Shadowsocks 2022 (SIP022) client support for TCP and UDP.
2. Fix TLS-over-Shadowsocks reliability for secure hubs and HTTPS downloads.
3. Add a category-filtered Live Log tab to the main window.
4. Add visible Add and Remove controls to Downloads > Download to.
5. Show theme icons in the hub user-list context menu.
6. Complete DHT startup and bootstrapping through SOCKS5 proxy mode.

The workstreams share release verification but otherwise remain isolated. Existing legacy Shadowsocks methods must remain protocol-compatible, and direct-connect behavior must not change.

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

Deterministic tests cover key parsing, BLAKE3 derivation, TCP framing, UDP framing, EIH generation, timestamp validation, request-salt validation, and replay-window behavior. Integration tests connect through all three methods to a current `shadowsocks-rust` server deployed for testing on `192.168.4.71`, including one EIH configuration and both TCP and UDP traffic. The existing `shadowsocks-libev` service remains available for legacy-method regression tests.

## TLS over Shadowsocks Reliability

### Root Cause

The legacy Shadowsocks TCP decoder may receive multiple complete encrypted frames in one socket read. It currently returns after exposing the first plaintext frame and leaves the remaining ciphertext buffered. The next read calls the network before attempting to decode that buffer. If no additional packet arrives, TLS waits for data that is already in memory; if the peer closes, OpenSSL sees a false end-of-stream and reports `SSL_ERROR_SYSCALL` without an OpenSSL error.

This shared defect explains both intermittent `nmdcs://` reconnects and intermittent HTTPS public-hub-list downloads. The configured Shadowsocks server and both targets complete promptly through the reference `ss-local` client, isolating the failure to EiskaltDC++'s TLS-over-Shadowsocks stream adapter.

### Stream Behavior

The decoder must always drain complete buffered ciphertext before calling `recv()`. It preserves incomplete encrypted frames, pending plaintext, and nonce state across BIO reads. A clean remote close is reported only after all complete buffered frames have been decoded. An EOF with an incomplete frame is reported as a truncated Shadowsocks stream, while authentication failures retain their existing explicit error.

The TLS BIO remains the integration boundary; no external `ss-local` helper or socketpair relay is introduced.

### Reconnect Control

Automatic reconnects observe the configured reconnect delay from the most recent connection attempt or failure. Only one connection attempt may be active for a client at a time. Manual reconnect remains immediate, but repeated manual requests while an attempt is active are coalesced rather than creating concurrent attempts.

### Verification

Deterministic transport tests feed two or more encrypted Shadowsocks frames in a single raw read and verify that every plaintext frame is returned without another socket event. Tests also cover partial frames, clean EOF, truncated EOF, and failed authentication.

Integration verification repeatedly connects to `nmdcs://dchub.in.ua:711` and repeatedly downloads an HTTPS public hub list through the configured Shadowsocks server. Reconnect tests record attempt timestamps and verify the configured automatic delay and single-attempt invariant.

## Download-To Controls

The Download to tab receives a horizontal control row below the table. A stretch occupies the left side; Add and Remove `QPushButton` controls are aligned on the right. They use the existing `eiEDITADD` and `eiEDITDELETE` icon identifiers and translated `Add` and `Remove` labels, matching the Sharing settings page.

Add opens the existing alias prompt and directory chooser. Remove deletes all selected rows and is disabled when the selection is empty. The existing context menu remains and calls the same add/remove handlers so behavior and persistence are identical from either entry point.

Tests verify selection-dependent button state and list persistence independently from file-dialog interaction.

## Live Log Tab

### Log Data

`LogManager` exposes structured live entries containing a timestamp, `LogManager::Area`, and formatted message. It retains the latest 5,000 entries in a thread-safe in-memory history independent of whether the corresponding file log is enabled. The existing system-message listener used by the status bar remains unchanged.

The selectable categories are System, Status, Main Chat, Private Messages, Downloads, Finished Downloads, Uploads, Search Spy, and Command Debug. Category selection affects only the viewer and is persisted in GUI settings.

### User Interface

A singleton Live Log `ArenaWidget` opens as a normal main-window tab. The same action is available from the Tools menu and the customizable main action toolbar; triggering it again activates the existing tab.

The tab shows timestamp, category, and message columns. A checkable Categories menu controls visibility. Pause freezes visible updates while entries continue accumulating in the bounded core history; Resume catches up without losing entries that remain in that history. Clear removes the current session's in-memory live history and never modifies persisted log files. Automatic scrolling is enabled by default.

### Verification

Tests cover category routing and filtering, bounded history, persisted category selection, pause and catch-up behavior, in-memory clearing without file modification, automatic scrolling, and singleton tab activation.

## User-List Context Menu Icons

The hub user-list context menu shows existing theme icons for every actionable top-level row and submenu parent. Semantic mappings cover Browse files, Private Message, Add/Remove Favorites, Grant slot, Copy data, Match/Remove Queue, and User commands. Custom user-command leaves use the generic command icon.

macOS keeps the global `Qt::AA_DontShowIconsInMenus` safeguard because globally enabled native menu icons previously triggered a Cocoa menu-image crash. Only the persistent actions owned by the user-list popup opt in with `QAction::setIconVisibleInMenu(true)`. Icons are stable `QIcon` values from the loaded application theme rather than temporary pixmap references.

Verification opens and closes the popup repeatedly, changes icon themes, and switches application focus while the menu is active. The test confirms icons are visible without re-enabling icons in the native menu bar or reproducing the Cocoa crash.

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

1. Fix and verify buffered Shadowsocks decoding and TLS reliability.
2. Complete and verify the DHT startup fix already under development.
3. Add and verify Download-to controls.
4. Add and verify the Live Log tab.
5. Enable and stress-test user-list context-menu icons.
6. Implement AEAD-2022 key parsing and cryptographic primitives.
7. Implement AEAD-2022 TCP, then UDP and EIH.
8. Run unit, integration, release-build, signing, installation, and installed-app runtime checks.

## Non-Goals

- Running an external `ss-local` or `sslocal` helper.
- Supporting reduced-round ChaCha8 or ChaCha12 methods.
- Adding Shadowsocks plugins or configuration-URL import.
- Changing existing proxy selection or credential persistence behavior.
