#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <future>
#define private public
#include "DCContext.h"
#undef private
#include "TestContext.h"
#include "BufferedSocket.h"
#include "ClientManager.h"
#include "FavoriteManager.h"
#include "File.h"
#include "LogManager.h"
#include "ProxyRoute.h"
#include "ScopedFunctor.h"
#include "Semaphore.h"
#include "ThrottleManager.h"
#include "UploadManager.h"

#ifdef WITH_DHT
#define private public
#include "dht/DHT.h"
#include "dht/BootstrapManager.h"
#include "dht/ConnectionManager.h"
#include "dht/IndexManager.h"
#include "dht/SearchManager.h"
#include "dht/TaskManager.h"
#undef private

using namespace dcpp;
using namespace std::chrono_literals;

namespace {
template<class Manager> void stopDht(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) manager.stopForShutdown();
    else manager.stop();
}
template<class Manager> void stopBootstrap(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) manager.stopForShutdown();
    else manager.httpConnection.abort();
}

struct Deadline {
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(15000)) {
            std::fputs("DHT shutdown fixture exceeded 15 seconds\n", stderr);
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
        c.favoriteManager_ = std::make_unique<FavoriteManager>(c);
        c.uploadManager_ = std::make_unique<UploadManager>(c);
        c.throttleManager_ = std::make_unique<ThrottleManager>(c);
        auto& settings = *c.getSettingsManager();
        settings.set(SettingsManager::USE_DHT, true);
        settings.set(SettingsManager::DHT_PORT, 0);
        settings.set(SettingsManager::DHT_BOOTSTRAP_URLS, ";");
        settings.set(SettingsManager::USE_IPV6, false);
        settings.set(SettingsManager::BIND_IFACE, false);
        settings.set(SettingsManager::BIND_ADDRESS, "127.0.0.1");
        settings.set(SettingsManager::NO_IP_OVERRIDE, true);
        settings.set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
        c.dht_ = std::make_unique<dht::DHT>(c);
    }
    ~Fixture() { stopDht(dht()); CHECK(BufferedSocket::waitShutdown(2000)); }
    DCContext& context() { return *fixture.ownedCtx; }
    dht::DHT& dht() { return *context().getDHT(); }
};

template<class Manager> void lateProducers(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) {
        const auto cid = CID(string(39, 'C'));
        CHECK_FALSE(manager.createNode(cid, "8.8.4.4", "6250", true, true));
        CHECK_FALSE(manager.addNode({}, false));
        manager.dispatch("URES malformed", "8.8.4.4", "6250", true);
        manager.info("8.8.4.4", "6250", 0, cid, {});
        manager.findFile(string(39, 'A'), "late");
        auto& search = manager.getSearchManager();
        search.findNode(cid);
        search.findFile(string(39, 'A'), "late");
        search.findStore(string(39, 'A'), 2000000, false);
        search.processSearchRequest({}, AdcCommand(AdcCommand::CMD_SCH));
        search.processSearchResult({}, AdcCommand(AdcCommand::CMD_RES));
        search.processSearches();
        CHECK_FALSE(search.processSearchResults({}, 0));
        auto& index = manager.getIndexManager();
        const auto queued = index.publishQueue.size();
        index.publishFile(TTHValue(string(39, 'A')), 2000000);
        index.publishPartialFile(TTHValue(string(39, 'A')));
        index.publishNextFile();
        index.processPublishSourceRequest({}, AdcCommand(AdcCommand::CMD_PUB));
        CHECK(index.publishQueue.size() == queued);
        manager.getConnectionManager().connect({}, "late");
        manager.getConnectionManager().connect({}, "late", false);
        manager.getConnectionManager().connectToMe({}, AdcCommand(AdcCommand::CMD_CTM));
        manager.getConnectionManager().revConnectToMe({}, AdcCommand(AdcCommand::CMD_RCM));
    }
}
}

TEST_CASE("Terminal DHT stop before start prevents later bootstrap and listener startup", "[dht-stop]") {
    Fixture f;
    auto* index = &f.dht().getIndexManager();
    stopDht(f.dht());
    f.dht().start();
    CHECK(f.dht().getPort().empty());
    CHECK_FALSE(f.dht().socket.socket);
    CHECK_FALSE(f.dht().bootstrapMgr_);
    CHECK(&f.dht().getIndexManager() == index);
    stopDht(f.dht());
}

TEST_CASE("Terminal DHT stop retains routing state but cancels queued and late producers", "[dht-stop]") {
    Fixture f;
    f.dht().start();
    const auto cid = CID(string(39, 'B'));
    auto node = f.dht().createNode(cid, "8.8.8.8", "6250", false, true);
    REQUIRE(node);
    REQUIRE(f.dht().addNode(node, false));
    auto* bucket = f.dht().bucket;
    auto* bootstrap = &f.dht().getBootstrapManager();
    auto* searches = &f.dht().getSearchManager();
    auto* tasks = &f.dht().getTaskManager();
    auto* connections = &f.dht().getConnectionManager();
    const auto route = f.context().getProxyRoute()->snapshot();
    f.dht().stop(); // Reusable stop, retaining routing state while USE_DHT is ON.
    AdcCommand queued(AdcCommand::CMD_GET, AdcCommand::TYPE_UDP);
    f.dht().send(queued, "127.0.0.1", "6250", cid, {});
    REQUIRE(f.dht().socket.sendQueue.size() == 1);
    stopDht(f.dht());
    CHECK(f.dht().bucket == bucket);
    CHECK(f.dht().getNodesCount() == 1);
    CHECK(f.dht().bootstrapMgr_.get() == bootstrap);
    CHECK(f.dht().searchMgr_.get() == searches);
    CHECK(f.dht().taskMgr_.get() == tasks);
    CHECK(f.dht().connectionMgr_.get() == connections);
    CHECK(f.dht().socket.sendQueue.empty());
    CHECK_FALSE(route->revoked->load());
    f.dht().saveData();
    File saved(Util::getPath(Util::PATH_USER_CONFIG) + DHT_FILE, File::READ, File::OPEN);
    CHECK(saved.read().find("I4=\"8.8.8.8\"") != string::npos);
    lateProducers(f.dht());
    AdcCommand late(AdcCommand::CMD_GET, AdcCommand::TYPE_UDP);
    f.dht().send(late, "127.0.0.1", "6250", cid, {});
    CHECK(late.getParameters().empty());
    CHECK(f.dht().socket.sendQueue.empty());
    f.dht().start();
    CHECK(f.dht().getPort().empty());
    stopDht(f.dht());
}

TEST_CASE("Ordinary DHT stop and restart retain transport reuse", "[dht-stop]") {
    Fixture f;
    Socket sink;
    sink.create(Socket::TYPE_UDP);
    const auto port = sink.bind("0", "127.0.0.1");
    for(int pass = 0; pass < 2; ++pass) {
        f.dht().start();
        REQUIRE_FALSE(f.dht().getPort().empty());
        AdcCommand command(AdcCommand::CMD_GET, AdcCommand::TYPE_UDP);
        f.dht().send(command, "127.0.0.1", port, CID(string(39, 'B')), {});
        REQUIRE(sink.wait(2000, Socket::WAIT_READ) & Socket::WAIT_READ);
        std::array<uint8_t, 2048> packet;
        sockaddr_storage address{};
        CHECK(sink.read(packet.data(), packet.size(), address) > 0);
        f.dht().stop();
        CHECK(f.dht().getPort().empty());
    }
}

TEST_CASE("Terminal DHT stop joins a UDP send callback without revoking the route", "[dht-stop]") {
    Fixture f;
    f.dht().start();
    struct BlockedSend : dht::UDPSocket::TransportObserver {
        Semaphore entered, release;
        void onSend(const dht::UDPSocket::SendObservation&) override { entered.signal(); release.wait(); }
    };
    auto observer = std::make_shared<BlockedSend>();
    f.dht().setTransportObserver(observer);
    Socket sink;
    sink.create(Socket::TYPE_UDP);
    const auto port = sink.bind("0", "127.0.0.1");
    AdcCommand command(AdcCommand::CMD_GET, AdcCommand::TYPE_UDP);
    f.dht().send(command, "127.0.0.1", port, CID(string(39, 'B')), {});
    auto unblocked = makeScopedFunctor([&] { observer->release.signal(); });
    REQUIRE(observer->entered.wait(2000));
    Semaphore stopping;
    auto stop = std::async(std::launch::async, [&] { stopping.signal(); stopDht(f.dht()); });
    stopping.wait();
    CHECK(stop.wait_for(50ms) == std::future_status::timeout);
    observer->release.signal(); stop.get();
    CHECK_FALSE(f.dht().socket.socket);
    CHECK(f.dht().getPort().empty());
    CHECK_FALSE(f.context().getProxyRoute()->snapshot()->revoked->load());
}

TEST_CASE("Terminal DHT stop drains timer dispatch and ignores its late snapshot", "[dht-stop]") {
    Fixture f;
    // Throttle keeps locks across ticks on its dedicated timer thread. This
    // fixture dispatches a single tick on a temporary thread instead.
    f.context().throttleManager_.reset();
    struct BlockedTimer : TimerManagerListener {
        Semaphore entered, release;
        void on(Second, uint64_t) noexcept override { entered.signal(); release.wait(); }
    } blocked;
    auto& timer = *f.context().getTimerManager();
    timer.addListener(&blocked);
    ScopedFunctor([&] { timer.removeListener(&blocked); });
    f.dht().start(); // Task listener is behind the blocked listener in the snapshot.
    auto* tasks = &f.dht().getTaskManager();
    auto callback = std::async(std::launch::async, [&] { timer.fire(TimerManagerListener::Second(), 6000); });
    auto unblocked = makeScopedFunctor([&] { blocked.release.signal(); });
    REQUIRE(blocked.entered.wait(2000));
    Semaphore stopping;
    auto stop = std::async(std::launch::async, [&] { stopping.signal(); stopDht(f.dht()); });
    stopping.wait();
    CHECK(stop.wait_for(50ms) == std::future_status::timeout);
    blocked.release.signal(); callback.get(); stop.get();
    CHECK(tasks->lastBootstrap == 0);
    CHECK(f.dht().taskMgr_.get() == tasks);
    CHECK(f.dht().getPort().empty());
}

TEST_CASE("Terminal bootstrap and DHT stops drain real HTTP callbacks and cannot restart", "[dht-stop]") {
    bool stopWholeDht = false;
    SECTION("Standalone bootstrap barrier") {}
    SECTION("Whole DHT barrier") { stopWholeDht = true; }
    Fixture f;
    f.dht().start();
    auto& bootstrap = f.dht().getBootstrapManager();
    struct BlockedLog : LogManagerListener {
        Semaphore entered, release;
        void on(Message, time_t, const string& message) noexcept override {
            if(message.find(_("DHT bootstrap error: ")) == 0) { entered.signal(); release.wait(); }
        }
    } blocked;
    auto& log = *f.context().getLogManager();
    log.addListener(&blocked);
    ScopedFunctor([&] { log.removeListener(&blocked); });
    Socket server;
    server.create(Socket::TYPE_TCP);
    const auto port = server.bind("0", "127.0.0.1");
    server.listen();
    const auto url = "http://127.0.0.1:" + port + "/nodes";
    f.context().getSettingsManager()->set(SettingsManager::DHT_BOOTSTRAP_URLS, url);
    bootstrap.bootstrap();
    auto unblocked = makeScopedFunctor([&] { blocked.release.signal(); });
    REQUIRE(server.wait(3000, Socket::WAIT_READ) & Socket::WAIT_READ);
    Socket peer;
    peer.accept(server);
    const string reply = "HTTP/1.1 503 Unavailable\r\n\r\n";
    peer.writeAll(reply.data(), reply.size(), 3000);
    REQUIRE(blocked.entered.wait(3000));
    Semaphore stopping;
    auto stop = std::async(std::launch::async, [&] {
        stopping.signal();
        if(stopWholeDht) stopDht(f.dht());
        else stopBootstrap(bootstrap);
    });
    stopping.wait();
    CHECK(stop.wait_for(50ms) == std::future_status::timeout);
    blocked.release.signal(); stop.get();
    CHECK(BufferedSocket::waitShutdown(2000));
    bootstrap.bootstrap();
    CHECK_FALSE(server.wait(100, Socket::WAIT_READ) & Socket::WAIT_READ);
    bootstrap.addBootstrapNode("8.8.8.8", "6250", CID(string(39, 'B')), {});
    CHECK(bootstrap.bootstrapNodes.empty());
    bootstrap.process();
    CHECK_FALSE(bootstrap.requestActive);
    CHECK_FALSE(f.context().getProxyRoute()->snapshot()->revoked->load());
    if(stopWholeDht) CHECK(f.dht().getPort().empty());
    stopBootstrap(bootstrap);
}

TEST_CASE("Terminal DHT stop drains an admitted direct index producer", "[dht-stop]") {
    Fixture f;
    auto& index = f.dht().getIndexManager();
    index.setPublish(false);
    const auto republishTime = index.nextRepublishTime.load();
    std::unique_lock held(index.cs);
    auto publish = std::async(std::launch::async, [&] {
        index.publishFile(TTHValue(string(39, 'A')), 2000000);
    });
    ScopedFunctor([&] { if(held.owns_lock()) held.unlock(); });
    bool admitted = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while(std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard lock(f.dht().operationMutex);
            admitted = f.dht().activeOperations != 0;
        }
        if(admitted) break;
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(admitted);
    Semaphore stopping;
    auto stop = std::async(std::launch::async, [&] { stopping.signal(); stopDht(f.dht()); });
    stopping.wait();
    CHECK(stop.wait_for(50ms) == std::future_status::timeout);
    held.unlock(); publish.get(); stop.get();
    CHECK(index.publishQueue.size() == 1);
    index.publishFile(TTHValue(string(39, 'B')), 2000000);
    index.setPublish(true);
    index.setNextPublishing();
    CHECK(index.publishQueue.size() == 1);
    CHECK_FALSE(index.getPublish());
    CHECK(index.nextRepublishTime.load() == republishTime);
}
#endif
