#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <future>
#include <ranges>
#define private public
#include "DCContext.h"
#include "ThrottleManager.h"
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "Semaphore.h"
#include "ScopedFunctor.h"

using namespace dcpp;
using namespace std::chrono_literals;

namespace {
struct Deadline {
    Semaphore finished;
    std::thread watchdog{[&] {
        if(!finished.wait(15000)) {
            std::fputs("Throttle shutdown fixture exceeded 15 seconds\n", stderr);
            std::_Exit(1);
        }
    }};
    ~Deadline() { finished.signal(); watchdog.join(); }
};
struct Fixture {
    Deadline deadline;
    test::TestContext owner;
    Semaphore requested, completed;
    std::atomic<bool> quit{false};
    std::thread timer;
    Fixture() {
        auto& c = *owner.ownedCtx;
        REQUIRE(std::filesystem::equivalent(Util::getPath(Util::PATH_USER_CONFIG), owner.tmpDir));
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.throttleManager_ = std::make_unique<ThrottleManager>(c);
        timer = std::thread([&] {
            while(true) {
                requested.wait();
                if(quit.load()) return;
                c.getTimerManager()->fire(TimerManagerListener::Second(), 0);
                completed.signal();
            }
        });
    }
    ThrottleManager& throttle() { return *owner.ownedCtx->getThrottleManager(); }
    void tick() { requested.signal(); REQUIRE(completed.wait(3000)); }
    ~Fixture() {
        // Even a failing legacy assertion releases locks on their owning thread.
        auto stopped = std::async(std::launch::async, [&] { throttle().shutdown(); });
        if(stopped.wait_for(60ms) != std::future_status::ready) {
            requested.signal(); completed.wait(3000);
        }
        stopped.get();
        quit = true; requested.signal(); timer.join();
    }
};
}

TEST_CASE("Throttle shutdown before its first timer tick is terminal", "[throttle-stop][shutdown]") {
    Fixture f;
    f.throttle().shutdown();
    f.tick();
    CHECK_FALSE(f.throttle().getCurThrottling());
    {
        Lock lock(f.throttle().stateCS);
        CHECK(f.throttle().activeWaiter == -1);
        CHECK(f.throttle().halt == -1);
    }
    auto repeated = std::async(std::launch::async, [&] { f.throttle().shutdown(); });
    repeated.wait_for(60ms);
    f.tick();
    repeated.get();
    CHECK_FALSE(f.throttle().getCurThrottling());
}

TEST_CASE("Concurrent throttle stops wait for the owning timer to release waiters", "[throttle-stop][shutdown]") {
    Fixture f; f.tick();
    REQUIRE(f.throttle().getCurThrottling());
    Semaphore entering;
    auto waiter = std::async(std::launch::async, [&] { entering.signal(); f.throttle().waitToken(); });
    REQUIRE(entering.wait(3000)); CHECK(waiter.wait_for(60ms) == std::future_status::timeout);
    auto first = std::async(std::launch::async, [&] { f.throttle().shutdown(); });
    CHECK(first.wait_for(60ms) == std::future_status::timeout);
    auto second = std::async(std::launch::async, [&] { f.throttle().shutdown(); });
    CHECK(second.wait_for(60ms) == std::future_status::timeout);
    CHECK_FALSE(f.throttle().getCurThrottling());
    f.tick(); first.get(); second.get(); waiter.get();
    f.tick(); CHECK_FALSE(f.throttle().getCurThrottling());
}

TEST_CASE("Throttle stop during token refill releases the current timer-owned locks", "[throttle-stop][shutdown]") {
    Fixture f;
    f.throttle().downCS.lock();
    bool refillLocked = true;
    auto release = makeScopedFunctor([&] { if(refillLocked) f.throttle().downCS.unlock(); });
    f.requested.signal();
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    bool active = false;
    while(std::chrono::steady_clock::now() < deadline) {
        { Lock lock(f.throttle().stateCS); active = f.throttle().activeWaiter != -1; }
        if(active) break;
        std::this_thread::yield();
    }
    REQUIRE(active);
    auto stopping = std::async(std::launch::async, [&] { f.throttle().shutdown(); });
    CHECK(stopping.wait_for(60ms) == std::future_status::timeout);
    f.throttle().downCS.unlock(); refillLocked = false;
    REQUIRE(f.completed.wait(3000));
    CHECK(stopping.wait_for(500ms) == std::future_status::ready);
    // Baseline cleanup gets one final owning-thread tick if it still needs it.
    if(stopping.wait_for(0ms) != std::future_status::ready) f.tick();
    stopping.get(); CHECK_FALSE(f.throttle().getCurThrottling());
}
