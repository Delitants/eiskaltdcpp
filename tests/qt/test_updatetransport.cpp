#include "dcpp/stdinc.h"
#include "dcpp/HttpRequestPolicy.h"
#include "dcpp/HttpConnection.h"
#include "dcpp/SettingsManager.h"
#include "tests/proxy/NestedTlsOrigin.h"
#include "updates/UpdateTransport.h"
#include "tests/TestContext.h"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QUrlQuery>
#include <atomic>
#include <thread>
#include <future>
#include "dcpp/ScopedFunctor.h"

namespace updates {
struct UpdateTransportTestAccess {
    static std::unique_ptr<UpdateTransport> create(dcpp::DCContext& context, transport_detail::Backend backend) {
        return std::unique_ptr<UpdateTransport>(new UpdateTransport(context, std::move(backend), nullptr));
    }
};
}
namespace {
using namespace updates;
const QUrl feed("https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/arm64.xml");
bool pump(const std::function<bool()>& done, int timeout = 3000) {
    QElapsedTimer elapsed; elapsed.start();
    while(!done() && elapsed.elapsed() < timeout) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return done();
}
struct Result {
    int successes = 0, failures = 0, progresses = 0;
    quint64 id = 0, bytes = 0;
    QString path, error;
    explicit Result(UpdateTransport& transport) {
        QObject::connect(&transport, &UpdateTransport::finished, &transport, [this](auto value, auto file) { id = value; path = file; ++successes; });
        QObject::connect(&transport, &UpdateTransport::failed, &transport, [this](auto value, auto message) { id = value; error = message; ++failures; });
        QObject::connect(&transport, &UpdateTransport::progress, &transport, [this](auto, auto received, auto) { bytes = received; ++progresses; });
    }
    bool done() const { return successes || failures; }
};
auto directRoute() { return std::make_shared<dcpp::ProxyRouteSnapshot>(); }
template<class T> bool fence(T& transport) {
    if constexpr(requires { transport.stopWorkersForQuit(); }) return transport.stopWorkersForQuit();
    else return false;
}
}

TEST_CASE("Update worker commits private bytes without blocking the GUI", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    REQUIRE(staging.isValid());
    const auto destination = staging.filePath("feed.xml");
    const auto guiThread = std::this_thread::get_id();
    std::atomic_bool background{false};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto&, const auto&, const auto& sink) {
        background = std::this_thread::get_id() != guiThread;
        const uint8_t bytes[]{'a', 0, 'b'};
        sink(bytes, sizeof(bytes));
    });
    Result result(*transport);
    const auto id = transport->fetch(feed, ResourceKind::Feed, destination, directRoute());
    CHECK_FALSE(result.done());
    REQUIRE(pump([&] { return result.done(); }));
    CHECK(background.load());
    CHECK(result.successes == 1);
    CHECK(result.failures == 0);
    CHECK(result.id == id);
    CHECK(result.path == destination);
    QFile file(destination);
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == QByteArray("a\0b", 3));
    CHECK_FALSE(file.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther));
    CHECK(result.bytes == 3);
}

TEST_CASE("Update worker cleans partial files and redacts backend failures", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [](const auto&, const auto&, const auto& sink) {
        const uint8_t bytes[]{'x'};
        sink(bytes, 1);
        throw std::runtime_error("secret-password proxy.internal signed-query");
    });
    Result result(*transport);
    transport->fetch(feed, ResourceKind::Feed, staging.filePath("feed.xml"), directRoute());
    REQUIRE(pump([&] { return result.done(); }));
    CHECK(result.successes == 0);
    CHECK(result.failures == 1);
    CHECK_FALSE(result.error.contains("secret-password"));
    CHECK_FALSE(result.error.contains("proxy.internal"));
    CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Update worker never overwrites an existing destination", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    const auto destination = staging.filePath("keep");
    QFile existing(destination); REQUIRE(existing.open(QIODevice::WriteOnly)); existing.write("keep"); existing.close();
    std::atomic_bool called{false};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto&, const auto&, const auto&) { called = true; });
    Result result(*transport);
    transport->fetch(feed, ResourceKind::Feed, destination, directRoute());
    REQUIRE(pump([&] { return result.done(); }));
    CHECK(result.failures == 1);
    CHECK_FALSE(called.load());
    REQUIRE(existing.open(QIODevice::ReadOnly)); CHECK(existing.readAll() == "keep");
}

TEST_CASE("Update target preparation validates namespaces and pins public direct DNS", "[updates][update-transport][update-worker]") {
    auto route = directRoute();
    int lookups = 0;
    auto policy = transport_detail::policy(ResourceKind::Feed, route, [&](const auto&, auto, const auto&) {
        ++lookups; return std::vector<std::string>{"140.82.114.3"};
    });
    REQUIRE(bool(policy.prepare));
    const auto target = policy.prepare(feed.toString().toStdString(), "", *route, 1000, [] { return false; });
    CHECK(target.address == "140.82.114.3");
    CHECK(target.host == "raw.githubusercontent.com");
    CHECK(lookups == 1);
    CHECK_THROWS(policy.prepare("http://localhost/private", "", *route, 1000, [] { return false; }));
    CHECK(lookups == 1);
    route->mode = 3;
    CHECK_THROWS(policy.prepare(feed.toString().toStdString(), "", *route, 1000, [] { return false; }));
    CHECK(lookups == 1);
    CHECK(policy.limits.bodyBytes == 1024 * 1024);
    CHECK(policy.limits.headerBytes == 65536);
    CHECK(policy.redirects == 5);
    CHECK(policy.connectMs == 15000);
    CHECK(policy.idleMs == 30000);
    CHECK(policy.totalMs == 1800000);
    CHECK(transport_detail::policy(ResourceKind::Archive, directRoute()).limits.bodyBytes == 512ULL * 1024 * 1024);
}

TEST_CASE("Update worker resolves proxied origins only through its same-route DoH backend", "[updates][update-dns-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    auto route = directRoute();
    route->mode = dcpp::SettingsManager::OUTGOING_GOST;
    route->proxy.type = dcpp::Socket::StreamProxyConfig::Gost;
    std::atomic<unsigned> queries{0}, origins{0};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto& policy, const auto& url, const auto& sink) {
        if(policy.route != route) throw std::runtime_error("Wrong route");
        const auto target = policy.prepare(url, "", *route, 5000, policy.cancelled);
        const QUrl request(QString::fromStdString(url));
        if(request.host() == "cloudflare-dns.com") {
            if(target.address != "1.1.1.1" || policy.redirects != 0) throw std::runtime_error("Wrong DoH pin");
            ++queries;
            auto body = QByteArray::fromBase64(QUrlQuery(request).queryItemValue("dns").toLatin1(), QByteArray::Base64UrlEncoding);
            const auto type = static_cast<unsigned char>(body[body.size() - 3]);
            body[2] = char(0x81); body[3] = char(0x80);
            if(type == 1) {
                body[7] = 1;
                body.append(QByteArray::fromHex("c00c000100010000003c00048c527203"));
            }
            sink(reinterpret_cast<const uint8_t*>(body.constData()), body.size());
        } else {
            if(target.address != "140.82.114.3" || target.host != "raw.githubusercontent.com")
                throw std::runtime_error("Wrong update pin");
            ++origins;
            const uint8_t bytes[]{'f', 'e', 'e', 'd'}; sink(bytes, sizeof(bytes));
        }
    });
    Result result(*transport);
    const auto destination = staging.filePath("feed.xml");
    transport->fetch(feed, ResourceKind::Feed, destination, route);
    REQUIRE(pump([&] { return result.done(); }));
    CHECK(result.successes == 1);
    CHECK(result.failures == 0);
    CHECK(queries.load() == 2);
    CHECK(origins.load() == 1);
    QFile file(destination); REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == "feed");
}

TEST_CASE("Update worker cancellation and destruction remove partial files", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    auto route = directRoute();
    std::atomic_bool started{false};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto& policy, const auto&, const auto& sink) {
        const uint8_t byte = 'x'; sink(&byte, 1); started = true;
        while(!policy.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    Result result(*transport);
    const auto id = transport->fetch(feed, ResourceKind::Feed, staging.filePath("feed"), route);
    REQUIRE(pump([&] { return started.load(); }));
    bool destroyed = false;
    SECTION("Cancel current operation") { transport->cancel(id); }
    SECTION("Route changed") { route->revoked->store(true); route->notifier->notify(); }
    SECTION("Owner destroyed") { transport.reset(); destroyed = true; }
    if(!destroyed) {
        REQUIRE(pump([&] { return result.done(); }));
        CHECK(result.failures == 1);
    } else {
        QCoreApplication::processEvents();
        CHECK_FALSE(result.done());
    }
    CHECK(result.successes == 0);
    CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Update worker cancellation before completion delivery retracts staged output", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    const auto destination = staging.filePath("feed");
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [](const auto&, const auto&, const auto& sink) {
        const uint8_t byte = 'x'; sink(&byte, 1);
    });
    Result result(*transport);
    const auto id = transport->fetch(feed, ResourceKind::Feed, destination, directRoute());
    SECTION("While completion is queued") {
        QElapsedTimer clock; clock.start();
        while(!QFileInfo::exists(destination) && clock.elapsed() < 3000) QThread::msleep(1);
        REQUIRE(QFileInfo::exists(destination));
        transport->cancel(id);
    }
    SECTION("From the final progress notification") {
        QObject::connect(transport.get(), &UpdateTransport::progress, transport.get(), [&](auto value, auto, auto total) {
            if(total) transport->cancel(value);
        });
    }
    REQUIRE(pump([&] { return result.done(); }));
    CHECK(result.successes == 0);
    CHECK(result.failures == 1);
    CHECK_FALSE(QFileInfo::exists(destination));
}

TEST_CASE("Update worker enforces feed byte limit and bounds queued progress", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    bool oversized = false;
    SECTION("Exact limit") {}
    SECTION("Limit plus one") { oversized = true; }
    std::atomic_bool sent{false};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto& policy, const auto&, const auto& sink) {
        std::array<uint8_t, 8192> bytes{};
        for(uint64_t i = 0; i < policy.limits.bodyBytes; i += bytes.size()) sink(bytes.data(), bytes.size());
        sent = true;
        if(oversized) sink(bytes.data(), 1);
    });
    Result result(*transport);
    const auto destination = staging.filePath("feed");
    transport->fetch(feed, ResourceKind::Feed, destination, directRoute());
    QElapsedTimer clock; clock.start();
    while(!sent.load() && clock.elapsed() < 3000) QThread::msleep(1);
    REQUIRE(sent.load());
    REQUIRE(pump([&] { return result.done(); }));
    CHECK(result.successes == (oversized ? 0 : 1));
    CHECK(result.failures == (oversized ? 1 : 0));
    CHECK(result.progresses <= 3);
    if(!oversized) CHECK(QFileInfo(destination).size() == 1024 * 1024);
    else CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Update worker rejects shared staging directories before its backend", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    REQUIRE(QFile::setPermissions(staging.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther));
    std::atomic_bool called{false};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto&, const auto&, const auto&) { called = true; });
    Result result(*transport);
    transport->fetch(feed, ResourceKind::Feed, staging.filePath("feed"), directRoute());
    REQUIRE(pump([&] { return result.done(); }));
    CHECK(result.failures == 1);
    CHECK_FALSE(called.load());
}

TEST_CASE("Update DNS rejects private answers, mixed answers and revoked routes", "[updates][update-transport][update-worker]") {
    auto route = directRoute();
    std::vector<std::string> addresses;
    bool revoke = false;
    SECTION("Private only") { addresses = {"127.0.0.1"}; }
    SECTION("Public and private") { addresses = {"140.82.114.3", "10.1.2.3"}; }
    SECTION("Empty result") {}
    SECTION("Revoked during DNS") { addresses = {"140.82.114.3"}; revoke = true; }
    auto policy = transport_detail::policy(ResourceKind::Feed, route, [addresses, route, revoke](const auto&, auto, const auto&) {
        if(revoke) route->revoked->store(true);
        return addresses;
    });
    CHECK_THROWS(policy.prepare(feed.toString().toStdString(), "", *route, 1000, [] { return false; }));
}

TEST_CASE("Update worker destruction from final progress cannot deliver stale completion", "[updates][update-transport][update-worker]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [](const auto&, const auto&, const auto& sink) {
        const uint8_t byte = 'x'; sink(&byte, 1);
    });
    Result result(*transport);
    QObject::connect(transport.get(), &UpdateTransport::progress, transport.get(), [&](auto, auto, auto total) {
        if(total) transport.reset();
    });
    transport->fetch(feed, ResourceKind::Feed, staging.filePath("feed"), directRoute());
    REQUIRE(pump([&] { return !transport; }));
    QCoreApplication::processEvents();
    CHECK_FALSE(result.done());
    CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Update worker integrates strict HTTPS over authenticated local GOST", "[updates][update-transport][update-worker-network]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    bool badTrust = false, truncated = false, badAuth = false;
    SECTION("Exact downloaded bytes are staged") {}
    SECTION("Origin certificate fails") { badTrust = true; }
    SECTION("Incomplete body is discarded") { truncated = true; }
    SECTION("Proxy authentication fails") { badAuth = true; }
    const std::string body("a\0b", 3);
    auto origin = std::make_shared<proxy_test::NestedTlsEcho>(
        "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(truncated ? 4 : 3) + "\r\n\r\n" + body, true);
    proxy_test::GostOptions options;
    options.authStatus = badAuth ? 1 : 0;
    options.transform = [origin](auto input) { return origin->exchange(input); };
    proxy_test::GostServer server(options);
    auto route = directRoute();
    route->mode = 3;
    auto& proxy = route->proxy;
    proxy.type = dcpp::Socket::StreamProxyConfig::Gost;
    proxy.host = proxy.connectHost = "127.0.0.1";
    proxy.port = server.port();
    proxy.user = "test-user"; proxy.password = "test-password";
    proxy.caPem = server.ca.pem;
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto& original, const auto& url, const auto& sink) {
        auto policy = original;
        // Only this fixture replaces resolution/trust; routed DNS has separate
        // tests and production pinning is not weakened by these test roots.
        policy.rootsPem = badTrust ? server.ca.pem : origin->roots();
        policy.prepare = [](const auto& next, const auto&, const auto&, auto, const auto&) {
            return dcpp::HttpRequestPolicy::Target{next, "proxy.test", "443", "/feed", "8.8.8.8"};
        };
        struct Receiver : dcpp::HttpConnectionListener {
            const transport_detail::Sink& sink;
            bool complete = false;
            explicit Receiver(const transport_detail::Sink& value) : sink(value) {}
            void on(Data, dcpp::HttpConnection*, const uint8_t* bytes, size_t count) override { sink(bytes, count); }
            void on(Complete, dcpp::HttpConnection*, const std::string&) override { complete = true; }
        } receiver(sink);
        dcpp::HttpConnection connection(*context.ownedCtx, policy, "test-updater");
        connection.addListener(&receiver);
        connection.downloadFile(url);
        if(!receiver.complete) throw std::runtime_error("fixture HTTPS failed");
    });
    Result result(*transport);
    server.start();
    const auto destination = staging.filePath("feed");
    transport->fetch(feed, ResourceKind::Feed, destination, route);
    REQUIRE(pump([&] { return result.done(); }, 5000));
    server.requestStop(); server.join();
    if(badTrust || truncated || badAuth) {
        CHECK(result.successes == 0); CHECK(result.failures == 1);
        CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    } else {
        REQUIRE(result.successes == 1);
        QFile file(destination); REQUIRE(file.open(QIODevice::ReadOnly));
        CHECK(file.readAll() == QByteArray("a\0b", 3));
        CHECK(server.user == "test-user");
        CHECK(server.target == "8.8.8.8");
        CHECK(origin->request.find("Authorization") == std::string::npos);
        CHECK(origin->request.find("test-password") == std::string::npos);
    }
}

TEST_CASE("Update worker physically routes DNS feed and archive bytes through one authenticated GOST snapshot", "[updates][update-dns-worker][update-whole-route]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    dcpp::Socket listener;
    listener.create(dcpp::Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1"); listener.listen();
    const auto proxyCa = proxy_test::Identity::make(), originCa = proxy_test::Identity::make();
    auto route = directRoute();
    route->mode = dcpp::SettingsManager::OUTGOING_GOST;
    auto& proxy = route->proxy;
    proxy.type = dcpp::Socket::StreamProxyConfig::Gost;
    proxy.host = proxy.connectHost = "127.0.0.1";
    proxy.port = std::stoi(listener.getLocalPort()); proxy.caPem = proxyCa.pem;
    proxy.user = "disposable-user"; proxy.password = "disposable-password";
    bool archive = false, redirect = false, fallback = false, privateDns = false, badRedirect = false, badOriginTrust = false;
    SECTION("Feed bytes") {}
    SECTION("Archive bytes") { archive = true; }
    SECTION("Archive redirect resolves and pins each hop") { archive = true; redirect = true; }
    SECTION("Feed provider outage uses same route") { fallback = true; }
    SECTION("Archive provider outage uses same route") { archive = true; redirect = true; fallback = true; }
    SECTION("Private DNS cannot reach update origin") { privateDns = true; }
    SECTION("Unapproved archive redirect is terminal") { archive = true; redirect = true; badRedirect = true; }
    SECTION("Origin trust failure after valid DNS is terminal") { badOriginTrust = true; }
    unsigned queries = 0, origins = 0;
    std::vector<std::string> requests;
    const QByteArray payload("fixture\0data", 12);
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto& original, const auto& url, const auto& sink) {
        if(original.route != route) throw std::runtime_error("Snapshot changed");
        struct Relay {
            std::shared_ptr<proxy_test::NestedTlsEcho> origin;
            std::unique_ptr<proxy_test::GostServer> server;
            std::string address;
        };
        std::vector<Relay> relays;
        auto policy = original;
        policy.rootsPem = originCa.pem;
        const auto prepare = original.prepare;
        // Start the fixture relay only after production URL and DNS admission.
        // Nested DNS uses the same listener, without racing an origin accept.
        policy.prepare = [&](const auto& next, const auto& previous, const auto& snapshot, auto timeout, const auto& stop) {
            auto target = prepare(next, previous, snapshot, timeout, stop);
            const QUrl request(QString::fromStdString(next));
            QByteArray body;
            std::string http;
            if(original.dnsMessage) {
                ++queries;
                if(fallback && request.host() == "cloudflare-dns.com") {
                    http = "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n";
                } else {
                    body = QByteArray::fromBase64(QUrlQuery(request).queryItemValue("dns").toLatin1(), QByteArray::Base64UrlEncoding);
                    const auto type = static_cast<uint8_t>(body[body.size() - 3]);
                    body[2] = char(0x81); body[3] = char(0x80);
                    if(type == 1) {
                        body[7] = 1;
                        body.append(QByteArray::fromHex(privateDns ? "c00c000100010000003c00047f000001" : "c00c000100010000003c00048c527203"));
                    }
                }
            } else {
                ++origins;
                if(redirect && request.host() == "github.com") {
                    http = std::string("HTTP/1.1 302 Redirect\r\nLocation: https://") +
                        (badRedirect ? "unapproved.test" : "release-assets.githubusercontent.com") +
                        "/fixture.zip\r\nContent-Length: 0\r\n\r\n";
                } else body = payload;
            }
            if(http.empty()) http = std::string("HTTP/1.1 200 OK\r\n") +
                (original.dnsMessage ? "Content-Type: application/dns-message\r\n" : "") +
                "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body.toStdString();
            auto origin = std::make_shared<proxy_test::NestedTlsEcho>(http, false,
                badOriginTrust && !original.dnsMessage ? &proxyCa : &originCa,
                "DNS:cloudflare-dns.com,DNS:dns.google,DNS:raw.githubusercontent.com,DNS:github.com,DNS:release-assets.githubusercontent.com");
            proxy_test::GostOptions options;
            options.transform = [origin](auto input) { return origin->exchange(input); };
            auto server = std::make_unique<proxy_test::GostServer>(options, "IP:127.0.0.1", false, &proxyCa);
            server->start(&listener);
            relays.push_back({origin, std::move(server), target.address});
            return target;
        };
        std::exception_ptr error;
        try { transport_detail::request(*context.ownedCtx, policy, url, sink); }
        catch(...) { error = std::current_exception(); }
        for(auto& relay : relays) {
            relay.server->requestStop(); relay.server->join();
            if(relay.server->user != "disposable-user" || relay.server->target != relay.address || relay.server->targetPort != 443)
                throw std::runtime_error("Wrong proxy envelope");
            requests.push_back(relay.origin->request);
        }
        if(error) std::rethrow_exception(error);
    });
    Result result(*transport);
    const auto destination = staging.filePath("resource");
    transport->fetch(archive ? QUrl("https://github.com/Delitants/eiskaltdcpp/releases/download/v3.0.0-pre.1/fixture.zip") : feed,
        archive ? ResourceKind::Archive : ResourceKind::Feed, destination, route);
    REQUIRE(pump([&] { return result.done(); }, 8000));
    if(privateDns || badRedirect || badOriginTrust) {
        CHECK(result.successes == 0); CHECK(result.failures == 1);
        CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
        CHECK(origins == (privateDns ? 0 : 1)); CHECK(queries == (privateDns ? 1 : 2));
    } else {
        REQUIRE(result.successes == 1); CHECK(result.failures == 0);
        QFile file(destination); REQUIRE(file.open(QIODevice::ReadOnly)); CHECK(file.readAll() == payload);
        CHECK(origins == (redirect ? 2 : 1)); CHECK(queries == origins * (fallback ? 3 : 2));
    }
    for(const auto& request : requests) {
        CHECK(request.find("Authorization") == std::string::npos);
        CHECK(request.find("disposable-password") == std::string::npos);
    }
}

TEST_CASE("Updater quit fence physically joins an admitted worker without pumping callbacks", "[updates][updater-worker-fence]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    std::atomic_bool entered{false}, cancelled{false}, release{false}, tailDone{false};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto& policy, const auto&, const auto& sink) {
        const uint8_t byte = 'x'; sink(&byte, 1); entered = true;
        while(!release && !policy.cancelled()) QThread::msleep(1);
        cancelled = policy.cancelled();
        while(!release) QThread::msleep(1);
        tailDone = true;
    });
    auto releaseOnExit = dcpp::makeScopedFunctor([&] { release = true; });
    Result result(*transport);
    transport->fetch(feed, ResourceKind::Feed, staging.filePath("partial"), directRoute());
    QElapsedTimer wait; wait.start();
    while(!entered && wait.elapsed() < 3000) QThread::msleep(1);
    REQUIRE(entered.load());
    auto unblock = std::async(std::launch::async, [&] {
        QElapsedTimer deadline; deadline.start();
        while(!cancelled && deadline.elapsed() < 150) QThread::msleep(1);
        QThread::msleep(30); release = true;
    });
    const bool drained = fence(*transport);
    CHECK(drained); CHECK(tailDone.load()); CHECK(cancelled.load());
    unblock.get();
    CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    QCoreApplication::processEvents();
    CHECK_FALSE(result.done()); CHECK(result.progresses == 0);
    CHECK(fence(*transport));
}

TEST_CASE("Updater quit fence retracts queued completion and terminally rejects new fetches", "[updates][updater-worker-fence]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    std::atomic_int calls{0};
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [&](const auto&, const auto&, const auto& sink) {
        ++calls; const uint8_t byte = 'x'; sink(&byte, 1);
    });
    Result result(*transport);
    const auto path = staging.filePath("undelivered");
    transport->fetch(feed, ResourceKind::Feed, path, directRoute());
    QElapsedTimer wait; wait.start();
    while(!QFileInfo::exists(path) && wait.elapsed() < 3000) QThread::msleep(1);
    REQUIRE(QFileInfo::exists(path));
    // Also leave a busy rejection queued, not just worker completion/progress.
    transport->fetch(feed, ResourceKind::Feed, staging.filePath("busy"), directRoute());
    CHECK(fence(*transport)); CHECK_FALSE(QFileInfo::exists(path));
    CHECK(transport->fetch(feed, ResourceKind::Feed, staging.filePath("late"), directRoute()) == 0);
    QCoreApplication::processEvents();
    CHECK_FALSE(result.done()); CHECK(result.progresses == 0); CHECK(calls.load() == 1);
}

TEST_CASE("Updater quit fence preserves bytes already delivered into staging", "[updates][updater-worker-fence]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [](const auto&, const auto&, const auto& sink) {
        const uint8_t bytes[]{'a', 'b'}; sink(bytes, 2);
    });
    Result result(*transport); const auto path = staging.filePath("delivered");
    auto route = directRoute(); transport->fetch(feed, ResourceKind::Feed, path, route);
    REQUIRE(pump([&] { return result.successes == 1; }));
    const auto notifications = result.progresses;
    CHECK(fence(*transport)); route->revoked->store(true);
    CHECK(fence(*transport)); QCoreApplication::processEvents();
    CHECK(result.successes == 1); CHECK(result.failures == 0); CHECK(result.progresses == notifications);
    QFile file(path); REQUIRE(file.open(QIODevice::ReadOnly)); CHECK(file.readAll() == "ab");
}

TEST_CASE("Updater quit fence rejects a foreign caller without consuming the owner attempt", "[updates][updater-worker-fence]") {
    dcpp::test::TestContext context;
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [](const auto&, const auto&, const auto&) {});
    CHECK_FALSE(std::async(std::launch::async, [&] { return fence(*transport); }).get());
    CHECK(fence(*transport));
}

TEST_CASE("Updater quit fence from final progress suppresses the rest of completion", "[updates][updater-worker-fence]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [](const auto&, const auto&, const auto& sink) {
        const uint8_t byte = 'x'; sink(&byte, 1);
    });
    Result result(*transport); bool stopped = false;
    QObject::connect(transport.get(), &UpdateTransport::progress, transport.get(), [&](auto, auto, auto total) {
        if(total) stopped = fence(*transport);
    });
    transport->fetch(feed, ResourceKind::Feed, staging.filePath("feed"), directRoute());
    REQUIRE(pump([&] { return stopped || result.done(); }));
    CHECK(stopped); CHECK_FALSE(result.done());
    CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

#ifndef _WIN32
TEST_CASE("Updater quit fence permanently refuses failed undelivered file removal", "[updates][updater-worker-fence]") {
    dcpp::test::TestContext context;
    QTemporaryDir staging;
    auto transport = UpdateTransportTestAccess::create(*context.ownedCtx, [](const auto&, const auto&, const auto& sink) {
        const uint8_t byte = 'x'; sink(&byte, 1);
    });
    Result result(*transport); const auto path = staging.filePath("undelivered");
    transport->fetch(feed, ResourceKind::Feed, path, directRoute());
    QElapsedTimer wait; wait.start();
    while(!QFileInfo::exists(path) && wait.elapsed() < 3000) QThread::msleep(1);
    REQUIRE(QFileInfo::exists(path));
    const auto restore = dcpp::makeScopedFunctor([&] {
        QFile::setPermissions(staging.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    });
    REQUIRE(QFile::setPermissions(staging.path(), QFile::ReadOwner | QFile::ExeOwner));
    CHECK_FALSE(fence(*transport)); CHECK(QFileInfo::exists(path));
    REQUIRE(QFile::setPermissions(staging.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    CHECK_FALSE(fence(*transport));
    CHECK(transport->fetch(feed, ResourceKind::Feed, staging.filePath("late"), directRoute()) == 0);
    QCoreApplication::processEvents(); CHECK_FALSE(result.done()); CHECK(result.progresses == 0);
}
#endif
