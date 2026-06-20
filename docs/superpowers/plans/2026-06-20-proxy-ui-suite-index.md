# Proxy and UI Suite Execution Index

> **For agentic workers:** REQUIRED SUB-SKILL: Execute each linked plan with `superpowers:test-driven-development`; use `superpowers:verification-before-completion` at every plan boundary.

**Goal:** Deliver the confirmed proxy reliability, DHT, Shadowsocks 2022, Live Log, context-menu icon, and Download-to improvements in a low-risk sequence.

**Architecture:** Land isolated reliability and UI fixes first, then the core logging feature, and implement Shadowsocks 2022 last because it has the largest protocol and interoperability surface. Each plan has independent commits and verification gates; the final application is rebuilt, installed, and smoke-tested only after the combined suite is green.

**Tech Stack:** C++17, Qt 6, OpenSSL, Catch2, CMake, macOS application bundle tooling.

---

## Confirmed design

- Source: `docs/superpowers/specs/2026-06-20-shadowsocks-2022-download-controls-dht-design.md`
- Design commits: `942e90d9`, `37c6ef8d`

## Execution order

1. `2026-06-20-shadowsocks-tls-reliability.md`
   - Fix the known buffered-frame/TLS EOF bug and reconnect storm first.
2. `2026-06-20-socks5-dht.md`
   - Complete the already-started SOCKS5 UDP/DHT work and decouple passive startup.
3. `2026-06-20-download-to-controls.md`
   - Add the small Preferences UI improvement with persistence tests.
4. `2026-06-20-live-log-tab.md`
   - Add structured core logging and the category-filtered singleton UI.
5. `2026-06-20-user-list-menu-icons.md`
   - Apply per-action icon opt-in while retaining the macOS global safeguard.
6. `2026-06-20-shadowsocks-2022.md`
   - Add the new cryptographic protocols only after transport reliability is stable.

## Worktree and commit safety

- The worktree already contains intentional uncommitted changes in:
  - `dcpp/Socket.cpp`, `dcpp/Socket.h`
  - `dht/BootstrapManager.cpp`, `dht/BootstrapManager.h`
  - `dht/DHT.h`, `dht/UDPSocket.cpp`, `dht/UDPSocket.h`
  - `tests/qt/CMakeLists.txt`, `tests/qt/test_socket_shadowsocks.cpp`
  - untracked `tests/qt/test_dht_bootstrap.cpp`
- Do not reset, checkout, or overwrite these files. Begin by reviewing their current diff and commit the DHT test baseline as described in the DHT plan.
- Ignore unrelated DMGs, backup files, generated `.qm` files, and other pre-existing untracked artifacts.
- Do not amend the confirmed design commits.

## Common deterministic verification

After every plan:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests "~[integration]"
```

Expected current baseline: 125 discovered cases before new tests; all non-integration cases pass.

## Final combined verification

- [ ] Reconfigure both build trees so globbed Qt sources/forms and explicit test sources are current:

```bash
cmake -S . -B build-test -DBUILD_TESTS=ON
cmake -S . -B builddir-release -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
```

- [ ] Run the complete deterministic tests with the bundled Cocoa plugin.
- [ ] Run configured live SOCKS5, Shadowsocks legacy, and Shadowsocks 2022 integration tests.
- [ ] Build release:

```bash
cmake --build builddir-release --target eiskaltdcpp-qt -j4
```

- [ ] Verify bundle integrity/signature:

```bash
codesign --verify --deep --strict --verbose=2 \
  builddir-release/eiskaltdcpp-qt/EiskaltDC++.app
```

- [ ] Install automatically, as required for this workspace:

```bash
rsync -aE --delete builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/ /Applications/EiskaltDC++.app/
touch /Applications/EiskaltDC++.app
```

- [ ] Launch and smoke-test:
  - secure NMDC/ADC reconnect through Shadowsocks;
  - HTTPS public hub-list download through Shadowsocks;
  - DHT through SOCKS5 UDP in passive proxy mode;
  - Download-to Add/Remove persistence;
  - user-list context menu icons and Cocoa stability;
  - Live Log categories/pause/clear/auto-scroll;
  - all three Shadowsocks 2022 methods, including one EIH configuration.
- [ ] Compare installed and build binaries:

```bash
shasum -a 256 \
  builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/MacOS/EiskaltDC++ \
  /Applications/EiskaltDC++.app/Contents/MacOS/EiskaltDC++
```

Expected: hashes match.

## Final commit hygiene

- [ ] `git diff --check` reports no whitespace errors.
- [ ] `git status --short` contains only known unrelated pre-existing artifacts after all intended source/docs changes are committed.
- [ ] Review `git log --oneline` to ensure each plan has small, reversible commits and no implementation commit mixes unrelated subsystems.
