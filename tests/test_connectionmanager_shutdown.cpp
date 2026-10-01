#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <ranges>
#define private public
#define protected public
#include "DCContext.h"
#include "ConnectionManager.h"
#include "UserConnection.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "LogManagerListener.h"
#include "ScopedFunctor.h"
#include "ThrottleManager.h"

using namespace dcpp;
using namespace std::chrono_literals;

namespace {
template<class Manager> void stopConnections(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) manager.stopForShutdown();
    else manager.shutdown();
}
template<class Manager> void awaitClosure(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) {
        for(unsigned i = 0; i < 5000; ++i) {
            if(manager.shuttingDown) return;
            Thread::sleep(1);
        }
        FAIL("Connection admission did not close");
    }
}
template<class Manager> void lateCallbacksWithoutDependencies(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) {
        manager.listen();
        manager.on(UserConnectionListener::Connected(), nullptr);
        manager.on(UserConnectionListener::Key(), nullptr, "");
        manager.on(UserConnectionListener::CLock(), nullptr, "", "");
        manager.on(UserConnectionListener::Direction(), nullptr, "Download", "0");
        manager.on(UserConnectionListener::MyNick(), nullptr, "late");
        manager.on(UserConnectionListener::Supports(), nullptr, {});
        manager.on(UserConnectionListener::Failed(), nullptr, "stale");
        manager.on(UserConnectionListener::ProtocolError(), nullptr, "CTM2HUB");
        manager.on(AdcCommand::SUP(), nullptr, AdcCommand(AdcCommand::CMD_SUP));
        manager.on(AdcCommand::INF(), nullptr, AdcCommand(AdcCommand::CMD_INF));
    }
}
struct Deadline {
    std::atomic<const char*> stage{"setup"};
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(40000)) {
            std::fprintf(stderr, "Connection shutdown fixture exceeded 40 seconds: %s\n", stage.load());
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
        auto& c = *fixture.ownedCtx;
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.cryptoManager_ = std::make_unique<CryptoManager>(c);
        c.debugManager_ = std::make_unique<DebugManager>(c);
        c.connectionManager_ = std::make_unique<ConnectionManager>(c);
        auto& settings = *c.getSettingsManager();
        settings.set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
        settings.set(SettingsManager::BIND_ADDRESS, "127.0.0.1");
        settings.set(SettingsManager::BIND_IFACE, false);
        settings.set(SettingsManager::USE_IPV6, false);
        settings.set(SettingsManager::TCP_PORT, 0);
    }
    ~Fixture() {
        deadline.stage = "cleanup";
        stopConnections(manager());
        BufferedSocket::waitShutdown();
        Lock lock(manager().cs);
        while(!manager().downloads.empty()) manager().putCQI(manager().downloads.back());
        while(!manager().uploads.empty()) manager().putCQI(manager().uploads.back());
    }
    ConnectionManager& manager() { return *fixture.ownedCtx->getConnectionManager(); }
    UserPtr user() { return UserPtr(new User(CID::generate())); }
    void networkSupport() {
        auto& c = *fixture.ownedCtx;
        c.throttleManager_ = std::make_unique<ThrottleManager>(c);
    }
};
struct QueueListener : ConnectionManagerListener {
    std::atomic<unsigned> added{0}, removed{0};
    void on(Added, ConnectionQueueItem*) noexcept override { ++added; }
    void on(Removed, ConnectionQueueItem*) noexcept override { ++removed; }
};
}

TEST_CASE("Terminal connection stop rejects late creation and preserves pending state", "[connection-stop][shutdown]") {
    Fixture f;
    auto user = f.user();
    QueueListener listener;
    f.manager().addListener(&listener);
    ScopedFunctor([&] { f.manager().removeListener(&listener); });
    f.manager().getDownloadConnection(HintedUser(user, "adc://fixture"));
    REQUIRE(f.manager().downloads.size() == 1);
    f.manager().downloads.front()->setLastAttempt(123456);
    f.manager().nmdcExpect("existing", "me", "dchub://fixture");
    stopConnections(f.manager());
    f.manager().getDownloadConnection(HintedUser(f.user(), "adc://fixture"));
    f.manager().force(user);
    f.manager().nmdcExpect("late", "me", "dchub://fixture");
    CHECK(listener.added == 1);
    CHECK(listener.removed == 0);
    CHECK(f.manager().downloads.size() == 1);
    CHECK(f.manager().downloads.front()->getLastAttempt() == 123456);
    CHECK(f.manager().expectedConnections.remove("late").first.empty());
    CHECK(f.manager().expectedConnections.remove("existing").first == "me");
    auto* raw = f.manager().getConnection(false, false);
    CHECK(raw == nullptr);
    if(raw) { f.manager().putConnection(raw); delete raw; }
    {
        Lock lock(f.manager().cs);
        auto* queued = f.manager().getCQI(HintedUser(f.user(), "adc://fixture"), false);
        CHECK(queued == nullptr);
        if(queued) f.manager().putCQI(queued);
    }
    auto* source = new UserConnection(false, *f.fixture.ownedCtx);
    f.manager().on(UserConnectionListener::Supports(), source, {UserConnection::FEATURE_ADCGET});
    CHECK_FALSE(source->isSet(UserConnection::FLAG_SUPPORTS_ADCGET));
    delete source;
    f.manager().listen();
    CHECK(f.manager().getPort().empty());
    f.manager().disconnect();
    stopConnections(f.manager());
    f.fixture.ownedCtx->cryptoManager_.reset();
    lateCallbacksWithoutDependencies(f.manager());
}

TEST_CASE("Connection stop drains admitted log callbacks and concurrent stops join", "[connection-stop][shutdown]") {
    Fixture f;
    f.manager().hubsBlockingCC.insert("127.0.0.1");
    struct Listener : LogManagerListener {
        Semaphore entered, release;
        void on(Message, time_t, const string&) noexcept override { entered.signal(); release.wait(); }
    } listener;
    auto& log = *f.fixture.ownedCtx->getLogManager();
    log.addListener(&listener);
    ScopedFunctor([&] { log.removeListener(&listener); });
    auto producer = std::async(std::launch::async, [&] {
        f.manager().nmdcConnect("127.0.0.1", "9", "me", "dchub://fixture", Text::utf8, false);
    });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "admitted log callback";
    auto stop = std::async(std::launch::async, [&] { stopConnections(f.manager()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.manager());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto otherStop = std::async(std::launch::async, [&] { joining.signal(); stopConnections(f.manager()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000));
    CHECK(otherStop.wait_for(30ms) == std::future_status::timeout);
    f.manager().getDownloadConnection(HintedUser(f.user(), "adc://fixture"));
    CHECK(f.manager().downloads.empty());
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); otherStop.get(); producer.get();
    CHECK(f.manager().userConnections.empty());
}

TEST_CASE("Connection stop rejects a timer snapshot delivered after closure", "[connection-stop][shutdown]") {
    Fixture f;
    auto& timer = *f.fixture.ownedCtx->getTimerManager();
    f.manager().getDownloadConnection(HintedUser(f.user(), "adc://fixture"));
    struct Listener : TimerManagerListener {
        Semaphore entered, release;
        void on(Second, uint64_t) noexcept override { entered.signal(); release.wait(); }
    } listener;
    // Capture the registered base pointer; CM privately inherits timer events.
    auto* connectionListener = timer.listeners.back();
    timer.removeListener(connectionListener);
    timer.addListener(&listener);
    timer.addListener(connectionListener);
    ScopedFunctor([&] { timer.removeListener(&listener); });
    auto tick = std::async(std::launch::async, [&] { timer.fire(TimerManagerListener::Second(), 1000); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "late timer snapshot";
    auto stop = std::async(std::launch::async, [&] { stopConnections(f.manager()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.manager());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); tick.get();
    CHECK(f.manager().downloads.size() == 1);
    f.manager().on(TimerManagerListener::Second(), 2000);
    CHECK(f.manager().downloads.size() == 1);
}

TEST_CASE("Terminal connection stop rejects a pending real loopback accept", "[connection-stop][shutdown]") {
    Fixture f;
    f.networkSupport();
    stopConnections(f.manager());
    Socket acceptor;
    acceptor.create(Socket::TYPE_TCP, AF_INET);
    acceptor.bind("0", "127.0.0.1"); acceptor.listen();
    Socket peer;
    peer.create(Socket::TYPE_TCP, AF_INET);
    peer.connect("127.0.0.1", acceptor.getLocalPort());
    REQUIRE(peer.waitConnected(5000));
    REQUIRE(acceptor.wait(5000, Socket::WAIT_READ) & Socket::WAIT_READ);
    f.manager().accept(acceptor, false);
    { Lock lock(f.manager().cs); CHECK(f.manager().userConnections.empty()); }
    stopConnections(f.manager());
    CHECK(BufferedSocket::waitShutdown(5000));
}

TEST_CASE("Connection registry stop retains dependencies until separate socket callback drain", "[connection-stop][shutdown]") {
    Fixture f;
    f.networkSupport();
    Socket acceptor;
    acceptor.create(Socket::TYPE_TCP, AF_INET);
    acceptor.bind("0", "127.0.0.1"); acceptor.listen();
    f.manager().nmdcConnect("127.0.0.1", acceptor.getLocalPort(), "me", "dchub://fixture", Text::utf8, false);
    REQUIRE(acceptor.wait(5000, Socket::WAIT_READ) & Socket::WAIT_READ);
    Socket peer;
    peer.accept(acceptor);
    struct Listener : UserConnectionListener {
        Semaphore entered, release;
        void on(Failed, UserConnection*, const string&) noexcept override { entered.signal(); release.wait(); }
    } listener;
    {
        Lock lock(f.manager().cs);
        REQUIRE(f.manager().userConnections.size() == 1);
        f.manager().userConnections.front()->addListener(&listener);
    }
    f.deadline.stage = "real failure callback resource drain";
    auto stop = std::async(std::launch::async, [&] { stopConnections(f.manager()); });
    ScopedFunctor([&] { listener.release.signal(); BufferedSocket::waitShutdown(); });
    REQUIRE(listener.entered.wait(5000));
    // CM is the first failure listener; later transfer listeners still own work.
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get();
    CHECK(f.manager().userConnections.empty());
    CHECK_FALSE(BufferedSocket::waitShutdown(30));
    listener.release.signal();
    REQUIRE(BufferedSocket::waitShutdown(5000));
    CHECK(f.fixture.ownedCtx->getClientManager() != nullptr);
    CHECK(f.fixture.ownedCtx->getLogManager() != nullptr);
}

TEST_CASE("Connection stop joins an active listener and retires an incoming peer", "[connection-stop][shutdown]") {
    Fixture f;
    f.networkSupport();
    f.manager().listen();
    REQUIRE_FALSE(f.manager().getPort().empty());
    Socket peer;
    peer.create(Socket::TYPE_TCP, AF_INET);
    peer.connect("127.0.0.1", f.manager().getPort());
    REQUIRE(peer.waitConnected(5000));
    bool registered = false;
    for(unsigned i = 0; i < 5000 && !registered; ++i) {
        { Lock lock(f.manager().cs); registered = !f.manager().userConnections.empty(); }
        if(!registered) Thread::sleep(1);
    }
    REQUIRE(registered);
    f.deadline.stage = "active listener and incoming peer";
    stopConnections(f.manager());
    CHECK(f.manager().getPort().empty());
    CHECK(f.manager().getSecurePort().empty());
    CHECK(f.manager().userConnections.empty());
    REQUIRE(BufferedSocket::waitShutdown(5000));
    stopConnections(f.manager());
}

TEST_CASE("Ordinary listener disconnect remains reusable before terminal connection stop", "[connection-stop][shutdown]") {
    Fixture f;
    for(unsigned i = 0; i < 5; ++i) {
        f.manager().listen();
        CHECK_FALSE(f.manager().getPort().empty());
        f.manager().disconnect();
        CHECK(f.manager().getPort().empty());
    }
    stopConnections(f.manager());
    f.manager().listen();
    CHECK(f.manager().getPort().empty());
    f.manager().disconnect();
}
