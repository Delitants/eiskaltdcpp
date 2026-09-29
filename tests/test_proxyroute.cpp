#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "TestContext.h"
#include "dcpp/SettingsManager.h"
#define private public
#include "dcpp/ProxyRoute.h"
#undef private
#include "dcpp/ProxyTrust.h"
#include "proxy/DisposableGostServer.h"
#include <fstream>
#include <future>

namespace {
using SM = dcpp::SettingsManager;
void configure(SM& sm) {
    sm.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    sm.set(SM::GOST_SERVER, "proxy.test");
    sm.set(SM::GOST_PORT, 1080);
    sm.set(SM::GOST_USER, "fixture-user");
    sm.set(SM::GOST_PASSWORD, "fixture-password");
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
    REQUIRE(out.good());
}
}

TEST_CASE("Global GOST profile persists without changing alternative profiles", "[gost-global][profile]") {
    dcpp::test::TestContext tc;
    auto& sm = *dcpp::getContext()->getSettingsManager();
    CHECK(SM::OUTGOING_DIRECT == 0);
    CHECK(SM::OUTGOING_SOCKS5 == 1);
    CHECK(SM::OUTGOING_SHADOWSOCKS == 2);
    CHECK(SM::OUTGOING_GOST == 3);
    CHECK(sm.get(SM::GOST_SERVER).empty());
    CHECK(sm.get(SM::GOST_PORT) == 0);
    configure(sm);
    sm.set(SM::GOST_CA_FILE, "fixture-ca.pem");
    sm.set(SM::SOCKS_SERVER, "legacy.test");
    sm.set(SM::SOCKS_PASSWORD, "old-fixture-secret");
    sm.set(SM::SHADOWSOCKS_SERVER, "shadowsocks.test");
    auto file = tc.tmpDir / "profile.xml";
    sm.save(file.string());
    SM restored(*dcpp::getContext());
    restored.load(file.string());
    CHECK(restored.get(SM::OUTGOING_CONNECTIONS) == SM::OUTGOING_GOST);
    CHECK(restored.get(SM::GOST_SERVER) == "proxy.test");
    CHECK(restored.get(SM::GOST_PORT) == 1080);
    CHECK(restored.get(SM::GOST_USER) == "fixture-user");
    CHECK(restored.get(SM::GOST_PASSWORD) == "fixture-password");
    CHECK(restored.get(SM::GOST_CA_FILE) == "fixture-ca.pem");
    CHECK(restored.get(SM::SOCKS_SERVER) == "legacy.test");
    CHECK(restored.get(SM::SOCKS_PASSWORD) == "old-fixture-secret");
    CHECK(restored.get(SM::SHADOWSOCKS_SERVER) == "shadowsocks.test");
}

TEST_CASE("Global GOST immutable generations revoke changes but not no-op reloads", "[gost-global][profile]") {
    dcpp::test::TestContext tc;
    auto& ctx = *dcpp::getContext();
    auto& sm = *ctx.getSettingsManager();
    auto& route = *ctx.getProxyRoute();
    configure(sm);
    REQUIRE(route.reload(sm));
    auto first = route.snapshot();
    REQUIRE(first->valid);
    CHECK(first->proxy.type == dcpp::Socket::StreamProxyConfig::Gost);
    CHECK(first->proxy.verifyTls);
    CHECK(first->proxy.tls);
    CHECK(first->proxy.remoteDns);
    CHECK_FALSE(first->proxy.cancelled());
    CHECK_FALSE(route.reload(sm));
    CHECK(route.snapshot() == first);
    sm.set(SM::SOCKS_PASSWORD, "inactive-fixture-change");
    CHECK_FALSE(route.reload(sm));
    sm.set(SM::GOST_PASSWORD, "replacement-fixture-password");
    REQUIRE(route.reload(sm));
    CHECK(first->proxy.cancelled());
    CHECK(route.snapshot()->generation > first->generation);
    CHECK_FALSE(route.snapshot()->proxy.cancelled());
    auto second = route.snapshot();
    route.stop();
    CHECK(second->proxy.cancelled());
    CHECK_FALSE(route.snapshot()->valid);
}

TEST_CASE("Route revocation wakes subscribers but no-op reload leaves waits idle", "[socket-wake][gost-global]") {
    dcpp::test::TestContext tc;
    auto& sm = *dcpp::getContext()->getSettingsManager();
    dcpp::ProxyRoute route;
    configure(sm);
    REQUIRE(route.reload(sm));
    auto first = route.snapshot();
    auto wake = std::make_shared<dcpp::SocketWake>();
    auto subscription = first->notifier->subscribe(wake);
    REQUIRE_FALSE(route.reload(sm));
    CHECK_FALSE(wake->wait(0));
    sm.set(SM::GOST_PASSWORD, "replacement-fixture-password");
    REQUIRE(route.reload(sm));
    REQUIRE(wake->wait(0));
    CHECK(first->revoked->load());
    wake->consume();
    subscription = first->notifier->subscribe(wake);
    CHECK(wake->wait(0));
    wake->consume();
    subscription = route.snapshot()->notifier->subscribe(wake);
    route.stop();
    CHECK(wake->wait(0));
    wake->consume();
    subscription = route.snapshot()->notifier->subscribe(wake);
    CHECK(wake->wait(0));
}

TEST_CASE("Route stop races safely with subscription and owner destruction", "[socket-wake][gost-global]") {
    dcpp::test::TestContext tc;
    for(int i = 0; i < 50; ++i) {
        dcpp::ProxyRoute route;
        auto generation = route.snapshot();
        auto wake = std::make_shared<dcpp::SocketWake>();
        auto stopped = std::async(std::launch::async, [&] { route.stop(); });
        auto subscription = generation->notifier->subscribe(wake);
        stopped.get();
        REQUIRE(wake->wait(1000));
        CHECK(generation->revoked->load());
        std::weak_ptr<dcpp::SocketWake> owner = wake;
        wake.reset();
        subscription.reset();
        CHECK(owner.expired());
    }
}

TEST_CASE("Route stop revokes and wakes even if replacement allocation fails", "[socket-wake][gost-global][review-fixes]") {
    dcpp::test::TestContext tc;
    dcpp::ProxyRoute route;
    auto& settings = *tc.ownedCtx->getSettingsManager();
    configure(settings);
    REQUIRE(route.reload(settings));
    REQUIRE(route.supportsUdp());
    const auto generation = route.snapshot();
    auto wake = std::make_shared<dcpp::SocketWake>();
    auto subscription = generation->notifier->subscribe(wake);
    const auto originalFactory = route.makeStoppedSnapshot;
    route.makeStoppedSnapshot = []() -> std::shared_ptr<dcpp::ProxyRouteSnapshot> { throw std::bad_alloc(); };
    bool threw = false;
    try { route.stop(); } catch(...) { threw = true; }
    route.makeStoppedSnapshot = originalFactory;
    CHECK_FALSE(threw);
    CHECK(generation->revoked->load());
    CHECK(wake->wait(0));
    CHECK_FALSE(route.supportsUdp());
    REQUIRE(route.reload(settings));
    CHECK_FALSE(route.snapshot()->revoked->load());
    CHECK(route.supportsUdp());
}

TEST_CASE("Legacy XML without global GOST settings keeps empty defaults", "[gost-global][profile]") {
    dcpp::test::TestContext tc;
    auto& ctx = *dcpp::getContext();
    auto path = tc.tmpDir / "legacy.xml";
    write(path, "<DCPlusPlus><Settings><OutgoingConnections type=\"int\">1</OutgoingConnections>"
        "<SocksServer type=\"string\">legacy.test</SocksServer></Settings></DCPlusPlus>");
    SM restored(ctx);
    restored.load(path.string());
    CHECK(restored.get(SM::OUTGOING_CONNECTIONS) == SM::OUTGOING_SOCKS5);
    CHECK(restored.get(SM::SOCKS_SERVER) == "legacy.test");
    CHECK(restored.get(SM::GOST_SERVER).empty());
    CHECK(restored.get(SM::GOST_USER).empty());
    CHECK(restored.get(SM::GOST_PASSWORD).empty());
    CHECK(restored.get(SM::GOST_CA_FILE).empty());
    CHECK(restored.get(SM::GOST_PORT) == 0);
}

TEST_CASE("Core settings reload refreshes its route and shutdown revokes it", "[gost-global][profile]") {
    dcpp::test::TestContext tc;
    auto& ctx = *dcpp::getContext();
    auto& sm = *ctx.getSettingsManager();
    configure(sm);
    auto path = tc.tmpDir / "route.xml";
    sm.save(path.string());
    sm.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_DIRECT);
    ctx.getProxyRoute()->reload(sm);
    sm.load(path.string());
    auto route = ctx.getProxyRoute()->snapshot();
    REQUIRE(route->valid);
    CHECK(route->mode == SM::OUTGOING_GOST);
    CHECK_FALSE(route->proxy.cancelled());
    ctx.shutdown();
    CHECK(route->proxy.cancelled());
}

TEST_CASE("Global GOST rejects missing credentials and invalid modes without secret disclosure", "[gost-global][profile]") {
    dcpp::test::TestContext tc;
    auto& sm = *dcpp::getContext()->getSettingsManager();
    dcpp::ProxyRoute route;
    configure(sm);
    route.reload(sm);
    auto old = route.snapshot();
    SECTION("empty password") { sm.set(SM::GOST_PASSWORD, ""); }
    SECTION("empty user") { sm.set(SM::GOST_USER, ""); }
    SECTION("unknown mode") { sm.set(SM::OUTGOING_CONNECTIONS, 999); }
    SECTION("bad port") { sm.set(SM::GOST_PORT, 65536); }
    SECTION("bad host") { sm.set(SM::GOST_SERVER, std::string("x\0y", 3)); }
    REQUIRE(route.reload(sm));
    CHECK(old->proxy.cancelled());
    CHECK_FALSE(route.snapshot()->valid);
    CHECK(route.snapshot()->proxy.cancelled());
    CHECK_FALSE(route.snapshot()->error.empty());
    CHECK(route.snapshot()->error.find("fixture-password") == std::string::npos);
    CHECK_FALSE(route.reload(sm));
}

TEST_CASE("Global GOST authentication lengths count bytes", "[gost-global][trust]") {
    dcpp::test::TestContext tc;
    auto& sm = *dcpp::getContext()->getSettingsManager();
    configure(sm);
    dcpp::ProxyRoute route;
    std::string utf8;
    for(int i = 0; i < 127; ++i) utf8 += "\xc3\xa9";
    sm.set(SM::GOST_USER, utf8 + "x");
    sm.set(SM::GOST_PASSWORD, utf8 + "x");
    route.reload(sm);
    REQUIRE(route.snapshot()->valid);
    sm.set(SM::GOST_PASSWORD, utf8 + "xy");
    route.reload(sm);
    CHECK_FALSE(route.snapshot()->valid);
    sm.set(SM::GOST_PASSWORD, "valid-fixture-password");
    sm.set(SM::GOST_USER, utf8 + "xy");
    route.reload(sm);
    CHECK_FALSE(route.snapshot()->valid);
}

TEST_CASE("Core proxy trust validates bounded CA-only PEM without Qt", "[gost-global][trust]") {
    dcpp::test::TestContext tc;
    auto ca = proxy_test::Identity::make();
    auto leaf = proxy_test::Identity::make(&ca);
    auto path = tc.tmpDir / "ca.pem";
    CHECK(dcpp::loadProxyCaPem("").empty());
    write(path, ca.pem);
    CHECK(dcpp::loadProxyCaPem(path.string()) == ca.pem);
    write(path, ca.pem + "garbage");
    CHECK_THROWS(dcpp::loadProxyCaPem(path.string()));
    write(path, leaf.pem);
    CHECK_THROWS(dcpp::loadProxyCaPem(path.string()));
    write(path, std::string(1024 * 1024 + 1, ' '));
    CHECK_THROWS(dcpp::loadProxyCaPem(path.string()));
    write(path, "");
    CHECK_THROWS(dcpp::loadProxyCaPem(path.string()));
    CHECK_THROWS(dcpp::loadProxyCaPem(tc.tmpDir.string()));
    CHECK_THROWS(dcpp::loadProxyCaPem((tc.tmpDir / "missing").string()));
}

TEST_CASE("Replacing or invalidating the same CA path revokes its previous route", "[gost-global][trust]") {
    dcpp::test::TestContext tc;
    auto& sm = *dcpp::getContext()->getSettingsManager();
    configure(sm);
    auto path = tc.tmpDir / "ca.pem";
    write(path, proxy_test::Identity::make().pem);
    sm.set(SM::GOST_CA_FILE, path.string());
    dcpp::ProxyRoute route;
    route.reload(sm);
    auto first = route.snapshot();
    REQUIRE(first->valid);
    write(path, proxy_test::Identity::make().pem);
    REQUIRE(route.reload(sm));
    auto second = route.snapshot();
    CHECK(first->proxy.cancelled());
    CHECK(second->proxy.caPem != first->proxy.caPem);
    write(path, "invalid");
    REQUIRE(route.reload(sm));
    CHECK(second->proxy.cancelled());
    CHECK_FALSE(route.snapshot()->valid);
    CHECK(route.snapshot()->proxy.caPem.empty());
}
