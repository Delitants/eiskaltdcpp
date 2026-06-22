#include <catch2/catch_test_macros.hpp>

#include "dht/stdafx.h"
#include "dht/BootstrapManager.h"
#include "dht/DHT.h"
#include "dht/UDPSocket.h"
#include "dcpp/ConnectivityManager.h"
#include "dcpp/HttpConnection.h"
#include "dcpp/SettingsManager.h"

using namespace dht;

TEST_CASE("DHT startup policy separates incoming mode from proxy UDP capability", "[qt][dht][connectivity]")
{
    using dcpp::ConnectivityManager;
    using dcpp::SettingsManager;

    REQUIRE(ConnectivityManager::shouldStartDht(true, SettingsManager::OUTGOING_DIRECT, false));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(false, SettingsManager::OUTGOING_DIRECT, false));
    REQUIRE(ConnectivityManager::shouldStartDht(true, SettingsManager::OUTGOING_SOCKS5, true));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, SettingsManager::OUTGOING_SOCKS5, false));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(false, SettingsManager::OUTGOING_SOCKS5, true));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, SettingsManager::OUTGOING_SHADOWSOCKS, false));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, SettingsManager::OUTGOING_SHADOWSOCKS, true));
}

TEST_CASE("DHT bootstrap HTTP transport follows the configured outgoing proxy", "[qt][dht][bootstrap][proxy]")
{
    using dcpp::HttpConnection;
    using dcpp::SettingsManager;

    REQUIRE_FALSE(HttpConnection::shouldUseOutgoingProxy(false, SettingsManager::OUTGOING_DIRECT));
    REQUIRE(HttpConnection::shouldUseOutgoingProxy(false, SettingsManager::OUTGOING_SOCKS5));
    REQUIRE(HttpConnection::shouldUseOutgoingProxy(false, SettingsManager::OUTGOING_SHADOWSOCKS));
    REQUIRE_FALSE(HttpConnection::shouldUseOutgoingProxy(true, SettingsManager::OUTGOING_DIRECT));
}

TEST_CASE("Explicit DHT bootstrap URLs are trimmed and preserved", "[qt][dht][bootstrap]")
{
    const auto servers = BootstrapManager::parseServers(
        " https://dht.hublist.eu/dcDHT.php ; https://backup.example/dht ; "
        "https://dht.hublist.eu/dcDHT.php ");

    REQUIRE(servers.size() == 2);
    REQUIRE(servers[0] == "https://dht.hublist.eu/dcDHT.php");
    REQUIRE(servers[1] == "https://backup.example/dht");
}

TEST_CASE("DHT listen and advertised relay ports remain separate", "[qt][dht][bootstrap]")
{
    const string listenPort = "6250";
    const string relayPort = "49152";

    REQUIRE(UDPSocket::selectPort(listenPort, relayPort, false) == listenPort);
    REQUIRE(UDPSocket::selectPort(listenPort, relayPort, true) == relayPort);
}

TEST_CASE("DHT firewall check retains its advertised port for command and response processing", "[qt][dht][firewall]")
{
    const string listenPort = "6250";
    const string relayPort = "49152";
    FirewallCheckCycle cycle;
    cycle.begin(listenPort, relayPort);

    const std::array<string, 3> peers = { "192.0.2.1", "192.0.2.2", "192.0.2.3" };
    for(const auto& peer : peers) {
        AdcCommand command(AdcCommand::CMD_INF, AdcCommand::TYPE_UDP);
        REQUIRE(cycle.appendRequest(command, peer));

        string commandPort;
        REQUIRE(command.getParam("FW", 0, commandPort));
        REQUIRE(commandPort == relayPort);
    }

    FirewallCheckCycle::Result result;
    result = cycle.recordResponse(peers[0], "198.51.100.10", relayPort);
    REQUIRE_FALSE(result.complete);
    result = cycle.recordResponse(peers[1], "198.51.100.10", relayPort);
    REQUIRE_FALSE(result.complete);
    result = cycle.recordResponse(peers[2], "198.51.100.10", relayPort);
    REQUIRE(result.complete);
    REQUIRE_FALSE(result.firewalled);
    REQUIRE(result.externalIp == "198.51.100.10");

    REQUIRE(UDPSocket::selectPort(listenPort, relayPort, false) == listenPort);

    AdcCommand completedCommand(AdcCommand::CMD_INF, AdcCommand::TYPE_UDP);
    REQUIRE_FALSE(cycle.appendRequest(completedCommand, "192.0.2.4"));

    const string nextRelayPort = "49153";
    cycle.begin(listenPort, nextRelayPort);
    AdcCommand nextCommand(AdcCommand::CMD_INF, AdcCommand::TYPE_UDP);
    REQUIRE(cycle.appendRequest(nextCommand, "192.0.2.4"));
    string nextCommandPort;
    REQUIRE(nextCommand.getParam("FW", 0, nextCommandPort));
    REQUIRE(nextCommandPort == nextRelayPort);

    cycle.stop();
    AdcCommand stoppedCommand(AdcCommand::CMD_INF, AdcCommand::TYPE_UDP);
    REQUIRE_FALSE(cycle.appendRequest(stoppedCommand, "192.0.2.5"));
}

TEST_CASE("DHT firewall check falls back to the local port without a relay", "[qt][dht][firewall]")
{
    FirewallCheckCycle cycle;
    cycle.begin("6250", "");

    AdcCommand command(AdcCommand::CMD_INF, AdcCommand::TYPE_UDP);
    REQUIRE(cycle.appendRequest(command, "192.0.2.1"));

    string commandPort;
    REQUIRE(command.getParam("FW", 0, commandPort));
    REQUIRE(commandPort == "6250");
}
