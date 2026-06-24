#include <catch2/catch_test_macros.hpp>

#include "DiagnosticSignalPolicy.h"

TEST_CASE("macOS preserves native fatal-signal crash reporting", "[qt][diagnostics]")
{
#if defined(Q_OS_MACOS)
    REQUIRE_FALSE(diagnostic_log::shouldInstallFatalSignalHandlers());
#else
    REQUIRE(diagnostic_log::shouldInstallFatalSignalHandlers());
#endif
}
