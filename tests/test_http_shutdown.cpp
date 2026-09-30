#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <future>
#include <ranges>
#define private public
#include "DCContext.h"
#include "HttpConnection.h"
#include "FavoriteManager.h"
#include "extra/dyndns.h"
#undef private
#include "TestContext.h"
#include "BufferedSocket.h"
#include "ClientManager.h"
#include "TimerManager.h"
#include "ThrottleManager.h"
#include "UploadManager.h"
#include "Semaphore.h"
#include "HttpRequestPolicy.h"

using namespace dcpp;
using namespace std::chrono_literals;

namespace {
template<class Owner> void stopOwner(Owner& owner) {
    if constexpr(requires { owner.stopForShutdown(); }) owner.stopForShutdown();
    else if constexpr(requires { owner.abort(); }) owner.abort();
}

struct Deadline {
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(15000)) {
            std::fputs("HTTP shutdown fixture exceeded 15 seconds\n", stderr);
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }};
    ~Deadline() { finished.signal(); watchdog.join(); }
};

struct Fixture {
    Deadline deadline;
    test::TestContext fixture;
    Fixture() {
        auto& c = context();
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.uploadManager_ = std::make_unique<UploadManager>(c);
        c.throttleManager_ = std::make_unique<ThrottleManager>(c);
        c.getSettingsManager()->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
    }
    DCContext& context() { return *fixture.ownedCtx; }
    ~Fixture() { BufferedSocket::waitShutdown(); }
};
}

TEST_CASE("Terminal HTTP stop rejects later GET and POST but ordinary abort remains reusable", "[http-shutdown]") {
    Fixture fixture;
    int connections = 0;
    HttpConnection http(fixture.context(), "fixture", [&](auto&, const auto&, const auto&, bool, bool) { ++connections; });
    const string url = "http://unused.invalid/test";
    http.downloadFile(url);
    REQUIRE(connections == 1);
    http.abort();
    http.downloadFile(url);
    REQUIRE(connections == 2);
    stopOwner(http);
    http.downloadFile(url);
    http.download(url, {{"test", "value"}});
    CHECK(connections == 2);
    stopOwner(http);
    CHECK(BufferedSocket::waitShutdown(2000));
}

TEST_CASE("Terminal HTTP stop drains callbacks and prevents redirect or retry revival", "[http-shutdown]") {
    for(const bool redirect : {false, true}) {
        Fixture fixture;
        Socket server;
        server.create(Socket::TYPE_TCP, AF_INET);
        server.bind("0", "127.0.0.1");
        server.listen();
        const string url = "http://127.0.0.1:" + server.getLocalPort() + "/first";
        HttpConnection http(fixture.context());
        struct Listener : HttpConnectionListener {
            Semaphore entered, release;
            string url;
            void on(Data, HttpConnection*, const uint8_t*, size_t) override { }
            void on(Redirected, HttpConnection*, const string&) override {
                entered.signal(); release.wait();
            }
            void on(Failed, HttpConnection* http, const string&) override {
                entered.signal(); release.wait();
                http->downloadFile(url);
            }
        } listener;
        listener.url = url;
        http.addListener(&listener);
        http.downloadFile(url);
        REQUIRE(server.wait(3000, Socket::WAIT_READ) & Socket::WAIT_READ);
        Socket peer;
        peer.accept(server);
        const string reply = redirect ? "HTTP/1.1 302 Found\r\nLocation: /next\r\n\r\n" :
            "HTTP/1.1 503 Unavailable\r\n\r\n";
        peer.writeAll(reply.data(), reply.size(), 3000);
        REQUIRE(listener.entered.wait(3000));
        Semaphore stopping;
        auto stopped = std::async(std::launch::async, [&] { stopping.signal(); stopOwner(http); });
        REQUIRE(stopping.wait(3000));
        CHECK(stopped.wait_for(50ms) == std::future_status::timeout);
        listener.release.signal();
        REQUIRE(stopped.wait_for(3s) == std::future_status::ready);
        stopped.get();
        // Also drain the owner listener before inspecting the legacy RED path.
        http.removeListener(&listener);
        CHECK_FALSE(server.wait(150, Socket::WAIT_READ) & Socket::WAIT_READ);
        stopOwner(http);
        CHECK(BufferedSocket::waitShutdown(2000));
    }
}

TEST_CASE("HTTP redirects and callback retries still complete before terminal stop", "[http-shutdown]") {
    for(const bool redirect : {false, true}) {
        Fixture fixture;
        Socket server;
        server.create(Socket::TYPE_TCP, AF_INET);
        server.bind("0", "127.0.0.1");
        server.listen();
        const string url = "http://127.0.0.1:" + server.getLocalPort() + "/first";
        HttpConnection http(fixture.context());
        struct Listener : HttpConnectionListener {
            Semaphore complete;
            string url, body;
            int failures = 0, redirects = 0;
            void on(Data, HttpConnection*, const uint8_t* bytes, size_t size) override {
                body.append(reinterpret_cast<const char*>(bytes), size);
            }
            void on(Complete, HttpConnection*, const string&) override { complete.signal(); }
            void on(Redirected, HttpConnection*, const string&) override { ++redirects; }
            void on(Failed, HttpConnection* http, const string&) override {
                if(++failures == 1) http->downloadFile(url);
            }
        } listener;
        listener.url = url;
        http.addListener(&listener);
        http.downloadFile(url);
        REQUIRE(server.wait(3000, Socket::WAIT_READ) & Socket::WAIT_READ);
        Socket first;
        first.accept(server);
        const string reply = redirect ? "HTTP/1.1 302 Found\r\nLocation: /next\r\n\r\n" :
            "HTTP/1.1 503 Unavailable\r\n\r\n";
        first.writeAll(reply.data(), reply.size(), 3000);
        REQUIRE(server.wait(3000, Socket::WAIT_READ) & Socket::WAIT_READ);
        Socket second;
        second.accept(server);
        const string success = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx";
        second.writeAll(success.data(), success.size(), 3000);
        REQUIRE(listener.complete.wait(3000));
        stopOwner(http);
        http.removeListener(&listener);
        CHECK(listener.body == "x");
        CHECK(listener.failures == (redirect ? 0 : 1));
        CHECK(listener.redirects == (redirect ? 1 : 0));
        CHECK(BufferedSocket::waitShutdown(2000));
    }
}

TEST_CASE("Hub-list shutdown preserves its manager and rejects fresh downloads", "[http-shutdown]") {
    Fixture fixture;
    FavoriteManager manager(fixture.context());
    const string url = "http://unused.invalid/hubs.xml";
    fixture.context().getSettingsManager()->set(SettingsManager::HUBLIST_SERVERS, url);
    int connections = 0;
    manager.c = new HttpConnection(fixture.context(), "fixture", [&](auto&, const auto&, const auto&, bool, bool) { ++connections; });
    manager.refresh(true);
    REQUIRE(connections == 1);
    REQUIRE(manager.isDownloading());
    stopOwner(manager);
    CHECK_FALSE(manager.isDownloading());
    CHECK(manager.getCurrentHubList() == url);
    manager.refresh(true);
    manager.c->downloadFile(url);
    CHECK(connections == 1);
    stopOwner(manager);
    manager.c->abort();
    CHECK(BufferedSocket::waitShutdown(2000));
}

TEST_CASE("External IP shutdown cancels requests and rejects later load and retry", "[http-shutdown]") {
    Fixture fixture;
    DynDNS manager(fixture.context());
    int connections = 0;
    manager.httpConnection.connector = [&](auto&, const auto&, const auto&, bool, bool) { ++connections; };
    auto& settings = *fixture.context().getSettingsManager();
    settings.set(SettingsManager::DYNDNS_ENABLE, true);
    settings.set(SettingsManager::DYNDNS_SERVER, "http://unused.invalid/ip");
    manager.load();
    REQUIRE(connections == 1);
    stopOwner(manager);
    CHECK_FALSE(manager.request);
    manager.load();
    manager.on(HttpConnectionListener::Failed(), &manager.httpConnection, "fixture");
    manager.on(TimerManagerListener::Minute(), 60000);
    manager.on(TimerManagerListener::Minute(), 120000);
    CHECK(connections == 1);
    stopOwner(manager);
    manager.httpConnection.abort();
    CHECK(BufferedSocket::waitShutdown(2000));
}

TEST_CASE("Terminal strict HTTP stop retains cancellation and rejects later requests", "[http-shutdown]") {
    Fixture fixture;
    HttpRequestPolicy policy;
    HttpConnection http(fixture.context(), policy, "fixture");
    struct Listener : HttpConnectionListener {
        int failures = 0;
        void on(Data, HttpConnection*, const uint8_t*, size_t) override { }
        void on(Failed, HttpConnection*, const string&) override { ++failures; }
    } listener;
    http.addListener(&listener);
    stopOwner(http);
    http.download("https://unused.invalid/test", {});
    CHECK(listener.failures == 0);
    CHECK(http.strictCancelled.load());
    stopOwner(http);
    http.removeListener(&listener);
}
