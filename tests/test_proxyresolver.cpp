#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "dcpp/ProxyResolver.h"
#include "dcpp/Socket.h"
#include <chrono>
#include <atomic>
#include <thread>

TEST_CASE("Proxy resolver recognizes numeric endpoints and cancellation", "[gost-global][resolver]") {
    CHECK(dcpp::resolveProxyEndpoint("127.0.0.1", 1000, {}) == std::vector<std::string>{"127.0.0.1"});
    CHECK(dcpp::resolveProxyEndpoint("::1", 1000, {}) == std::vector<std::string>{"::1"});
    CHECK_THROWS(dcpp::resolveProxyEndpoint("127.0.0.1", 1000, [] { return true; }));
    CHECK_THROWS(dcpp::resolveProxyEndpoint("", 1000, {}));
    CHECK_THROWS(dcpp::resolveProxyEndpoint(std::string("proxy\0.invalid", 14), 1000, {}));
    CHECK_THROWS(dcpp::resolveProxyEndpoint("proxy.invalid", 0, {}));
}

TEST_CASE("Proxy DNS cancellation destroys pending local queries without a resolver thread", "[gost-global][resolver]") {
    dcpp::Socket silentDns;
    silentDns.create(dcpp::Socket::TYPE_UDP);
    silentDns.bind("0", "127.0.0.1");
    silentDns.setBlocking(false);
    const auto server = "127.0.0.1:" + silentDns.getLocalPort();
    bool cancel = false;
    SECTION("Deadline") { }
    SECTION("Cancellation") { cancel = true; }
    std::atomic<bool> requested{false};
    std::jthread cancelling;
    if(cancel) cancelling = std::jthread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        requested = true;
    });
    auto start = std::chrono::steady_clock::now();
    CHECK_THROWS(dcpp::proxy_resolver_detail::resolve("fixture.invalid", 150,
        [&] { return requested.load(); }, server));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    CHECK(silentDns.wait(100, dcpp::Socket::WAIT_READ) == dcpp::Socket::WAIT_READ);
    std::array<char, 2048> packet{};
    sockaddr_storage source{};
    while(silentDns.read(packet.data(), packet.size(), source) > 0) { }
    CHECK(silentDns.wait(250, dcpp::Socket::WAIT_READ) == dcpp::Socket::WAIT_NONE);
}

TEST_CASE("Proxy resolver uses the local host database without requiring public DNS", "[gost-global][resolver]") {
    auto addresses = dcpp::resolveProxyEndpoint("localhost", 1000, {});
    REQUIRE_FALSE(addresses.empty());
    for(const auto& address : addresses)
        CHECK((address == "127.0.0.1" || address == "::1"));
}
