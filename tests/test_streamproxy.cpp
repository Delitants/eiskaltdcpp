#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "dcpp/Socket.h"
#include "dcpp/SocketWake.h"
#include <array>
#include <thread>
#include <chrono>
#include <atomic>
#include "TestContext.h"
#include "dcpp/SettingsManager.h"

namespace {
using dcpp::Socket;
void listenLoopback(Socket& listener) {
    listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
}

bool acceptSocks(Socket& peer, Socket& listener) {
    if(listener.wait(2000, Socket::WAIT_READ) != Socket::WAIT_READ) return false;
    peer.accept(listener);
    peer.setBlocking(false);
#ifdef SO_NOSIGPIPE
    peer.setSocketOpt(SO_NOSIGPIPE, 1);
#endif
    unsigned char hello[3]{};
    if(peer.readAll(hello, 3, 1000) != 3) return false;
    const unsigned char selected[] = {5, hello[2]};
    peer.writeAll(selected, 2, 1000);
    if(hello[2] == 2) {
        unsigned char header[2]{};
        if(peer.readAll(header, 2, 1000) != 2) return false;
        std::vector<char> user(header[1]);
        if(peer.readAll(user.data(), user.size(), 1000) != int(user.size())) return false;
        unsigned char length{};
        if(peer.readAll(&length, 1, 1000) != 1) return false;
        std::vector<char> password(length);
        if(peer.readAll(password.data(), length, 1000) != length ||
           std::string(password.begin(), password.end()) != "socks-secret") return false;
        const unsigned char authenticated[] = {1, 0};
        peer.writeAll(authenticated, 2, 1000);
    }
    unsigned char request[5]{};
    if(peer.readAll(request, 5, 1000) != 5 || request[3] != 3) return false;
    std::vector<char> target(request[4] + 2);
    return peer.readAll(target.data(), target.size(), 1000) == int(target.size());
}
}

TEST_CASE("GOST offers only TLS-AUTH before rejecting a downgrade", "[gost][wire][streamproxy]") {
    Socket listener;
    listenLoopback(listener);
    std::array<uint8_t, 3> hello{};
    int bytesAfterSelection = -1;
    std::jthread server([&] {
        try {
            if(listener.wait(500, Socket::WAIT_READ) != Socket::WAIT_READ) return;
            Socket peer;
            peer.accept(listener);
            peer.setBlocking(false);
            if(peer.readAll(hello.data(), hello.size(), 500) != 3) return;
            const uint8_t selected[] = {5, 0};
            peer.writeAll(selected, 2, 500);
            uint8_t extra{};
            bytesAfterSelection = peer.readAll(&extra, 1, 500);
        } catch(...) { }
    });
    Socket::StreamProxyConfig config;
    config.type = static_cast<Socket::StreamProxyConfig::Type>(3);
    config.host = "127.0.0.1";
    config.port = std::stoi(listener.getLocalPort());
    config.user = "disposable-user";
    config.password = "disposable-secret";
    Socket client;
    REQUIRE_THROWS_AS(client.proxyConnect("example.invalid", "443", config, 1000), dcpp::SocketException);
    server.join();
    REQUIRE(hello == std::array<uint8_t, 3>{5, 1, 0x82});
    REQUIRE(bytesAfterSelection == 0);
}

TEST_CASE("Explicit stream proxy authenticates and resolves through SOCKS without shared settings", "[socket][streamproxy][network]") {
    using dcpp::Socket;
    Socket listener;
    listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    bool serverPassed = false;
    std::jthread server([&] {
        try {
            if(listener.wait(3000, Socket::WAIT_READ) != Socket::WAIT_READ) return;
            Socket peer;
            peer.accept(listener);
            std::array<unsigned char, 3> hello{};
            if(peer.readAll(hello.data(), hello.size(), 2000) != 3 ||
               hello != std::array<unsigned char, 3>{5, 1, 2}) return;
            const unsigned char selected[] = {5, 2};
            peer.writeAll(selected, 2, 2000);
            std::array<unsigned char, 2> auth{};
            if(peer.readAll(auth.data(), 2, 2000) != 2 || auth[0] != 1 || auth[1] != 4) return;
            std::array<char, 4> user{};
            if(peer.readAll(user.data(), 4, 2000) != 4 || std::string(user.data(), 4) != "test") return;
            unsigned char passwordLength = 0;
            if(peer.readAll(&passwordLength, 1, 2000) != 1 || passwordLength != 6) return;
            std::array<char, 6> password{};
            if(peer.readAll(password.data(), 6, 2000) != 6 || std::string(password.data(), 6) != "secret") return;
            const unsigned char authenticated[] = {1, 0};
            peer.writeAll(authenticated, 2, 2000);
            std::array<unsigned char, 5> request{};
            if(peer.readAll(request.data(), 5, 2000) != 5 || request[0] != 5 ||
               request[1] != 1 || request[3] != 3 || request[4] != 15) return;
            std::array<char, 17> target{};
            if(peer.readAll(target.data(), 17, 2000) != 17 ||
               std::string(target.data(), 15) != "example.invalid") return;
            const unsigned char connected[] = {5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
            peer.writeAll(connected, sizeof(connected), 2000);
            std::array<char, 4> payload{};
            if(peer.readAll(payload.data(), payload.size(), 2000) != 4) return;
            peer.writeAll(payload.data(), payload.size(), 2000);
            serverPassed = true;
        } catch(...) { }
    });
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Socks5;
    config.host = "127.0.0.1";
    config.port = std::stoi(listener.getLocalPort());
    config.user = "test";
    config.password = "secret";
    Socket client;
    client.proxyConnect("example.invalid", "443", config, 3000);
    client.writeAll("ping", 4, 2000);
    std::array<char, 4> reply{};
    REQUIRE(client.readAll(reply.data(), reply.size(), 2000) == 4);
    REQUIRE(std::string(reply.data(), reply.size()) == "ping");
    server.join();
    REQUIRE(serverPassed);
}

TEST_CASE("Explicit stream proxy rejects missing endpoint without a DC context", "[socket][streamproxy]") {
    dcpp::Socket socket;
    dcpp::Socket::StreamProxyConfig config;
    config.type = dcpp::Socket::StreamProxyConfig::Socks5;
    REQUIRE_THROWS_AS(socket.proxyConnect("example.invalid", "443", config, 100), dcpp::SocketException);
}

TEST_CASE("Explicit stream proxy cannot fall back to a direct connection", "[socket][streamproxy]") {
    dcpp::Socket socket;
    dcpp::Socket::StreamProxyConfig config;
    REQUIRE_THROWS_AS(socket.proxyConnect("127.0.0.1", "9", config, 100), dcpp::SocketException);
    config.type = dcpp::Socket::StreamProxyConfig::Socks5;
    config.host = "127.0.0.1";
    config.port = 65536;
    REQUIRE_THROWS_AS(socket.proxyConnect("example.invalid", "443", config, 100), dcpp::SocketException);
}

TEST_CASE("Shadowsocks accepts credentials longer than a SOCKS auth field", "[socket][streamproxy][network]") {
    Socket listener;
    listenLoopback(listener);
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Shadowsocks;
    config.host = "127.0.0.1";
    config.port = std::stoi(listener.getLocalPort());
    config.cipher = "aes-256-gcm";
    config.password.assign(300, 's');
    Socket client;
    REQUIRE_NOTHROW(client.proxyConnect("example.invalid", "443", config, 1000));
}

TEST_CASE("Legacy SOCKS entry point never selects Shadowsocks credentials", "[socket][streamproxy][network]") {
    dcpp::test::TestContext context;
    auto* settings = context.ownedCtx->getSettingsManager();
    Socket listener;
    listenLoopback(listener);
    settings->set(dcpp::SettingsManager::SOCKS_SERVER, "127.0.0.1");
    settings->set(dcpp::SettingsManager::SOCKS_PORT, std::stoi(listener.getLocalPort()));
    settings->set(dcpp::SettingsManager::SOCKS_TLS, false);
    settings->set(dcpp::SettingsManager::SOCKS_USER, "user");
    settings->set(dcpp::SettingsManager::SOCKS_PASSWORD, "socks-secret");
    settings->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_SHADOWSOCKS);
    settings->set(dcpp::SettingsManager::SHADOWSOCKS_PASSWORD, "must-not-leak");
    bool accepted = false;
    std::jthread server([&] {
        try {
            Socket peer;
            if(!acceptSocks(peer, listener)) return;
            const unsigned char response[] = {5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
            peer.writeAll(response, sizeof(response), 1000);
            accepted = true;
        } catch(...) { }
    });
    Socket client;
    client.setContext(context.ownedCtx.get());
    REQUIRE_NOTHROW(client.socksConnect("example.invalid", "443", 1000));
    server.join();
    REQUIRE(accepted);
}

TEST_CASE("SOCKS trickle response cannot restart the handshake deadline", "[socket][streamproxy][network]") {
    Socket listener;
    listenLoopback(listener);
    std::jthread server([&] {
        try {
            Socket peer;
            if(!acceptSocks(peer, listener)) return;
            const unsigned char response[] = {5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
            peer.writeAll(response, 4, 1000);
            for(size_t i = 4; i < sizeof(response); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                peer.writeAll(response + i, 1, 1000);
            }
        } catch(...) { }
    });
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Socks5;
    config.host = "127.0.0.1";
    config.port = std::stoi(listener.getLocalPort());
    Socket client;
    const auto start = std::chrono::steady_clock::now();
    REQUIRE_THROWS_AS(client.proxyConnect("example.invalid", "443", config, 200), dcpp::SocketException);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(450));
}

TEST_CASE("A cancelled TLS proxy handshake interrupts its native wait", "[socket][streamproxy][network]") {
    Socket listener;
    listenLoopback(listener);
    std::atomic<bool> cancelled{false};
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Socks5;
    config.host = "127.0.0.1";
    config.port = std::stoi(listener.getLocalPort());
    config.tls = true;
    config.cancelled = [&] { return cancelled.load(); };
    SECTION("Callback-only callers retain bounded polling") { }
    SECTION("Notified callers wake the same wait") { config.cancellationNotifier = std::make_shared<dcpp::WakeNotifier>(); }
    std::jthread cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancelled = true;
        if(config.cancellationNotifier) config.cancellationNotifier->notify();
    });
    Socket client;
    const auto start = std::chrono::steady_clock::now();
    REQUIRE_THROWS_AS(client.proxyConnect("example.invalid", "443", config, 5000), dcpp::SocketException);
    REQUIRE(cancelled.load());
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(750));
    REQUIRE_FALSE(client.isConnected());
}

TEST_CASE("Shadowsocks flushes its last accepted ciphertext without another plaintext write", "[socket][streamproxy][network]") {
    for(const auto& method : {"aes-256-gcm", "2022-blake3-aes-256-gcm"}) {
        DYNAMIC_SECTION(method) {
            Socket listener;
            listenLoopback(listener);
            Socket::StreamProxyConfig config;
            config.type = Socket::StreamProxyConfig::Shadowsocks;
            config.host = "127.0.0.1";
            config.port = std::stoi(listener.getLocalPort());
            config.cipher = method;
            config.password = config.cipher == "aes-256-gcm" ? "test-password" :
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
            Socket client;
            client.proxyConnect("example.invalid", "443", config, 1000);
            Socket peer;
            peer.accept(listener);
            peer.setBlocking(false);
            client.setSocketOpt(SO_SNDBUF, 4096);
            std::array<char, 65536> bytes{};
            // The completed proxyConnect has already flushed the request header.
            while(peer.wait(20, Socket::WAIT_READ) == Socket::WAIT_READ)
                REQUIRE(peer.read(bytes.data(), bytes.size()) > 0);
            bool buffered = false;
            size_t expectedWireBytes = 0;
            for(int i = 0; i < 1024; ++i) {
                const int accepted = client.write(bytes.data(), bytes.size());
                if(accepted > 0) expectedWireBytes += accepted + 34; // encrypted length + two AEAD tags
                if(client.hasPendingProxyOutput()) {
                    REQUIRE(accepted > 0);
                    buffered = true;
                    break;
                }
            }
            REQUIRE(buffered);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            bool flushed = false;
            size_t receivedWireBytes = 0;
            while(std::chrono::steady_clock::now() < deadline) {
                const int received = peer.read(bytes.data(), bytes.size());
                if(received > 0) receivedWireBytes += received;
                flushed = client.flushProxyOutput();
                if(flushed && receivedWireBytes == expectedWireBytes) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE(flushed);
            REQUIRE_FALSE(client.hasPendingProxyOutput());
            REQUIRE(receivedWireBytes == expectedWireBytes);
        }
    }
}

TEST_CASE("Oversized SOCKS credentials are still rejected", "[socket][streamproxy]") {
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Socks5;
    config.host = "127.0.0.1";
    config.port = 9;
    config.password.assign(256, 'x');
    Socket socket;
    REQUIRE_THROWS_AS(socket.proxyConnect("example.invalid", "443", config, 100), dcpp::SocketException);
    REQUIRE_FALSE(socket.isConnected());
}

#include "proxy/DisposableGostServer.h"
#include "dcpp/GostProtocol.h"
#include <fstream>
#include <filesystem>

namespace {
Socket::StreamProxyConfig gostConfig(proxy_test::GostServer& server) {
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Gost;
    config.host = "127.0.0.1";
    config.port = server.port();
    config.user = "disposable-user";
    config.password = "disposable-password";
    config.caPem = server.ca.pem;
    return config;
}
void requireGostFailure(Socket& client, const Socket::StreamProxyConfig& config, uint32_t timeout = 1000) {
    bool failed = false;
    try { client.proxyConnect("destination.invalid", "443", config, timeout); }
    catch(const dcpp::SocketException& e) {
        failed = true;
        const auto message = e.getError();
        if(!config.user.empty()) CHECK(message.find(config.user) == std::string::npos);
        if(!config.password.empty()) CHECK(message.find(config.password) == std::string::npos);
    }
    REQUIRE(failed);
    REQUIRE_FALSE(client.isConnected());
    REQUIRE_FALSE(client.hasStreamProxy());
}
void echoPing(Socket& client) {
    client.writeAll("ping", 4, 1000);
    char data[4]{};
    REQUIRE(client.readAll(data, 4, 1000) == 4);
    REQUIRE(std::string(data, 4) == "ping");
    client.disconnect();
}
}

TEST_CASE("GOST rejects every downgrade and wrong method version without credentials", "[gost][wire][streamproxy]") {
    for(const auto method : {0, 2, 0x80, 0xff, 0x81}) {
        DYNAMIC_SECTION(method) {
            proxy_test::GostOptions options;
            options.method = method;
            proxy_test::GostServer server(options);
            auto config = gostConfig(server);
            server.start();
            Socket client;
            requireGostFailure(client, config);
            server.join();
            REQUIRE(server.hello == std::array<uint8_t, 3>{5, 1, 0x82});
            REQUIRE(server.bytesAfterSelection == 0);
            REQUIRE_FALSE(server.authReceived);
            REQUIRE_FALSE(server.requestReceived);
        }
    }
    proxy_test::GostOptions options;
    options.methodVersion = 4;
    proxy_test::GostServer server(options);
    auto config = gostConfig(server);
    server.start();
    Socket client;
    requireGostFailure(client, config);
    server.join();
    REQUIRE(server.bytesAfterSelection == 0);
}

TEST_CASE("GOST verifies TLS before authentication and preserves remote address types", "[gost][wire][streamproxy]") {
    for(const auto& target : {"destination.invalid", "2001:db8::1", "192.0.2.1"}) {
        DYNAMIC_SECTION(target) {
            proxy_test::GostServer server;
            auto config = gostConfig(server);
            // Even remoteDns=false must not leak target names in GOST mode.
            config.remoteDns = false;
            server.start();
            Socket client;
            REQUIRE_NOTHROW(client.proxyConnect(target, "443", config, 2000));
            REQUIRE(client.hasStreamProxy());
            echoPing(client);
            server.join();
            REQUIRE(server.tlsAccepted);
            REQUIRE(server.user == config.user);
            REQUIRE(server.password == config.password);
            REQUIRE(server.command == 1);
            REQUIRE(server.target == target);
            REQUIRE(server.targetPort == 443);
            REQUIRE(server.atyp == (std::string(target) == "destination.invalid" ? 3 : std::string(target) == "2001:db8::1" ? 4 : 1));
        }
    }
}

TEST_CASE("GOST DNS identity and TLS 1.2 are supported", "[gost][wire][streamproxy]") {
    proxy_test::GostOptions options;
    options.tlsMax = TLS1_2_VERSION;
    proxy_test::GostServer server(options);
    auto config = gostConfig(server);
    config.host = "proxy.test";
    config.connectHost = "127.0.0.1";
    server.start();
    Socket client;
    REQUIRE_NOTHROW(client.proxyConnect("destination.invalid", "443", config, 2000));
    echoPing(client);
    server.join();
    REQUIRE(server.authReceived);
}

TEST_CASE("TLS proxy buffered plaintext remains ready without another packet", "[socket-wake][streamproxy]") {
    proxy_test::GostServer server;
    server.start();
    Socket client;
    client.proxyConnect("destination.invalid", "443", gostConfig(server), 2000);
    auto wake = std::make_shared<dcpp::SocketWake>();
    client.setWaitWake(wake);
    client.writeAll("ping", 4, 1000);
    char bytes[4]{};
    REQUIRE(client.readAll(bytes, 1, 1000) == 1);
    CHECK(client.wait(0, Socket::WAIT_READ | Socket::WAIT_WAKE) & Socket::WAIT_READ);
    REQUIRE(client.readAll(bytes + 1, 3, 1000) == 3);
    CHECK(std::string(bytes, 4) == "ping");
    client.disconnect();
    server.join();
}

TEST_CASE("Notifier-backed GOST idle wait makes one native wait rather than timed slices", "[socket-wake][streamproxy][review-fixes]") {
    proxy_test::GostServer server;
    server.start();
    auto config = gostConfig(server);
    config.cancelled = [] { return false; };
    config.cancellationNotifier = std::make_shared<dcpp::WakeNotifier>();
    Socket client;
    client.proxyConnect("destination.invalid", "443", config, 2000);
    const auto before = client.getNativeWaitCount();
    CHECK(client.wait(200, Socket::WAIT_READ) == Socket::WAIT_NONE);
    CHECK(client.getNativeWaitCount() - before == 1);
    client.disconnect();
    server.join();
}

TEST_CASE("GOST UDP tunnel uses F3 and reusable encrypted stream framing", "[gost][wire][streamproxy]") {
    proxy_test::GostServer server;
    auto config = gostConfig(server);
    server.start();
    Socket client;
    REQUIRE_NOTHROW(client.gostOpenUdpTunnel(config, 2000));
    REQUIRE(client.hasStreamProxy());
    dcpp::gost::Datagram packet{"dns.invalid", 53, {0, 1, 0xff, 0x80}};
    auto bytes = dcpp::gost::encodeTunnel(packet);
    auto second = dcpp::gost::encodeTunnel({"2001:db8::1", 51413, {4, 3, 2, 1}});
    bytes.insert(bytes.end(), second.begin(), second.end());
    client.writeAll(bytes.data(), bytes.size(), 1000);
    dcpp::ByteVector reply(bytes.size());
    REQUIRE(client.readAll(reply.data(), reply.size(), 1000) == int(reply.size()));
    REQUIRE(reply == bytes);
    client.disconnect();
    server.join();
    REQUIRE(server.command == 0xf3);
    // GOST networkAddr restricts ATYP=1 to udp4. Domain-form wildcard keeps
    // one tunnel family-neutral without resolving any destination locally.
    REQUIRE(server.atyp == 3);
    REQUIRE(server.target == "0.0.0.0");
    REQUIRE(server.targetPort == 0);
}

TEST_CASE("GOST certificate and protocol failures send no credentials or target", "[gost][wire][streamproxy]") {
    for(const auto& failure : {"expired", "wrong-ip", "wrong-name", "unknown-ca", "missing-ca", "malformed-ca", "tls11"}) {
        DYNAMIC_SECTION(failure) {
            const std::string mode = failure;
            proxy_test::GostOptions options;
            if(mode == "tls11") options.tlsMax = TLS1_1_VERSION;
            proxy_test::GostServer server(options, "IP:127.0.0.1,DNS:proxy.test", mode == "expired");
            auto config = gostConfig(server);
            if(mode == "wrong-ip") { config.host = "127.0.0.2"; config.connectHost = "127.0.0.1"; }
            if(mode == "wrong-name") { config.host = "wrong.test"; config.connectHost = "127.0.0.1"; }
            if(mode == "unknown-ca") config.caPem = proxy_test::Identity::make().pem;
            if(mode == "missing-ca") config.caPem.clear();
            if(mode == "malformed-ca") config.caPem = "not PEM";
            server.start();
            Socket client;
            requireGostFailure(client, config);
            server.join();
            REQUIRE_FALSE(server.authReceived);
            REQUIRE(server.applicationBytes == 0);
            REQUIRE_FALSE(server.requestReceived);
        }
    }
}

TEST_CASE("GOST authentication response requires version one and successful status", "[gost][wire][streamproxy]") {
    for(const auto version : {0, 1, 5}) {
        proxy_test::GostOptions options;
        options.authVersion = version;
        options.authStatus = version == 1 ? 1 : 0;
        proxy_test::GostServer server(options);
        auto config = gostConfig(server);
        server.start();
        Socket client;
        requireGostFailure(client, config);
        server.join();
        REQUIRE(server.authReceived);
        REQUIRE_FALSE(server.requestReceived);
    }
}

TEST_CASE("GOST rejects invalid configuration before connecting and bounds UTF8 bytes", "[gost][wire][streamproxy]") {
    proxy_test::GostServer server;
    auto base = gostConfig(server);
    std::string utf8;
    for(int i = 0; i < 127; ++i) utf8 += "\xc3\xa9";
    for(const auto mode : {0, 1, 2, 3, 4, 5}) {
        auto config = base;
        if(mode == 0) config.user.clear();
        if(mode == 1) config.password.clear();
        if(mode == 2) config.user = utf8 + "\xc3\xa9";
        if(mode == 3) config.password = utf8 + "\xc3\xa9";
        if(mode == 4) config.verifyTls = false;
        if(mode == 5) config.caPem.assign(1024 * 1024 + 1, 'x');
        Socket client;
        requireGostFailure(client, config, 100);
    }
    // The fixture must receive its first connection only for the valid case.
    base.user = utf8 + "x";
    base.password = utf8 + "y";
    server.start();
    Socket client;
    REQUIRE_NOTHROW(client.proxyConnect("destination.invalid", "443", base, 2000));
    echoPing(client);
    server.join();
    REQUIRE(server.user == base.user);
    REQUIRE(server.password == base.password);
}

TEST_CASE("GOST shares one deadline across negotiation TLS auth and reply bytes", "[gost][wire][streamproxy]") {
#ifndef _WIN32
    sigset_t callerMaskBefore;
    struct sigaction dispositionBefore{};
    REQUIRE(pthread_sigmask(SIG_BLOCK, nullptr, &callerMaskBefore) == 0);
    REQUIRE(sigaction(SIGPIPE, nullptr, &dispositionBefore) == 0);
#endif
    proxy_test::GostOptions options;
    options.methodDelayMs = 80;
    options.authDelayMs = 80;
    options.replyByteDelayMs = 40;
    proxy_test::GostServer server(options);
    auto config = gostConfig(server);
    server.start();
    Socket client;
    const auto start = std::chrono::steady_clock::now();
    requireGostFailure(client, config, 300);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= std::chrono::milliseconds(250));
    REQUIRE(elapsed < std::chrono::milliseconds(550));
    server.join();
    REQUIRE(server.authReceived);
#ifndef _WIN32
    sigset_t callerMaskAfter;
    struct sigaction dispositionAfter{};
    REQUIRE(pthread_sigmask(SIG_BLOCK, nullptr, &callerMaskAfter) == 0);
    REQUIRE(sigaction(SIGPIPE, nullptr, &dispositionAfter) == 0);
    CHECK(sigismember(&callerMaskAfter, SIGPIPE) == sigismember(&callerMaskBefore, SIGPIPE));
    CHECK(dispositionAfter.sa_handler == dispositionBefore.sa_handler);
    CHECK(dispositionAfter.sa_flags == dispositionBefore.sa_flags);
    REQUIRE(server.sigpipeBlocked);
#endif
}

TEST_CASE("GOST cancellation interrupts TLS or auth wait without target request", "[gost][wire][streamproxy]") {
    for(bool stallTls : {true, false}) {
        proxy_test::GostOptions options;
        options.stallTls = stallTls;
        options.authDelayMs = 700;
        proxy_test::GostServer server(options);
        auto config = gostConfig(server);
        std::atomic<bool> cancelled{false};
        config.cancelled = [&] { return cancelled.load(); };
        config.cancellationNotifier = std::make_shared<dcpp::WakeNotifier>();
        server.start();
        std::jthread cancel([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            cancelled = true;
            config.cancellationNotifier->notify();
        });
        Socket client;
        const auto start = std::chrono::steady_clock::now();
        requireGostFailure(client, config, 5000);
        REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(600));
        server.join();
        REQUIRE_FALSE(server.requestReceived);
        if(stallTls) REQUIRE_FALSE(server.authReceived);
    }
}

TEST_CASE("GOST validates command replies and preserves SOCKS failure codes", "[gost][wire][streamproxy]") {
    for(const auto& reply : {dcpp::ByteVector{4, 0, 0, 1}, dcpp::ByteVector{5, 0, 1, 1},
                           dcpp::ByteVector{5, 0, 0, 2}, dcpp::ByteVector{5, 0, 0, 3, 0},
                           dcpp::ByteVector{5, 0, 0, 4, 0}, dcpp::ByteVector{5, 5, 0, 1}}) {
        proxy_test::GostOptions options;
        options.reply = reply;
        proxy_test::GostServer server(options);
        auto config = gostConfig(server);
        server.start();
        Socket client;
        requireGostFailure(client, config, 300);
        server.join();
    }
    proxy_test::GostOptions options;
    options.reply = {5, 5, 0, 1};
    proxy_test::GostServer server(options);
    auto config = gostConfig(server);
    server.start();
    Socket client;
    bool failed = false;
    try { client.gostOpenUdpTunnel(config, 1000); }
    catch(const dcpp::SocketException& e) {
        failed = true;
        REQUIRE(e.getSocksReplyCode() == 5);
    }
    REQUIRE(failed);
    REQUIRE_FALSE(client.isConnected());
    server.join();
}

TEST_CASE("GOST consumes IPv6 and domain command replies before stream data", "[gost][wire][streamproxy]") {
    for(const auto& reply : {dcpp::ByteVector{5, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1},
                           dcpp::ByteVector{5, 0, 0, 3, 3, 'a', '.', 'b', 0, 1}}) {
        proxy_test::GostOptions options;
        options.reply = reply;
        proxy_test::GostServer server(options);
        auto config = gostConfig(server);
        server.start();
        Socket client;
        REQUIRE_NOTHROW(client.proxyConnect("destination.invalid", "443", config, 2000));
        echoPing(client);
        server.join();
    }
}

TEST_CASE("Legacy immediate SOCKS TLS retains its greeting and explicit trust bypass", "[gost][wire][streamproxy]") {
    proxy_test::GostOptions options;
    options.immediateTls = true;
    proxy_test::GostServer server(options);
    auto config = gostConfig(server);
    config.type = Socket::StreamProxyConfig::Socks5;
    config.tls = true;
    config.verifyTls = false;
    server.start();
    Socket client;
    REQUIRE_NOTHROW(client.proxyConnect("destination.invalid", "443", config, 2000));
    echoPing(client);
    server.join();
    REQUIRE(server.hello == std::array<uint8_t, 3>{5, 1, 2});
}

TEST_CASE("GOST supports OpenSSL default trust paths without a profile CA", "[gost][wire][streamproxy]") {
    proxy_test::GostServer server;
    auto config = gostConfig(server);
    config.caPem.clear();
    // Redirect this process's default roots, never change the system trust store.
    const auto path = std::filesystem::temp_directory_path() /
        ("eiskalt-gost-ca-" + std::to_string(server.port()) + ".pem");
    struct Cleanup {
        std::filesystem::path path;
        std::string previous;
        bool hadPrevious;
        ~Cleanup() {
#ifndef _WIN32
            if(hadPrevious) setenv("SSL_CERT_FILE", previous.c_str(), 1);
            else unsetenv("SSL_CERT_FILE");
#endif
            std::filesystem::remove(path);
        }
    } cleanup{path, std::getenv("SSL_CERT_FILE") ? std::getenv("SSL_CERT_FILE") : "", std::getenv("SSL_CERT_FILE") != nullptr};
    { std::ofstream file(path); file << server.ca.pem; }
#ifndef _WIN32
    REQUIRE(setenv("SSL_CERT_FILE", path.c_str(), 1) == 0);
    server.start();
    Socket client;
    REQUIRE_NOTHROW(client.proxyConnect("destination.invalid", "443", config, 2000));
    echoPing(client);
    server.join();
    REQUIRE(server.authReceived);
#else
    SKIP("Default trust environment fixture requires POSIX");
#endif
}

TEST_CASE("GOST exceptions expose diagnostic stages without changing legacy errors", "[gost][wire][diagnostic-stage]") {
    using Stage = dcpp::SocketException::ProxyStage;
    const dcpp::SocketException legacy(std::string("legacy sentinel"));
    REQUIRE(legacy.getProxyStage() == Stage::None);
    auto copy = legacy;
    copy.setProxyStage(Stage::Tunnel);
    REQUIRE(copy.getError() == legacy.getError());
    REQUIRE_FALSE(copy.getSocksReplyCode());
    proxy_test::GostOptions options;
    Stage expected = Stage::Negotiation;
    bool trust = false;
    SECTION("method") { options.method = 0; }
    SECTION("certificate") { trust = true; expected = Stage::Certificate; }
    SECTION("authentication") { options.authStatus = 1; expected = Stage::Authentication; }
    SECTION("tunnel") { options.reply[0] = 4; expected = Stage::Tunnel; }
    SECTION("rejection retains REP") { options.reply[1] = 2; expected = Stage::Tunnel; }
    proxy_test::GostServer server(options);
    auto config = gostConfig(server);
    if(trust) config.caPem = proxy_test::Identity::make().pem;
    server.start();
    Socket client;
    try {
        client.proxyConnect("destination.invalid", "443", config, 1200);
        FAIL("Expected fixture rejection");
    } catch(const dcpp::SocketException& error) {
        REQUIRE(error.getProxyStage() == expected);
        if(options.reply[1]) REQUIRE(error.getSocksReplyCode() == 2);
        REQUIRE(error.getError().find(config.password) == std::string::npos);
    }
}
