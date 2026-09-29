#include <catch2/catch_test_macros.hpp>
#include "MacGuiConfigCompatibility.h"

TEST_CASE("Modern Mac GUI settings survive the v3 application version change", "[qt][config-compatibility]")
{
    REQUIRE_FALSE(mac_gui_config::needsMigration(QStringLiteral("2.5.6")));
    REQUIRE_FALSE(mac_gui_config::needsMigration(QStringLiteral("3.0.0")));
}

TEST_CASE("Mac GUI compatibility uses a schema marker instead of an application release", "[qt][config-compatibility]")
{
    REQUIRE(mac_gui_config::compatibilityVersion() == QStringLiteral("qt6-native-palette-v1"));
    REQUIRE_FALSE(mac_gui_config::needsMigration(mac_gui_config::compatibilityVersion()));
}

TEST_CASE("Unmigrated legacy Mac GUI settings still require the existing migration", "[qt][config-compatibility]")
{
    REQUIRE(mac_gui_config::needsMigration(QString()));
    REQUIRE(mac_gui_config::needsMigration(QStringLiteral("2.5.4-v3-pre.3")));
    REQUIRE(mac_gui_config::needsMigration(QStringLiteral("invalid-marker")));
}
