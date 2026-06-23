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

TEST_CASE("SettingsConnectionHelpers: Shadowsocks 2022 methods are identified exactly", "[qt][settingsconnection][shadowsocks2022]")
{
    REQUIRE(settings_connection::isShadowsocks2022Method(QStringLiteral("2022-blake3-aes-128-gcm")));
    REQUIRE(settings_connection::isShadowsocks2022Method(QStringLiteral("2022-blake3-aes-256-gcm")));
    REQUIRE(settings_connection::isShadowsocks2022Method(QStringLiteral("2022-blake3-chacha20-poly1305")));
    REQUIRE_FALSE(settings_connection::isShadowsocks2022Method(QStringLiteral("aes-256-gcm")));
    REQUIRE_FALSE(settings_connection::isShadowsocks2022Method(QStringLiteral("2022-blake3-aes-128-gcm-extra")));
}

TEST_CASE("SettingsConnectionHelpers: Shadowsocks 2022 validates canonical PSK chains", "[qt][settingsconnection][shadowsocks2022]")
{
    using namespace settings_connection;

    const auto aes128 = validateShadowsocksPassword(
        QStringLiteral("2022-blake3-aes-128-gcm"),
        QStringLiteral("AAECAwQFBgcICQoLDA0ODw=="));
    REQUIRE(aes128.isValid());
    REQUIRE(aes128.expectedBytes == 16);

    const auto aes256Chain = validateShadowsocksPassword(
        QStringLiteral("2022-blake3-aes-256-gcm"),
        QStringLiteral("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=:Hh0cGxoZGBcWFRQTEhEQDw4NDAsKCQgHBgUEAwIBAAA="));
    REQUIRE(aes256Chain.isValid());
    REQUIRE(aes256Chain.expectedBytes == 32);

    const auto legacy = validateShadowsocksPassword(
        QStringLiteral("aes-256-gcm"), QStringLiteral("ordinary password"));
    REQUIRE(legacy.isValid());
    REQUIRE(legacy.expectedBytes == 0);
}

TEST_CASE("SettingsConnectionHelpers: Shadowsocks 2022 reports precise PSK failures", "[qt][settingsconnection][shadowsocks2022]")
{
    using namespace settings_connection;

    const auto empty = validateShadowsocksPassword(
        QStringLiteral("2022-blake3-aes-128-gcm"), QString());
    REQUIRE(empty.error == ShadowsocksPasswordEmpty);
    REQUIRE(empty.segment == 1);

    const auto malformed = validateShadowsocksPassword(
        QStringLiteral("2022-blake3-aes-128-gcm"), QStringLiteral("not base64!"));
    REQUIRE(malformed.error == ShadowsocksPasswordInvalidBase64);
    REQUIRE(malformed.segment == 1);

    const auto nonCanonical = validateShadowsocksPassword(
        QStringLiteral("2022-blake3-aes-128-gcm"), QStringLiteral("AAECAwQFBgcICQoLDA0ODw"));
    REQUIRE(nonCanonical.error == ShadowsocksPasswordInvalidBase64);

    const auto wrongLength = validateShadowsocksPassword(
        QStringLiteral("2022-blake3-aes-256-gcm"),
        QStringLiteral("AAECAwQFBgcICQoLDA0ODw=="));
    REQUIRE(wrongLength.error == ShadowsocksPasswordWrongLength);
    REQUIRE(wrongLength.expectedBytes == 32);
    REQUIRE(wrongLength.segment == 1);

    const auto emptyIdentity = validateShadowsocksPassword(
        QStringLiteral("2022-blake3-chacha20-poly1305"),
        QStringLiteral(":AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="));
    REQUIRE(emptyIdentity.error == ShadowsocksPasswordEmpty);
    REQUIRE(emptyIdentity.segment == 1);
}

TEST_CASE("SettingsConnectionHelpers: Shadowsocks 2022 method and transport survive proxy switching", "[qt][settingsconnection][shadowsocks2022]")
{
    using namespace settings_connection;

    ProxyUiState socks;
    socks.server = QStringLiteral("socks.example.test");
    socks.port = QStringLiteral("1080");

    ProxyUiState shadowsocks;
    shadowsocks.server = QStringLiteral("shadow.example.test");
    shadowsocks.port = QStringLiteral("8388");
    shadowsocks.password = QStringLiteral("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");
    shadowsocks.method = QStringLiteral("2022-blake3-chacha20-poly1305");
    shadowsocks.shadowsocksTransport = ShadowsocksTransportTcpAndUdp;

    int currentMode = ProxyUiShadowsocks;
    const ProxyUiState visibleShadow = shadowsocks;
    const ProxyUiState shownSocks = switchProxyUiState(
        socks, shadowsocks, currentMode, ProxyUiSocks5, visibleShadow);
    REQUIRE(shownSocks.server == socks.server);

    const ProxyUiState restored = switchProxyUiState(
        socks, shadowsocks, currentMode, ProxyUiShadowsocks, shownSocks);
    REQUIRE(restored.method == QStringLiteral("2022-blake3-chacha20-poly1305"));
    REQUIRE(restored.password == shadowsocks.password);
    REQUIRE(restored.shadowsocksTransport == ShadowsocksTransportTcpAndUdp);
}
