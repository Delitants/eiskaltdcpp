#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "TestContext.h"
#include "dcpp/ProxyRoute.h"
#include "dcpp/SettingsManager.h"
#include "proxy/DisposableGostServer.h"
#include <fstream>
#include "dcpp/BufferedSocket.h"
#include "dcpp/HttpConnection.h"

namespace {
using dcpp::Socket;
using SM = dcpp::SettingsManager;
void configure(dcpp::test::TestContext& tc, proxy_test::GostServer& server) {
    const auto path = tc.tmpDir / "disposable-ca.pem";
    std::ofstream(path) << server.ca.pem;
    auto& settings = *tc.ownedCtx->getSettingsManager();
    settings.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    settings.set(SM::GOST_SERVER, "127.0.0.1");
    settings.set(SM::GOST_PORT, server.port());
    settings.set(SM::GOST_USER, "fixture-user");
    settings.set(SM::GOST_PASSWORD, "fixture-password");
    settings.set(SM::GOST_CA_FILE, path.string());
    tc.ownedCtx->getProxyRoute()->reload(settings);
}
}

TEST_CASE("Global GOST routes implicit and raw DC streams with remote target DNS", "[gost-global][routing]") {
    dcpp::test::TestContext tc;
    proxy_test::GostServer server;
    configure(tc, server);
    server.start();
    Socket client;
    client.setContext(tc.ownedCtx.get());
    SECTION("Implicit dispatch") { client.proxyConnect("peer.invalid", "411", 2000); }
    SECTION("Raw connect cannot bypass") { client.connect("peer.invalid", "411"); }
    client.writeAll("ping", 4, 1000);
    char reply[4]{};
    REQUIRE(client.readAll(reply, 4, 1000) == 4);
    CHECK(std::string(reply, 4) == "ping");
    tc.ownedCtx->getSettingsManager()->set(SM::GOST_PASSWORD, "changed-fixture-password");
    tc.ownedCtx->getProxyRoute()->reload(*tc.ownedCtx->getSettingsManager());
    CHECK_THROWS(client.write("x", 1));
    CHECK_THROWS(client.read(reply, 4));
    CHECK_THROWS(client.wait(1000, Socket::WAIT_READ));
    client.disconnect();
    server.requestStop(); server.join();
    CHECK(server.hello == std::array<uint8_t, 3>{5, 1, 0x82});
    CHECK(server.target == "peer.invalid");
    CHECK(server.user == "fixture-user");
}

TEST_CASE("Global invalid routes never dial a direct destination", "[gost-global][routing]") {
    dcpp::test::TestContext tc;
    auto& settings = *tc.ownedCtx->getSettingsManager();
    SECTION("Unconfigured GOST") { settings.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST); }
    SECTION("Unknown mode") { settings.set(SM::OUTGOING_CONNECTIONS, 99); }
    tc.ownedCtx->getProxyRoute()->reload(settings);
    Socket listener;
    listener.create(); listener.bind("0", "127.0.0.1"); listener.listen();
    Socket client;
    client.setContext(tc.ownedCtx.get());
    CHECK_THROWS(client.proxyConnect("127.0.0.1", listener.getLocalPort(), 250));
    CHECK_THROWS(client.connect("127.0.0.1", listener.getLocalPort()));
    CHECK(listener.wait(50, Socket::WAIT_READ) == Socket::WAIT_NONE);
}

TEST_CASE("Switching from direct to GOST revokes established DC streams", "[gost-global][routing]") {
    dcpp::test::TestContext tc;
    Socket listener;
    listener.create(); listener.bind("0", "127.0.0.1"); listener.listen();
    Socket client;
    client.setContext(tc.ownedCtx.get());
    client.connect("127.0.0.1", listener.getLocalPort());
    REQUIRE(client.waitConnected(1000));
    REQUIRE(listener.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
    Socket accepted; accepted.accept(listener);
    tc.ownedCtx->getSettingsManager()->set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    tc.ownedCtx->getProxyRoute()->reload(*tc.ownedCtx->getSettingsManager());
    CHECK_THROWS(client.write("must not escape", 15));
    CHECK(accepted.wait(50, Socket::WAIT_READ) == Socket::WAIT_NONE);
}

TEST_CASE("Global GOST UDP is isolated and cannot be bypassed by a send flag", "[gost-global][dc-udp]") {
    dcpp::test::TestContext tc;
    proxy_test::GostServer server;
    configure(tc, server); server.start();
    Socket client;
    client.setContext(tc.ownedCtx.get());
    client.create(Socket::TYPE_UDP);
    client.bind("0", "0.0.0.0");
    CHECK(client.getLocalIp() == "127.0.0.1");
    CHECK_FALSE(client.hasGostUdpTransport());
    Socket::UdpSendInfo info;
    client.writeTo("192.0.2.7", "6000", "test", 4, false, &info);
    CHECK(client.hasGostUdpTransport());
    CHECK(info.logicalIp == "192.0.2.7");
    CHECK(info.physicalIp == "127.0.0.1");
    CHECK(info.proxied);
    REQUIRE(client.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);
    char reply[1024]{}; sockaddr_storage remote{};
    REQUIRE(client.read(reply, sizeof(reply), remote) == 4);
    CHECK(std::string(reply, 4) == "test");
    CHECK(remote.ss_family == AF_INET);
    CHECK(ntohs(reinterpret_cast<sockaddr_in&>(remote).sin_port) == 6000);
    client.writeTo("2001:db8::7", "6001", "v6", 2, false);
    REQUIRE(client.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);
    REQUIRE(client.read(reply, sizeof(reply), remote) == 2);
    CHECK(remote.ss_family == AF_INET6);
    Socket intruder;
    intruder.writeTo("127.0.0.1", client.getLocalPort(), "raw", 3);
    REQUIRE(client.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
    CHECK(client.read(reply, sizeof(reply), remote) <= 0);
    // Replies from the old association cannot survive a route revocation.
    client.writeTo("192.0.2.7", "6000", "old", 3);
    REQUIRE(client.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
    tc.ownedCtx->getProxyRoute()->stop();
    CHECK(client.read(reply, sizeof(reply), remote) <= 0);
    CHECK_THROWS(client.writeTo("192.0.2.7", "6000", "x", 1, false));
    client.disconnect(); server.requestStop(); server.join();
    CHECK(server.command == 0xf3);
}

TEST_CASE("Global GOST UDP rejects invalid routes without sending directly", "[gost-global][dc-udp]") {
    dcpp::test::TestContext tc;
    auto& settings = *tc.ownedCtx->getSettingsManager();
    settings.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    tc.ownedCtx->getProxyRoute()->reload(settings);
    Socket receiver;
    receiver.create(Socket::TYPE_UDP); receiver.bind("0", "127.0.0.1");
    Socket client; client.setContext(tc.ownedCtx.get());
    CHECK_THROWS(client.writeTo("127.0.0.1", receiver.getLocalPort(), "x", 1, false));
    CHECK(receiver.wait(50, Socket::WAIT_READ) == Socket::WAIT_NONE);
}

TEST_CASE("Global GOST limits UDP consumers per context and recovers released slots", "[gost-global][dc-udp]") {
    dcpp::ProxyRoute route;
    std::vector<std::shared_ptr<void>> slots;
    for(int i = 0; i < 8; ++i) {
        slots.push_back(route.acquireUdpSlot());
        REQUIRE(slots.back());
    }
    CHECK_FALSE(route.acquireUdpSlot());
    slots.pop_back();
    CHECK(route.acquireUdpSlot());
    dcpp::ProxyRoute independent;
    CHECK(independent.acquireUdpSlot());
}

TEST_CASE("Global GOST keeps public hub-list HTTP proxy scope separate from bootstrap HTTP", "[gost-global][routing]") {
    dcpp::test::TestContext tc;
    auto& sm = *tc.ownedCtx->getSettingsManager();
    sm.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    sm.set(SM::HTTP_PROXY, "http://public-proxy.invalid:8080");
    tc.ownedCtx->getProxyRoute()->reload(sm);
    bool publicList = false;
    SECTION("Public hub list uses its explicit HTTP route") { publicList = true; }
    SECTION("DHT bootstrap stays on global route") { }
    bool called = false;
    {
        dcpp::HttpConnection request(*tc.ownedCtx, "fixture", [&](dcpp::BufferedSocket&, const std::string& host,
                const std::string& port, bool, bool proxy) {
            called = true;
            CHECK(host == (publicList ? "public-proxy.invalid" : "bootstrap.invalid"));
            CHECK(port == (publicList ? "8080" : "80"));
            CHECK(proxy == !publicList);
        });
        request.setPublicHubListProxy(publicList);
        request.downloadFile("http://bootstrap.invalid/nodes.xml");
    }
    dcpp::BufferedSocket::waitShutdown();
    CHECK(called);
}

TEST_CASE("Global GOST BufferedSocket consumers revoke established hub and peer traffic", "[gost-global][routing]") {
    dcpp::test::TestContext tc;
    proxy_test::GostServer server;
    configure(tc, server); server.start();
    struct Listener : dcpp::BufferedSocketListener {
        std::atomic<bool> connected{false}, line{false}, failed{false};
        void on(Connected) override { connected = true; }
        void on(Line, const std::string& value) override { line = value == "probe"; }
        void on(Failed, const std::string&) override { failed = true; }
    } listener;
    auto cleanup = [](dcpp::BufferedSocket* socket) {
        dcpp::BufferedSocket::putSocket(socket);
        dcpp::BufferedSocket::waitShutdown();
    };
    std::unique_ptr<dcpp::BufferedSocket, decltype(cleanup)> socket(
        dcpp::BufferedSocket::getSocket('\n', *tc.ownedCtx), cleanup);
    socket->addListener(&listener);
    auto waitFor = [](const std::atomic<bool>& value) {
        for(int n = 0; n < 200 && !value; ++n) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return value.load();
    };
    // Peer callers historically pass proxy=false; neither path may bypass GOST.
    bool proxy = false;
    SECTION("Hub") { proxy = true; }
    SECTION("Peer") { proxy = false; }
    socket->connect("consumer.invalid", "411", false, false, proxy, Socket::PROTO_ADC);
    REQUIRE(waitFor(listener.connected));
    socket->write("probe\n");
    REQUIRE(waitFor(listener.line));
    tc.ownedCtx->getProxyRoute()->stop();
    socket->write("revoked\n");
    CHECK(waitFor(listener.failed));
    socket.reset(); server.requestStop(); server.join();
    CHECK(server.target == "consumer.invalid");
}

TEST_CASE("Global GOST UDP associations in one context do not share replies or lifetime", "[gost-global][dc-udp]") {
    dcpp::test::TestContext tc;
    proxy_test::GostServer first;
    proxy_test::GostServer second({}, "IP:127.0.0.1", false, &first.ca);
    Socket acceptor; acceptor.create(); acceptor.bind("0", "127.0.0.1"); acceptor.listen();
    configure(tc, first);
    tc.ownedCtx->getSettingsManager()->set(SM::GOST_PORT, std::stoi(acceptor.getLocalPort()));
    tc.ownedCtx->getProxyRoute()->reload(*tc.ownedCtx->getSettingsManager());
    Socket search, dht;
    search.setContext(tc.ownedCtx.get()); dht.setContext(tc.ownedCtx.get());
    first.start(&acceptor);
    search.writeTo("192.0.2.1", "6000", "search", 6);
    second.start(&acceptor);
    dht.writeTo("2001:db8::1", "6001", "dht", 3);
    char buffer[128]; sockaddr_storage source{};
    REQUIRE(search.wait(1500, Socket::WAIT_READ) == Socket::WAIT_READ);
    REQUIRE(search.read(buffer, sizeof(buffer), source) == 6);
    CHECK(std::string(buffer, 6) == "search");
    REQUIRE(dht.wait(1500, Socket::WAIT_READ) == Socket::WAIT_READ);
    REQUIRE(dht.read(buffer, sizeof(buffer), source) == 3);
    CHECK(std::string(buffer, 3) == "dht");
    search.disconnect();
    dht.writeTo("2001:db8::1", "6001", "alive", 5);
    REQUIRE(dht.wait(1500, Socket::WAIT_READ) == Socket::WAIT_READ);
    REQUIRE(dht.read(buffer, sizeof(buffer), source) == 5);
    CHECK(std::string(buffer, 5) == "alive");
    dht.disconnect(); first.requestStop(); second.requestStop(); first.join(); second.join();
}

TEST_CASE("Global GOST UDP recovery backs off and never replays failed datagrams", "[gost-global][dc-udp]") {
    dcpp::test::TestContext tc;
    proxy_test::GostOptions closed; closed.closeAfterReply = true;
    proxy_test::GostServer failed(closed);
    proxy_test::GostServer recovered({}, "IP:127.0.0.1", false, &failed.ca);
    Socket acceptor; acceptor.create(); acceptor.bind("0", "127.0.0.1"); acceptor.listen();
    configure(tc, failed);
    tc.ownedCtx->getSettingsManager()->set(SM::GOST_PORT, std::stoi(acceptor.getLocalPort()));
    tc.ownedCtx->getProxyRoute()->reload(*tc.ownedCtx->getSettingsManager());
    failed.start(&acceptor);
    Socket client; client.setContext(tc.ownedCtx.get());
    try { client.writeTo("192.0.2.7", "6000", "discard", 7); } catch(const dcpp::SocketException&) { }
    failed.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto before = std::chrono::steady_clock::now();
    CHECK_THROWS(client.writeTo("192.0.2.7", "6000", "discard", 7));
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(500));
    CHECK(acceptor.wait(25, Socket::WAIT_READ) == Socket::WAIT_NONE);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    recovered.start(&acceptor);
    client.writeTo("192.0.2.7", "6000", "fresh", 5);
    REQUIRE(client.wait(1500, Socket::WAIT_READ) == Socket::WAIT_READ);
    char reply[128]; sockaddr_storage source{};
    REQUIRE(client.read(reply, sizeof(reply), source) == 5);
    CHECK(std::string(reply, 5) == "fresh");
    CHECK(client.wait(50, Socket::WAIT_READ) == Socket::WAIT_NONE);
    client.disconnect(); recovered.requestStop(); recovered.join();
}

TEST_CASE("Leaving GOST restores the requested UDP binding and drops queued replies", "[gost-global][review-fixes]") {
    dcpp::test::TestContext tc;
    proxy_test::GostServer server;
    configure(tc, server); server.start();
    Socket client; client.setContext(tc.ownedCtx.get());
    client.create(Socket::TYPE_UDP);
    client.bind("0", "0.0.0.0");
    client.writeTo("192.0.2.1", "6000", "old", 3);
    REQUIRE(client.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
    auto& settings = *tc.ownedCtx->getSettingsManager();
    settings.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_DIRECT);
    tc.ownedCtx->getProxyRoute()->reload(settings);
    Socket receiver; receiver.create(Socket::TYPE_UDP); receiver.bind("0", "127.0.0.1");
    client.writeTo("127.0.0.1", receiver.getLocalPort(), "new", 3);
    CHECK(client.getLocalIp() == "0.0.0.0");
    CHECK(client.wait(50, Socket::WAIT_READ) == Socket::WAIT_NONE);
    REQUIRE(receiver.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
    char bytes[16]; sockaddr_storage source{};
    REQUIRE(receiver.read(bytes, sizeof(bytes), source) == 3);
    CHECK(std::string(bytes, 3) == "new");
    client.disconnect(); server.requestStop(); server.join();
}

TEST_CASE("An idle dead or revoked GOST worker releases its admission slot", "[gost-global][review-fixes]") {
    dcpp::test::TestContext tc;
    proxy_test::GostOptions options;
    bool revoke = false;
    SECTION("Remote EOF") { options.closeAfterReply = true; }
    SECTION("Route revoked") { revoke = true; }
    proxy_test::GostServer server(options);
    configure(tc, server); server.start();
    auto* route = tc.ownedCtx->getProxyRoute();
    std::vector<std::shared_ptr<void>> occupied;
    for(int i = 0; i < 7; ++i) occupied.push_back(route->acquireUdpSlot());
    Socket client; client.setContext(tc.ownedCtx.get());
    try { client.writeTo("192.0.2.1", "6000", "test", 4); } catch(const dcpp::SocketException&) { }
    if(revoke) route->stop();
    std::shared_ptr<void> freeSlot;
    for(int i = 0; i < 100 && !freeSlot; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        freeSlot = route->acquireUdpSlot();
    }
    CHECK(freeSlot != nullptr);
    client.disconnect(); server.requestStop(); server.join();
}

#include "dcpp/ConnectivityManager.h"
#ifdef WITH_DHT
#include "dht/DHT.h"
#include "dht/UDPSocket.h"
#endif
TEST_CASE("Global GOST enables firewalled DHT without advertising a loopback port", "[gost-global][routing]") {
    CHECK(dcpp::ConnectivityManager::shouldStartDht(true, false, SM::OUTGOING_GOST, true));
    CHECK_FALSE(dcpp::ConnectivityManager::shouldStartDht(false, false, SM::OUTGOING_GOST, true));
    CHECK_FALSE(dcpp::ConnectivityManager::shouldStartDht(true, false, SM::OUTGOING_GOST, false));
#ifdef WITH_DHT
    dcpp::test::TestContext tc;
    tc.ownedCtx->getSettingsManager()->set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    dht::DHT dht(*tc.ownedCtx);
    dht::UDPSocket socket;
    socket.setDHT(dht);
    socket.listen();
    CHECK_FALSE(socket.getPort().empty());
    CHECK(socket.getAdvertisedPort().empty());
    CHECK_FALSE(socket.hasUdpProxyEndpoint());
    CHECK(dht.isFirewalled());
    socket.disconnect();
#endif
}

#include "dcpp/CryptoManager.h"
#include "dcpp/SSLSocket.h"
namespace {
class NestedTlsEcho {
    proxy_test::Identity ca = proxy_test::Identity::make();
    proxy_test::Identity identity = proxy_test::Identity::make(&ca);
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{SSL_CTX_new(TLS_server_method()), SSL_CTX_free};
    std::unique_ptr<SSL, decltype(&SSL_free)> session{nullptr, SSL_free};
public:
    NestedTlsEcho() {
        SSL_CTX_use_certificate(context.get(), identity.cert.get());
        SSL_CTX_use_PrivateKey(context.get(), identity.key.get());
        session.reset(SSL_new(context.get()));
        SSL_set_bio(session.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_accept_state(session.get());
    }
    std::vector<uint8_t> exchange(std::span<const uint8_t> input) {
        BIO_write(SSL_get_rbio(session.get()), input.data(), input.size());
        if(!SSL_is_init_finished(session.get())) SSL_do_handshake(session.get());
        if(SSL_is_init_finished(session.get())) {
            char plain[1024];
            int count;
            while((count = SSL_read(session.get(), plain, sizeof(plain))) > 0)
                SSL_write(session.get(), plain, count);
        }
        std::vector<uint8_t> result(BIO_ctrl_pending(SSL_get_wbio(session.get())));
        if(!result.empty()) BIO_read(SSL_get_wbio(session.get()), result.data(), result.size());
        return result;
    }
};
}
TEST_CASE("Global GOST preserves inner hub and peer TLS", "[gost-global][routing]") {
    dcpp::test::TestContext tc;
    auto inner = std::make_shared<NestedTlsEcho>();
    proxy_test::GostOptions options;
    options.transform = [inner](auto data) { return inner->exchange(data); };
    proxy_test::GostServer server(options);
    configure(tc, server); server.start();
    dcpp::CryptoManager crypto(*tc.ownedCtx);
    std::unique_ptr<dcpp::SSLSocket> client(crypto.getClientSocket(true, Socket::PROTO_ADC));
    client->setContext(tc.ownedCtx.get());
    client->setServerName("hub.invalid");
    client->proxyConnect("hub.invalid", "1511", 2000);
    bool connected = false;
    for(int i = 0; i < 30 && !connected; ++i) connected = client->waitConnected(50);
    REQUIRE(connected);
    REQUIRE(client->isSecure());
    REQUIRE_FALSE(client->getCipherName().empty());
    client->writeAll("secure", 6, 1000);
    char reply[6]{};
    REQUIRE(client->readAll(reply, sizeof(reply), 1000) == 6);
    CHECK(std::string(reply, 6) == "secure");
    tc.ownedCtx->getProxyRoute()->stop();
    CHECK_THROWS(client->write("x", 1));
    CHECK_THROWS(client->wait(50, Socket::WAIT_READ));
    CHECK_NOTHROW(client->disconnect()); server.requestStop(); server.join();
}

TEST_CASE("An unpublished mode edit cannot bypass the current GOST generation", "[gost-global][routing]") {
    dcpp::test::TestContext tc;
    proxy_test::GostServer server;
    configure(tc, server); server.start();
    tc.ownedCtx->getSettingsManager()->set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_DIRECT);
    // Settings are committed in a batch; the route remains active until reload.
    Socket client; client.setContext(tc.ownedCtx.get());
    client.proxyConnect("peer.invalid", "411", 1000);
    client.writeAll("test", 4, 1000);
    char reply[4]{};
    CHECK(client.readAll(reply, sizeof(reply), 1000) == 4);
    client.disconnect(); server.requestStop(); server.join();
    CHECK(server.target == "peer.invalid");
}
