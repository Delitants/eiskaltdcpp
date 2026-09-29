#include "dcpp/stdinc.h"
#include "ProxyTestRunner.h"
#include "tests/TestContext.h"
#include "dcpp/SettingsManager.h"
#include "tests/proxy/DisposableGostServer.h"
#include "dcpp/GostProtocol.h"
#include <QMetaEnum>
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QTcpServer>
#include <atomic>
#include <array>
#include <thread>

namespace {
using Socket = dcpp::Socket;
using Status = ProxyTestRunner::Status;

bool pumpUntil(const std::function<bool()>& ready, int milliseconds = 3000) {
    QElapsedTimer time;
    time.start();
    while (!ready() && time.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}

struct Proxy {
    Socket listener;
    Socket::StreamProxyConfig config;
    std::jthread thread;
    std::atomic<bool> accepted{false};
    explicit Proxy(std::function<void(Socket&)> serve, int connections = 1) {
        listener.create(Socket::TYPE_TCP, AF_INET);
        listener.bind("0", "127.0.0.1");
        listener.listen();
        config.type = Socket::StreamProxyConfig::Socks5;
        config.host = "127.0.0.1";
        config.port = std::stoi(listener.getLocalPort());
        thread = std::jthread([this, serve, connections] {
            try {
              for (int i = 0; i < connections; ++i) {
                if (listener.wait(2000, Socket::WAIT_READ) != Socket::WAIT_READ) return;
                Socket peer;
                peer.accept(listener);
                peer.setBlocking(false);
#ifdef SO_NOSIGPIPE
                peer.setSocketOpt(SO_NOSIGPIPE, 1);
#endif
                accepted = true;
                serve(peer);
              }
            } catch (...) { }
        });
    }
};

bool hello(Socket& peer, bool auth = false) {
    unsigned char bytes[3]{};
    if (peer.readAll(bytes, 3, 1000) != 3 || bytes[0] != 5 || bytes[2] != (auth ? 2 : 0)) return false;
    const unsigned char selected[] = {5, static_cast<unsigned char>(auth ? 2 : 0)};
    peer.writeAll(selected, 1, 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    peer.writeAll(selected + 1, 1, 1000);
    return true;
}

bool request(Socket& peer, int command = 1) {
    unsigned char header[4]{};
    if (peer.readAll(header, 4, 1000) != 4 || header[0] != 5 || header[1] != command) return false;
    unsigned char length = 0;
    if (header[3] == 3 && peer.readAll(&length, 1, 1000) != 1) return false;
    const int size = header[3] == 1 ? 4 : header[3] == 4 ? 16 : length;
    std::vector<char> tail(size + 2);
    return peer.readAll(tail.data(), tail.size(), 1000) == int(tail.size());
}

void connected(Socket& peer) {
    const unsigned char response[] = {5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
    for (const auto byte : response) {
        peer.writeAll(&byte, 1, 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
}

TEST_CASE("Proxy test accepts fragmented SOCKS replies without touching shared settings", "[qt][proxytestrunner][network]") {
    dcpp::test::TestContext context;
    auto* settings = context.ownedCtx->getSettingsManager();
    settings->set(dcpp::SettingsManager::SOCKS_SERVER, "untouched.invalid");
    settings->unset(dcpp::SettingsManager::SOCKS_PORT);
    Proxy proxy([](Socket& peer) { if (hello(peer) && request(peer)) connected(peer); });
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, false, 1500));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(result.tcp.status == Status::Success);
    REQUIRE(result.udp.status == Status::NotRequested);
    REQUIRE_FALSE(runner.isRunning());
    REQUIRE(settings->get(dcpp::SettingsManager::SOCKS_SERVER) == "untouched.invalid");
    REQUIRE(settings->isDefault(dcpp::SettingsManager::SOCKS_PORT));
}

TEST_CASE("GOST diagnostics use immutable targets and never reveal credentials", "[qt][proxytestrunner][gost][routing]") {
    dcpp::test::TestContext context;
    auto *settings = context.ownedCtx->getSettingsManager();
    settings->set(dcpp::SettingsManager::SOCKS_SERVER, "unchanged.invalid");
    proxy_test::GostOptions options;
    const char *expected = "Success";
    bool badTrust = false;
    SECTION("authenticated CONNECT") {}
    SECTION("plain method rejected") { options.method = 0; expected = "NegotiationFailed"; }
    SECTION("certificate rejected") { badTrust = true; expected = "CertificateFailed"; }
    SECTION("credentials rejected") { options.authStatus = 1; expected = "AuthenticationFailed"; }
    SECTION("command reply malformed") { options.reply[0] = 4; expected = "TunnelFailed"; }
    SECTION("destination rejected") { options.reply[1] = 2; expected = "DestinationRejected"; }
    proxy_test::GostServer server(options);
    server.start();
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Gost;
    config.host = "proxy.test";
    config.connectHost = "127.0.0.1";
    config.port = server.port();
    config.user = "sensitive-fixture-user";
    config.password = "sensitive-fixture-password";
    config.remoteDns = false; // GOST still forwards the target name, never resolves it here.
    config.caPem = badTrust ? proxy_test::Identity::make().pem : server.ca.pem;
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false, tick = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    const ProxyTestRunner::ProbeTargets targets("original.invalid", 32123);
    REQUIRE(runner.start(config, targets, false, 1500));
    config.host = "mutated.invalid";
    config.password = "mutated";
    QTimer::singleShot(0, &runner, [&] { tick = true; });
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(tick);
    const int status = QMetaEnum::fromType<Status>().keyToValue(expected);
    REQUIRE(status >= 0);
    INFO(result.tcp.message.toStdString());
    REQUIRE(int(result.tcp.status) == status);
    REQUIRE_FALSE(result.tcp.message.contains("sensitive-fixture"));
    REQUIRE(settings->get(dcpp::SettingsManager::SOCKS_SERVER) == "unchanged.invalid");
    server.requestStop();
    server.join();
    if (result.tcp.status == Status::Success) {
        REQUIRE(server.target == "original.invalid");
        REQUIRE(server.targetPort == 32123);
        REQUIRE(server.user == "sensitive-fixture-user");
        REQUIRE(result.tcp.message.contains("not an application"));
    }
    if (badTrust) REQUIRE_FALSE(server.authReceived);
}

TEST_CASE("GOST diagnostic timeout and cancellation leave the UI responsive", "[qt][proxytestrunner][gost][routing][gost-native]") {
    proxy_test::GostOptions options;
    options.stallTls = true;
    proxy_test::GostServer server(options);
    server.start();
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Gost;
    config.host = "127.0.0.1";
    config.port = server.port();
    config.user = "fixture-user";
    config.password = "fixture-secret";
    config.caPem = server.ca.pem;
    bool cancel = false;
    SECTION("deadline") {}
    SECTION("cancel") { cancel = true; }
    ProxyTestRunner runner;
    bool done = false;
    int ticks = 0;
    ProxyTestRunner::Result result;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &runner, [&] { ++ticks; });
    heartbeat.start(10);
    REQUIRE(runner.start(config, false, 300));
    if (cancel) QTimer::singleShot(80, &runner, &ProxyTestRunner::cancel);
    REQUIRE(pumpUntil([&] { return done; }, 1500));
    REQUIRE(result.tcp.status == (cancel ? Status::Cancelled : Status::TimedOut));
    REQUIRE(ticks >= 3);
}

TEST_CASE("GOST UDP success requires a matching tunneled DNS response", "[qt][proxytestrunner][gost][routing]") {
    int mode = 0;
    SECTION("matching reply") {}
    SECTION("echo is not a DNS response") { mode = 1; }
    SECTION("wrong transaction") { mode = 2; }
    SECTION("wrong question") { mode = 3; }
    SECTION("wrong resolver") { mode = 4; }
    SECTION("wrong resolver port") { mode = 5; }
    SECTION("truncated DNS record") { mode = 6; }
    SECTION("malformed tunnel frame") { mode = 7; }
    Socket listener;
    listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    const auto ca = proxy_test::Identity::make();
    proxy_test::GostOptions options;
    options.transform = [mode, buffered = dcpp::ByteVector{}](std::span<const uint8_t> bytes) mutable {
        buffered.insert(buffered.end(), bytes.begin(), bytes.end());
        dcpp::gost::Datagram message;
        size_t consumed = 0;
        if (dcpp::gost::decodeTunnel(buffered, message, consumed) != dcpp::gost::DecodeResult::Complete)
            return dcpp::ByteVector{};
        buffered.erase(buffered.begin(), buffered.begin() + consumed);
        if (message.host != "2001:db8::53" || message.port != 5300 || message.payload.size() < 12)
            return dcpp::ByteVector{};
        if (mode != 1) message.payload[2] |= 0x80;
        if (mode == 2) message.payload[0] ^= 1;
        if (mode == 3) message.payload[13] ^= 1;
        if (mode == 4) message.host = "2001:db8::54";
        if (mode == 5) ++message.port;
        if (mode == 6) message.payload[7] = 1;
        auto frame = dcpp::gost::encodeTunnel(message);
        if (mode == 7) frame[2] = 1;
        return frame;
    };
    proxy_test::GostServer first(options, "IP:127.0.0.1", false, &ca);
    proxy_test::GostServer second(options, "IP:127.0.0.1", false, &ca);
    first.start(&listener);
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Gost;
    config.host = "127.0.0.1";
    config.port = std::stoi(listener.getLocalPort());
    config.user = "fixture-user";
    config.password = "fixture-secret";
    config.caPem = ca.pem;
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(config, {"original.invalid", 32123, "2001:db8::53", 5300, "probe.invalid"}, true, 1000));
    // One acceptor at a time: two nonblocking acceptors can race on the same
    // readiness event, causing the loser to exit before the UDP connection.
    REQUIRE(pumpUntil([&] { return first.commandReady.load(); }));
    second.start(&listener);
    REQUIRE(pumpUntil([&] { return done; }));
    first.requestStop(); second.requestStop();
    first.join(); second.join();
    CAPTURE(mode, first.command, second.command, first.error, second.error);
    INFO(result.udp.message.toStdString());
    REQUIRE(result.tcp.status == Status::Success);
    if (!mode) REQUIRE(result.udp.status == Status::Success);
    else {
        REQUIRE(result.udp.status != Status::Success);
        REQUIRE(result.udp.status != Status::Unsupported);
    }
    REQUIRE(((first.command == 1 && second.command == 0xf3) || (first.command == 0xf3 && second.command == 1)));
}

TEST_CASE("Proxy test preserves rejection code even when proxy closes before reply tail", "[qt][proxytestrunner][network]") {
    unsigned char code = 2;
    SECTION("general server failure observed live") { code = 1; }
    SECTION("connection forbidden by rules") { code = 2; }
    SECTION("command unsupported is not authentication failure") { code = 7; }
    Proxy proxy([code](Socket& peer) {
        if (!hello(peer) || !request(peer)) return;
        const unsigned char denied[] = {5, code, 0, 3};
        peer.writeAll(denied, sizeof(denied), 1000);
    });
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, false, 1500));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(result.tcp.status == Status::DestinationRejected);
    REQUIRE(result.tcp.socksReply == code);
    REQUIRE(result.tcp.message.contains("example.com"));
    REQUIRE(result.tcp.message.contains(QStringLiteral("REP=%1").arg(code)));
}

TEST_CASE("Proxy test never attaches SOCKS rejection codes to malformed headers", "[qt][proxytestrunner][network]") {
    std::array<unsigned char, 4> response{5, 2, 0, 3};
    int count = 4;
    SECTION("invalid version") { response[0] = 4; }
    SECTION("nonzero reserved byte") { response[2] = 1; }
    SECTION("nonzero reserved byte with success code") { response[1] = 0; response[2] = 1; }
    SECTION("truncated header") { count = 2; }
    Proxy proxy([=](Socket& peer) {
        if (!hello(peer) || !request(peer)) return;
        peer.writeAll(response.data(), count, 1000);
    });
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, false, 1500));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(result.tcp.status == Status::Failed);
    REQUIRE(result.tcp.socksReply == -1);
}

TEST_CASE("Socket exceptions retain legacy messages and have no implicit SOCKS reply code", "[qt][proxytestrunner]") {
    const dcpp::SocketException legacy(std::string("legacy error"));
    REQUIRE(legacy.getError().find("legacy error") != std::string::npos);
    REQUIRE_FALSE(legacy.getSocksReplyCode().has_value());
    const dcpp::SocketException systemError(ECONNREFUSED);
    REQUIRE_FALSE(systemError.getSocksReplyCode().has_value());
    const dcpp::SocketException rejection(std::string("legacy error"), 2);
    REQUIRE(rejection.getError() == legacy.getError());
    REQUIRE(rejection.getSocksReplyCode().value() == 2);
}

TEST_CASE("Proxy test cancellation keeps UI ticking and does not allow concurrent starts", "[qt][proxytestrunner][network]") {
    Proxy proxy([](Socket&) { std::this_thread::sleep_for(std::chrono::milliseconds(400)); });
    ProxyTestRunner runner;
    bool done = false;
    ProxyTestRunner::Result result;
    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] { ++ticks; });
    timer.start(5);
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, true, 1500));
    REQUIRE_FALSE(runner.start(proxy.config, false));
    QTimer::singleShot(80, &runner, &ProxyTestRunner::cancel);
    REQUIRE(pumpUntil([&] { return done; }, 500));
    REQUIRE(ticks >= 3);
    REQUIRE(result.tcp.status == Status::Cancelled);
    REQUIRE(result.udp.status == Status::Cancelled);
}

TEST_CASE("Destroying a proxy test owner is prompt and suppresses completion callbacks", "[qt][proxytestrunner][network]") {
    Proxy proxy([](Socket&) { std::this_thread::sleep_for(std::chrono::milliseconds(250)); });
    bool called = false;
    auto runner = std::make_unique<ProxyTestRunner>();
    QObject::connect(runner.get(), &ProxyTestRunner::finished, QCoreApplication::instance(), [&](auto) { called = true; });
    REQUIRE(runner->start(proxy.config, false, 1000));
    REQUIRE(pumpUntil([&] { return proxy.accepted.load(); }));
    QElapsedTimer elapsed;
    elapsed.start();
    runner.reset();
    REQUIRE(elapsed.elapsed() < 100);
    pumpUntil([] { return false; }, 350);
    REQUIRE_FALSE(called);
}

TEST_CASE("Proxy test validates authenticated SOCKS exchange", "[qt][proxytestrunner][network]") {
    std::atomic<bool> credentials{false};
    Proxy proxy([&](Socket& peer) {
        if (!hello(peer, true)) return;
        unsigned char auth[2]{};
        if (peer.readAll(auth, 2, 1000) != 2 || auth[0] != 1 || auth[1] != 4) return;
        char user[4]{};
        unsigned char size{};
        if (peer.readAll(user, 4, 1000) != 4 || peer.readAll(&size, 1, 1000) != 1 || size != 6) return;
        char password[6]{};
        if (peer.readAll(password, 6, 1000) != 6) return;
        credentials = std::string(user, 4) == "test" && std::string(password, 6) == "secret";
        const unsigned char response[] = {1, 0};
        peer.writeAll(response, 2, 1000);
        if (request(peer)) connected(peer);
    });
    proxy.config.user = "test";
    proxy.config.password = "secret";
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, false, 1500));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(credentials.load());
    REQUIRE(result.tcp.status == Status::Success);
}

TEST_CASE("Proxy test has an overall timeout rather than a timeout per fragment", "[qt][proxytestrunner][network]") {
    Proxy proxy([](Socket& peer) {
        if (!hello(peer) || !request(peer)) return;
        const unsigned char bytes[] = {5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
        for (const auto byte : bytes) {
            peer.writeAll(&byte, 1, 1000);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    });
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, false, 150));
    REQUIRE(pumpUntil([&] { return done; }, 500));
    REQUIRE(result.tcp.status == Status::TimedOut);
}

TEST_CASE("Proxy test does not claim TLS UDP is supported", "[qt][proxytestrunner][network]") {
    Proxy proxy([](Socket&) { std::this_thread::sleep_for(std::chrono::milliseconds(250)); });
    proxy.config.tls = true;
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, true, 100));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(result.udp.status == Status::Unsupported);
}

TEST_CASE("Proxy test UDP success requires a relayed matching DNS transaction", "[qt][proxytestrunner][network]") {
    bool sendValid = false;
    bool associateOnly = false;
    bool fragmented = false;
    SECTION("matching relay response") { sendValid = true; }
    SECTION("wrong transaction is not success") { sendValid = false; }
    SECTION("association alone is not success") { associateOnly = true; }
    SECTION("fragmented SOCKS datagram is not success") { sendValid = true; fragmented = true; }
    std::atomic<int> commands{0};
    std::atomic<bool> sawQuery{false};
    Proxy proxy([&](Socket& peer) {
        if (!hello(peer)) return;
        if (++commands == 1) {
            if (request(peer)) connected(peer);
            return;
        }
        if (!request(peer, 3)) return;
        QUdpSocket relay;
        if (!relay.bind(QHostAddress::LocalHost, 0)) return;
        const auto port = relay.localPort();
        const unsigned char response[] = {5, 0, 0, 1, 127, 0, 0, 1,
            static_cast<unsigned char>(port >> 8), static_cast<unsigned char>(port)};
        peer.writeAll(response, sizeof(response), 1000);
        if (!relay.waitForReadyRead(1000)) return;
        QByteArray packet(int(relay.pendingDatagramSize()), '\0');
        QHostAddress sender;
        quint16 senderPort = 0;
        relay.readDatagram(packet.data(), packet.size(), &sender, &senderPort);
        if (packet.size() < 27 || packet.left(4) != QByteArray::fromHex("00000001") ||
            packet.mid(8, 2) != QByteArray::fromHex("0035")) return;
        sawQuery = true;
        // Preserve the relay header, transaction ID, and question exactly.
        packet[12] = char(0x81);
        packet[13] = char(0x80);
        if (!sendValid) packet[10] = char(packet[10] ^ 0x7f);
        if (fragmented) packet[2] = char(1);
        if (!associateOnly) relay.writeDatagram(packet, sender, senderPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }, 2);
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, true, 350));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(sawQuery.load());
    REQUIRE(result.tcp.status == Status::Success);
    REQUIRE(result.udp.status == (sendValid && !fragmented && !associateOnly ? Status::Success : Status::TimedOut));
}

TEST_CASE("Proxy test supports an IPv6 UDP relay behind IPv4 control and preserves the associate port", "[qt][proxytestrunner][network]") {
    QUdpSocket ipv6Available;
    ipv6Available.setProxy(QNetworkProxy::NoProxy);
    if (!ipv6Available.bind(QHostAddress::LocalHostIPv6, 0))
        SKIP("IPv6 loopback is unavailable on this host");
    ipv6Available.close();
    std::atomic<int> commands{0};
    std::atomic<bool> portPreserved{false};
    std::atomic<bool> sawIpv6Sender{false};
    Proxy proxy([&](Socket& peer) {
        if (!hello(peer)) return;
        if (++commands == 1) {
            if (request(peer)) connected(peer);
            return;
        }
        unsigned char associate[10]{};
        if (peer.readAll(associate, sizeof(associate), 1000) != sizeof(associate) ||
            associate[0] != 5 || associate[1] != 3 || associate[3] != 1) return;
        const quint16 requestedPort = (quint16(associate[8]) << 8) | associate[9];
        QUdpSocket relay;
        relay.setProxy(QNetworkProxy::NoProxy);
        if (!relay.bind(QHostAddress::LocalHostIPv6, 0)) return;
        QByteArray response = QByteArray::fromHex("05000004");
        response += QByteArray(15, '\0');
        response += char(1);
        response += char(relay.localPort() >> 8);
        response += char(relay.localPort());
        peer.writeAll(response.constData(), response.size(), 1000);
        if (!relay.waitForReadyRead(1000)) return;
        QByteArray packet(int(relay.pendingDatagramSize()), '\0');
        QHostAddress sender;
        quint16 senderPort = 0;
        relay.readDatagram(packet.data(), packet.size(), &sender, &senderPort);
        portPreserved = requestedPort != 0 && senderPort == requestedPort;
        sawIpv6Sender = sender == QHostAddress::LocalHostIPv6;
        if (packet.size() < 27 || !packet.startsWith(QByteArray::fromHex("00000001010101010035"))) return;
        packet[12] = char(0x81);
        packet[13] = char(0x80);
        relay.writeDatagram(packet, sender, senderPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }, 2);
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, true, 1500));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(result.tcp.status == Status::Success);
    REQUIRE(result.udp.status == Status::Success);
    REQUIRE(sawIpv6Sender.load());
    REQUIRE(portPreserved.load());
}

TEST_CASE("Proxy test reports authentication rejection without a target attempt", "[qt][proxytestrunner][network]") {
    Proxy proxy([](Socket& peer) {
        unsigned char hello[3]{};
        if (peer.readAll(hello, 3, 1000) != 3) return;
        const unsigned char denied[] = {5, 255};
        peer.writeAll(denied, sizeof(denied), 1000);
    });
    ProxyTestRunner runner;
    ProxyTestRunner::Result result;
    bool done = false;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, false, 1000));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(result.tcp.status == Status::AuthenticationFailed);
}

TEST_CASE("Proxy test copies the chosen TCP target without local DNS in remote mode", "[qt][proxytestrunner][proxy-targets][network]") {
    std::atomic<bool> sawTarget{false};
    Proxy proxy([&](Socket& peer) {
        if (!hello(peer)) return;
        const QByteArray expected = QByteArray::fromHex("050100030e") + "chosen.invalid" + QByteArray::fromHex("20fb");
        QByteArray bytes(expected.size(), '\0');
        if (peer.readAll(bytes.data(), bytes.size(), 1000) != bytes.size()) return;
        sawTarget = bytes == expected;
        connected(peer);
    });
    ProxyTestRunner runner;
    bool done = false;
    ProxyTestRunner::Result result;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    {
        const ProxyTestRunner::ProbeTargets targets("chosen.invalid", 8443, "192.0.2.53", 5353, "dns.invalid");
        REQUIRE(runner.start(proxy.config, targets, false, 1500));
    }
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(sawTarget.load());
    REQUIRE(result.tcp.status == Status::Success);
    REQUIRE(result.tcp.message.contains("chosen.invalid:8443"));
}

TEST_CASE("Proxy targets reject unsafe or unbounded input before opening a proxy socket", "[qt][proxytestrunner][proxy-targets][network]") {
    QString host = "chosen.invalid", resolver = "192.0.2.53", query = "dns.invalid";
    int tcpPort = 443, dnsPort = 53;
    SECTION("URL") { host = "https://chosen.invalid"; }
    SECTION("path") { host = "chosen.invalid/path"; }
    SECTION("HTTP header injection") { host = "chosen.invalid\r\nX-Test: injected"; }
    SECTION("embedded nul") { host = QString("chosen") + QChar(0) + ".invalid"; }
    SECTION("overlong host") { host = QString(254, 'a'); }
    SECTION("overlong DNS label") { query = QString(64, 'a') + ".invalid"; }
    SECTION("empty query label") { query = "dns..invalid"; }
    SECTION("DNS URL") { query = "https://dns.invalid"; }
    SECTION("resolver hostname") { resolver = "resolver.invalid"; }
    SECTION("resolver unspecified") { resolver = "::"; }
    SECTION("resolver multicast") { resolver = "ff02::1"; }
    SECTION("scoped resolver") { resolver = "fe80::1%en0"; }
    SECTION("TCP zero port") { tcpPort = 0; }
    SECTION("TCP oversized port") { tcpPort = 65536; }
    SECTION("UDP negative port") { dnsPort = -1; }
    SECTION("UDP oversized port") { dnsPort = 65536; }
    QTcpServer proxy;
    proxy.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(proxy.listen(QHostAddress::LocalHost));
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Socks5;
    config.host = "127.0.0.1";
    config.port = proxy.serverPort();
    ProxyTestRunner runner;
    bool done = false;
    ProxyTestRunner::Result result;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    const ProxyTestRunner::ProbeTargets targets(host, tcpPort, resolver, dnsPort, query);
    REQUIRE_FALSE(ProxyTestRunner::validateTargets(targets, true).isEmpty());
    REQUIRE(runner.start(config, targets, true, 200));
    REQUIRE(pumpUntil([&] { return done; }, 500));
    REQUIRE(result.tcp.status == Status::Failed);
    REQUIRE(result.udp.status == Status::Failed);
    REQUIRE_FALSE(proxy.hasPendingConnections());
}

TEST_CASE("Custom proxy targets never fall back to a direct connection", "[qt][proxytestrunner][proxy-targets][network]") {
    QTcpServer target, closedProxy;
    target.setProxy(QNetworkProxy::NoProxy);
    closedProxy.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(target.listen(QHostAddress::LocalHost));
    REQUIRE(closedProxy.listen(QHostAddress::LocalHost));
    Socket::StreamProxyConfig config;
    config.type = Socket::StreamProxyConfig::Socks5;
    config.host = "127.0.0.1";
    config.port = closedProxy.serverPort();
    closedProxy.close();
    ProxyTestRunner runner;
    bool done = false;
    ProxyTestRunner::Result result;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    const ProxyTestRunner::ProbeTargets targets("127.0.0.1", target.serverPort());
    REQUIRE(runner.start(config, targets, false, 200));
    REQUIRE(pumpUntil([&] { return done; }, 1000));
    REQUIRE(result.tcp.status != Status::Success);
    REQUIRE_FALSE(target.hasPendingConnections());
}

TEST_CASE("Proxy target numeric IPv4 retains explicit local-resolution mode", "[qt][proxytestrunner][proxy-targets][network]") {
    std::atomic<bool> sawTarget{false};
    Proxy proxy([&](Socket& peer) {
        if (!hello(peer)) return;
        QByteArray bytes(10, '\0');
        if (peer.readAll(bytes.data(), bytes.size(), 1000) != bytes.size()) return;
        sawTarget = bytes == QByteArray::fromHex("05010001c000026320fb");
        connected(peer);
    });
    proxy.config.remoteDns = false;
    ProxyTestRunner runner;
    bool done = false;
    ProxyTestRunner::Result result;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    REQUIRE(runner.start(proxy.config, ProxyTestRunner::ProbeTargets("192.0.2.99", 8443), false, 1000));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(sawTarget.load());
    REQUIRE(result.tcp.status == Status::Success);
}

TEST_CASE("Proxy UDP target envelope and DNS question follow the immutable diagnostic choices", "[qt][proxytestrunner][proxy-targets][network]") {
    bool ipv6 = false;
    enum class Reply { Valid, WrongSource, WrongPort, WrongQuestion, WrongType, WrongId, Fragmented, WrongSender };
    Reply reply = Reply::Valid;
    SECTION("IPv4 numeric destination") {}
    SECTION("IPv6 numeric destination") { ipv6 = true; }
    SECTION("wrong resolver source") { reply = Reply::WrongSource; }
    SECTION("wrong resolver port") { reply = Reply::WrongPort; }
    SECTION("same ID different question") { reply = Reply::WrongQuestion; }
    SECTION("same ID different question type") { reply = Reply::WrongType; }
    SECTION("wrong transaction") { reply = Reply::WrongId; }
    SECTION("fragmented reply") { reply = Reply::Fragmented; }
    SECTION("wrong UDP sender") { reply = Reply::WrongSender; }
    const QByteArray envelope = QByteArray::fromHex(ipv6
        ? "0000000420010db800000000000000000000005314e9" : "00000001c000023514e9");
    const QByteArray question = QByteArray::fromHex("05636865636b07696e76616c69640000010001");
    std::atomic<int> commands{0};
    std::atomic<bool> sawQuery{false};
    Proxy proxy([&](Socket& peer) {
        if (!hello(peer)) return;
        if (++commands == 1) {
            if (request(peer)) connected(peer);
            return;
        }
        if (!request(peer, 3)) return;
        QUdpSocket relay;
        relay.setProxy(QNetworkProxy::NoProxy);
        if (!relay.bind(QHostAddress::LocalHost, 0)) return;
        QByteArray response = QByteArray::fromHex("050000017f000001");
        response += char(relay.localPort() >> 8);
        response += char(relay.localPort());
        peer.writeAll(response.constData(), response.size(), 1000);
        if (!relay.waitForReadyRead(1000)) return;
        QByteArray packet(int(relay.pendingDatagramSize()), '\0');
        QHostAddress sender;
        quint16 senderPort{};
        relay.readDatagram(packet.data(), packet.size(), &sender, &senderPort);
        const int offset = envelope.size();
        if (!packet.startsWith(envelope) || packet.mid(offset + 12) != question) return;
        sawQuery = true;
        packet[offset + 2] = char(0x81);
        packet[offset + 3] = char(0x80);
        if (reply == Reply::WrongSource) packet[4] ^= char(1);
        if (reply == Reply::WrongPort) packet[offset - 1] ^= char(1);
        if (reply == Reply::WrongQuestion) packet[offset + 13] = 'x';
        if (reply == Reply::WrongType) packet[packet.size() - 3] = char(28);
        if (reply == Reply::WrongId) packet[offset] ^= char(1);
        if (reply == Reply::Fragmented) packet[2] = char(1);
        QUdpSocket stranger;
        stranger.setProxy(QNetworkProxy::NoProxy);
        if (reply == Reply::WrongSender) stranger.writeDatagram(packet, sender, senderPort);
        else relay.writeDatagram(packet, sender, senderPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
    }, 2);
    ProxyTestRunner runner;
    bool done = false;
    ProxyTestRunner::Result result;
    QObject::connect(&runner, &ProxyTestRunner::finished, &runner, [&](auto value) { result = value; done = true; });
    const ProxyTestRunner::ProbeTargets targets("chosen.invalid", 8443,
        ipv6 ? "2001:db8::53" : "192.0.2.53", 5353, "check.invalid");
    REQUIRE(runner.start(proxy.config, targets, true, 450));
    REQUIRE(pumpUntil([&] { return done; }));
    REQUIRE(sawQuery.load());
    REQUIRE(result.tcp.status == Status::Success);
    REQUIRE(result.udp.status == (reply == Reply::Valid ? Status::Success : Status::TimedOut));
    if (reply == Reply::Valid) {
        REQUIRE(result.udp.message.contains("5353"));
        REQUIRE(result.udp.message.contains("check.invalid"));
    }
}
