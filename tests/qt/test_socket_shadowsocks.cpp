#include <catch2/catch_test_macros.hpp>

#include "tests/TestContext.h"

#include "dcpp/SettingsManager.h"
#include "dcpp/Socket.h"

#include <cstdlib>
#include <string>

using namespace dcpp;

namespace {

const char* envOrNull(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

}

TEST_CASE("SOCKS5 UDP request includes the destination address and port", "[qt][socket][socks5][udp]")
{
    const uint8_t payload[] = { 0xde, 0xad, 0xbe, 0xef };
    ByteVector packet;

    REQUIRE(Socket::encodeSocks5UdpPacket("dht.example", "6250", payload, sizeof(payload), true, packet));

    const ByteVector expected = {
        0x00, 0x00, 0x00,
        0x03, 0x0b,
        'd', 'h', 't', '.', 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x18, 0x6a,
        0xde, 0xad, 0xbe, 0xef
    };
    REQUIRE(packet == expected);
}

TEST_CASE("SOCKS5 UDP reply exposes the original sender and payload", "[qt][socket][socks5][udp]")
{
    const ByteVector packet = {
        0x00, 0x00, 0x00,
        0x01, 0xc0, 0x00, 0x02, 0x2a,
        0x18, 0x6a,
        'D', 'H', 'T'
    };
    sockaddr_storage remote = {};
    size_t payloadOffset = 0;

    REQUIRE(Socket::decodeSocks5UdpPacket(packet.data(), packet.size(), remote, payloadOffset));
    REQUIRE(payloadOffset == 10);
    REQUIRE(remote.ss_family == AF_INET);

    const auto* remote4 = reinterpret_cast<const sockaddr_in*>(&remote);
    REQUIRE(ntohl(remote4->sin_addr.s_addr) == 0xc000022a);
    REQUIRE(ntohs(remote4->sin_port) == 6250);
    REQUIRE(ByteVector(packet.begin() + payloadOffset, packet.end()) == ByteVector{ 'D', 'H', 'T' });
}

TEST_CASE("SOCKS5 UDP fragmented replies are rejected", "[qt][socket][socks5][udp]")
{
    const ByteVector packet = {
        0x00, 0x00, 0x01,
        0x01, 0xc0, 0x00, 0x02, 0x2a,
        0x18, 0x6a,
        'D', 'H', 'T'
    };
    sockaddr_storage remote = {};
    size_t payloadOffset = 0;

    REQUIRE_FALSE(Socket::decodeSocks5UdpPacket(packet.data(), packet.size(), remote, payloadOffset));
}

TEST_CASE("SOCKS5 UDP maps an IPv4 relay for an IPv6 socket", "[qt][socket][socks5][udp]")
{
    vector<sockaddr_storage> endpoints;

    REQUIRE(Socket::resolveUdpEndpoint("127.0.0.1", "6250", AF_INET6, endpoints));
    REQUIRE_FALSE(endpoints.empty());
    REQUIRE(endpoints.front().ss_family == AF_INET6);

    const auto* endpoint = reinterpret_cast<const sockaddr_in6*>(&endpoints.front());
    REQUIRE(IN6_IS_ADDR_V4MAPPED(&endpoint->sin6_addr));
    REQUIRE(ntohs(endpoint->sin6_port) == 6250);
#ifdef AI_V4MAPPED
    REQUIRE((Socket::udpResolverFlags(AF_INET6) & AI_V4MAPPED) != 0);
#endif
    REQUIRE(Socket::matchesUdpEndpoint("127.0.0.1", "6250", endpoints.front()));
}

TEST_CASE("Socket SOCKS5 UDP relay completes an opt-in DNS round trip", "[qt][socket][socks5][udp][integration]")
{
    const char* server = envOrNull("EISKALT_TEST_SOCKS5_SERVER");
    const char* portText = envOrNull("EISKALT_TEST_SOCKS5_PORT");
    const char* user = envOrNull("EISKALT_TEST_SOCKS5_USER");
    const char* password = envOrNull("EISKALT_TEST_SOCKS5_PASSWORD");

    if(!server || !portText) {
        SKIP("Set EISKALT_TEST_SOCKS5_SERVER and PORT to run this integration test");
    }

    test::TestContext tc;
    SettingsManager* settings = tc.ownedCtx->getSettingsManager();
    settings->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SOCKS5);
    settings->set(SettingsManager::SOCKS_SERVER, std::string(server));
    settings->set(SettingsManager::SOCKS_PORT, Util::toInt(portText));
    settings->set(SettingsManager::SOCKS_USER, std::string(user ? user : ""));
    settings->set(SettingsManager::SOCKS_PASSWORD, std::string(password ? password : ""));
    settings->set(SettingsManager::SOCKS_RESOLVE, true);

    Socket::socksUpdated(*tc.ownedCtx);

    std::string relayHost;
    std::string relayPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
    REQUIRE_FALSE(relayHost.empty());
    REQUIRE(Util::toInt(relayPort) > 0);

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "0.0.0.0");

    const uint8_t query[] = {
        0x51, 0x7a, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01
    };
    socket.writeTo("1.1.1.1", "53", query, sizeof(query), true);
    REQUIRE(socket.wait(8000, Socket::WAIT_READ) == Socket::WAIT_READ);

    uint8_t reply[512] = {};
    sockaddr_storage remote = {};
    const int len = socket.read(reply, sizeof(reply), remote);
    REQUIRE(len >= 12);
    REQUIRE(reply[0] == query[0]);
    REQUIRE(reply[1] == query[1]);
    REQUIRE(remote.ss_family == AF_INET);
    REQUIRE(ntohs(reinterpret_cast<const sockaddr_in*>(&remote)->sin_port) == 53);

    settings->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
    Socket::socksUpdated(*tc.ownedCtx);
}

TEST_CASE("Socket Shadowsocks proxy connects to an opt-in test server", "[qt][socket][shadowsocks][integration]")
{
    const char* server = envOrNull("EISKALT_TEST_SHADOWSOCKS_SERVER");
    const char* portText = envOrNull("EISKALT_TEST_SHADOWSOCKS_PORT");
    const char* password = envOrNull("EISKALT_TEST_SHADOWSOCKS_PASSWORD");
    const char* method = envOrNull("EISKALT_TEST_SHADOWSOCKS_METHOD");

    if(!server || !portText || !password) {
        SKIP("Set EISKALT_TEST_SHADOWSOCKS_SERVER, PORT, and PASSWORD to run this integration test");
    }

    test::TestContext tc;
    SettingsManager* settings = tc.ownedCtx->getSettingsManager();
    settings->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SHADOWSOCKS);
    settings->set(SettingsManager::SHADOWSOCKS_SERVER, std::string(server));
    settings->set(SettingsManager::SHADOWSOCKS_PORT, Util::toInt(portText));
    settings->set(SettingsManager::SHADOWSOCKS_PASSWORD, std::string(password));
    settings->set(SettingsManager::SHADOWSOCKS_METHOD, std::string(method ? method : "aes-256-gcm"));
    settings->set(SettingsManager::SOCKS_RESOLVE, true);

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.proxyConnect("example.com", "80", 8000);

    const std::string request = "HEAD / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    socket.writeAll(request.data(), static_cast<int>(request.size()), 8000);

    REQUIRE(socket.wait(8000, Socket::WAIT_READ) == Socket::WAIT_READ);

    char reply[16] = {};
    const int read = socket.read(reply, sizeof(reply));
    REQUIRE(read > 0);
}

TEST_CASE("Shadowsocks proxy endpoint advertised to ADC hubs must be public", "[qt][socket][shadowsocks]")
{
    REQUIRE_FALSE(Util::isPublicIp("192.168.4.71"));
    REQUIRE_FALSE(Util::isPublicIp("10.0.0.5"));
    REQUIRE_FALSE(Util::isPublicIp("172.16.0.10"));
    REQUIRE_FALSE(Util::isPublicIp("127.0.0.1"));
    REQUIRE(Util::isPublicIp("8.8.8.8"));
}

TEST_CASE("Shadowsocks ADC advertisement prefers proxy observed public IP", "[qt][socket][shadowsocks]")
{
    REQUIRE(Util::firstPublicIp(StringList{ "192.168.4.71", "8.8.8.8" }) == "8.8.8.8");
    REQUIRE(Util::firstPublicIp(StringList{ "203.0.113.10", "8.8.8.8" }) == "8.8.8.8");
    REQUIRE(Util::firstPublicIp(StringList{ "147.81.150.184", "192.168.4.71" }) == "147.81.150.184");
    REQUIRE(Util::firstPublicIp(StringList{ "192.168.4.71", "10.0.0.5" }).empty());
}

TEST_CASE("External IP responses can be parsed for proxy ADC advertisement", "[qt][socket][shadowsocks]")
{
    REQUIRE(Util::firstPublicIpFromText("<html><body>Current IP Address: 8.8.8.8</body></html>") == "8.8.8.8");
    REQUIRE(Util::firstPublicIpFromText("1.1.1.1\n") == "1.1.1.1");
    REQUIRE(Util::firstPublicIpFromText("local 192.168.4.71 public 9.9.9.9") == "9.9.9.9");
    REQUIRE(Util::firstPublicIpFromText("version 2.5.4 public 8.8.4.4") == "8.8.4.4");
    REQUIRE(Util::firstPublicIpFromText("local 192.168.4.71 only").empty());
}
