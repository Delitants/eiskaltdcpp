#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "dcpp/GostUdpRelay.h"
#include "dcpp/GostProtocol.h"
#include "dcpp/SocketWake.h"
#include "proxy/DisposableGostServer.h"
#include <chrono>

namespace {
using dcpp::Socket;
Socket::StreamProxyConfig config(proxy_test::GostServer& server) {
    Socket::StreamProxyConfig result;
    result.type = Socket::StreamProxyConfig::Gost;
    result.host = result.connectHost = "127.0.0.1";
    result.port = server.port();
    result.user = "fixture-user";
    result.password = "fixture-password";
    result.caPem = server.ca.pem;
    result.tls = result.verifyTls = true;
    return result;
}
void bindClient(Socket& socket, const std::string& ip) {
    socket.create(Socket::TYPE_UDP, ip == "::1" ? AF_INET6 : AF_INET);
    socket.bind("0", ip);
    socket.setBlocking(false);
}
dcpp::gost::Datagram receive(Socket& socket) {
    REQUIRE(socket.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);
    std::array<uint8_t, 65535> bytes{};
    sockaddr_storage source{};
    int received = socket.read(bytes.data(), bytes.size(), source);
    REQUIRE(received > 0);
    dcpp::gost::Datagram packet;
    REQUIRE(dcpp::gost::decodeSocks(std::span(bytes.data(), received), packet) == dcpp::gost::DecodeResult::Complete);
    return packet;
}
}

TEST_CASE("Core GOST relay preserves datagram endpoints and binary payload", "[gost-global][udp]") {
    std::string local = "127.0.0.1";
    SECTION("IPv4 local") {}
    SECTION("IPv6 local") { local = "::1"; }
    proxy_test::GostServer server;
    server.start();
    Socket client;
    bindClient(client, local);
    dcpp::GostUdpRelay relay(config(server));
    auto port = relay.start(local, local, static_cast<uint16_t>(std::stoi(client.getLocalPort())));
    REQUIRE(port != 0);
    for(const auto& destination : {"192.0.2.1", "2001:db8::1", "peer.test"}) {
        auto packet = dcpp::gost::encodeSocks({destination, 6000, {0, 1, 255, 7}});
        client.writeTo(local, std::to_string(port), packet.data(), packet.size());
        auto response = receive(client);
        CHECK(response.host == destination);
        CHECK(response.port == 6000);
        CHECK(response.payload == dcpp::ByteVector{0, 1, 255, 7});
    }
    auto start = std::chrono::steady_clock::now();
    relay.requestStop(); relay.join();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    CHECK_FALSE(relay.isRunning());
    CHECK_THROWS(relay.start(local, local, 0));
}

TEST_CASE("Core GOST relay restricts the sender and malformed packets cannot claim it", "[gost-global][udp]") {
    proxy_test::GostServer server;
    server.start();
    Socket client, intruder;
    bindClient(client, "127.0.0.1"); bindClient(intruder, "127.0.0.1");
    dcpp::GostUdpRelay relay(config(server));
    uint16_t expected = 0;
    SECTION("Explicit sender port") { expected = std::stoi(client.getLocalPort()); }
    SECTION("First valid sender port") {}
    auto port = relay.start("127.0.0.1", "127.0.0.1", expected);
    const auto packet = dcpp::gost::encodeSocks({"peer.test", 6000, {42}});
    const uint8_t malformed[]{0, 0, 1, 1, 127, 0, 0, 1, 1, 1, 4};
    intruder.writeTo("127.0.0.1", std::to_string(port), malformed, sizeof(malformed));
    client.writeTo("127.0.0.1", std::to_string(port), packet.data(), packet.size());
    CHECK(receive(client).payload == dcpp::ByteVector{42});
    intruder.writeTo("127.0.0.1", std::to_string(port), packet.data(), packet.size());
    CHECK(intruder.wait(100, Socket::WAIT_READ) == Socket::WAIT_NONE);
    relay.requestStop(); relay.join();
}

TEST_CASE("Core GOST relay rejects nonloopback bindings and cancelled setup", "[gost-global][udp]") {
    Socket::StreamProxyConfig upstream;
    upstream.type = Socket::StreamProxyConfig::Gost;
    upstream.host = upstream.connectHost = "127.0.0.1";
    upstream.port = 1;
    upstream.user = "fixture-user";
    upstream.password = "fixture-password";
    SECTION("Nonloopback bind") {
        dcpp::GostUdpRelay relay(upstream);
        CHECK_THROWS(relay.start("0.0.0.0", "127.0.0.1", 0));
    }
    SECTION("Nonloopback sender") {
        dcpp::GostUdpRelay relay(upstream);
        CHECK_THROWS(relay.start("127.0.0.1", "192.0.2.1", 0));
    }
    SECTION("Cancellation") {
        upstream.cancelled = [] { return true; };
        dcpp::GostUdpRelay relay(upstream);
        auto start = std::chrono::steady_clock::now();
        CHECK_THROWS(relay.start("127.0.0.1", "127.0.0.1", 0));
        relay.join();
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    }
}

TEST_CASE("GOST UDP tries later proxy addresses only before negotiation", "[gost-global][review-fixes]") {
    proxy_test::GostOptions options;
    bool reject = false;
    SECTION("Unreachable first candidate") { }
    SECTION("Authentication is terminal") { reject = true; options.authStatus = 1; }
    proxy_test::GostServer server(options);
    server.start();
    auto upstream = config(server);
    std::vector<std::string> candidates = reject ?
        std::vector<std::string>{"127.0.0.1", "127.0.0.2"} :
        std::vector<std::string>{"127.0.0.2", "127.0.0.1"};
    dcpp::GostUdpRelay relay(upstream, 2000, candidates);
    Socket client; bindClient(client, "127.0.0.1");
    if(reject) {
        try {
            relay.start("127.0.0.1", "127.0.0.1", std::stoi(client.getLocalPort()));
            FAIL("Authentication rejection was ignored");
        } catch(const dcpp::SocketException& error) {
            CHECK(error.getProxyStage() == dcpp::SocketException::ProxyStage::Authentication);
        }
    } else {
        const auto port = relay.start("127.0.0.1", "127.0.0.1", std::stoi(client.getLocalPort()));
        const auto packet = dcpp::gost::encodeSocks({"192.0.2.1", 6000, {42}});
        client.writeTo("127.0.0.1", std::to_string(port), packet.data(), packet.size());
        CHECK(receive(client).payload == dcpp::ByteVector{42});
    }
    relay.requestStop(); relay.join(); server.requestStop(); server.join();
}

TEST_CASE("Independent core GOST associations do not exchange consumer replies", "[gost-global][udp]") {
    proxy_test::GostServer firstServer, secondServer;
    firstServer.start(); secondServer.start();
    Socket first, second;
    bindClient(first, "127.0.0.1"); bindClient(second, "127.0.0.1");
    dcpp::GostUdpRelay firstRelay(config(firstServer)), secondRelay(config(secondServer));
    auto firstPort = firstRelay.start("127.0.0.1", "127.0.0.1", std::stoi(first.getLocalPort()));
    auto secondPort = secondRelay.start("127.0.0.1", "127.0.0.1", std::stoi(second.getLocalPort()));
    auto one = dcpp::gost::encodeSocks({"search.test", 6000, {1}});
    auto two = dcpp::gost::encodeSocks({"dht.test", 6001, {2}});
    first.writeTo("127.0.0.1", std::to_string(firstPort), one.data(), one.size());
    second.writeTo("127.0.0.1", std::to_string(secondPort), two.data(), two.size());
    CHECK(receive(first).host == "search.test");
    CHECK(receive(second).host == "dht.test");
    firstRelay.requestStop(); firstRelay.join();
    second.writeTo("127.0.0.1", std::to_string(secondPort), two.data(), two.size());
    CHECK(receive(second).payload == dcpp::ByteVector{2});
    CHECK(secondRelay.isRunning());
    secondRelay.requestStop(); secondRelay.join();
}

TEST_CASE("Core GOST setup cancellation interrupts a stalled TLS exchange", "[gost-global][udp]") {
    proxy_test::GostOptions options;
    options.stallTls = true;
    proxy_test::GostServer server(options);
    server.start();
    auto upstream = config(server);
    std::atomic<bool> cancelled{false};
    upstream.cancelled = [&] { return cancelled.load(); };
    dcpp::GostUdpRelay relay(upstream);
    std::jthread trigger([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancelled = true;
    });
    auto start = std::chrono::steady_clock::now();
    CHECK_THROWS(relay.start("127.0.0.1", "127.0.0.1", 0));
    relay.join();
    CHECK_FALSE(relay.isRunning());
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
}

TEST_CASE("Local relay stop still cancels when its profile has a route-only notifier", "[socket-wake][gost-global][review-fixes]") {
    proxy_test::GostOptions options;
    options.stallTls = true;
    options.stallTlsMs = 3000;
    proxy_test::GostServer server(options);
    server.start();
    auto upstream = config(server);
    upstream.cancellationNotifier = std::make_shared<dcpp::WakeNotifier>();
    upstream.cancelled = [] { return false; };
    dcpp::GostUdpRelay relay(upstream);
    std::jthread stop([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        relay.requestStop();
    });
    const auto start = std::chrono::steady_clock::now();
    CHECK_THROWS(relay.start("127.0.0.1", "127.0.0.1", 0));
    relay.join();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
}
