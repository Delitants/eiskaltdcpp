#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dht/stdafx.h"
#include "dht/BootstrapManager.h"
#include "dht/DHT.h"
#include "dht/UDPSocket.h"
#include "dcpp/ConnectivityManager.h"
#include "dcpp/BufferedSocket.h"
#include "dcpp/ClientManager.h"
#include "dcpp/DCContext.h"
#include "dcpp/Exception.h"
#include "dcpp/HttpConnection.h"
#include "dcpp/LogManager.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/Thread.h"
#include "dcpp/Util.h"
#include "dcpp/DCPlusPlus.h"

using namespace dht;

namespace {

const char* envOrNull(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

bool envFlag(const char* name)
{
    const char* value = envOrNull(name);
    return value && (value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
        value[0] == 'y' || value[0] == 'Y');
}

class ScopedUtilPaths
{
public:
    explicit ScopedUtilPaths(const dcpp::Util::PathsMap& overrides) :
        previousPaths(snapshotCurrentPaths())
    {
        try {
            dcpp::Util::uninitialize();
            dcpp::Util::initialize(overrides);
            active = true;
        } catch(...) {
            try {
                dcpp::Util::uninitialize();
                dcpp::Util::initialize(previousPaths);
            } catch(...) { }
            throw;
        }
    }

    ~ScopedUtilPaths()
    {
        restore();
    }

    void restore() noexcept
    {
        if(!active) {
            return;
        }

        try {
            dcpp::Util::uninitialize();
            dcpp::Util::initialize(previousPaths);
        } catch(...) {
        }
        active = false;
    }

private:
    static dcpp::Util::PathsMap snapshotCurrentPaths()
    {
        dcpp::Util::PathsMap snapshot;
        for(int path = dcpp::Util::PATH_GLOBAL_CONFIG; path < dcpp::Util::PATH_LAST; ++path) {
            const auto currentPath = static_cast<dcpp::Util::Paths>(path);
            snapshot[currentPath] = dcpp::Util::getPath(currentPath);
        }
        return snapshot;
    }

    dcpp::Util::PathsMap previousPaths;
    bool active = false;
};

class FullStartupContext
{
public:
    std::filesystem::path tmpDir;
    std::unique_ptr<dcpp::DCContext> ownedCtx;
    std::unique_ptr<ScopedUtilPaths> utilPaths;

    FullStartupContext()
    {
        tmpDir = std::filesystem::temp_directory_path() / "eiskalt_dht_integration";
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "_%p", static_cast<void*>(this));
        tmpDir += suffix;
        std::filesystem::create_directories(tmpDir);
        tmpDir = tmpDir.make_preferred();

        const auto dirStr = tmpDir.string() + std::string(1, PATH_SEPARATOR);
        dcpp::Util::PathsMap overrides;
        overrides[dcpp::Util::PATH_USER_CONFIG] = dirStr;
        overrides[dcpp::Util::PATH_USER_LOCAL] = dirStr;
        overrides[dcpp::Util::PATH_DOWNLOADS] = dirStr + "Downloads" + std::string(1, PATH_SEPARATOR);
        utilPaths = std::make_unique<ScopedUtilPaths>(overrides);

        ownedCtx = std::make_unique<dcpp::DCContext>();
        ownedCtx->startup();
        dcpp::setContext(ownedCtx.get());
    }

    ~FullStartupContext()
    {
        if(ownedCtx) {
            ownedCtx->getDHT()->stop();
        }
        if(dcpp::getContext()) {
            dcpp::getContext()->shutdown();
            dcpp::setContext(nullptr);
        }
        utilPaths.reset();
        ownedCtx.reset();
        std::error_code ec;
        std::filesystem::remove_all(tmpDir, ec);
    }

    FullStartupContext(const FullStartupContext&) = delete;
    FullStartupContext& operator=(const FullStartupContext&) = delete;
};

class SocksSettingsScope
{
public:
    explicit SocksSettingsScope(dcpp::DCContext& context) : context(context) { }

    ~SocksSettingsScope()
    {
        auto* settings = context.getSettingsManager();
        settings->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_DIRECT);
        settings->set(dcpp::SettingsManager::PROXY_P2P_CONNECTIONS, false);
        dcpp::Socket::socksUpdated(context);
    }

private:
    dcpp::DCContext& context;
};

void configureIntegrationSocks5(dcpp::DCContext& context, const char* server, const char* port,
    const char* user, const char* password, bool tls)
{
    auto* settings = context.getSettingsManager();
    settings->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_DIRECT);
    dcpp::Socket::socksUpdated(context);
    settings->set(dcpp::SettingsManager::SOCKS_SERVER, std::string(server));
    settings->set(dcpp::SettingsManager::SOCKS_PORT, dcpp::Util::toInt(port));
    settings->set(dcpp::SettingsManager::SOCKS_USER, std::string(user ? user : ""));
    settings->set(dcpp::SettingsManager::SOCKS_PASSWORD, std::string(password ? password : ""));
    settings->set(dcpp::SettingsManager::SOCKS_TLS, tls);
    settings->set(dcpp::SettingsManager::SOCKS_RESOLVE, true);
    settings->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_SOCKS5);
}

class TransportCapture final : public UDPSocket::TransportObserver
{
public:
    void onSend(const UDPSocket::SendObservation& observation) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            observations.push_back(observation);
        }
        changed.notify_all();
    }

    bool waitForObservation(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, timeout, [&] { return !observations.empty(); });
    }

    std::vector<UDPSocket::SendObservation> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return observations;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::vector<UDPSocket::SendObservation> observations;
};

class ScopedTransportObserver
{
public:
    ScopedTransportObserver(DHT& dht, std::shared_ptr<UDPSocket::TransportObserver> observer) :
        dht(dht)
    {
        dht.setTransportObserver(std::move(observer));
    }

    ~ScopedTransportObserver()
    {
        dht.setTransportObserver(nullptr);
    }

private:
    DHT& dht;
};

} // namespace

TEST_CASE("DHT startup policy separates incoming mode from proxy UDP capability", "[qt][dht][connectivity]")
{
    using dcpp::ConnectivityManager;
    using dcpp::SettingsManager;

    REQUIRE(ConnectivityManager::shouldStartDht(true, true, SettingsManager::OUTGOING_DIRECT, false));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, false, SettingsManager::OUTGOING_DIRECT, false));
    REQUIRE(ConnectivityManager::shouldStartDht(true, true, SettingsManager::OUTGOING_SOCKS5, true));
    REQUIRE(ConnectivityManager::shouldStartDht(true, false, SettingsManager::OUTGOING_SOCKS5, true));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, true, SettingsManager::OUTGOING_SOCKS5, false));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, false, SettingsManager::OUTGOING_SOCKS5, false));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, true, SettingsManager::OUTGOING_SHADOWSOCKS, true));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(true, false, SettingsManager::OUTGOING_SHADOWSOCKS, true));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(false, true, SettingsManager::OUTGOING_DIRECT, false));
    REQUIRE_FALSE(ConnectivityManager::shouldStartDht(false, true, SettingsManager::OUTGOING_SOCKS5, true));
}

TEST_CASE("Incoming listener probing rolls back partial success", "[qt][dht][connectivity]")
{
    std::vector<string> events;

    REQUIRE_THROWS_AS(
        dcpp::ConnectivityManager::runIncomingListenerProbe(
            [&events] { events.push_back("tcp"); },
            [&events] {
                events.push_back("udp");
                throw dcpp::Exception("UDP failed");
            },
            [&events] { events.push_back("rollback"); }),
        dcpp::Exception);

    REQUIRE(events == std::vector<string>{ "tcp", "udp", "rollback" });

    events.clear();
    dcpp::ConnectivityManager::runIncomingListenerProbe(
        [&events] { events.push_back("tcp"); },
        [&events] { events.push_back("udp"); },
        [&events] { events.push_back("rollback"); });
    REQUIRE(events == std::vector<string>{ "tcp", "udp" });
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

TEST_CASE("DHT bootstrap request preparation invokes SOCKS5 routing", "[qt][dht][bootstrap][proxy]")
{
    dcpp::DCContext context;
    context.startupMinimal();
    context.getSettingsManager()->set(
        dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_SOCKS5);

    bool connectorInvoked = false;
    bool proxy = false;
    {
        dcpp::HttpConnection connection(context, dcpp::Util::emptyString,
            [&connectorInvoked, &proxy](dcpp::BufferedSocket&, const string&, const string&, bool, bool useProxy) {
                connectorInvoked = true;
                proxy = useProxy;
            });
        connection.downloadFile("http://bootstrap.example/dht");
    }
    dcpp::BufferedSocket::waitShutdown();

    REQUIRE(connectorInvoked);
    REQUIRE(proxy);
    context.shutdown();
}

TEST_CASE("Repeated HTTP requests replace an active buffered socket", "[qt][http][regression]")
{
    dcpp::DCContext context;
    context.startupMinimal();

    std::vector<dcpp::BufferedSocket*> sockets;
    {
        dcpp::HttpConnection connection(context, dcpp::Util::emptyString,
            [&sockets](dcpp::BufferedSocket& socket, const string&, const string&, bool, bool) {
                sockets.push_back(&socket);
            });
        connection.downloadFile("http://first.example/ip");
        connection.downloadFile("http://second.example/ip");
    }
    dcpp::BufferedSocket::waitShutdown();

    REQUIRE(sockets.size() == 2);
    REQUIRE(sockets[0] != sockets[1]);
    context.shutdown();
}

TEST_CASE("Proxied DHT bootstrap URL advertises the UDP relay port", "[qt][dht][bootstrap][proxy]")
{
    REQUIRE(BootstrapManager::buildBootstrapUrl(
        "https://bootstrap.example/dht", "TESTCID", "49152", true) ==
        "https://bootstrap.example/dht?cid=TESTCID&encryption=1&u4=49152");
    REQUIRE(BootstrapManager::buildBootstrapUrl(
        "https://bootstrap.example/dht", "TESTCID", "49152", false) ==
        "https://bootstrap.example/dht?cid=TESTCID&encryption=1");
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

TEST_CASE("Passive SOCKS5 DHT startup sends UDP via the relay only", "[qt][dht][socks5][integration]")
{
    const char* server = envOrNull("EISKALT_TEST_SOCKS5_SERVER");
    const char* portText = envOrNull("EISKALT_TEST_SOCKS5_PORT");
    const char* user = envOrNull("EISKALT_TEST_SOCKS5_USER");
    const char* password = envOrNull("EISKALT_TEST_SOCKS5_PASSWORD");
    const char* bootstrapUrl = envOrNull("EISKALT_TEST_DHT_BOOTSTRAP_URL");

    if(!server || !portText || !bootstrapUrl) {
        SKIP("Set EISKALT_TEST_SOCKS5_SERVER, EISKALT_TEST_SOCKS5_PORT, and EISKALT_TEST_DHT_BOOTSTRAP_URL to run this opt-in network integration test");
    }

    FullStartupContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    auto* settings = tc.ownedCtx->getSettingsManager();
    auto* dht = tc.ownedCtx->getDHT();
    REQUIRE(dht != nullptr);

    configureIntegrationSocks5(*tc.ownedCtx, server, portText, user, password, envFlag("EISKALT_TEST_SOCKS5_TLS"));
    settings->set(dcpp::SettingsManager::AUTO_DETECT_CONNECTION, false);
    settings->set(dcpp::SettingsManager::INCOMING_CONNECTIONS, dcpp::SettingsManager::INCOMING_FIREWALL_PASSIVE);
    settings->set(dcpp::SettingsManager::PROXY_P2P_CONNECTIONS, true);
    settings->set(dcpp::SettingsManager::USE_DHT, true);
    settings->set(dcpp::SettingsManager::DHT_BOOTSTRAP_URLS, std::string(bootstrapUrl));

    std::string relayHost;
    std::string relayPort;
    REQUIRE(dcpp::Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
    REQUIRE_FALSE(relayHost.empty());
    REQUIRE(dcpp::Util::toInt(relayPort) > 0);

    auto observer = std::make_shared<TransportCapture>();
    ScopedTransportObserver observerScope(*dht, observer);

    REQUIRE_FALSE(tc.ownedCtx->getClientManager()->isActive());
    tc.ownedCtx->getConnectivityManager()->setup(true);

    const auto requestUrlDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::string requestUrl;
    while(std::chrono::steady_clock::now() < requestUrlDeadline) {
        requestUrl = dht->getBootstrapManager().getLastBootstrapRequestUrl();
        if(!requestUrl.empty()) {
            break;
        }
        dcpp::Thread::sleep(10);
    }

    CAPTURE(requestUrl, bootstrapUrl, relayPort);
    REQUIRE_FALSE(requestUrl.empty());
    REQUIRE(requestUrl.find(bootstrapUrl) == 0);
    REQUIRE(requestUrl.find("u4=" + relayPort) != std::string::npos);

    REQUIRE_FALSE(tc.ownedCtx->getClientManager()->isActive());
    REQUIRE(dht->hasUdpProxyEndpoint());
    REQUIRE_FALSE(dht->getPort().empty());
    REQUIRE(dht->getAdvertisedPort() == relayPort);

    const bool observedTraffic = observer->waitForObservation(std::chrono::seconds(60));
    const auto observations = observer->snapshot();
    CAPTURE(relayHost, relayPort, dht->getPort(), dht->getAdvertisedPort(), dht->getNodesCount());
    CAPTURE(tc.ownedCtx->getLogManager()->getLastLogs().size());

    REQUIRE(observedTraffic);
    REQUIRE_FALSE(observations.empty());

    bool sawRelayedLogicalDestination = false;
    for(const auto& observation : observations) {
        INFO("logical=" << observation.logicalIp << ":" << observation.logicalPort <<
            " physical=" << observation.physicalIp << ":" << observation.physicalPort);
        CHECK(observation.proxied);
        CHECK(observation.physicalIp == relayHost);
        CHECK(observation.physicalPort == relayPort);
        if(observation.logicalIp != observation.physicalIp || observation.logicalPort != observation.physicalPort) {
            sawRelayedLogicalDestination = true;
        }
    }

    REQUIRE(sawRelayedLogicalDestination);
}
