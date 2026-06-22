#include <catch2/catch_test_macros.hpp>

#include "dcpp/stdinc.h"
#include "dcpp/Client.h"

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

using dcpp::ReconnectAttemptGate;
using dcpp::ReconnectPolicy;
using dcpp::ReconnectAttemptRollback;

TEST_CASE("Only one concurrent hub reconnect attempt can begin", "[qt][reconnect]")
{
    ReconnectAttemptGate gate;
    std::atomic<unsigned> ready { 0 };
    std::atomic<unsigned> winners { 0 };
    std::atomic<bool> start { false };
    std::vector<std::thread> callers;

    constexpr unsigned callerCount = 32;
    callers.reserve(callerCount);
    for(unsigned i = 0; i < callerCount; ++i) {
        callers.emplace_back([&] {
            ready.fetch_add(1, std::memory_order_release);
            while(!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if(gate.tryBegin()) {
                winners.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    while(ready.load(std::memory_order_acquire) != callerCount) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for(auto& caller : callers) {
        caller.join();
    }

    REQUIRE(winners.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("A terminal reconnect result releases the attempt gate", "[qt][reconnect]")
{
    ReconnectAttemptGate gate;

    REQUIRE(gate.tryBegin());
    REQUIRE_FALSE(gate.tryBegin());

    gate.complete();
    REQUIRE(gate.tryBegin());
}

TEST_CASE("Synchronous reconnect setup failure restores the attempt gate", "[qt][reconnect]")
{
    ReconnectAttemptGate gate;

    REQUIRE(gate.tryBegin());
    {
        ReconnectAttemptRollback rollback(gate);
    }

    REQUIRE(gate.tryBegin());
}

TEST_CASE("Successful reconnect setup keeps the attempt active until a terminal result", "[qt][reconnect]")
{
    ReconnectAttemptGate gate;

    REQUIRE(gate.tryBegin());
    {
        ReconnectAttemptRollback rollback(gate);
        rollback.dismiss();
    }

    REQUIRE_FALSE(gate.tryBegin());

    gate.complete();
    REQUIRE(gate.tryBegin());
}

TEST_CASE("Duplicate manual reconnect requests schedule only once", "[qt][reconnect]")
{
    ReconnectAttemptGate gate;
    constexpr uint64_t now = 20'000;
    constexpr uint64_t lastAttempt = 20'000;
    constexpr uint32_t configuredDelay = 30;

    REQUIRE(gate.requestManual());
    REQUIRE_FALSE(gate.requestManual());
    REQUIRE(gate.tryBeginScheduled(true, true, now, lastAttempt, configuredDelay));

    gate.complete();
    REQUIRE_FALSE(gate.tryBeginScheduled(true, true, now, lastAttempt, configuredDelay));
}

TEST_CASE("An active reconnect attempt suppresses manual and timer starts", "[qt][reconnect]")
{
    ReconnectAttemptGate gate;

    REQUIRE(gate.tryBegin());
    REQUIRE_FALSE(gate.requestManual());
    REQUIRE_FALSE(gate.tryBeginScheduled(true, true, 20'000, 10'000, 1));
}

TEST_CASE("Automatic hub reconnect observes the exact deadline", "[qt][reconnect]")
{
    constexpr uint64_t lastAttempt = 10'000;
    constexpr uint32_t delaySeconds = 30;
    constexpr uint64_t deadline = lastAttempt + static_cast<uint64_t>(delaySeconds) * 1000;

    REQUIRE_FALSE(ReconnectPolicy::due(true, true, deadline - 1, lastAttempt, delaySeconds));
    REQUIRE(ReconnectPolicy::due(true, true, deadline, lastAttempt, delaySeconds));
    REQUIRE(ReconnectPolicy::due(true, true, deadline + 1, lastAttempt, delaySeconds));
}

TEST_CASE("Automatic hub reconnect requires a disconnected enabled client", "[qt][reconnect]")
{
    REQUIRE_FALSE(ReconnectPolicy::due(false, true, 20'000, 10'000, 1));
    REQUIRE_FALSE(ReconnectPolicy::due(true, false, 20'000, 10'000, 1));
    REQUIRE_FALSE(ReconnectPolicy::due(true, true, 9'999, 10'000, 1));
}

TEST_CASE("Reconnect delay arithmetic remains 64 bit", "[qt][reconnect]")
{
    constexpr uint64_t lastAttempt = uint64_t { 1 } << 40;
    constexpr uint32_t delaySeconds = std::numeric_limits<uint32_t>::max();
    constexpr uint64_t delayMilliseconds = static_cast<uint64_t>(delaySeconds) * 1000;

    REQUIRE_FALSE(ReconnectPolicy::due(true, true,
        lastAttempt + delayMilliseconds - 1, lastAttempt, delaySeconds));
    REQUIRE(ReconnectPolicy::due(true, true,
        lastAttempt + delayMilliseconds, lastAttempt, delaySeconds));
}

TEST_CASE("Reset clears queued manual reconnects and active attempts", "[qt][reconnect]")
{
    ReconnectAttemptGate gate;

    REQUIRE(gate.requestManual());
    gate.reset();

    REQUIRE_FALSE(gate.tryBeginScheduled(true, true, 20'000, 20'000, 30));
    REQUIRE(gate.tryBegin());

    gate.reset();
    REQUIRE(gate.tryBegin());
}
