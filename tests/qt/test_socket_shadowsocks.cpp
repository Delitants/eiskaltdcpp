#include <catch2/catch_test_macros.hpp>

#include "tests/TestContext.h"

#include "dcpp/SettingsManager.h"
#include "dcpp/Socket.h"

#include <cstdlib>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace dcpp;

namespace {

const char* envOrNull(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

class LocalSocks5UdpServer
{
public:
    explicit LocalSocks5UdpServer(bool wildcardReply = false) : wildcardReply(wildcardReply)
    {
        udpRelay.create(Socket::TYPE_UDP, AF_INET);
        udpRelay.bind("0", "127.0.0.1");

        listener.create(Socket::TYPE_TCP, AF_INET);
        listener.setSocketOpt(SO_REUSEADDR, 1);
        listener.bind("0", "127.0.0.1");
        listener.listen();
        worker = std::thread([this] { run(); });
    }

    ~LocalSocks5UdpServer()
    {
        stopping = true;
        releaseBlockedAssociation();
        closeControls();
        if(worker.joinable()) {
            worker.join();
        }
    }

    std::string port() { return listener.getLocalPort(); }
    std::string relayPort() { return udpRelay.getLocalPort(); }
    size_t associationCount() const { return associations.load(); }

    bool waitForAssociations(size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(eventMutex);
        return eventChanged.wait_for(lock, timeout, [this, count] { return associations.load() >= count; });
    }

    void closeControls()
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        controls.clear();
    }

    void blockNextAssociation()
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        blockAssociation = associations.load() + 1;
        associationBlocked = false;
    }

    bool waitUntilAssociationBlocked(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(eventMutex);
        return eventChanged.wait_for(lock, timeout, [this] { return associationBlocked; });
    }

    void releaseBlockedAssociation()
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        blockAssociation = 0;
        eventChanged.notify_all();
    }

    void sendUdp(const std::string& destinationPort, const ByteVector& packet)
    {
        udpRelay.writeTo("127.0.0.1", destinationPort, packet.data(), static_cast<int>(packet.size()), false);
    }

private:
    static bool readExact(Socket& socket, void* data, size_t length)
    {
        auto* bytes = static_cast<uint8_t*>(data);
        size_t offset = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while(offset < length && std::chrono::steady_clock::now() < deadline) {
            if(socket.wait(50, Socket::WAIT_READ) != Socket::WAIT_READ) {
                continue;
            }
            const int read = socket.read(bytes + offset, static_cast<int>(length - offset));
            if(read <= 0) {
                return false;
            }
            offset += static_cast<size_t>(read);
        }
        return offset == length;
    }

    bool handshake(Socket& control)
    {
        uint8_t greeting[3] = {};
        if(!readExact(control, greeting, sizeof(greeting)) ||
                greeting[0] != 5 || greeting[1] != 1 || greeting[2] != 0) {
            return false;
        }

        const uint8_t authReply[2] = { 5, 0 };
        control.writeAll(authReply, sizeof(authReply), 2000);

        uint8_t request[10] = {};
        if(!readExact(control, request, sizeof(request)) || request[0] != 5 || request[1] != 3) {
            return false;
        }

        const size_t count = ++associations;
        {
            std::unique_lock<std::mutex> lock(eventMutex);
            eventChanged.notify_all();
            if(blockAssociation == count) {
                associationBlocked = true;
                eventChanged.notify_all();
                eventChanged.wait_for(lock, std::chrono::seconds(2), [this] {
                    return stopping.load() || blockAssociation == 0;
                });
            }
        }

        const uint16_t portValue = htons(static_cast<uint16_t>(Util::toInt(relayPort())));
        const uint8_t* portBytes = reinterpret_cast<const uint8_t*>(&portValue);
        const ByteVector reply = {
            5, 0, 0, 1,
            static_cast<uint8_t>(wildcardReply ? 0 : 127), 0, 0, static_cast<uint8_t>(wildcardReply ? 0 : 1),
            portBytes[0], portBytes[1]
        };
        control.writeAll(reply.data(), static_cast<int>(reply.size()), 2000);
        return true;
    }

    void run()
    {
        while(!stopping) {
            try {
                if(listener.wait(50, Socket::WAIT_READ) != Socket::WAIT_READ) {
                    continue;
                }

                auto control = std::make_unique<Socket>();
                control->accept(listener);
                if(!handshake(*control)) {
                    continue;
                }

                std::lock_guard<std::mutex> lock(controlMutex);
                controls.push_back(std::move(control));
            } catch(const SocketException&) {
                if(!stopping) {
                    protocolFailed = true;
                }
            }
        }
    }

    const bool wildcardReply;
    Socket listener;
    Socket udpRelay;
    std::atomic<bool> stopping { false };
    std::atomic<bool> protocolFailed { false };
    std::atomic<size_t> associations { 0 };
    std::thread worker;
    std::mutex controlMutex;
    std::vector<std::unique_ptr<Socket>> controls;
    std::mutex eventMutex;
    std::condition_variable eventChanged;
    size_t blockAssociation = 0;
    bool associationBlocked = false;
};

class SocksSettingsScope
{
public:
    explicit SocksSettingsScope(DCContext& context) : context(context) { }

    ~SocksSettingsScope()
    {
        context.getSettingsManager()->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
        Socket::socksUpdated(context);
    }

private:
    DCContext& context;
};

void configureLocalSocks(DCContext& context, const std::string& host, const std::string& port)
{
    auto* settings = context.getSettingsManager();
    settings->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
    Socket::socksUpdated(context);
    settings->set(SettingsManager::SOCKS_SERVER, host);
    settings->set(SettingsManager::SOCKS_PORT, Util::toInt(port));
    settings->set(SettingsManager::SOCKS_USER, std::string());
    settings->set(SettingsManager::SOCKS_PASSWORD, std::string());
    settings->set(SettingsManager::SOCKS_TLS, false);
    settings->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SOCKS5);
}

}

TEST_CASE("SOCKS5 UDP control closure triggers a new association", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    std::string relayHost;
    std::string relayPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
    REQUIRE(proxy.waitForAssociations(1, std::chrono::seconds(2)));

    proxy.closeControls();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(proxy.associationCount() < 2 && std::chrono::steady_clock::now() < deadline) {
        Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(proxy.associationCount() == 2);
    REQUIRE(relayHost == "127.0.0.1");
    REQUIRE(relayPort == proxy.relayPort());
}

TEST_CASE("SOCKS5 UDP wildcard relay uses the established control peer", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy(true);
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "localhost", proxy.port());

    std::string relayHost;
    std::string relayPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
    REQUIRE(relayHost == "127.0.0.1");
    REQUIRE(relayPort == proxy.relayPort());
}

TEST_CASE("SOCKS5 UDP malformed and fragmented replies are dropped without closing the socket", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    std::string relayHost;
    std::string relayPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");

    const ByteVector malformed = { 0, 0, 0, 1, 127 };
    proxy.sendUdp(socket.getLocalPort(), malformed);
    REQUIRE(socket.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);

    uint8_t buffer[64] = {};
    sockaddr_storage remote = {};
    int received = -1;
    CHECK_NOTHROW(received = socket.read(buffer, sizeof(buffer), remote));
    CHECK(received == 0);

    const ByteVector fragmented = {
        0, 0, 1,
        1, 192, 0, 2, 42,
        0x18, 0x6a,
        'D', 'H', 'T'
    };
    proxy.sendUdp(socket.getLocalPort(), fragmented);
    REQUIRE(socket.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);
    received = -1;
    CHECK_NOTHROW(received = socket.read(buffer, sizeof(buffer), remote));
    CHECK(received == 0);

    const ByteVector valid = {
        0, 0, 0,
        1, 192, 0, 2, 42,
        0x18, 0x6a,
        'D', 'H', 'T'
    };
    proxy.sendUdp(socket.getLocalPort(), valid);
    REQUIRE(socket.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);
    REQUIRE(socket.read(buffer, sizeof(buffer), remote) == 3);
    REQUIRE(ByteVector(buffer, buffer + 3) == ByteVector{ 'D', 'H', 'T' });
    REQUIRE(socket.getFamily() == AF_INET);
}

TEST_CASE("Concurrent SOCKS5 UDP relay requests publish one association", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    constexpr size_t requestCount = 8;
    std::atomic<bool> start { false };
    std::atomic<size_t> completed { 0 };
    std::vector<std::thread> threads;
    threads.reserve(requestCount);
    for(size_t i = 0; i < requestCount; ++i) {
        threads.emplace_back([&] {
            while(!start.load()) {
                std::this_thread::yield();
            }
            std::string relayHost;
            std::string relayPort;
            if(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort)) {
                ++completed;
            }
        });
    }

    start = true;
    for(auto& thread : threads) {
        thread.join();
    }

    REQUIRE(completed == requestCount);
    REQUIRE(proxy.associationCount() == 1);
}

TEST_CASE("SOCKS5 UDP relay lookup remains responsive during association refresh", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    std::string initialHost;
    std::string initialPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, initialHost, initialPort));

    proxy.blockNextAssociation();
    std::thread refresh([&] { Socket::socksUpdated(*tc.ownedCtx); });
    const bool refreshBlocked = proxy.waitUntilAssociationBlocked(std::chrono::seconds(2));

    std::atomic<bool> lookupComplete { false };
    bool lookupSucceeded = false;
    std::string lookupHost;
    std::string lookupPort;
    std::thread lookup([&] {
        lookupSucceeded = Socket::getUdpProxyEndpoint(*tc.ownedCtx, lookupHost, lookupPort);
        lookupComplete = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while(!lookupComplete && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool lookupWasPrompt = lookupComplete.load();

    proxy.releaseBlockedAssociation();
    refresh.join();
    lookup.join();

    REQUIRE(refreshBlocked);
    REQUIRE(lookupWasPrompt);
    REQUIRE(lookupSucceeded);
    REQUIRE(lookupHost == initialHost);
    REQUIRE(lookupPort == initialPort);
    REQUIRE(proxy.associationCount() == 2);
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

TEST_CASE("SOCKS5 UDP request accepts a null zero-length payload", "[qt][socket][socks5][udp]")
{
    ByteVector packet;
    REQUIRE(Socket::encodeSocks5UdpPacket("192.0.2.42", "6250", nullptr, 0, false, packet));
    REQUIRE(packet == ByteVector{ 0, 0, 0, 1, 192, 0, 2, 42, 0x18, 0x6a });
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
