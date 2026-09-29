#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "TorrentProxyAdapter.h"
#include "GostUdpRelay.h"
#include <QCoreApplication>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryFile>
#include <QUdpSocket>
#include <QNetworkProxy>
#include <QNetworkInterface>
#include <QHostInfo>
#include "tests/proxy/DisposableGostServer.h"
#include "dcpp/GostProtocol.h"
#ifndef _WIN32
#include <sys/resource.h>
#endif
#include "dcpp/Socket.h"
#include <catch2/generators/catch_generators.hpp>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <atomic>
#include <thread>

namespace {
template<typename Peer>
bool upstreamHandshake(Peer& peer) {
    unsigned char hello[3]{};
    if(peer.readAll(hello, 3, 2000) != 3 || hello[0] != 5 || hello[2] != 0) return false;
    const unsigned char selected[] = {5, 0};
    peer.writeAll(selected, 2, 2000);
    unsigned char request[5]{};
    if(peer.readAll(request, 5, 2000) != 5 || request[3] != 3) return false;
    std::string target(request[4], '\0');
    if(peer.readAll(target.data(), target.size(), 2000) != int(target.size()) || target != "example.invalid") return false;
    unsigned char port[2]{};
    if(peer.readAll(port, 2, 2000) != 2 || port[0] != 1 || port[1] != 187) return false;
    const unsigned char connected[] = {5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
    peer.writeAll(connected, sizeof(connected), 2000);
    return true;
}

dcpp::Socket::StreamProxyConfig localConfig(const eiskalt::torrent::ProxyConfig& local) {
    dcpp::Socket::StreamProxyConfig config;
    config.type = dcpp::Socket::StreamProxyConfig::Socks5;
    config.host = local.host.toStdString();
    config.port = local.port;
    config.user = local.user.toStdString();
    config.password = local.password.toStdString();
    return config;
}

void pumpUntil(const std::atomic<bool>& done, int timeout = 10000) {
    QElapsedTimer deadline;
    deadline.start();
    while(!done && deadline.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}

// One ephemeral test CA/server certificate; no installed roots or external hosts.
struct TlsFixture {
    dcpp::ssl::SSL_CTX context{SSL_CTX_new(TLS_server_method())};
    QTemporaryFile certificate;
    const bool hadRoots = qEnvironmentVariableIsSet("SSL_CERT_FILE");
    const QByteArray oldRoots = qgetenv("SSL_CERT_FILE");
    TlsFixture() {
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> keys(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
        REQUIRE(keys);
        REQUIRE(EVP_PKEY_keygen_init(keys.get()) == 1);
        REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(keys.get(), 2048) == 1);
        EVP_PKEY* generated = nullptr;
        REQUIRE(EVP_PKEY_keygen(keys.get(), &generated) == 1);
        dcpp::ssl::EVP_PKEY key(generated);
        dcpp::ssl::X509 cert(X509_new());
        REQUIRE(bool(cert));
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
        X509_set_pubkey(cert, key);
        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("loopback proxy test"), -1, -1, 0);
        X509_set_issuer_name(cert, name);
        X509_EXTENSION* san = X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name,
                                                "IP:127.0.0.1,DNS:localhost");
        REQUIRE(san != nullptr);
        X509_add_ext(cert, san, -1);
        X509_EXTENSION_free(san);
        REQUIRE(X509_sign(cert, key, EVP_sha256()) > 0);
        REQUIRE(SSL_CTX_use_certificate(context, cert) == 1);
        REQUIRE(SSL_CTX_use_PrivateKey(context, key) == 1);
        std::unique_ptr<BIO, decltype(&BIO_free)> pem(BIO_new(BIO_s_mem()), BIO_free);
        REQUIRE(PEM_write_bio_X509(pem.get(), cert) == 1);
        char* data = nullptr;
        const long size = BIO_get_mem_data(pem.get(), &data);
        REQUIRE(certificate.open());
        REQUIRE(certificate.write(data, size) == size);
        REQUIRE(certificate.flush());
        qputenv("SSL_CERT_FILE", certificate.fileName().toUtf8());
    }
    ~TlsFixture() {
        if(hadRoots) qputenv("SSL_CERT_FILE", oldRoots);
        else qunsetenv("SSL_CERT_FILE");
    }
};

class TlsPeer {
public:
    TlsPeer(dcpp::Socket& socket, SSL_CTX* context) : socket(socket), tls(SSL_new(context)) {
        if(!tls || SSL_set_fd(tls, static_cast<int>(socket.sock)) != 1) throw std::runtime_error("TLS setup");
        QElapsedTimer deadline;
        deadline.start();
        int result;
        while((result = SSL_accept(tls)) != 1) retry(result, deadline, 3000);
    }
    ~TlsPeer() { SSL_shutdown(tls); }
    int readAll(void* bytes, int size, int timeout) { return transfer(false, bytes, size, timeout); }
    void writeAll(const void* bytes, int size, int timeout) { transfer(true, const_cast<void*>(bytes), size, timeout); }
private:
    dcpp::Socket& socket;
    dcpp::ssl::SSL tls;
    void retry(int result, const QElapsedTimer& deadline, int timeout) {
        const int error = SSL_get_error(tls, result);
        if(deadline.elapsed() >= timeout || (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE))
            throw std::runtime_error("TLS test peer I/O failed");
        socket.wait(20, error == SSL_ERROR_WANT_READ ? dcpp::Socket::WAIT_READ : dcpp::Socket::WAIT_WRITE);
    }
    int transfer(bool writing, void* bytes, int size, int timeout) {
        QElapsedTimer deadline;
        deadline.start();
        int offset = 0;
        while(offset < size) {
            char* at = static_cast<char*>(bytes) + offset;
            const int n = writing ? SSL_write(tls, at, size - offset) : SSL_read(tls, at, size - offset);
            if(n > 0) offset += n;
            else if(!writing && SSL_get_error(tls, n) == SSL_ERROR_ZERO_RETURN) break;
            else retry(n, deadline, timeout);
        }
        return offset;
    }
};
}

TEST_CASE("Torrent adapter refuses a direct or invalid upstream", "[torrent][adapter]") {
    TorrentProxyAdapter adapter;
    eiskalt::torrent::ProxyConfig proxy;
    QString error;
    REQUIRE_FALSE(adapter.start(proxy, &error));
    REQUIRE_FALSE(error.isEmpty());
    proxy.type = eiskalt::torrent::ProxyType::Shadowsocks;
    proxy.host = "127.0.0.1";
    proxy.port = 70000;
    REQUIRE_FALSE(adapter.start(proxy, &error));
    proxy.type = static_cast<eiskalt::torrent::ProxyType>(99);
    proxy.port = 9;
    REQUIRE_FALSE(adapter.start(proxy, &error));
}

TEST_CASE("Torrent adapter binds only loopback and exposes authenticated TCP capability", "[torrent][adapter][network]") {
    TorrentProxyAdapter adapter;
    eiskalt::torrent::ProxyConfig proxy;
    proxy.type = eiskalt::torrent::ProxyType::Shadowsocks;
    proxy.host = "127.0.0.1";
    proxy.port = 9;
    proxy.cipher = "aes-256-gcm";
    proxy.password = "test-only";
    QString error;
    REQUIRE(adapter.start(proxy, &error));
    auto local = adapter.endpoint();
    REQUIRE(local.type == eiskalt::torrent::ProxyType::Socks5);
    REQUIRE(local.host == "127.0.0.1");
    REQUIRE(local.port > 0);
    REQUIRE(local.password.size() >= 32);
    REQUIRE_FALSE(local.user.isEmpty());
    REQUIRE(local.remoteDns);
    REQUIRE_FALSE(local.udp);
    QTcpSocket client;
    client.connectToHost(local.host, local.port);
    REQUIRE(client.waitForConnected(2000));
    client.write("\x05\x01\x00", 3);
    // The listener shares this thread. Pump until the reply is buffered instead
    // of blocking accept(), or waiting for a new readyRead after reply + FIN.
    QElapsedTimer deadline;
    deadline.start();
    while(client.bytesAvailable() < 2 && deadline.elapsed() < 2000 &&
          client.state() != QAbstractSocket::UnconnectedState) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    REQUIRE(client.bytesAvailable() == 2);
    REQUIRE(client.readAll() == QByteArray("\x05\xff", 2));
    adapter.stop();
    REQUIRE(adapter.endpoint().port == 0);
}

TEST_CASE("Torrent adapter drains a backpressured upload and preserves half-close response", "[torrent][adapter][network]") {
    using dcpp::Socket;
    const bool useTls = GENERATE(false, true);
    CAPTURE(useTls);
    std::unique_ptr<TlsFixture> tls;
    if(useTls) tls = std::make_unique<TlsFixture>();
    Socket listener;
    listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    TorrentProxyAdapter adapter;
    eiskalt::torrent::ProxyConfig proxy;
    proxy.type = useTls ? eiskalt::torrent::ProxyType::Socks5Tls : eiskalt::torrent::ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = std::stoi(listener.getLocalPort());
    REQUIRE(adapter.start(proxy));
    auto config = localConfig(adapter.endpoint());
    std::string payload(4 * 1024 * 1024, 'x');
    for(size_t i = 0; i < payload.size(); i += 997) payload[i] = 'y';
    std::string received;
    bool upstreamPassed = false, clientPassed = false;
    std::atomic<bool> done{false};
    std::jthread server([&] {
        try {
            if(listener.wait(3000, Socket::WAIT_READ) != Socket::WAIT_READ) return;
            Socket peer;
            peer.accept(listener);
            peer.setBlocking(false);
#ifdef SO_NOSIGPIPE
            peer.setSocketOpt(SO_NOSIGPIPE, 1);
#endif
            auto exchange = [&](auto& stream) {
                if(!upstreamHandshake(stream)) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                received.resize(payload.size());
                const int n = stream.readAll(received.data(), received.size(), 5000);
                received.resize(n);
                char byte{};
                upstreamPassed = stream.readAll(&byte, 1, 5000) == 0;
                stream.writeAll("ok", 2, 2000);
            };
            if(useTls) {
                TlsPeer stream(peer, tls->context);
                exchange(stream);
            } else exchange(peer);
        } catch(...) { }
    });
    std::jthread client([&] {
        try {
            Socket socket;
            socket.proxyConnect("example.invalid", "443", config, 3000);
            socket.setSocketOpt(SO_SNDBUF, 4096);
            socket.writeAll(payload.data(), payload.size(), 5000);
            ::shutdown(socket.sock, 1);
            char reply[2]{};
            clientPassed = socket.readAll(reply, 2, 5000) == 2 && std::string(reply, 2) == "ok";
        } catch(...) { }
        done = true;
    });
    pumpUntil(done);
    adapter.stop();
    client.join();
    server.join();
    REQUIRE(received.size() == payload.size());
    REQUIRE(bool(received == payload));
    REQUIRE(upstreamPassed);
    REQUIRE(clientPassed);
}

TEST_CASE("Torrent adapter stop cancels an upstream TLS handshake", "[torrent][adapter][network]") {
    using dcpp::Socket;
    Socket listener;
    listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    TorrentProxyAdapter adapter;
    eiskalt::torrent::ProxyConfig proxy;
    proxy.type = eiskalt::torrent::ProxyType::Socks5Tls;
    proxy.host = "127.0.0.1";
    proxy.port = std::stoi(listener.getLocalPort());
    REQUIRE(adapter.start(proxy));
    const auto config = localConfig(adapter.endpoint());
    std::atomic<bool> done{false};
    std::jthread client([&] {
        try {
            Socket socket;
            socket.proxyConnect("example.invalid", "443", config, 3000);
        } catch(...) { }
        done = true;
    });
    QElapsedTimer deadline;
    deadline.start();
    while(listener.wait(0, Socket::WAIT_READ) != Socket::WAIT_READ && deadline.elapsed() < 2000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    const bool dialled = listener.wait(0, Socket::WAIT_READ) == Socket::WAIT_READ;
    Socket peer;
    if(dialled) {
        peer.accept(listener);
        peer.setBlocking(false);
    }
    const bool hello = dialled && peer.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ;
    deadline.restart();
    adapter.stop();
    const auto elapsed = deadline.elapsed();
    client.join();
    REQUIRE(dialled);
    REQUIRE(hello);
    REQUIRE(elapsed < 750);
    REQUIRE(done.load());
    REQUIRE(adapter.endpoint().port == 0);
}

namespace {
bool spinFor(const std::function<bool()>& condition, int timeout = 2500) {
    QElapsedTimer clock;
    clock.start();
    while(!condition() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return condition();
}
QByteArray readControl(QTcpSocket& socket, int count, int timeout = 2500) {
    spinFor([&] { return socket.bytesAvailable() >= count || socket.state() == QAbstractSocket::UnconnectedState; }, timeout);
    return socket.read(count);
}
eiskalt::torrent::ProxyConfig gostConfig(proxy_test::GostServer& server) {
    eiskalt::torrent::ProxyConfig config;
    config.type = eiskalt::torrent::ProxyType::Gost;
    config.host = "127.0.0.1";
    config.port = server.port();
    config.user = "fixture-user";
    config.password = "fixture-password";
    config.caPem = QByteArray::fromStdString(server.ca.pem);
    config.udp = true;
    return config;
}
void authenticate(QTcpSocket& control, const eiskalt::torrent::ProxyConfig& local) {
    control.setProxy(QNetworkProxy::NoProxy);
    control.connectToHost(local.host, local.port);
    REQUIRE(control.waitForConnected(2000));
    control.write("\x05\x01\x02", 3);
    REQUIRE(readControl(control, 2) == QByteArray::fromHex("0502"));
    QByteArray auth(1, char(1));
    auth += char(local.user.size());
    auth += local.user.toLatin1();
    auth += char(local.password.size());
    auth += local.password.toLatin1();
    control.write(auth);
    REQUIRE(readControl(control, 2) == QByteArray::fromHex("0100"));
}
QByteArray associate(QTcpSocket& control, quint16 port = 0, const QByteArray& ip = QByteArray::fromHex("00000000")) {
    QByteArray req = QByteArray::fromHex("05030001") + ip;
    req += char(port >> 8);
    req += char(port);
    control.write(req);
    return readControl(control, 10, 12000);
}
quint16 relayPort(const QByteArray& reply) {
    REQUIRE(reply.size() == 10);
    REQUIRE(reply.left(8) == QByteArray::fromHex("050000017f000001"));
    const quint16 port = (quint8(reply[8]) << 8) | quint8(reply[9]);
    REQUIRE(port != 0);
    return port;
}
QByteArray packet(const char* host = "198.51.100.7", QByteArray payload = QByteArray::fromHex("00017fff")) {
    auto bytes = dcpp::gost::encodeSocks({host, 443,
        dcpp::ByteVector(payload.cbegin(), payload.cend())});
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
void bindUdp(QUdpSocket& socket, QHostAddress host = QHostAddress::LocalHost) {
    socket.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(socket.bind(host, 0));
}
QByteArray receiveUdp(QUdpSocket& socket, int timeout = 2500) {
    if(!spinFor([&] { return socket.hasPendingDatagrams(); }, timeout)) return {};
    QByteArray bytes(socket.pendingDatagramSize(), char(0));
    socket.readDatagram(bytes.data(), bytes.size());
    return bytes;
}
struct GlobalProxyGuard {
    QNetworkProxy previous = QNetworkProxy::applicationProxy();
    ~GlobalProxyGuard() { QNetworkProxy::setApplicationProxy(previous); }
};
}

TEST_CASE("GOST adapter advertises UDP without changing legacy encrypted capability", "[gost][adapter]") {
    proxy_test::GostServer server;
    TorrentProxyAdapter adapter;
    auto config = gostConfig(server);
    REQUIRE(adapter.start(config));
    REQUIRE(adapter.endpoint().udp);
    REQUIRE(adapter.endpoint().remoteDns);
    REQUIRE(adapter.endpoint().host == "127.0.0.1");
    for(auto type : {eiskalt::torrent::ProxyType::Socks5Tls, eiskalt::torrent::ProxyType::Shadowsocks}) {
        config.type = type;
        config.cipher = "aes-256-gcm";
        REQUIRE(adapter.start(config));
        REQUIRE_FALSE(adapter.endpoint().udp);
        QTcpSocket control;
        authenticate(control, adapter.endpoint());
        const auto response = associate(control);
        REQUIRE(response.size() == 10);
        REQUIRE(quint8(response[1]) == 7);
    }
}

TEST_CASE("GOST adapter honors an explicitly disabled UDP preference", "[gost][routing][adapter]") {
    proxy_test::GostServer server;
    TorrentProxyAdapter adapter;
    auto config = gostConfig(server);
    config.udp = false;
    REQUIRE(adapter.start(config));
    REQUIRE_FALSE(adapter.endpoint().udp);
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    const auto response = associate(control);
    REQUIRE(response.size() == 10);
    REQUIRE(quint8(response[1]) == 7);
    REQUIRE_FALSE(server.commandReady.load());
}

TEST_CASE("GOST UDP associations isolate sender and translate remote address forms under a global proxy", "[gost][adapter]") {
    GlobalProxyGuard restore;
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, "192.0.2.1", 9));
    proxy_test::GostOptions options;
    options.sessionMs = 15000;
    proxy_test::GostServer server(options);
    server.start();
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(gostConfig(server)));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    QUdpSocket sender, foreignPort, foreignIp;
    bindUdp(sender);
    bindUdp(foreignPort);
    foreignIp.setProxy(QNetworkProxy::NoProxy);
    if(!foreignIp.bind(QHostAddress("127.0.0.2"), 0)) {
        // macOS does not assign all of 127/8 to lo0. Use an existing local
        // interface as source, still sending only to the loopback destination.
        for(const auto& address : QNetworkInterface::allAddresses()) {
            if(address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback() &&
               foreignIp.bind(address, 0)) break;
        }
    }
    REQUIRE(foreignIp.state() == QAbstractSocket::BoundState);
    {
        // Confirm this alternate source can actually reach a loopback UDP
        // socket, so the rejection below cannot pass due to host routing.
        QUdpSocket probe;
        bindUdp(probe);
        REQUIRE(foreignIp.writeDatagram("probe", 5, QHostAddress::LocalHost, probe.localPort()) == 5);
        REQUIRE(spinFor([&] { return probe.hasPendingDatagrams(); }));
        QHostAddress observedSource;
        char bytes[5]{};
        REQUIRE(probe.readDatagram(bytes, sizeof(bytes), &observedSource) == 5);
        REQUIRE(observedSource == foreignIp.localAddress());
        REQUIRE(observedSource != QHostAddress::LocalHost);
    }
    const bool explicitPort = GENERATE(false, true);
    const auto port = relayPort(associate(control, explicitPort ? sender.localPort() : 0,
        QByteArray::fromHex(explicitPort ? "7f000001" : "00000000")));
    auto bad = packet();
    bad[2] = 1; // Fragmented standard packet must not claim the sender port.
    foreignPort.writeDatagram(bad, QHostAddress::LocalHost, port);
    for(const auto& malformed : {QByteArray::fromHex("000000"), QByteArray::fromHex("000100017f000001000161"),
        QByteArray::fromHex("000000037f61"), QByteArray::fromHex("0000000900000000000161"),
        QByteArray::fromHex("000000017f000001000061")}) {
        foreignPort.writeDatagram(malformed, QHostAddress::LocalHost, port);
    }
    foreignPort.writeDatagram(packet("198.51.100.7", {}), QHostAddress::LocalHost, port);
    if(explicitPort) foreignPort.writeDatagram(packet(), QHostAddress::LocalHost, port);
    REQUIRE(foreignIp.writeDatagram(packet(), QHostAddress::LocalHost, port) == packet().size());
    REQUIRE(receiveUdp(foreignPort, 120).isEmpty());
    REQUIRE(receiveUdp(foreignIp, 120).isEmpty());
    for(const auto* host : {"198.51.100.7", "2001:db8::7", "target-never-resolve.invalid"}) {
        const auto request = packet(host);
        REQUIRE(sender.writeDatagram(request, QHostAddress::LocalHost, port) == request.size());
        REQUIRE(receiveUdp(sender) == request);
    }
    foreignPort.writeDatagram(packet(), QHostAddress::LocalHost, port);
    REQUIRE(receiveUdp(foreignPort, 150).isEmpty());
    REQUIRE(receiveUdp(sender, 150).isEmpty());
    control.abort();
    adapter.stop();
    server.join();
    REQUIRE(server.command == 0xf3);
    REQUIRE(server.target == "0.0.0.0");
    REQUIRE(server.targetPort == 0);
    REQUIRE(server.user == "fixture-user");
    REQUIRE(server.password == "fixture-password");
    size_t offset = 0;
    for(int i = 0; i != 3; ++i) {
        dcpp::gost::Datagram dg;
        size_t used = 0;
        REQUIRE(dcpp::gost::decodeTunnel(std::span(server.echoedInput).subspan(offset), dg, used) == dcpp::gost::DecodeResult::Complete);
        REQUIRE(dg.payload == dcpp::ByteVector{0, 1, 127, 255});
        offset += used;
    }
    REQUIRE(offset == server.echoedInput.size());
#ifndef _WIN32
    REQUIRE(server.sigpipeBlocked);
#endif
}

TEST_CASE("GOST UDP rejects noncontrol sender address and unauthenticated clients", "[gost][adapter]") {
    proxy_test::GostServer server;
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(gostConfig(server)));
    QTcpSocket control;
    auto local = adapter.endpoint();
    local.password = "wrong";
    control.setProxy(QNetworkProxy::NoProxy);
    control.connectToHost(local.host, local.port);
    REQUIRE(control.waitForConnected(2000));
    control.write("\x05\x01\x00", 3);
    REQUIRE(readControl(control, 2) == QByteArray::fromHex("05ff"));
    QTcpSocket badPassword;
    badPassword.setProxy(QNetworkProxy::NoProxy);
    badPassword.connectToHost(local.host, local.port);
    REQUIRE(badPassword.waitForConnected(2000));
    badPassword.write("\x05\x01\x02", 3);
    REQUIRE(readControl(badPassword, 2) == QByteArray::fromHex("0502"));
    QByteArray wrongAuth(1, char(1));
    wrongAuth += char(local.user.size());
    wrongAuth += local.user.toLatin1();
    wrongAuth += char(5);
    wrongAuth += "wrong";
    badPassword.write(wrongAuth);
    REQUIRE(readControl(badPassword, 2) == QByteArray::fromHex("0101"));
    QTcpSocket authenticated;
    authenticate(authenticated, adapter.endpoint());
    const auto response = associate(authenticated, 0, QByteArray::fromHex("7f000002"));
    REQUIRE(response.size() == 10);
    REQUIRE(quint8(response[1]) != 0);
}

TEST_CASE("GOST UDP control EOF and upstream failure invalidate association without replay", "[gost][adapter]") {
    const bool upstreamLoss = GENERATE(false, true);
    proxy_test::GostOptions options;
    options.sessionMs = 15000;
    options.echoDelayMs = 300;
    proxy_test::GostServer first(options);
    dcpp::Socket listener;
    listener.create(dcpp::Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    first.start(&listener);
    auto config = gostConfig(first);
    config.port = std::stoi(listener.getLocalPort());
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(config));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    QUdpSocket sender;
    bindUdp(sender);
    const auto oldPort = relayPort(associate(control, sender.localPort()));
    sender.writeDatagram(packet("old.invalid"), QHostAddress::LocalHost, oldPort);
    REQUIRE(spinFor([&] { return first.receivedDataBytes >= size_t(packet("old.invalid").size()); }));
    if(upstreamLoss) first.requestStop();
    else control.abort();
    REQUIRE(spinFor([&] { return control.state() == QAbstractSocket::UnconnectedState; }));
    proxy_test::GostServer second({}, "IP:127.0.0.1", false, &first.ca);
    second.start(&listener);
    QTcpSocket next;
    authenticate(next, adapter.endpoint());
    const auto newPort = relayPort(associate(next, sender.localPort()));
    // Keep the old peer's delayed write pending while the new association is
    // already live, rather than joining away the very race under test.
    REQUIRE(receiveUdp(sender, 400).isEmpty());
    first.join();
    const auto fresh = packet("new.invalid");
    sender.writeDatagram(fresh, QHostAddress::LocalHost, newPort);
    REQUIRE(receiveUdp(sender) == fresh);
    adapter.stop();
    second.join();
    REQUIRE(second.echoedInput.size() == size_t(fresh.size()));
}

TEST_CASE("GOST adapter closes malformed upstream frames and fails closed on supplied malformed CA", "[gost][adapter]") {
    const bool badCa = GENERATE(false, true);
    proxy_test::GostOptions options;
    options.initialData = {0, 0, 255, 1, 127, 0, 0, 1, 0, 1};
    proxy_test::GostServer server(options);
    server.start();
    TorrentProxyAdapter adapter;
    auto config = gostConfig(server);
    if(badCa) config.caPem = "not a certificate";
    REQUIRE(adapter.start(config));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    const auto response = associate(control);
    REQUIRE(response.size() == 10);
    if(badCa) REQUIRE(quint8(response[1]) != 0);
    REQUIRE(spinFor([&] { return control.state() == QAbstractSocket::UnconnectedState; }));
    adapter.stop();
    server.join();
    if(badCa) REQUIRE(server.applicationBytes == 0);
}

TEST_CASE("GOST adapter bounds associations and releases slots on control close", "[gost][adapter][limits]") {
    dcpp::Socket listener;
    listener.create(dcpp::Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    auto ca = proxy_test::Identity::make();
    proxy_test::GostOptions options;
    options.sessionMs = 20000;
    std::vector<std::unique_ptr<proxy_test::GostServer>> peers;
    std::vector<std::unique_ptr<QTcpSocket>> controls;
    TorrentProxyAdapter adapter;
    for(int i = 0; i < 8; ++i) {
        peers.push_back(std::make_unique<proxy_test::GostServer>(options, "IP:127.0.0.1", false, &ca));
        peers.back()->start(&listener);
        if(i == 0) {
            auto config = gostConfig(*peers.back());
            config.port = std::stoi(listener.getLocalPort());
            REQUIRE(adapter.start(config));
        }
        controls.push_back(std::make_unique<QTcpSocket>());
        authenticate(*controls.back(), adapter.endpoint());
        relayPort(associate(*controls.back()));
    }
    QTcpSocket ninth;
    authenticate(ninth, adapter.endpoint());
    auto refused = associate(ninth);
    REQUIRE(refused.size() == 10);
    REQUIRE(quint8(refused[1]) != 0);
    controls.front()->abort();
    peers.front()->join();
    peers.push_back(std::make_unique<proxy_test::GostServer>(options, "IP:127.0.0.1", false, &ca));
    peers.back()->start(&listener);
    QTcpSocket replacement;
    authenticate(replacement, adapter.endpoint());
    relayPort(associate(replacement));
    adapter.stop();
}

TEST_CASE("GOST adapter limits total workers including UDP relay threads", "[gost][adapter][limits]") {
    proxy_test::GostServer server;
    server.start();
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(gostConfig(server)));
    QTcpSocket udpControl;
    authenticate(udpControl, adapter.endpoint());
    relayPort(associate(udpControl));
    std::vector<std::unique_ptr<QTcpSocket>> idle;
    for(int i = 0; i < 62; ++i) {
        idle.push_back(std::make_unique<QTcpSocket>());
        auto& client = *idle.back();
        client.setProxy(QNetworkProxy::NoProxy);
        client.connectToHost(adapter.endpoint().host, adapter.endpoint().port);
        REQUIRE(client.waitForConnected(1000));
        QCoreApplication::processEvents();
    }
    QTcpSocket excess;
    excess.setProxy(QNetworkProxy::NoProxy);
    excess.connectToHost(adapter.endpoint().host, adapter.endpoint().port);
    REQUIRE(excess.waitForConnected(1000));
    REQUIRE(spinFor([&] { return excess.state() == QAbstractSocket::UnconnectedState; }, 1000));
    QElapsedTimer timer;
    timer.start();
    adapter.stop();
    REQUIRE(timer.elapsed() < 2000);
}

TEST_CASE("GOST adapter preserves upload half-close and remote target DNS", "[gost][adapter]") {
    const bool hostname = GENERATE(false, true);
    CAPTURE(hostname);
    std::unique_ptr<dcpp::Socket> namedListener;
    if(hostname) {
        // Match this host's first localhost family instead of assuming DNS
        // orders IPv4 before IPv6. The leaf deliberately has no ::1 IP SAN.
        const auto addresses = QHostInfo::fromName("localhost").addresses();
        REQUIRE_FALSE(addresses.isEmpty());
        const auto address = addresses.first();
        REQUIRE(address.isLoopback());
        namedListener = std::make_unique<dcpp::Socket>();
        namedListener->create(dcpp::Socket::TYPE_TCP,
            address.protocol() == QAbstractSocket::IPv6Protocol ? AF_INET6 : AF_INET);
        namedListener->bind("0", address.toString().toStdString());
        namedListener->listen();
    }
    proxy_test::GostOptions options;
    options.sessionMs = 10000;
    options.halfCloseReply = "upload-complete";
    proxy_test::GostServer server(options, "IP:127.0.0.1,DNS:localhost");
    server.start(namedListener.get());
    TorrentProxyAdapter adapter;
    auto config = gostConfig(server);
    if(hostname) { config.host = "localhost"; config.port = std::stoi(namedListener->getLocalPort()); }
    REQUIRE(adapter.start(config));
    const auto local = localConfig(adapter.endpoint());
    std::atomic<bool> done{false};
    bool passed = false;
    std::string clientError;
    std::jthread client([&] {
        try {
            dcpp::Socket socket;
            socket.proxyConnect("must-not-resolve.invalid", "443", local, 5000);
            std::string upload(512 * 1024, 'u');
            socket.writeAll(upload.data(), upload.size(), 5000);
            ::shutdown(socket.sock, 1);
            char result[15]{};
            passed = socket.readAll(result, 15, 5000) == 15 && std::string(result, 15) == "upload-complete";
        } catch(const dcpp::Exception& error) { clientError = error.getError(); }
        catch(const std::exception& error) { clientError = error.what(); }
        done = true;
    });
    pumpUntil(done);
    adapter.stop();
    client.join();
    server.join();
    CAPTURE(clientError, server.error, server.tlsAccepted, server.authReceived, server.target, server.echoedInput.size());
    REQUIRE(passed);
    REQUIRE(server.target == "must-not-resolve.invalid");
    REQUIRE(server.command == 1);
    REQUIRE(server.echoedInput.size() == 512 * 1024);
}

TEST_CASE("GOST adapter setup deadline and shutdown cancel stalled TLS and UDP", "[gost][adapter][limits]") {
    const int mode = GENERATE(0, 1, 2);
    CAPTURE(mode);
    proxy_test::GostOptions options;
    options.sessionMs = 15000;
    options.stallTls = mode != 2;
    options.stallTlsMs = 15000;
    options.stallData = mode == 2;
    proxy_test::GostServer server(options);
    server.start();
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(gostConfig(server)));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    QElapsedTimer setup;
    setup.start();
    if(mode == 0) {
        const auto response = associate(control);
        REQUIRE(response.size() == 10);
        REQUIRE(quint8(response[1]) != 0);
        REQUIRE(setup.elapsed() >= 9000);
        REQUIRE(setup.elapsed() < 11000);
    } else if(mode == 1) {
        control.write(QByteArray::fromHex("05030001000000000000"));
        spinFor([] { return false; }, 150);
    } else {
        const auto port = relayPort(associate(control));
        QUdpSocket sender;
        bindUdp(sender);
        sender.writeDatagram(packet(), QHostAddress::LocalHost, port);
    }
    QElapsedTimer shutdown;
    shutdown.start();
    adapter.stop();
    REQUIRE(shutdown.elapsed() < 2000);
    REQUIRE(adapter.endpoint().port == 0);
    server.requestStop();
    server.join();
}

TEST_CASE("GOST adapter shared establishment backoff resets on success but ignores target rejection", "[gost][adapter][backoff]") {
    dcpp::Socket listener;
    listener.create(dcpp::Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    auto ca = proxy_test::Identity::make();
    TorrentProxyAdapter adapter;
    auto attempt = [&](int status, bool udp, int minimumDelay, int maximumDelay, bool first = false) {
        proxy_test::GostOptions opts;
        opts.sessionMs = 15000;
        if(status == -1) opts.authStatus = 1;
        else opts.reply[1] = uint8_t(status);
        proxy_test::GostServer peer(opts, "IP:127.0.0.1", false, &ca);
        if(first) {
            auto config = gostConfig(peer);
            config.port = std::stoi(listener.getLocalPort());
            REQUIRE(adapter.start(config));
        }
        // The fixture's accept deadline is short; start only after backoff can
        // elapse. The queued TCP connection itself is still measured below.
        QTcpSocket control;
        authenticate(control, adapter.endpoint());
        QByteArray request = udp ? QByteArray::fromHex("05030001000000000000")
                                 : QByteArray::fromHex("050100010102030401bb");
        QElapsedTimer timer;
        timer.start();
        control.write(request);
        REQUIRE(spinFor([&] { return listener.wait(0, dcpp::Socket::WAIT_READ) == dcpp::Socket::WAIT_READ; }, maximumDelay));
        const auto dialTime = timer.elapsed();
        WARN("establishment backoff: dial_ms=" << dialTime << " expected_ms=" << minimumDelay << ".." << maximumDelay);
        peer.start(&listener);
        const auto response = readControl(control, 10, 4000);
        REQUIRE(response.size() == 10);
        REQUIRE((quint8(response[1]) == 0) == (status == 0));
        INFO("dial after " << dialTime << "ms; expected " << minimumDelay << ".." << maximumDelay);
        REQUIRE(dialTime >= minimumDelay);
        REQUIRE(dialTime < maximumDelay);
        control.abort();
        peer.requestStop();
        peer.join();
    };
    attempt(-1, true, 0, 1500, true);
    attempt(-1, false, 850, 2200);
    attempt(-1, true, 1800, 3200);
    attempt(-1, false, 3800, 5200);
    attempt(-1, true, 7800, 9200);
    attempt(-1, false, 15800, 17200);
    attempt(-1, true, 29800, 31200);
    attempt(0, false, 29800, 31200);
    attempt(5, false, 0, 900);
    attempt(5, false, 0, 900);
    attempt(-1, true, 0, 900);
    attempt(0, true, 850, 2200);
    // A cancelled backoff must not hold stop() until its retry time.
    attempt(-1, true, 0, 900);
    QTcpSocket waiting;
    authenticate(waiting, adapter.endpoint());
    waiting.write(QByteArray::fromHex("05030001000000000000"));
    spinFor([] { return false; }, 100);
    QElapsedTimer timer;
    timer.start();
    adapter.stop();
    REQUIRE(timer.elapsed() < 2000);
}

#ifndef _WIN32
double adapterCpuSeconds() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
           usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}
long adapterMaxRss() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return usage.ru_maxrss * 1024;
#endif
}
TEST_CASE("GOST native UDP idle blocks for thirty seconds and shutdown is bounded", "[gost][adapter][idle]") {
    proxy_test::GostOptions options;
    options.sessionMs = 45000;
    proxy_test::GostServer server(options);
    server.start();
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(gostConfig(server)));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    const auto port = relayPort(associate(control));
    QUdpSocket sender;
    bindUdp(sender);
    sender.writeDatagram(packet(), QHostAddress::LocalHost, port);
    REQUIRE(receiveUdp(sender) == packet());
    const double before = adapterCpuSeconds();
    QElapsedTimer wall;
    wall.start();
    while(wall.elapsed() < 30000) {
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }
    const double cpu = adapterCpuSeconds() - before;
    WARN("native idle: wall_ms=" << wall.elapsed() << " process_cpu_seconds=" << cpu);
    REQUIRE(cpu < 1.0);
    REQUIRE(control.state() == QAbstractSocket::ConnectedState);
    sender.writeDatagram(packet(), QHostAddress::LocalHost, port);
    REQUIRE(receiveUdp(sender) == packet());
    wall.restart();
    adapter.stop();
    WARN("native shutdown_ms=" << wall.elapsed());
    REQUIRE(wall.elapsed() < 2000);
}
TEST_CASE("GOST UDP backpressure bounds memory and cancels a flooded stalled tunnel", "[gost][adapter][limits]") {
    proxy_test::GostOptions options;
    options.sessionMs = 20000;
    options.stallData = true;
    proxy_test::GostServer server(options);
    server.start();
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(gostConfig(server)));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    const auto port = relayPort(associate(control));
    QUdpSocket sender;
    bindUdp(sender);
    // Stay below macOS's default net.inet.udp.maxdgram without altering it.
    const auto large = packet("198.51.100.7", QByteArray(8192, 'q'));
    const long before = adapterMaxRss();
    QElapsedTimer timer;
    timer.start();
    size_t sent = 0;
    while(timer.elapsed() < 3000) {
        for(int i = 0; i < 32; ++i) {
            if(sender.writeDatagram(large, QHostAddress::LocalHost, port) == large.size()) ++sent;
        }
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    const long growth = adapterMaxRss() - before;
    WARN("backpressure: datagrams_sent=" << sent << " peak_rss_growth=" << growth);
    REQUIRE(sent > 1024);
    REQUIRE(growth < 8 * 1024 * 1024);
    REQUIRE(control.state() == QAbstractSocket::ConnectedState);
    timer.restart();
    adapter.stop();
    REQUIRE(timer.elapsed() < 2000);
    server.requestStop();
}
#endif

TEST_CASE("GOST adapter cancels queued proxy DNS without pumping GUI events", "[gost][adapter][limits]") {
    proxy_test::GostServer server;
    auto config = gostConfig(server);
    config.host = "localhost";
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(config));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    control.write(QByteArray::fromHex("05030001000000000000"));
    control.flush();
    // Resolver dispatch is queued on the owner; stop must not depend on its
    // callback running on the GUI event loop (or on synchronous getaddrinfo).
    QThread::msleep(100);
    QElapsedTimer timer;
    timer.start();
    adapter.stop();
    REQUIRE(timer.elapsed() < 2000);
    QCoreApplication::processEvents();
    REQUIRE(adapter.endpoint().port == 0);
}

TEST_CASE("GOST UDP parser enforces payload ceiling across fragmented TLS records", "[gost][adapter][limits]") {
    const bool oversized = GENERATE(false, true);
    proxy_test::GostOptions opts;
    opts.sessionMs = 10000;
    // Literal lengths: 65507 (ff e3) is allowed, 65508 (ff e4) is not.
    opts.initialData = {0xff, uint8_t(oversized ? 0xe4 : 0xe3), 0xff, 1, 198, 51, 100, 7, 1, 187};
    opts.initialData.resize(10 + (oversized ? 65508 : 65507), 0x71);
    proxy_test::GostServer server(opts);
    server.start();
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(gostConfig(server)));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    const auto port = relayPort(associate(control));
    if(oversized) {
        REQUIRE(spinFor([&] { return control.state() == QAbstractSocket::UnconnectedState; }));
    } else {
        // Unsolicited data cannot acquire a local port. The following small
        // echo proves the full maximum-size frame was consumed, not truncated.
        spinFor([] { return false; }, 100);
        REQUIRE(control.state() == QAbstractSocket::ConnectedState);
        QUdpSocket sender;
        bindUdp(sender);
        sender.writeDatagram(packet(), QHostAddress::LocalHost, port);
        REQUIRE(receiveUdp(sender) == packet());
    }
    adapter.stop();
}

TEST_CASE("GOST UDP helper never reuses a stopped association", "[gost][adapter]") {
    proxy_test::GostServer server;
    server.start();
    dcpp::Socket::StreamProxyConfig config;
    config.type = dcpp::Socket::StreamProxyConfig::Gost;
    config.host = "127.0.0.1";
    config.port = server.port();
    config.user = "fixture-user";
    config.password = "fixture-password";
    config.caPem = server.ca.pem;
    GostUdpRelay relay(config);
    QString error;
    REQUIRE(relay.start(QHostAddress::LocalHost, QHostAddress::LocalHost, 0, &error) != 0);
    REQUIRE(relay.isRunning());
    relay.requestStop();
    relay.join();
    REQUIRE_FALSE(relay.isRunning());
    REQUIRE(relay.start(QHostAddress::LocalHost, QHostAddress::LocalHost, 0, &error) == 0);
    REQUIRE_FALSE(error.isEmpty());
}

TEST_CASE("GOST UDP control EOF cancels setup without poisoning shared backoff", "[gost][adapter][limits]") {
    using dcpp::Socket;
    Socket listener;
    listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    proxy_test::GostServer identity;
    auto config = gostConfig(identity);
    config.port = std::stoi(listener.getLocalPort());
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(config));
    QTcpSocket control;
    authenticate(control, adapter.endpoint());
    control.write(QByteArray::fromHex("05030001000000000000"));
    REQUIRE(spinFor([&] { return listener.wait(0, Socket::WAIT_READ) == Socket::WAIT_READ; }));
    Socket stalled;
    stalled.accept(listener);
    stalled.setBlocking(false);
    uint8_t hello[3]{};
    REQUIRE(stalled.readAll(hello, 3, 1500) == 3);
    REQUIRE(dcpp::ByteVector(hello, hello + 3) == dcpp::ByteVector{5, 1, 0x82});
    const uint8_t selection[]{5, 0x82};
    stalled.writeAll(selection, 2, 1500);
    control.abort();
    REQUIRE(spinFor([&] {
        char bytes[4096];
        try { return stalled.read(bytes, sizeof(bytes)) == 0; }
        catch(const dcpp::SocketException&) { return true; }
    }, 1000));
    proxy_test::GostServer healthy({}, "IP:127.0.0.1", false, &identity.ca);
    healthy.start(&listener);
    QElapsedTimer timer;
    timer.start();
    QTcpSocket next;
    authenticate(next, adapter.endpoint());
    relayPort(associate(next));
    REQUIRE(timer.elapsed() < 900);
    adapter.stop();
}
