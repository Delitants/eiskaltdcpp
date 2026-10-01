#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <future>
#include <ranges>
#define private public
#define protected public
#include "DCContext.h"
#include "Client.h"
#include "BufferedSocket.h"
#include "NmdcHub.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "SettingsManager.h"
#include "ThrottleManager.h"
#include "UploadManager.h"
#include "Semaphore.h"

using namespace dcpp;
using namespace std::chrono_literals;

namespace {
template<class Gate> void stopGate(Gate& gate) {
    if constexpr(requires { gate.stop(); }) gate.stop();
    else gate.reset();
}

template<class Hub> void stopHub(Hub& hub) {
    if constexpr(requires { hub.stopForShutdown(); }) hub.stopForShutdown();
    else hub.shutdown();
}

template<class Manager> void stopRegistry(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) manager.stopForShutdown();
}

template<class SocketType> bool drained(uint32_t timeout) {
    if constexpr(requires { SocketType::waitShutdown(timeout); })
        return SocketType::waitShutdown(timeout);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while(SocketType::sockets > 0 && std::chrono::steady_clock::now() < deadline)
        Thread::sleep(1);
    return SocketType::sockets == 0;
}

struct Deadline {
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(15000)) {
            std::fputs("Hub shutdown fixture exceeded 15 seconds\n", stderr);
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }};
    ~Deadline() { finished.signal(); watchdog.join(); }
};

struct HubFixture {
    Deadline deadline;
    test::TestContext fixture;
    Client* hub;
    explicit HubFixture(const string& url) {
        auto& context = *fixture.ownedCtx;
        context.timerManager_ = std::make_unique<TimerManager>(context);
        context.clientManager_ = std::make_unique<ClientManager>(context);
        context.favoriteManager_ = std::make_unique<FavoriteManager>(context);
        context.debugManager_ = std::make_unique<DebugManager>(context);
        context.uploadManager_ = std::make_unique<UploadManager>(context);
        context.throttleManager_ = std::make_unique<ThrottleManager>(context);
        context.getSettingsManager()->set(SettingsManager::OUTGOING_CONNECTIONS,
            SettingsManager::OUTGOING_DIRECT);
        hub = context.getClientManager()->getClient(url);
    }
    ~HubFixture() {
        if(hub) fixture.ownedCtx->getClientManager()->putClient(hub);
        BufferedSocket::waitShutdown();
    }
};
}

TEST_CASE("Terminal registry stop retains clients and rejects late creation", "[hub-shutdown][client-registry-stop]") {
    HubFixture fixture("adc://127.0.0.1:1");
    auto& manager = *fixture.fixture.ownedCtx->getClientManager();
    auto* second = manager.getClient("dchub://127.0.0.1:1");
    stopRegistry(manager);
    CHECK(manager.getClients().size() == 2);
    Client* late = nullptr;
    CHECK_THROWS_AS(late = manager.getClient("adc://127.0.0.1:2"), Exception);
    if(late) manager.putClient(late);
    fixture.hub->connect();
    second->connect();
    CHECK(fixture.hub->sock == nullptr);
    CHECK(second->sock == nullptr);
    stopRegistry(manager);
    manager.putClient(second);
    CHECK(manager.getClients().size() == 1);
}

TEST_CASE("Registry stop waits for an admitted client release without holding registry locks", "[hub-shutdown][client-registry-stop]") {
    HubFixture fixture("adc://127.0.0.1:1");
    auto& manager = *fixture.fixture.ownedCtx->getClientManager();
    struct Listener : ClientManagerListener {
        Semaphore entered, release;
        std::atomic<bool> registryRead{false};
        ClientManager& manager;
        explicit Listener(ClientManager& owner) : manager(owner) { }
        void on(ClientDisconnected, Client*) override {
            entered.signal();
            release.wait();
            // A callback taking the registry lock must not deadlock the stop.
            registryRead = manager.getUserCount() == 0;
        }
    } listener(manager);
    manager.addListener(&listener);
    auto* released = fixture.hub;
    fixture.hub = nullptr;
    auto releasing = std::async(std::launch::async, [&] { manager.putClient(released); });
    REQUIRE(listener.entered.wait(3000));
    Semaphore stopping;
    auto stopped = std::async(std::launch::async, [&] { stopping.signal(); stopRegistry(manager); });
    REQUIRE(stopping.wait(3000));
    const auto result = stopped.wait_for(50ms);
    listener.release.signal();
    releasing.get();
    REQUIRE(stopped.wait_for(3s) == std::future_status::ready);
    stopped.get();
    manager.removeListener(&listener);
    CHECK(result == std::future_status::timeout);
    CHECK(listener.registryRead.load());
    CHECK(manager.getClients().empty());
}

TEST_CASE("Registry stop defers release during a hub callback and concurrent stops join", "[hub-shutdown][client-registry-stop]") {
    for(const auto* protocol : {"adc", "dchub"}) {
        Socket acceptor;
        acceptor.create(Socket::TYPE_TCP, AF_INET);
        acceptor.bind("0", "127.0.0.1");
        acceptor.listen();
        HubFixture fixture(string(protocol) + "://127.0.0.1:" + acceptor.getLocalPort());
        auto& manager = *fixture.fixture.ownedCtx->getClientManager();
        struct Listener : ClientListener {
            Semaphore entered, release;
            void on(Connected, Client*) override { entered.signal(); release.wait(); }
        } listener;
        struct RegistryListener : ClientManagerListener {
            std::atomic<int> releases{0};
            void on(ClientDisconnected, Client*) override { ++releases; }
        } registryListener;
        manager.addListener(&registryListener);
        fixture.hub->addListener(&listener);
        fixture.hub->connect();
        REQUIRE(acceptor.wait(3000, Socket::WAIT_READ) & Socket::WAIT_READ);
        Socket peer;
        peer.accept(acceptor);
        REQUIRE(listener.entered.wait(3000));
        Semaphore stopping;
        auto stopped = std::async(std::launch::async, [&] { stopping.signal(); stopRegistry(manager); });
        REQUIRE(stopping.wait(3000));
        // Wait until terminal admission is observable, not just thread entry.
        bool admissionClosed = false;
        int earlyCreations = 0;
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while(std::chrono::steady_clock::now() < deadline) {
            try {
                auto* late = manager.getClient("adc://127.0.0.1:1");
                ++earlyCreations;
                manager.putClient(late);
            } catch(const Exception&) { admissionClosed = true; break; }
            Thread::sleep(1);
        }
        Semaphore joining;
        auto anotherStop = std::async(std::launch::async, [&] { joining.signal(); stopRegistry(manager); });
        REQUIRE(joining.wait(3000));
        const auto previousNotifications = registryListener.releases.load();
        auto* released = fixture.hub;
        fixture.hub = nullptr;
        auto releasing = std::async(std::launch::async, [&] { manager.putClient(released); });
        const auto releaseResult = releasing.wait_for(50ms);
        const auto stopResult = stopped.wait_for(0ms);
        const auto secondResult = anotherStop.wait_for(50ms);
        const auto notifications = registryListener.releases.load();
        listener.release.signal();
        releasing.get();
        stopped.get();
        anotherStop.get();
        manager.removeListener(&registryListener);
        CHECK(admissionClosed);
        CHECK(releaseResult == std::future_status::ready);
        CHECK(stopResult == std::future_status::timeout);
        CHECK(secondResult == std::future_status::timeout);
        CHECK(notifications == previousNotifications);
        CHECK(registryListener.releases.load() == earlyCreations + 1);
        CHECK(manager.getClients().empty());
        CHECK(drained<BufferedSocket>(3000));
    }
}

TEST_CASE("Stopped reconnect gates cannot be reopened by late completion or reset", "[hub-shutdown]") {
    for(const auto active : {false, true}) {
        ReconnectAttemptGate gate;
        if(active) REQUIRE(gate.tryBegin());
        else REQUIRE(gate.requestManual());
        stopGate(gate);
        gate.complete();
        gate.reset();
        CHECK_FALSE(gate.tryBegin());
        CHECK_FALSE(gate.requestManual());
        CHECK_FALSE(gate.tryBeginScheduled(true, true, 20000, 0, 1));
        stopGate(gate);
        CHECK_FALSE(gate.tryBegin());
    }
}

TEST_CASE("Hub stop retains client ownership but permanently disables reconnect", "[hub-shutdown]") {
    for(const auto* protocol : {"adc", "dchub"}) {
        HubFixture fixture(string(protocol) + "://127.0.0.1:1");
        auto& hub = *fixture.hub;
        hub.setAutoReconnect(true);
        hub.reconnect();
        stopHub(hub);
        CHECK_FALSE(hub.getAutoReconnect());
        CHECK_FALSE(hub.isConnected());
        CHECK(hub.sock == nullptr);
        REQUIRE(fixture.fixture.ownedCtx->getClientManager()->getClients().front() == &hub);
        // A late callback or GUI action must not revive a stopped client.
        hub.setAutoReconnect(true);
        hub.reconnect();
        CHECK_FALSE(hub.reconnectAttempt.tryBeginScheduled(true, true, 20000, 0, 1));
        stopHub(hub);
        CHECK_FALSE(hub.reconnectAttempt.tryBegin());
    }
}

TEST_CASE("Hub stop drains an active socket callback without deleting its client", "[hub-shutdown]") {
    for(const auto* protocol : {"adc", "dchub"}) {
        Socket acceptor;
        acceptor.create(Socket::TYPE_TCP, AF_INET);
        acceptor.bind("0", "127.0.0.1");
        acceptor.listen();
        HubFixture fixture(string(protocol) + "://127.0.0.1:" + acceptor.getLocalPort());
        struct Listener : ClientListener {
            Semaphore entered, release;
            void on(Connected, Client* client) override {
                entered.signal();
                release.wait();
                client->setAutoReconnect(true);
            }
        } listener;
        fixture.hub->addListener(&listener);
        fixture.hub->connect();
        REQUIRE(acceptor.wait(3000, Socket::WAIT_READ) & Socket::WAIT_READ);
        Socket peer;
        peer.accept(acceptor);
        REQUIRE(listener.entered.wait(3000));
        Semaphore stopping;
        auto stopped = std::async(std::launch::async, [&] {
            stopping.signal();
            stopHub(*fixture.hub);
        });
        REQUIRE(stopping.wait(3000));
        CHECK(stopped.wait_for(50ms) == std::future_status::timeout);
        listener.release.signal();
        REQUIRE(stopped.wait_for(3s) == std::future_status::ready);
        stopped.get();
        fixture.hub->removeListener(&listener);
        REQUIRE(drained<BufferedSocket>(3000));
        CHECK_FALSE(fixture.hub->isConnected());
        CHECK(fixture.hub->sock == nullptr);
        CHECK_FALSE(fixture.hub->getAutoReconnect());
        fixture.hub->connect();
        CHECK(fixture.hub->sock == nullptr);
        stopHub(*fixture.hub);
    }
}

TEST_CASE("Hub stop detaches the timer after an in-flight tick completes", "[hub-shutdown]") {
    HubFixture fixture("dchub://127.0.0.1:1");
    // Throttling requires a continuing timer on one thread to release its
    // token locks. This fixture drives only a single hub callback by hand.
    fixture.fixture.ownedCtx->throttleManager_.reset();
    struct TickHub : NmdcHub {
        Semaphore entered, release;
        std::atomic<int> ticks{0};
        explicit TickHub(DCContext& context) : NmdcHub(context, "dchub://127.0.0.1:1", false) { }
        void on(Second tick, uint64_t now) override {
            ++ticks;
            entered.signal();
            release.wait();
            NmdcHub::on(tick, now);
        }
    } hub(*fixture.fixture.ownedCtx);
    auto& timer = *fixture.fixture.ownedCtx->getTimerManager();
    auto tick = std::async(std::launch::async, [&] { timer.fire(TimerManagerListener::Second(), 20000); });
    REQUIRE(hub.entered.wait(3000));
    Semaphore stopping;
    auto stopped = std::async(std::launch::async, [&] {
        stopping.signal();
        stopHub(hub);
    });
    REQUIRE(stopping.wait(3000));
    CHECK(stopped.wait_for(50ms) == std::future_status::timeout);
    hub.release.signal();
    REQUIRE(stopped.wait_for(3s) == std::future_status::ready);
    stopped.get();
    tick.get();
    // Pre-release so a regression fails an assertion instead of hanging.
    hub.release.signal();
    timer.fire(TimerManagerListener::Second(), 30000);
    CHECK(hub.ticks.load() == 1);
    CHECK_FALSE(hub.isConnected());
}

TEST_CASE("Socket drain remains incomplete until transport destruction finishes", "[hub-shutdown][socket]") {
    Deadline deadline;
    test::TestContext fixture;
    Semaphore destroying, release;
    struct SlowTransport : Socket {
        Semaphore& destroying;
        Semaphore& release;
        SlowTransport(Semaphore& entered, Semaphore& proceed) : destroying(entered), release(proceed) { }
        ~SlowTransport() override { destroying.signal(); release.wait(); }
    };
    auto* socket = BufferedSocket::getSocket('\n', *fixture.ownedCtx);
    auto transport = std::make_unique<SlowTransport>(destroying, release);
    transport->create(Socket::TYPE_TCP, AF_INET);
    socket->setSocket(std::move(transport));
    CHECK_FALSE(drained<BufferedSocket>(0));
    BufferedSocket::putSocket(socket);
    REQUIRE(destroying.wait(3000));
    CHECK_FALSE(drained<BufferedSocket>(50));
    release.signal();
    REQUIRE(drained<BufferedSocket>(3000));
    CHECK(drained<BufferedSocket>(0));
}
