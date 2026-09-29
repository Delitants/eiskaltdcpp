#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
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
#define private public
#include "SearchManager.h"
#undef private

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>

using namespace dcpp;

namespace {
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
