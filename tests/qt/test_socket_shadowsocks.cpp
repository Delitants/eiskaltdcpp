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
