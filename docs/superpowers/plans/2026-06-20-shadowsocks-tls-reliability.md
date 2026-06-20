# Shadowsocks TLS Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:test-driven-development` for each task and `superpowers:verification-before-completion` before claiming completion.

**Goal:** Eliminate false TLS EOFs and reconnect storms for `nmdcs://`, `adcs://`, and HTTPS hub-list traffic carried through Shadowsocks.

**Architecture:** Fix the legacy Shadowsocks stream reader so it always drains authenticated ciphertext already buffered in memory before waiting for another network packet. Add explicit truncated-stream reporting. Separately make hub reconnect scheduling single-flight and measure automatic delay from the latest attempt or failure, while preserving an immediate manual reconnect.

**Tech Stack:** C++17, OpenSSL EVP, EiskaltDC++ socket/client core, Catch2, Qt test harness.

---

## Task 1: Reproduce coalesced-frame starvation deterministically

**Files:**
- Modify: `tests/qt/test_socket_shadowsocks.cpp`
- Modify: `tests/qt/CMakeLists.txt` only if a new fixture source is split out

- [ ] Add a loopback fake Shadowsocks AEAD server fixture to `test_socket_shadowsocks.cpp`. The fixture must implement the existing legacy `aes-256-gcm` wire format independently of `dcpp::Socket`: EVP_BytesToKey-compatible master key, HKDF-SHA1 session subkey, zero-based 96-bit little-endian nonce, and `length + tag` / `payload + tag` frames.
- [ ] Have the server accept one client, consume the request salt and destination frame, then issue one `send()` containing its salt plus two complete encrypted response frames. Keep the connection open for at least 500 ms without sending more bytes.
- [ ] Configure a `test::TestContext` to use the loopback Shadowsocks server, call `Socket::proxyConnect()`, and read the two plaintext responses with two `Socket::read()` calls.
- [ ] Assert the second read succeeds immediately from buffered ciphertext. Before the production fix, this test must fail by returning `-1` or waiting for a new packet.

Test shape:

```cpp
TEST_CASE("Shadowsocks reader drains every complete buffered frame", "[qt][socket][shadowsocks]") {
    LegacyShadowsocksServer server("test-password");
    server.replyWithCoalescedFrames({"first", "second"});

    test::TestContext tc;
    configureShadowsocks(*tc.ownedCtx, server.port(), "aes-256-gcm", "test-password");

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.proxyConnect("example.test", "443", 3000);

    REQUIRE(readExactly(socket, 5, 3000) == "first");
    REQUIRE(readExactly(socket, 6, 100) == "second");
}
```

- [ ] Build and run only the new case:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests \
  "Shadowsocks reader drains every complete buffered frame"
```

Expected before the fix: one failed case proving the stall.

- [ ] Commit the failing regression test:

```bash
git add tests/qt/test_socket_shadowsocks.cpp tests/qt/CMakeLists.txt
git commit -m "Test coalesced Shadowsocks stream frames"
```

## Task 2: Drain buffered ciphertext before raw reads

**Files:**
- Modify: `dcpp/Socket.cpp:1480-1520`
- Modify: `dcpp/Socket.h:250-280` only if an EOF-state helper is added
- Modify: `tests/qt/test_socket_shadowsocks.cpp`

- [ ] In `Socket::shadowsocksRead()`, after `copyPlain()` and before `rawRead()`, call `shadowsocksTryDecode()`. If it produces plaintext, return `copyPlain()` without touching the network.

Required control flow:

```cpp
if(int copied = copyPlain())
    return copied;

if(shadowsocksTryDecode()) {
    if(int copied = copyPlain())
        return copied;
}

uint8_t buf[8192];
while(true) {
    const int len = rawRead(buf, sizeof(buf));
    // ...
}
```

- [ ] When `rawRead()` returns EOF, return `0` only if `shadowsocksCipherIn` is empty and no frame is partially decoded. Otherwise throw `SocketException(_("Shadowsocks stream ended with an incomplete frame"))`.
- [ ] Keep authentication failures mapped to `Shadowsocks decryption failed`; do not turn a bad tag into an EOF.
- [ ] Add one deterministic test where the fake server closes halfway through an encrypted payload and assert the explicit incomplete-frame error.
- [ ] Run the focused cases and the full deterministic socket subset:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests "[socket]" "~[integration]"
```

Expected: all deterministic socket cases pass; integration cases are skipped.

- [ ] Commit:

```bash
git add dcpp/Socket.cpp dcpp/Socket.h tests/qt/test_socket_shadowsocks.cpp
git commit -m "Drain buffered Shadowsocks frames before reading"
```

## Task 3: Make reconnect scheduling single-flight

**Files:**
- Modify: `dcpp/Client.h:180-220`
- Modify: `dcpp/Client.cpp:35-180`
- Modify: `dcpp/Client.cpp:305-315`
- Create: `tests/qt/test_client_reconnect_policy.cpp`
- Modify: `tests/qt/CMakeLists.txt`

- [ ] Extract a small pure policy helper in `Client.h` so timing can be tested without constructing a full hub client:

```cpp
struct ReconnectPolicy {
    static bool due(bool disconnected, bool autoReconnect, bool attemptActive,
                    uint64_t now, uint64_t lastAttempt, uint32_t delaySeconds);
};
```

- [ ] Write failing tests covering:
  - automatic reconnect is not due before `lastAttempt + delay`;
  - it is due at or after the deadline;
  - it is never due while an attempt is active;
  - a manual request may set the deadline to now but must not start a second in-flight attempt.
- [ ] Add `bool connectAttemptActive` to `Client`, initialized `false`.
- [ ] In `Client::connect()`, return early if `connectAttemptActive` or `state == STATE_CONNECTING`; otherwise set `connectAttemptActive = true` and call `updateActivity()` immediately before creating the socket.
- [ ] Clear `connectAttemptActive` in `on(Connected)`, `on(Failed)`, synchronous exception handling, and `shutdown()`.
- [ ] In `on(Failed)`, call `updateActivity()` before firing the failure event so automatic retry delay is measured from the failure.
- [ ] Change `Client::reconnect()` to disconnect once, enable auto-reconnect, set the deadline for one immediate attempt, and do nothing if a connect attempt is already active.
- [ ] Replace the inline timing expression in `on(Second)` with `ReconnectPolicy::due(...)`.
- [ ] Run:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests "[reconnect]"
```

Expected: all reconnect policy cases pass.

- [ ] Commit:

```bash
git add dcpp/Client.h dcpp/Client.cpp tests/qt/test_client_reconnect_policy.cpp tests/qt/CMakeLists.txt
git commit -m "Coalesce hub reconnect attempts"
```

## Task 4: Verify secure hub and hub-list traffic through Shadowsocks

**Files:**
- Modify: `tests/qt/test_socket_shadowsocks.cpp`
- Modify: `tests/qt/CMakeLists.txt`

- [ ] Add opt-in integration cases driven by these environment variables:
  - `EISKALT_TEST_SHADOWSOCKS_SERVER`
  - `EISKALT_TEST_SHADOWSOCKS_PORT`
  - `EISKALT_TEST_SHADOWSOCKS_PASSWORD`
  - `EISKALT_TEST_SHADOWSOCKS_METHOD`
  - `EISKALT_TEST_NMDCS_HOST` and `EISKALT_TEST_NMDCS_PORT`
  - `EISKALT_TEST_HTTPS_URL`
- [ ] For secure NMDC, establish five fresh TLS connections through the proxy, complete each handshake, and require every attempt to finish within 10 seconds.
- [ ] For HTTPS, fetch the configured URL five times through the same proxy settings and require a non-empty HTTP response each time.
- [ ] Run the complete deterministic suite first:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests "~[integration]"
```

Expected: all deterministic tests pass.

- [ ] Run the opt-in integration tests against the configured local Shadowsocks server and `nmdcs://dchub.in.ua:711` / `https://hublist.eu/hublist.xml.bz2`.

Expected: 5/5 secure hub handshakes and 5/5 HTTPS downloads succeed with no `SSL_get_error=5` false EOF.

- [ ] Build the release application:

```bash
cmake --build builddir-release --target eiskaltdcpp-qt -j4
```

Expected: `EiskaltDC++.app` links successfully.

- [ ] Commit:

```bash
git add tests/qt/test_socket_shadowsocks.cpp tests/qt/CMakeLists.txt
git commit -m "Cover TLS traffic through Shadowsocks"
```
