#include <catch2/catch_test_macros.hpp>

#include "tests/TestContext.h"

#include "dcpp/SettingsManager.h"
#include "dcpp/CryptoManager.h"
#include "dcpp/SSLSocket.h"
#include "dcpp/Socket.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

using namespace dcpp;

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

class LocalSocks5UdpServer
{
public:
    enum class RelayReply { IPv4, Wildcard, Domain };

    explicit LocalSocks5UdpServer(RelayReply relayReply = RelayReply::IPv4) : relayReply(relayReply)
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
    bool hasProtocolFailed() const { return protocolFailed.load(); }

    bool waitForAssociations(size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(eventMutex);
        return eventChanged.wait_for(lock, timeout, [this, count] { return associations.load() >= count; });
    }

    bool waitForUdpPacket(std::chrono::milliseconds timeout)
    {
        return udpRelay.wait(static_cast<uint32_t>(timeout.count()), Socket::WAIT_READ) == Socket::WAIT_READ;
    }

    void closeControls()
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        controls.clear();
    }

    size_t openControlCount()
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        for(auto i = controls.begin(); i != controls.end();) {
            bool closed = false;
            try {
                if((*i)->wait(0, Socket::WAIT_READ) == Socket::WAIT_READ) {
                    uint8_t byte = 0;
                    const int read = (*i)->read(&byte, 1);
                    closed = read == 0;
                    if(read > 0) {
                        protocolFailed = true;
                    }
                }
            } catch(const SocketException&) {
                closed = true;
            }

            if(closed) {
                i = controls.erase(i);
            } else {
                ++i;
            }
        }
        return controls.size();
    }

    bool waitForOpenControls(size_t count, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            if(openControlCount() == count) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while(std::chrono::steady_clock::now() < deadline);
        return openControlCount() == count;
    }

    bool sendControlByte(uint8_t byte)
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if(controls.empty()) {
            return false;
        }

        try {
            controls.front()->writeAll(&byte, 1, 2000);
            return true;
        } catch(const SocketException&) {
            return false;
        }
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
        ByteVector reply = { 5, 0, 0 };
        if(relayReply == RelayReply::Domain) {
            static const std::string relayHost = "localhost";
            reply.push_back(3);
            reply.push_back(static_cast<uint8_t>(relayHost.size()));
            reply.insert(reply.end(), relayHost.begin(), relayHost.end());
        } else {
            const bool wildcardReply = relayReply == RelayReply::Wildcard;
            reply.push_back(1);
            reply.push_back(static_cast<uint8_t>(wildcardReply ? 0 : 127));
            reply.push_back(0);
            reply.push_back(0);
            reply.push_back(static_cast<uint8_t>(wildcardReply ? 0 : 1));
        }
        reply.push_back(portBytes[0]);
        reply.push_back(portBytes[1]);
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

    const RelayReply relayReply;
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

class UdpDatagramSink
{
public:
    UdpDatagramSink()
    {
        socket.create(Socket::TYPE_UDP, AF_INET);
        socket.bind("0", "127.0.0.1");
    }

    std::string port() { return socket.getLocalPort(); }

    bool waitForPacket(std::chrono::milliseconds timeout)
    {
        return socket.wait(static_cast<uint32_t>(timeout.count()), Socket::WAIT_READ) == Socket::WAIT_READ;
    }

    ByteVector readPacket()
    {
        sockaddr_storage remote = {};
        std::array<uint8_t, 1024> buffer {};
        const int received = socket.read(buffer.data(), static_cast<int>(buffer.size()), remote);
        if(received <= 0) {
            return {};
        }
        return ByteVector(buffer.begin(), buffer.begin() + received);
    }

private:
    Socket socket;
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

ByteVector legacyMasterKey(const std::string& password)
{
    ByteVector key;
    ByteVector previous;
    while(key.size() < 32) {
        ByteVector input(previous);
        input.insert(input.end(), password.begin(), password.end());

        std::array<uint8_t, EVP_MAX_MD_SIZE> digest {};
        unsigned int digestLength = 0;
        if(EVP_Digest(input.data(), input.size(), digest.data(), &digestLength, EVP_md5(), nullptr) != 1) {
            return {};
        }
        previous.assign(digest.begin(), digest.begin() + digestLength);
        key.insert(key.end(), previous.begin(), previous.end());
    }
    key.resize(32);
    return key;
}

ByteVector legacySubkey(const ByteVector& masterKey, const ByteVector& salt)
{
    std::array<uint8_t, EVP_MAX_MD_SIZE> prk {};
    unsigned int prkLength = 0;
    if(!HMAC(EVP_sha1(), salt.data(), static_cast<int>(salt.size()), masterKey.data(),
            masterKey.size(), prk.data(), &prkLength)) {
        return {};
    }

    ByteVector result;
    ByteVector previous;
    const std::string info = "ss-subkey";
    uint8_t counter = 1;
    while(result.size() < 32) {
        ByteVector input(previous);
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter++);

        std::array<uint8_t, EVP_MAX_MD_SIZE> digest {};
        unsigned int digestLength = 0;
        if(!HMAC(EVP_sha1(), prk.data(), static_cast<int>(prkLength), input.data(),
                input.size(), digest.data(), &digestLength)) {
            return {};
        }
        previous.assign(digest.begin(), digest.begin() + digestLength);
        result.insert(result.end(), previous.begin(), previous.end());
    }
    result.resize(32);
    return result;
}

void incrementLegacyNonce(ByteVector& nonce)
{
    for(auto& byte : nonce) {
        if(++byte != 0) {
            break;
        }
    }
}

ByteVector encryptLegacyFramePart(const ByteVector& key, ByteVector& nonce,
    const uint8_t* plaintext, size_t plaintextLength)
{
    constexpr size_t tagLength = 16;
    ByteVector encrypted(plaintextLength + tagLength);
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if(!context) {
        return {};
    }

    int encryptedLength = 0;
    int finalLength = 0;
    const bool ok = EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1 &&
        EVP_EncryptInit_ex(context, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
        EVP_EncryptUpdate(context, encrypted.data(), &encryptedLength, plaintext,
            static_cast<int>(plaintextLength)) == 1 &&
        EVP_EncryptFinal_ex(context, encrypted.data() + encryptedLength, &finalLength) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_GET_TAG, tagLength,
            encrypted.data() + encryptedLength + finalLength) == 1;
    EVP_CIPHER_CTX_free(context);
    if(!ok) {
        return {};
    }

    encrypted.resize(encryptedLength + finalLength + tagLength);
    incrementLegacyNonce(nonce);
    return encrypted;
}

bool decryptLegacyFramePart(const ByteVector& key, ByteVector& nonce,
    const ByteVector& encrypted, ByteVector& plaintext)
{
    constexpr size_t tagLength = 16;
    if(encrypted.size() < tagLength) {
        return false;
    }

    const size_t cipherLength = encrypted.size() - tagLength;
    plaintext.assign(cipherLength, 0);
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if(!context) {
        return false;
    }

    int plaintextLength = 0;
    int finalLength = 0;
    const bool ok = EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1 &&
        EVP_DecryptInit_ex(context, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
        EVP_DecryptUpdate(context, plaintext.data(), &plaintextLength, encrypted.data(),
            static_cast<int>(cipherLength)) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_SET_TAG, tagLength,
            const_cast<uint8_t*>(encrypted.data() + cipherLength)) == 1 &&
        EVP_DecryptFinal_ex(context, plaintext.data() + plaintextLength, &finalLength) == 1;
    EVP_CIPHER_CTX_free(context);
    if(!ok) {
        return false;
    }

    plaintext.resize(plaintextLength + finalLength);
    incrementLegacyNonce(nonce);
    return true;
}

ByteVector makeLegacyFrame(const ByteVector& key, ByteVector& nonce, const std::string& plaintext)
{
    const uint8_t length[2] = {
        static_cast<uint8_t>((plaintext.size() >> 8) & 0xff),
        static_cast<uint8_t>(plaintext.size() & 0xff)
    };
    ByteVector frame = encryptLegacyFramePart(key, nonce, length, sizeof(length));
    ByteVector payload = encryptLegacyFramePart(key, nonce,
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

class LegacyShadowsocksServer
{
public:
    enum class ReplyMode { CoalescedFrames, TruncatedPayload };

    LegacyShadowsocksServer(std::string password, ReplyMode replyMode) :
        password(std::move(password)), replyMode(replyMode)
    {
        listener.create(Socket::TYPE_TCP, AF_INET);
        listener.setSocketOpt(SO_REUSEADDR, 1);
        listener.bind("0", "127.0.0.1");
        listener.listen();
        worker = std::thread([this] { run(); });
    }

    ~LegacyShadowsocksServer()
    {
        stopping = true;
        listener.disconnect();
        holdOpen.notify_all();
        if(worker.joinable()) {
            worker.join();
        }
    }

    std::string port() { return listener.getLocalPort(); }
    bool hasProtocolFailed() const { return protocolFailed.load(); }

private:
    static bool readExact(Socket& socket, ByteVector& bytes, size_t length)
    {
        bytes.assign(length, 0);
        size_t offset = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while(offset < length && std::chrono::steady_clock::now() < deadline) {
            if(socket.wait(50, Socket::WAIT_READ) != Socket::WAIT_READ) {
                continue;
            }
            const int received = socket.read(bytes.data() + offset, static_cast<int>(length - offset));
            if(received <= 0) {
                return false;
            }
            offset += static_cast<size_t>(received);
        }
        return offset == length;
    }

    bool consumeDestination(Socket& client)
    {
        ByteVector requestSalt;
        if(!readExact(client, requestSalt, 32)) {
            return false;
        }

        const ByteVector masterKey = legacyMasterKey(password);
        const ByteVector subkey = legacySubkey(masterKey, requestSalt);
        ByteVector nonce(12, 0);
        ByteVector encryptedLength;
        ByteVector plaintextLength;
        if(subkey.size() != 32 || !readExact(client, encryptedLength, 18) ||
                !decryptLegacyFramePart(subkey, nonce, encryptedLength, plaintextLength) ||
                plaintextLength.size() != 2) {
            return false;
        }

        const size_t payloadLength = (static_cast<size_t>(plaintextLength[0]) << 8) | plaintextLength[1];
        ByteVector encryptedPayload;
        ByteVector destination;
        return readExact(client, encryptedPayload, payloadLength + 16) &&
            decryptLegacyFramePart(subkey, nonce, encryptedPayload, destination) &&
            !destination.empty();
    }

    bool sendOnce(Socket& client, const ByteVector& bytes)
    {
        const int sent = ::send(client.sock, reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()), 0);
        return sent == static_cast<int>(bytes.size());
    }

    void run()
    {
        try {
            if(listener.wait(2000, Socket::WAIT_READ) != Socket::WAIT_READ) {
                protocolFailed = true;
                return;
            }

            Socket client;
            client.accept(listener);
            if(!consumeDestination(client)) {
                protocolFailed = true;
                return;
            }

            ByteVector responseSalt(32, 0x5a);
            const ByteVector subkey = legacySubkey(legacyMasterKey(password), responseSalt);
            ByteVector nonce(12, 0);
            ByteVector response(responseSalt);

            if(replyMode == ReplyMode::CoalescedFrames) {
                const ByteVector first = makeLegacyFrame(subkey, nonce, "first");
                const ByteVector second = makeLegacyFrame(subkey, nonce, "second");
                response.insert(response.end(), first.begin(), first.end());
                response.insert(response.end(), second.begin(), second.end());
                if(!sendOnce(client, response)) {
                    protocolFailed = true;
                    return;
                }

                std::unique_lock<std::mutex> lock(holdMutex);
                holdOpen.wait_for(lock, std::chrono::milliseconds(600), [this] { return stopping.load(); });
            } else {
                const std::string payload = "truncated";
                const uint8_t length[2] = { 0, static_cast<uint8_t>(payload.size()) };
                const ByteVector encryptedLength = encryptLegacyFramePart(subkey, nonce, length, sizeof(length));
                ByteVector encryptedPayload = encryptLegacyFramePart(subkey, nonce,
                    reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
                encryptedPayload.resize(encryptedPayload.size() / 2);
                response.insert(response.end(), encryptedLength.begin(), encryptedLength.end());
                response.insert(response.end(), encryptedPayload.begin(), encryptedPayload.end());
                if(!sendOnce(client, response)) {
                    protocolFailed = true;
                }
            }
        } catch(const SocketException&) {
            if(!stopping) {
                protocolFailed = true;
            }
        }
    }

    const std::string password;
    const ReplyMode replyMode;
    Socket listener;
    std::atomic<bool> stopping { false };
    std::atomic<bool> protocolFailed { false };
    std::thread worker;
    std::mutex holdMutex;
    std::condition_variable holdOpen;
};

class ShadowsocksSettingsScope
{
public:
    explicit ShadowsocksSettingsScope(DCContext& context) : context(context) { }
    ~ShadowsocksSettingsScope()
    {
        context.getSettingsManager()->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
    }

private:
    DCContext& context;
};

void configureLocalShadowsocks(DCContext& context, const std::string& port, const std::string& password)
{
    auto* settings = context.getSettingsManager();
    settings->set(SettingsManager::SHADOWSOCKS_SERVER, std::string("127.0.0.1"));
    settings->set(SettingsManager::SHADOWSOCKS_PORT, Util::toInt(port));
    settings->set(SettingsManager::SHADOWSOCKS_PASSWORD, password);
    settings->set(SettingsManager::SHADOWSOCKS_METHOD, std::string("aes-256-gcm"));
    settings->set(SettingsManager::SHADOWSOCKS_TRANSPORT, SettingsManager::SHADOWSOCKS_TRANSPORT_TCP_AND_UDP);
    settings->set(SettingsManager::SOCKS_RESOLVE, true);
    settings->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SHADOWSOCKS);
}

void configureIntegrationShadowsocks(DCContext& context, const char* server, const char* port,
    const char* password, const char* method)
{
    auto* settings = context.getSettingsManager();
    settings->set(SettingsManager::SHADOWSOCKS_SERVER, std::string(server));
    settings->set(SettingsManager::SHADOWSOCKS_PORT, Util::toInt(port));
    settings->set(SettingsManager::SHADOWSOCKS_PASSWORD, std::string(password));
    settings->set(SettingsManager::SHADOWSOCKS_METHOD, std::string(method ? method : "aes-256-gcm"));
    settings->set(SettingsManager::SOCKS_RESOLVE, true);
    settings->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SHADOWSOCKS);
}

std::unique_ptr<SSLSocket> connectTlsThroughProxy(test::TestContext& testContext,
    const std::string& host, const std::string& port, Socket::Protocol protocol)
{
    std::unique_ptr<SSLSocket> socket(
        testContext.ownedCtx->getCryptoManager()->getClientSocket(true, protocol));
    socket->setContext(testContext.ownedCtx.get());
    socket->setServerName(host);
    socket->proxyConnect(host, port, 10000);
    if(!socket->waitConnected(10000)) {
        return {};
    }
    return socket;
}

std::string readPlainExactly(Socket& socket, size_t length, uint32_t timeout)
{
    std::string result(length, '\0');
    size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while(offset < length) {
        const int received = socket.read(result.data() + offset, static_cast<int>(length - offset));
        if(received > 0) {
            offset += static_cast<size_t>(received);
            continue;
        }
        if(received == 0 || std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if(remaining <= 0 || socket.wait(static_cast<uint32_t>(remaining), Socket::WAIT_READ) != Socket::WAIT_READ) {
            break;
        }
    }
    result.resize(offset);
    return result;
}

class SocksAssociationInspector : public Socket
{
public:
    static std::shared_ptr<const void> retainAssociation()
    {
        std::lock_guard<std::mutex> lock(udpProxyMutex);
        return udpAssociation;
    }

    static bool hasAssociation()
    {
        std::lock_guard<std::mutex> lock(udpProxyMutex);
        return udpAssociation != nullptr;
    }

#ifndef _WIN32
    static bool setControlBlocking(bool blocking)
    {
        std::lock_guard<std::mutex> lock(udpProxyMutex);
        if(!udpAssociation) {
            return false;
        }
        udpAssociation->control->setBlocking(blocking);
        return true;
    }

    static bool stateMutexIsLocked()
    {
        if(udpProxyMutex.try_lock()) {
            udpProxyMutex.unlock();
            return false;
        }
        return true;
    }
#endif
};

#ifndef _WIN32
volatile sig_atomic_t controlSignalHandled = 0;

void handleControlSignal(int)
{
    controlSignalHandled = 1;
}

class SignalHandlerScope
{
public:
    explicit SignalHandlerScope(int signal) : signal(signal)
    {
        struct sigaction action = {};
        action.sa_handler = handleControlSignal;
        sigemptyset(&action.sa_mask);
        installed = sigaction(signal, &action, &previous) == 0;
    }

    ~SignalHandlerScope()
    {
        if(installed) {
            sigaction(signal, &previous, nullptr);
        }
    }

    bool isInstalled() const { return installed; }

private:
    int signal;
    bool installed = false;
    struct sigaction previous = {};
};
#endif

}

TEST_CASE("Shadowsocks reader drains every complete buffered frame", "[qt][socket][shadowsocks]")
{
    LegacyShadowsocksServer server("test-password", LegacyShadowsocksServer::ReplyMode::CoalescedFrames);
    test::TestContext tc;
    ShadowsocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalShadowsocks(*tc.ownedCtx, server.port(), "test-password");

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.proxyConnect("example.test", "443", 3000);

    REQUIRE(readPlainExactly(socket, 5, 3000) == "first");
    REQUIRE(readPlainExactly(socket, 6, 100) == "second");
    REQUIRE_FALSE(server.hasProtocolFailed());
}

TEST_CASE("Shadowsocks reader reports a truncated encrypted frame", "[qt][socket][shadowsocks]")
{
    LegacyShadowsocksServer server("test-password", LegacyShadowsocksServer::ReplyMode::TruncatedPayload);
    test::TestContext tc;
    ShadowsocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalShadowsocks(*tc.ownedCtx, server.port(), "test-password");

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.proxyConnect("example.test", "443", 3000);

    std::string error;
    try {
        readPlainExactly(socket, 9, 3000);
    } catch(const SocketException& e) {
        error = e.getError();
    }
    REQUIRE(error == "Shadowsocks stream ended with an incomplete frame");
    REQUIRE_FALSE(server.hasProtocolFailed());
}

TEST_CASE("Retained SOCKS5 UDP association snapshot keeps its control channel alive", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    std::string relayHost;
    std::string relayPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
    REQUIRE(proxy.waitForOpenControls(1, std::chrono::seconds(2)));

    std::shared_ptr<const void> retained = SocksAssociationInspector::retainAssociation();
    REQUIRE(retained != nullptr);

    tc.ownedCtx->getSettingsManager()->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
    Socket::socksUpdated(*tc.ownedCtx);

    REQUIRE_FALSE(SocksAssociationInspector::hasAssociation());
    REQUIRE(proxy.openControlCount() == 1);

    retained.reset();
    REQUIRE(proxy.waitForOpenControls(0, std::chrono::seconds(2)));
    REQUIRE_FALSE(proxy.hasProtocolFailed());
}

TEST_CASE("SOCKS5 settings scope clears the association and closes its control channel", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    {
        SocksSettingsScope cleanup(*tc.ownedCtx);
        configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

        std::string relayHost;
        std::string relayPort;
        REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
        REQUIRE(proxy.waitForOpenControls(1, std::chrono::seconds(2)));
        REQUIRE(SocksAssociationInspector::hasAssociation());
    }

    REQUIRE_FALSE(SocksAssociationInspector::hasAssociation());
    REQUIRE(proxy.waitForOpenControls(0, std::chrono::seconds(2)));
    REQUIRE_FALSE(proxy.hasProtocolFailed());
}

#ifndef _WIN32
TEST_CASE("SOCKS5 UDP control liveness retries an interrupted peek", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    std::string initialHost;
    std::string initialPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, initialHost, initialPort));
    REQUIRE(proxy.waitForOpenControls(1, std::chrono::seconds(2)));
    REQUIRE(SocksAssociationInspector::setControlBlocking(true));

    SignalHandlerScope signalHandler(SIGUSR1);
    REQUIRE(signalHandler.isInstalled());
    controlSignalHandled = 0;

    bool lookupSucceeded = false;
    std::thread lookup([&] {
        std::string relayHost;
        std::string relayPort;
        lookupSucceeded = Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort) &&
            relayHost == initialHost && relayPort == initialPort;
    });

    const auto lockDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while(!SocksAssociationInspector::stateMutexIsLocked() && std::chrono::steady_clock::now() < lockDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool livenessCheckStarted = SocksAssociationInspector::stateMutexIsLocked();
    const int signalResult = pthread_kill(lookup.native_handle(), SIGUSR1);

    const auto signalDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while(!controlSignalHandled && std::chrono::steady_clock::now() < signalDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool sentControlByte = proxy.sendControlByte(0);
    lookup.join();
    SocksAssociationInspector::setControlBlocking(false);

    REQUIRE(livenessCheckStarted);
    REQUIRE(signalResult == 0);
    REQUIRE(controlSignalHandled == 1);
    REQUIRE(sentControlByte);
    REQUIRE(lookupSucceeded);
    REQUIRE(proxy.associationCount() == 1);
    REQUIRE_FALSE(proxy.hasProtocolFailed());
}
#endif

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
    REQUIRE(proxy.waitForOpenControls(1, std::chrono::seconds(2)));

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
    LocalSocks5UdpServer proxy(LocalSocks5UdpServer::RelayReply::Wildcard);
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "localhost", proxy.port());

    std::string relayHost;
    std::string relayPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
    REQUIRE(relayHost == "127.0.0.1");
    REQUIRE(relayPort == proxy.relayPort());
}

TEST_CASE("SOCKS5 UDP domain relay remains a hostname and follows the UDP socket family", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy(LocalSocks5UdpServer::RelayReply::Domain);
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    std::string relayHost;
    std::string relayPort;
    REQUIRE(Socket::getUdpProxyEndpoint(*tc.ownedCtx, relayHost, relayPort));
    REQUIRE(relayHost == "localhost");
    REQUIRE(relayPort == proxy.relayPort());

    vector<sockaddr_storage> endpoints;
    REQUIRE(Socket::resolveUdpEndpoint(relayHost, relayPort, AF_INET, endpoints));
    REQUIRE_FALSE(endpoints.empty());
    for(const auto& endpoint : endpoints) {
        REQUIRE(endpoint.ss_family == AF_INET);
    }

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");

    const ByteVector reply = {
        0, 0, 0,
        1, 192, 0, 2, 42,
        0x18, 0x6a,
        'D', 'H', 'T'
    };
    proxy.sendUdp(socket.getLocalPort(), reply);
    REQUIRE(socket.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);

    uint8_t buffer[16] = {};
    sockaddr_storage remote = {};
    REQUIRE(socket.read(buffer, sizeof(buffer), remote) == 3);
    REQUIRE(ByteVector(buffer, buffer + 3) == ByteVector{ 'D', 'H', 'T' });
}

TEST_CASE("SOCKS TLS UDP control probe distinguishes alive retry and dead states", "[qt][socket][socks5][udp]")
{
    using Decision = Socket::SocksTlsControlProbeDecision;

    REQUIRE(Socket::classifySocksTlsControlProbe(1, SSL_ERROR_NONE, 0) == Decision::Alive);
    REQUIRE(Socket::classifySocksTlsControlProbe(-1, SSL_ERROR_WANT_READ, 0) == Decision::Alive);
    REQUIRE(Socket::classifySocksTlsControlProbe(-1, SSL_ERROR_WANT_WRITE, 0) == Decision::Alive);
#ifdef _WIN32
    REQUIRE(Socket::classifySocksTlsControlProbe(-1, SSL_ERROR_SYSCALL, WSAEINTR) == Decision::Retry);
#else
    REQUIRE(Socket::classifySocksTlsControlProbe(-1, SSL_ERROR_SYSCALL, EINTR) == Decision::Retry);
#endif
    REQUIRE(Socket::classifySocksTlsControlProbe(0, SSL_ERROR_ZERO_RETURN, 0) == Decision::Dead);
    REQUIRE(Socket::classifySocksTlsControlProbe(-1, SSL_ERROR_SYSCALL, 0) == Decision::Dead);
    REQUIRE(Socket::classifySocksTlsControlProbe(-1, SSL_ERROR_SSL, 0) == Decision::Dead);
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

    std::mutex lookupMutex;
    std::condition_variable lookupChanged;
    bool lookupComplete = false;
    bool lookupSucceeded = false;
    std::string lookupHost;
    std::string lookupPort;
    std::thread lookup([&] {
        lookupSucceeded = Socket::getUdpProxyEndpoint(*tc.ownedCtx, lookupHost, lookupPort);
        {
            std::lock_guard<std::mutex> lock(lookupMutex);
            lookupComplete = true;
        }
        lookupChanged.notify_all();
    });

    std::unique_lock<std::mutex> lookupLock(lookupMutex);
    const bool lookupWasPrompt = lookupChanged.wait_for(lookupLock, std::chrono::seconds(1), [&] { return lookupComplete; });
    lookupLock.unlock();

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

TEST_CASE("Socket UDP send info records the successful direct endpoint", "[qt][socket][socks5][udp]")
{
    UdpDatagramSink sink;
    test::TestContext tc;

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");

    const uint8_t payload[] = { 0xde, 0xad, 0xbe, 0xef };
    Socket::UdpSendInfo sendInfo;
    socket.writeTo("127.0.0.1", sink.port(), payload, sizeof(payload), false, &sendInfo);

    REQUIRE(sendInfo.logicalIp == "127.0.0.1");
    REQUIRE(sendInfo.logicalPort == sink.port());
    REQUIRE(sendInfo.physicalIp == "127.0.0.1");
    REQUIRE(sendInfo.physicalPort == sink.port());
    REQUIRE_FALSE(sendInfo.proxied);
    REQUIRE(sendInfo.bytesSent == sizeof(payload));
    REQUIRE(sink.waitForPacket(std::chrono::seconds(2)));
    REQUIRE(sink.readPacket() == ByteVector(payload, payload + sizeof(payload)));
}

TEST_CASE("Socket UDP send info records the SOCKS5 relay endpoint", "[qt][socket][socks5][udp]")
{
    LocalSocks5UdpServer proxy;
    test::TestContext tc;
    SocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalSocks(*tc.ownedCtx, "127.0.0.1", proxy.port());

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");

    const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };
    Socket::UdpSendInfo sendInfo;
    socket.writeTo("198.51.100.24", "6250", payload, sizeof(payload), true, &sendInfo);

    REQUIRE(proxy.waitForAssociations(1, std::chrono::seconds(2)));
    REQUIRE(proxy.waitForUdpPacket(std::chrono::seconds(2)));
    REQUIRE(sendInfo.logicalIp == "198.51.100.24");
    REQUIRE(sendInfo.logicalPort == "6250");
    REQUIRE(sendInfo.physicalIp == "127.0.0.1");
    REQUIRE(sendInfo.physicalPort == proxy.relayPort());
    REQUIRE(sendInfo.proxied);
    REQUIRE(sendInfo.bytesSent > sizeof(payload));
    REQUIRE(sendInfo.logicalIp != sendInfo.physicalIp);
    REQUIRE(sendInfo.logicalPort != sendInfo.physicalPort);
}

TEST_CASE("Socket UDP send info records the Shadowsocks relay endpoint", "[qt][socket][shadowsocks]")
{
    UdpDatagramSink relay;
    test::TestContext tc;
    ShadowsocksSettingsScope cleanup(*tc.ownedCtx);
    configureLocalShadowsocks(*tc.ownedCtx, relay.port(), "test-password");

    Socket socket;
    socket.setContext(tc.ownedCtx.get());
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");

    const uint8_t payload[] = { 0xaa, 0xbb, 0xcc, 0xdd };
    Socket::UdpSendInfo sendInfo;
    socket.writeTo("203.0.113.77", "6250", payload, sizeof(payload), true, &sendInfo);

    REQUIRE(relay.waitForPacket(std::chrono::seconds(2)));
    REQUIRE(sendInfo.logicalIp == "203.0.113.77");
    REQUIRE(sendInfo.logicalPort == "6250");
    REQUIRE(sendInfo.physicalIp == "127.0.0.1");
    REQUIRE(sendInfo.physicalPort == relay.port());
    REQUIRE(sendInfo.proxied);
    REQUIRE(sendInfo.bytesSent > sizeof(payload));
    REQUIRE(sendInfo.logicalIp != sendInfo.physicalIp);
    REQUIRE(sendInfo.logicalPort != sendInfo.physicalPort);
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
#ifdef AI_ALL
    REQUIRE((Socket::udpResolverFlags(AF_INET6) & AI_ALL) != 0);
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
    settings->set(SettingsManager::SOCKS_TLS, envFlag("EISKALT_TEST_SOCKS5_TLS"));
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

TEST_CASE("Secure NMDC handshakes remain reliable through Shadowsocks", "[qt][socket][shadowsocks][integration]")
{
    const char* server = envOrNull("EISKALT_TEST_SHADOWSOCKS_SERVER");
    const char* proxyPort = envOrNull("EISKALT_TEST_SHADOWSOCKS_PORT");
    const char* password = envOrNull("EISKALT_TEST_SHADOWSOCKS_PASSWORD");
    const char* method = envOrNull("EISKALT_TEST_SHADOWSOCKS_METHOD");
    const char* host = envOrNull("EISKALT_TEST_NMDCS_HOST");
    const char* port = envOrNull("EISKALT_TEST_NMDCS_PORT");
    if(!server || !proxyPort || !password || !host || !port) {
        SKIP("Set Shadowsocks and EISKALT_TEST_NMDCS_HOST/PORT variables to run this integration test");
    }

    test::TestContext tc;
    ShadowsocksSettingsScope cleanup(*tc.ownedCtx);
    configureIntegrationShadowsocks(*tc.ownedCtx, server, proxyPort, password, method);

    for(int attempt = 0; attempt < 5; ++attempt) {
        CAPTURE(attempt);
        const auto started = std::chrono::steady_clock::now();
        auto socket = connectTlsThroughProxy(tc, host, port, Socket::PROTO_NMDC);
        REQUIRE(socket != nullptr);
        REQUIRE(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
    }
}

TEST_CASE("HTTPS responses remain reliable through Shadowsocks", "[qt][socket][shadowsocks][integration]")
{
    const char* server = envOrNull("EISKALT_TEST_SHADOWSOCKS_SERVER");
    const char* proxyPort = envOrNull("EISKALT_TEST_SHADOWSOCKS_PORT");
    const char* password = envOrNull("EISKALT_TEST_SHADOWSOCKS_PASSWORD");
    const char* method = envOrNull("EISKALT_TEST_SHADOWSOCKS_METHOD");
    const char* url = envOrNull("EISKALT_TEST_HTTPS_URL");
    if(!server || !proxyPort || !password || !url) {
        SKIP("Set Shadowsocks and EISKALT_TEST_HTTPS_URL variables to run this integration test");
    }

    std::string protocol;
    std::string host;
    std::string port;
    std::string path;
    std::string query;
    std::string fragment;
    Util::decodeUrl(url, protocol, host, port, path, query, fragment);
    REQUIRE(protocol == "https");
    REQUIRE_FALSE(host.empty());
    if(port.empty()) {
        port = "443";
    }
    if(path.empty()) {
        path = "/";
    }
    if(!query.empty()) {
        path += "?" + query;
    }

    test::TestContext tc;
    ShadowsocksSettingsScope cleanup(*tc.ownedCtx);
    configureIntegrationShadowsocks(*tc.ownedCtx, server, proxyPort, password, method);

    for(int attempt = 0; attempt < 5; ++attempt) {
        CAPTURE(attempt);
        auto socket = connectTlsThroughProxy(tc, host, port, Socket::PROTO_DEFAULT);
        REQUIRE(socket != nullptr);

        const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
            "\r\nConnection: close\r\n\r\n";
        socket->writeAll(request.data(), static_cast<int>(request.size()), 10000);
        REQUIRE(socket->wait(10000, Socket::WAIT_READ) == Socket::WAIT_READ);

        std::array<char, 1024> response {};
        REQUIRE(socket->read(response.data(), response.size()) > 0);
    }
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
