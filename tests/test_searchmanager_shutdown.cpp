#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#define private public
#include "DCContext.h"
#undef private
#include "TestContext.h"
#include "SettingsManager.h"
#include "Socket.h"
#include "User.h"
#include "Thread.h"
#include "Client.h"
#include "Semaphore.h"
#include "SearchManagerListener.h"
#include "TimerManager.h"
#include "AdcCommand.h"
#include "ClientManager.h"
#include "FavoriteManager.h"
#include "ScopedFunctor.h"
#define private public
#include "SearchManager.h"
#undef private

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <future>

using namespace dcpp;

namespace {
template<class Manager> void stopSearch(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) manager.stopForShutdown();
    else manager.disconnect();
}

template<class Manager> void lateSearchWork(Manager& manager, const UserPtr& user) {
    if constexpr(requires { manager.stopForShutdown(); }) {
        manager.onPSR(AdcCommand(AdcCommand::CMD_PSR, AdcCommand::TYPE_UDP), user);
        manager.respond(AdcCommand(AdcCommand::CMD_SCH, AdcCommand::TYPE_UDP), user->getCID(), true, "");
        manager.search("ignored", 0, SearchManager::TYPE_ANY, SearchManager::SIZE_DONTCARE, "");
        StringList hubs;
        CHECK(manager.search(hubs, "ignored", 0, SearchManager::TYPE_ANY,
            SearchManager::SIZE_DONTCARE, "", {}) == 0);
    }
}

// A broken join must fail the runner rather than leave CI hanging indefinitely.
struct ShutdownDeadline {
    Semaphore finished;
    std::thread watchdog{[this] {
        if (!finished.wait(10000)) {
            std::fputs("UDP queue failed to honor shutdown within 10 seconds\n", stderr);
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }};
    ~ShutdownDeadline() { finished.signal(); watchdog.join(); }
};

struct SearchFixture {
    ShutdownDeadline deadline;
    test::TestContext fixture;
    UserPtr user;
    SearchFixture() {
        auto& context = *fixture.ownedCtx;
        context.timerManager_ = std::make_unique<TimerManager>(context);
        context.clientManager_ = std::make_unique<ClientManager>(context);
        context.favoriteManager_ = std::make_unique<FavoriteManager>(context);
        context.searchManager_ = std::make_unique<SearchManager>(context);
        context.getSettingsManager()->set(SettingsManager::UDP_PORT, 0);
        context.getSettingsManager()->set(SettingsManager::BIND_ADDRESS, "127.0.0.1");
        context.getSettingsManager()->set(SettingsManager::BIND_IFACE, false);
        context.getSettingsManager()->set(SettingsManager::USE_IPV6, false);
        user = context.getClientManager()->getUser(CID(string(39, 'B')));
    }
    SearchManager& search() { return *fixture.ownedCtx->getSearchManager(); }
    AdcCommand result() {
        AdcCommand command(AdcCommand::CMD_RES, AdcCommand::TYPE_UDP);
        command.addParam("FNtest.bin").addParam("SI1").addParam("SL1")
            .addParam("TR" + string(39, 'A'));
        return command;
    }
};

struct ResultListener : SearchManagerListener {
    Semaphore received;
    std::atomic<unsigned> count{0};
    std::function<void()> first;
    void on(SR, const SearchResultPtr&) noexcept override {
        if(count.fetch_add(1) == 0 && first) first();
        received.signal();
    }
};
}

TEST_CASE("UDP search queue preserves shutdown requested before worker entry", "[searchmanager][shutdown]") {
    test::TestContext fixture;
    ShutdownDeadline deadline;
    SearchManager::UdpQueue queue(*fixture.ownedCtx);
    queue.shutdown();
    // Exactly the scheduling order of shutdown winning the race with run().
    CHECK(queue.run() == 0);
}

TEST_CASE("UDP search queue rejects results after shutdown", "[searchmanager][shutdown]") {
    test::TestContext fixture;
    SearchManager::UdpQueue queue(*fixture.ownedCtx);
    queue.shutdown();
    queue.addResult("", "127.0.0.1");
    CHECK(queue.resultList.empty());
}

TEST_CASE("UDP search worker ignores truncated packets and continues", "[searchmanager][shutdown]") {
    test::TestContext fixture;
    ShutdownDeadline deadline;
    for(const auto& packet : StringList{"", "$", "$SR ", "U", "URES", string(1, '\0')}) {
        SearchManager::UdpQueue queue(*fixture.ownedCtx);
        queue.addResult(packet, "127.0.0.1");
        queue.addResult("ignored", "127.0.0.1");
        queue.start();
        bool drained = false;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while(std::chrono::steady_clock::now() < end) {
            {
                Lock lock(queue.csudp);
                drained = queue.resultList.empty();
            }
            if(drained) break;
            Thread::sleep(1);
        }
        queue.shutdown();
        CAPTURE(packet.size());
        REQUIRE(drained);
        CHECK_FALSE(queue.joinable());
    }
}

TEST_CASE("UDP search queue joins while producers are active", "[searchmanager][shutdown]") {
    test::TestContext fixture;
    ShutdownDeadline deadline;
    for (int iteration = 0; iteration < 50; ++iteration) {
        SearchManager::UdpQueue queue(*fixture.ownedCtx);
        queue.start();
        Semaphore producerStarted;
        std::atomic<bool> finishProducing{false};
        std::thread producer([&] {
            queue.addResult("", "127.0.0.1");
            producerStarted.signal();
            // Keep the producer alive through shutdown, rather than relying on
            // the scheduler to overlap two finite bursts of work.
            while (!finishProducing.load()) {
                queue.addResult("", "127.0.0.1");
                Thread::sleep(1);
            }
        });
        producerStarted.wait();
        queue.shutdown();
        finishProducing.store(true);
        producer.join();
        CHECK_FALSE(queue.joinable());
        const auto remaining = queue.resultList.size();
        queue.addResult("", "127.0.0.1");
        CHECK(queue.resultList.size() == remaining);
    }
}

TEST_CASE("Terminal search stop joins the queue without a UDP listener", "[searchmanager][shutdown][search-stop]") {
    SearchFixture f;
    auto& search = f.search();
    REQUIRE(search.queue.joinable());
    stopSearch(search);
    CHECK_FALSE(search.queue.joinable());
    CHECK_FALSE(search.joinable());
    CHECK(search.getPort().empty());
    CHECK_THROWS_AS(search.listen(), Exception);
    stopSearch(search);
    CHECK_FALSE(search.queue.joinable());
    const auto remaining = search.queue.resultList.size();
    search.onSearchResult("ignored");
    CHECK(search.queue.resultList.size() == remaining);
}

TEST_CASE("Ordinary search disconnect permits real UDP results after relisten", "[searchmanager][shutdown][search-stop]") {
    SearchFixture f;
    auto& search = f.search();
    ResultListener listener;
    search.addListener(&listener);
    ScopedFunctor([&] { search.removeListener(&listener); });
    Socket sender;
    sender.create(Socket::TYPE_UDP);
    const auto packet = f.result().toString(f.user->getCID());
    for(int pass = 0; pass < 2; ++pass) {
        search.listen();
        REQUIRE_FALSE(search.getPort().empty());
        sender.writeTo("127.0.0.1", search.getPort(), packet);
        CHECK(listener.received.wait(2000));
        if(pass == 0) {
            search.disconnect();
            CHECK_FALSE(search.joinable());
        }
        CHECK(search.queue.joinable());
    }
    CHECK(listener.count.load() == 2);
    stopSearch(search);
    CHECK_FALSE(search.queue.joinable());
    CHECK_FALSE(search.joinable());
    CHECK(search.getPort().empty());
    CHECK_FALSE(search.socket);
}

TEST_CASE("Terminal search stop drains direct result callbacks and rejects late work", "[searchmanager][shutdown][search-stop]") {
    SearchFixture f;
    auto& search = f.search();
    ResultListener listener;
    Semaphore entered, release;
    listener.first = [&] { entered.signal(); release.wait(); };
    search.addListener(&listener);
    ScopedFunctor([&] { search.removeListener(&listener); });
    const auto command = f.result();
    auto callback = std::async(std::launch::async, [&] { search.onRES(command, f.user); });
    auto unblocked = makeScopedFunctor([&] { release.signal(); });
    REQUIRE(entered.wait(2000));
    Semaphore stopping;
    auto stop = std::async(std::launch::async, [&] { stopping.signal(); stopSearch(search); });
    stopping.wait();
    CHECK(stop.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    release.signal();
    callback.get();
    stop.get();
    CHECK_FALSE(search.queue.joinable());
    search.onRES(command, f.user);
    f.fixture.ownedCtx->favoriteManager_.reset();
    f.fixture.ownedCtx->clientManager_.reset();
    // Late work must be rejected before touching managers that can now be stopped.
    lateSearchWork(search, f.user);
    CHECK(listener.count.load() == 1);
    stopSearch(search);
}
