#include <catch2/catch_test_macros.hpp>

#include "dcpp/stdinc.h"
#include "dcpp/Client.h"

using dcpp::ReconnectPolicy;

TEST_CASE("Automatic hub reconnect waits for its full delay", "[qt][reconnect]")
{
    constexpr uint64_t lastAttempt = 10'000;
    constexpr uint32_t delaySeconds = 30;

    REQUIRE_FALSE(ReconnectPolicy::due(true, true, false,
        lastAttempt + delaySeconds * 1000 - 1, lastAttempt, delaySeconds));
    REQUIRE(ReconnectPolicy::due(true, true, false,
        lastAttempt + delaySeconds * 1000, lastAttempt, delaySeconds));
    REQUIRE(ReconnectPolicy::due(true, true, false,
        lastAttempt + delaySeconds * 1000 + 1, lastAttempt, delaySeconds));
}

TEST_CASE("Automatic hub reconnect requires a disconnected enabled idle client", "[qt][reconnect]")
{
    REQUIRE_FALSE(ReconnectPolicy::due(false, true, false, 20'000, 10'000, 1));
    REQUIRE_FALSE(ReconnectPolicy::due(true, false, false, 20'000, 10'000, 1));
    REQUIRE_FALSE(ReconnectPolicy::due(true, true, true, 20'000, 10'000, 1));
}

TEST_CASE("Manual zero-delay reconnect remains single-flight", "[qt][reconnect]")
{
    constexpr uint64_t now = 20'000;
    REQUIRE(ReconnectPolicy::due(true, true, false, now, now, 0));
    REQUIRE_FALSE(ReconnectPolicy::due(true, true, true, now, now, 0));
}
