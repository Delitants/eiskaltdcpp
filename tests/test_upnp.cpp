#include "dcpp/stdinc.h"
#include "dcpp/UPnP.h"
#include <catch2/catch_test_macros.hpp>
#include <map>

namespace {
class FakeGateway : public dcpp::UPnP {
public:
    using Key = std::pair<std::string, Protocol>;
    std::map<Key, std::string> mappings;
    std::string failingPort;
    int removals = 0;
    bool init() override { return true; }
    std::string getExternalIP() override { return "203.0.113.1"; }
    const std::string& getName() const override { static const std::string name = "test"; return name; }
private:
    bool add(const std::string& port, Protocol protocol, const std::string& description) override {
        if (port == failingPort) return false;
        mappings[{port, protocol}] = description;
        return true;
    }
    bool remove(const std::string& port, Protocol protocol) override {
        ++removals;
        return mappings.erase({port, protocol}) == 1;
    }
};
}

TEST_CASE("UPnP renews expired mappings without deleting live mappings", "[upnp]") {
    FakeGateway gateway;
    REQUIRE(gateway.open("3000", dcpp::UPnP::PROTOCOL_TCP, "Transfer"));
    REQUIRE(gateway.open("3000", dcpp::UPnP::PROTOCOL_UDP, "Search"));
    gateway.mappings.clear(); // Router restart or lease expiry.
    REQUIRE(gateway.renew());
    REQUIRE(gateway.mappings.size() == 2);
    REQUIRE(gateway.removals == 0);
    REQUIRE(gateway.renew());
    REQUIRE(gateway.close());
    REQUIRE(gateway.removals == 2);
}

TEST_CASE("UPnP retains failed renewals for retry and renews remaining rules", "[upnp]") {
    FakeGateway gateway;
    REQUIRE(gateway.open("3000", dcpp::UPnP::PROTOCOL_TCP, "Transfer"));
    REQUIRE(gateway.open("3001", dcpp::UPnP::PROTOCOL_TCP, "TLS"));
    gateway.mappings.clear();
    gateway.failingPort = "3000";
    REQUIRE_FALSE(gateway.renew());
    REQUIRE(gateway.mappings.size() == 1);
    REQUIRE(gateway.hasRules());
    gateway.failingPort.clear();
    REQUIRE(gateway.renew());
    REQUIRE(gateway.mappings.size() == 2);
}

TEST_CASE("UPnP remembers each port and protocol only once", "[upnp]") {
    FakeGateway gateway;
    REQUIRE(gateway.open("3000", dcpp::UPnP::PROTOCOL_UDP, "Search"));
    REQUIRE(gateway.open("3000", dcpp::UPnP::PROTOCOL_UDP, "Search and DHT"));
    REQUIRE(gateway.close());
    REQUIRE(gateway.removals == 1);
}

TEST_CASE("UPnP cancellation stops renewal between network requests", "[upnp]") {
    FakeGateway gateway;
    REQUIRE(gateway.open("3000", dcpp::UPnP::PROTOCOL_TCP, "Transfer"));
    REQUIRE(gateway.open("3001", dcpp::UPnP::PROTOCOL_TCP, "TLS"));
    gateway.mappings.clear();
    REQUIRE_FALSE(gateway.renew([&] { return !gateway.mappings.empty(); }));
    REQUIRE(gateway.mappings.size() == 1);
    REQUIRE(gateway.renew());
    REQUIRE(gateway.mappings.size() == 2);
}
