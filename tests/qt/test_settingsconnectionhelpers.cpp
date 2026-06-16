#include <catch2/catch_test_macros.hpp>

#include "SettingsConnectionHelpers.h"

TEST_CASE("SettingsConnectionHelpers: bind address options keep default, discovered, and current addresses once", "[qt][settingsconnection]")
{
    const QStringList options = settings_connection::bindAddressOptions(
        QStringLiteral("0.0.0.0"),
        QStringList{
            QStringLiteral("192.0.2.10"),
            QStringLiteral("0.0.0.0"),
            QStringLiteral("192.0.2.10"),
            QStringLiteral("198.51.100.5")
        },
        QStringLiteral("203.0.113.7"));

    REQUIRE(options == QStringList{
        QStringLiteral("0.0.0.0"),
        QStringLiteral("192.0.2.10"),
        QStringLiteral("198.51.100.5"),
        QStringLiteral("203.0.113.7")
    });
}

TEST_CASE("SettingsConnectionHelpers: empty current bind address does not add a blank option", "[qt][settingsconnection]")
{
    const QStringList options = settings_connection::bindAddressOptions(
        QStringLiteral("::"),
        QStringList{QStringLiteral("::1")},
        QString());

    REQUIRE(options == QStringList{QStringLiteral("::"), QStringLiteral("::1")});
}

TEST_CASE("SettingsConnectionHelpers: proxy form values are remembered per proxy type", "[qt][settingsconnection]")
{
    settings_connection::ProxyUiState socks;
    socks.server = QStringLiteral("socks.example.test");
    socks.port = QStringLiteral("1080");
    socks.user = QStringLiteral("alice");
    socks.password = QStringLiteral("socks-secret");

    settings_connection::ProxyUiState shadowsocks;
    shadowsocks.server = QStringLiteral("shadow.example.test");
    shadowsocks.port = QStringLiteral("8388");
    shadowsocks.password = QStringLiteral("shadow-secret");
    shadowsocks.method = QStringLiteral("aes-256-gcm");

    int currentMode = settings_connection::ProxyUiSocks5;

    settings_connection::ProxyUiState editedSocks = socks;
    editedSocks.server = QStringLiteral("edited-socks.example.test");
    editedSocks.password = QStringLiteral("edited-socks-secret");

    const settings_connection::ProxyUiState shownShadow = settings_connection::switchProxyUiState(
        socks,
        shadowsocks,
        currentMode,
        settings_connection::ProxyUiShadowsocks,
        editedSocks);

    REQUIRE(currentMode == settings_connection::ProxyUiShadowsocks);
    REQUIRE(socks.server == QStringLiteral("edited-socks.example.test"));
    REQUIRE(socks.password == QStringLiteral("edited-socks-secret"));
    REQUIRE(shownShadow.server == QStringLiteral("shadow.example.test"));
    REQUIRE(shownShadow.password == QStringLiteral("shadow-secret"));

    settings_connection::ProxyUiState editedShadow = shownShadow;
    editedShadow.server = QStringLiteral("edited-shadow.example.test");

    const settings_connection::ProxyUiState restoredSocks = settings_connection::switchProxyUiState(
        socks,
        shadowsocks,
        currentMode,
        settings_connection::ProxyUiSocks5,
        editedShadow);

    REQUIRE(currentMode == settings_connection::ProxyUiSocks5);
    REQUIRE(shadowsocks.server == QStringLiteral("edited-shadow.example.test"));
    REQUIRE(restoredSocks.server == QStringLiteral("edited-socks.example.test"));
    REQUIRE(restoredSocks.password == QStringLiteral("edited-socks-secret"));
}

TEST_CASE("SettingsConnectionHelpers: proxy transport options are remembered per proxy type", "[qt][settingsconnection]")
{
    settings_connection::ProxyUiState socks;
    socks.server = QStringLiteral("socks.example.test");
    socks.port = QStringLiteral("1080");
    socks.useTls = true;

    settings_connection::ProxyUiState shadowsocks;
    shadowsocks.server = QStringLiteral("shadow.example.test");
    shadowsocks.port = QStringLiteral("8388");
    shadowsocks.shadowsocksTransport = settings_connection::ShadowsocksTransportTcpAndUdp;

    int currentMode = settings_connection::ProxyUiSocks5;

    settings_connection::ProxyUiState editedSocks = socks;
    editedSocks.useTls = false;

    const settings_connection::ProxyUiState shownShadow = settings_connection::switchProxyUiState(
        socks,
        shadowsocks,
        currentMode,
        settings_connection::ProxyUiShadowsocks,
        editedSocks);

    REQUIRE(socks.useTls == false);
    REQUIRE(shownShadow.shadowsocksTransport == settings_connection::ShadowsocksTransportTcpAndUdp);

    settings_connection::ProxyUiState editedShadow = shownShadow;
    editedShadow.shadowsocksTransport = settings_connection::ShadowsocksTransportTcpOnly;

    const settings_connection::ProxyUiState restoredSocks = settings_connection::switchProxyUiState(
        socks,
        shadowsocks,
        currentMode,
        settings_connection::ProxyUiSocks5,
        editedShadow);

    REQUIRE(shadowsocks.shadowsocksTransport == settings_connection::ShadowsocksTransportTcpOnly);
    REQUIRE(restoredSocks.useTls == false);
}

TEST_CASE("SettingsConnectionHelpers: Shadowsocks UDP relay is controlled by transport", "[qt][settingsconnection]")
{
    REQUIRE_FALSE(settings_connection::shadowsocksUsesUdp(settings_connection::ShadowsocksTransportTcpOnly));
    REQUIRE(settings_connection::shadowsocksUsesUdp(settings_connection::ShadowsocksTransportTcpAndUdp));
}
