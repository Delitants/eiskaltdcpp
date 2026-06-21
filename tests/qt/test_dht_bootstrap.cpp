#include <catch2/catch_test_macros.hpp>

#include "dht/stdafx.h"
#include "dht/BootstrapManager.h"
#include "dht/UDPSocket.h"

using namespace dht;

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
