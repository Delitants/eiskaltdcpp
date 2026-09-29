#include "dcpp/stdinc.h"
#include "dcpp/Socket.h"
#include "dcpp/GostProtocol.h"
#include "TorrentProxyAdapter.h"
#include "TorrentRuntime.h"
#include "torrent/TorrentSettings.h"
#include "tests/TestContext.h"
#include "dcpp/ProxyRoute.h"
#include "dcpp/SettingsManager.h"
#include "torrent/TorrentEngine.h"
#include "tests/proxy/PinnedGostAdapter.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QProcess>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUdpSocket>
#include <QUrlQuery>
#include <functional>
#include <atomic>
#include <thread>
#ifdef Q_OS_MACOS
#include <sandbox.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace eiskalt::torrent;
static_assert(dcpp::Socket::StreamProxyConfig::Gost == 3);
static_assert(int(ProxyType::Gost) == 4);
namespace {
bool waitFor(const std::function<bool()> &predicate, int timeout = 30000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QTest::qWait(20);
        if (predicate()) return true;
    } while (timer.elapsed() < timeout);
    return false;
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
QJsonObject readJson(const QString &path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}
void writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
    REQUIRE(file.write(data) == data.size());
    REQUIRE(file.commit());
}
void writeJson(const QString &path, const QJsonObject &data)
{
    writeFile(path, QJsonDocument(data).toJson());
}
QString qaDirectory()
{
    auto root = qEnvironmentVariable("GOST_QA_DIR");
    if (root.isEmpty()) root = QDir::tempPath();
    return root;
}
struct Fixture {
    QTemporaryDir directory{qaDirectory() + "/gost-interop-XXXXXX"};
    QJsonObject info;
    bool started = false;
    Fixture()
    {
        REQUIRE(directory.isValid());
        directory.setAutoRemove(false); // Failures and credentials stay in private QA, never the checkout.
        QProcess process;
        process.start("/bin/sh", {GOST_FIXTURE_SCRIPT, "start", directory.path()});
        REQUIRE(process.waitForFinished(30000));
        writeFile(directory.filePath("start.stderr"), process.readAllStandardError());
        INFO("private fixture evidence: " << directory.path().toStdString());
        REQUIRE(process.exitCode() == 0);
        started = true;
        info = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        REQUIRE(info["proxy"].toInt() > 0);
    }
    ~Fixture()
    {
        if (!started) return;
        QProcess stop;
        stop.start("/bin/sh", {GOST_FIXTURE_SCRIPT, "stop", directory.path()});
        if (!stop.waitForFinished(15000) || stop.exitCode() != 0)
            WARN("fixture cleanup failed: " << directory.path().toStdString());
    }
    void control(const QByteArray &op)
    {
        QLocalSocket socket;
        socket.connectToServer(directory.filePath("control.sock"));
        REQUIRE(socket.waitForConnected(2000));
        REQUIRE(socket.write(op) == op.size());
        socket.waitForBytesWritten(2000);
        REQUIRE(socket.waitForReadyRead(10000));
        REQUIRE(socket.readAll() == "ok");
    }
};
ProxyConfig upstream(const QString &root)
{
    const auto info = readJson(root + "/fixture.json"), auth = readJson(root + "/auth.json");
    ProxyConfig proxy;
    proxy.type = ProxyType::Gost;
    proxy.host = "127.0.0.1";
    proxy.port = info["proxy"].toInt();
    proxy.user = auth["username"].toString();
    proxy.password = auth["password"].toString();
    proxy.caPem = readFile(root + "/ca.pem");
    proxy.remoteDns = true;
    proxy.udp = true;
    return proxy;
}
ProxyConfig inheritedUpstream(const QString &root, const QString &mode, dcpp::DCContext &context)
{
    const auto profile = upstream(root);
    using SM = dcpp::SettingsManager;
    auto &sm = *context.getSettingsManager();
    sm.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    sm.set(SM::GOST_SERVER, profile.host.toStdString()); sm.set(SM::GOST_PORT, profile.port);
    sm.set(SM::GOST_USER, profile.user.toStdString());
    sm.set(SM::GOST_PASSWORD, (profile.password + (mode == "bad-auth" ? "incorrect" : "")).toStdString());
    sm.set(SM::GOST_CA_FILE, mode == "bad-trust" ? std::string{} : (root + "/ca.pem").toStdString());
    context.getProxyRoute()->reload(sm);
    Settings settings;
    settings.proxyMode = ProxyMode::FollowApplication;
    auto inherited = selectedProxy(settings, TorrentRuntime::snapshotProxy(context));
    REQUIRE(inherited.type == ProxyType::Gost);
    REQUIRE(inherited.revoked);
    REQUIRE_FALSE(inherited.revoked->load());
    REQUIRE(inherited.udp);
    return inherited;
}
dcpp::Socket::StreamProxyConfig nativeConfig(const ProxyConfig &proxy)
{
    dcpp::Socket::StreamProxyConfig config;
    config.type = proxy.type == ProxyType::Gost ? dcpp::Socket::StreamProxyConfig::Gost
                                              : dcpp::Socket::StreamProxyConfig::Socks5;
    config.host = proxy.host.toStdString();
    config.port = proxy.port;
    config.user = proxy.user.toStdString();
    config.password = proxy.password.toStdString();
    config.caPem = proxy.caPem.toStdString();
    return config;
}
void tcpEcho(const dcpp::Socket::StreamProxyConfig &config, const char *host, int port)
{
    dcpp::Socket socket;
    socket.proxyConnect(host, std::to_string(port), config, 4000);
    const std::string payload("generated\0binary\xff", 17);
    socket.writeAll(payload.data(), int(payload.size()), 3000);
    std::string response(payload.size(), '\0');
    REQUIRE(socket.readAll(response.data(), int(response.size()), 3000) == int(response.size()));
    REQUIRE(response == payload);
}
QByteArray exchange(QTcpSocket &socket, const QByteArray &send, int count)
{
    REQUIRE(socket.write(send) == send.size());
    socket.waitForBytesWritten(2000);
    QByteArray reply;
    REQUIRE(waitFor([&] { reply += socket.readAll(); return reply.size() >= count; }, 5000));
    return reply;
}
void authenticate(QTcpSocket &socket, const ProxyConfig &proxy)
{
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(proxy.host, proxy.port);
    REQUIRE(socket.waitForConnected(3000));
    REQUIRE(exchange(socket, QByteArray::fromHex("050102"), 2) == QByteArray::fromHex("0502"));
    QByteArray auth(1, char(1));
    auth += char(proxy.user.toUtf8().size()); auth += proxy.user.toUtf8();
    auth += char(proxy.password.toUtf8().size()); auth += proxy.password.toUtf8();
    REQUIRE(exchange(socket, auth, 2) == QByteArray::fromHex("0100"));
}
void udpTunnel(dcpp::Socket &socket, const char *host, int port)
{
    const dcpp::gost::Datagram sent{host, uint16_t(port), {0, 7, 0xff, 42, 0}};
    const auto bytes = dcpp::gost::encodeTunnel(sent);
    socket.writeAll(bytes.data(), int(bytes.size()), 3000);
    dcpp::ByteVector input;
    dcpp::gost::Datagram received;
    size_t consumed = 0;
    auto state = dcpp::gost::DecodeResult::NeedMore;
    while (state == dcpp::gost::DecodeResult::NeedMore && input.size() < 1024) {
        uint8_t byte = 0;
        REQUIRE(socket.readAll(&byte, 1, 3000) == 1);
        input.push_back(byte);
        state = dcpp::gost::decodeTunnel(input, received, consumed);
    }
    REQUIRE(state == dcpp::gost::DecodeResult::Complete);
    REQUIRE(received.payload == sent.payload);
    REQUIRE(received.port == sent.port);
}
void contain(const QList<int> &ports, bool allLoopback = false)
{
#ifdef Q_OS_MACOS
    QString policy = "(version 1)(allow default)(deny network*)"
                     "(allow network-bind (local ip \"localhost:*\"))"
                     "(allow network-inbound (local ip \"localhost:*\"))"
                     "(deny mach-lookup (global-name \"com.apple.mDNSResponder\"))";
    if (allLoopback) policy += "(allow network-outbound (remote ip \"localhost:*\"))";
    else for (const int port : ports)
        policy += QString("(allow network-outbound (remote ip \"localhost:%1\"))").arg(port);
    char *error = nullptr;
    const int result = sandbox_init(policy.toUtf8().constData(), 0, &error);
    const QString message = error ? QString::fromUtf8(error) : QString();
    if (error) sandbox_free_error(error);
    INFO(message.toStdString());
    REQUIRE(result == 0);
#else
    (void)ports; (void)allLoopback;
    FAIL("Explicit acceptance requires a supported per-process network isolation backend");
#endif
}
void deniedDirect(int port)
{
#ifdef Q_OS_MACOS
    for (const int family : {AF_INET, AF_INET6}) {
        for (const int type : {SOCK_STREAM, SOCK_DGRAM}) {
            const int fd = ::socket(family, type, 0);
            REQUIRE(fd >= 0);
            sockaddr_storage storage{};
            socklen_t size;
            if (family == AF_INET) {
                auto *a = reinterpret_cast<sockaddr_in *>(&storage);
                a->sin_family = AF_INET; a->sin_port = htons(port);
                inet_pton(AF_INET, "127.0.0.1", &a->sin_addr); size = sizeof(*a);
            } else {
                auto *a = reinterpret_cast<sockaddr_in6 *>(&storage);
                a->sin6_family = AF_INET6; a->sin6_port = htons(port);
                inet_pton(AF_INET6, "::1", &a->sin6_addr); size = sizeof(*a);
            }
            const int result = type == SOCK_STREAM ? ::connect(fd, reinterpret_cast<sockaddr *>(&storage), size)
                : int(::sendto(fd, "x", 1, 0, reinterpret_cast<sockaddr *>(&storage), size));
            const int error = errno;
            ::close(fd);
            REQUIRE(result == -1);
            REQUIRE(error == EPERM);
        }
    }
#endif
}
Settings isolated(const QString &path, bool utp, bool ipv6, int port)
{
    Settings s;
    s.downloadPath = path;
    s.proxyMode = ProxyMode::Direct;
    s.bindAddress = ipv6 ? QString() : QString("127.0.0.1");
    s.bindAddress6 = ipv6 ? QString("::1") : QString();
    s.listenPort = port;
    s.randomizePort = false;
    s.dht = s.pex = s.localDiscovery = s.portMapping = false;
    s.bootstrapNodes.clear();
    s.tcp = !utp; s.utp = utp;
    s.seedRatio = 0;
    s.encryptionMode = EncryptionMode::Disabled;
    return s;
}
struct Child {
    QProcess process;
    ~Child() {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(10000)) { process.kill(); process.waitForFinished(5000); }
        }
    }
    void start(const QString &name, const QString &root, const QString &mode)
    {
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GOST_INTEROP_ROOT", root);
        env.insert("GOST_INTEROP_MODE", mode);
        env.insert("QT_ACCESSIBILITY", "0");
        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setStandardOutputFile(root + '/' + name + '-' + mode + ".log");
        process.start(QCoreApplication::applicationFilePath(), {"gost acceptance " + name, "--reporter", "compact"});
        REQUIRE(process.waitForStarted(5000));
    }
};
QByteArray payload(char salt)
{
    QByteArray bytes(2 * 1024 * 1024, '\0');
    for (int i = 0; i < bytes.size(); ++i) bytes[i] = char((i * 17 + salt) % 251);
    return bytes;
}
void metadata(const QString &root, const QString &name, char salt, int tracker, int udpTracker, bool ipv6, bool udpOnly = false)
{
    const auto bytes = payload(salt);
    QDir().mkpath(root + "/source");
    writeFile(root + "/source/" + name + ".bin", bytes);
    QByteArray hashes;
    for (int offset = 0; offset < bytes.size(); offset += 16384)
        hashes += QCryptographicHash::hash(bytes.mid(offset, 16384), QCryptographicHash::Sha1);
    auto bstring = [](const QByteArray &b) { return QByteArray::number(b.size()) + ':' + b; };
    const auto http = QString("http://tracker.fixture.test:%1/announce").arg(tracker).toUtf8();
    const auto udp = QString(ipv6 ? "udp://[::1]:%1/announce" : "udp://127.0.0.1:%1/announce").arg(udpTracker).toUtf8();
    const auto info = QByteArray("d6:lengthi") + QByteArray::number(bytes.size()) + "e4:name" + bstring(name.toUtf8() + ".bin") +
        "12:piece lengthi16384e6:pieces" + bstring(hashes) + 'e';
    writeFile(root + '/' + name + ".torrent", udpOnly ? "d8:announce" + bstring(udp) + "4:info" + info + 'e' :
        "d8:announce" + bstring(http) + "13:announce-listll" + bstring(http) + "el" + bstring(udp) + "ee4:info" + info + 'e');
    // The controlled peer never announces. Service counters must come from the client.
    writeFile(root + "/peer-" + name + ".torrent", "d4:info" + info + 'e');
}
}

TEST_CASE("Real GOST native TLS-AUTH and plain authenticated SOCKS5 interoperate", "[gost][interop]")
{
    if (qEnvironmentVariableIsEmpty("GOST_BIN")) SKIP("Opt-in: set verified GOST_BIN for real interoperability");
    Fixture fixture;
    const auto proxy = upstream(fixture.directory.path());
    auto config = nativeConfig(proxy);
    tcpEcho(config, "127.0.0.1", fixture.info["tcp4"].toInt());
    tcpEcho(config, "::1", fixture.info["tcp6"].toInt());
    config.remoteDns = false; // GOST must still resolve the destination remotely.
    tcpEcho(config, "native.fixture.test", fixture.info["tcp4"].toInt());
    dcpp::Socket tunnel;
    tunnel.gostOpenUdpTunnel(config, 4000);
    udpTunnel(tunnel, "127.0.0.1", fixture.info["udp4"].toInt());
    udpTunnel(tunnel, "::1", fixture.info["udp6"].toInt());
    udpTunnel(tunnel, "udp.fixture.test", fixture.info["udp4"].toInt());
    tunnel.disconnect();
    for (const auto failure : {"password", "roots", "identity"}) {
        {
            INFO(failure);
            auto bad = config;
            if (std::string(failure) == "password") bad.password += "wrong";
            if (std::string(failure) == "roots") bad.caPem.clear();
            if (std::string(failure) == "identity") { bad.host = "wrong.fixture.test"; bad.connectHost = "127.0.0.1"; }
            dcpp::Socket socket;
            bool failed = false;
            try { socket.proxyConnect("127.0.0.1", std::to_string(fixture.info["tcp4"].toInt()), bad, 3000); }
            catch (const dcpp::SocketException &e) {
                failed = true;
                CHECK(e.getError().find(config.user) == std::string::npos);
                CHECK(e.getError().find(config.password) == std::string::npos);
            }
            REQUIRE(failed);
            REQUIRE_FALSE(socket.isConnected());
            REQUIRE_FALSE(socket.hasStreamProxy());
        }
    }
    config.type = dcpp::Socket::StreamProxyConfig::Socks5;
    tcpEcho(config, "127.0.0.1", fixture.info["tcp4"].toInt());
    {
        QTcpSocket socket;
        authenticate(socket, proxy);
        const int port = fixture.info["tcp6"].toInt();
        QByteArray request = QByteArray::fromHex("05010004") + QByteArray(15, '\0') + '\1';
        request += char(port >> 8); request += char(port);
        const auto reply = exchange(socket, request, 22);
        REQUIRE(reply.left(4) == QByteArray::fromHex("05000004"));
        REQUIRE(exchange(socket, "ipv6", 4) == "ipv6");
    }
    // RFC1929 + RFC1928 UDP on the very same TCP listener, not UDP-TUN.
    for (int n = 0; n < 4; ++n) {
        QTcpSocket control;
        control.setProxy(QNetworkProxy::NoProxy);
        control.connectToHost("127.0.0.1", proxy.port);
        REQUIRE(control.waitForConnected(3000));
        auto exchange = [&](const QByteArray &send, int count) {
            REQUIRE(control.write(send) == send.size());
            control.waitForBytesWritten(3000);
            QByteArray reply;
            REQUIRE(waitFor([&] { reply += control.readAll(); return reply.size() >= count; }, 3000));
            return reply;
        };
        REQUIRE(exchange(QByteArray::fromHex("050102"), 2) == QByteArray::fromHex("0502"));
        QByteArray auth(1, char(1));
        auth += char(proxy.user.toUtf8().size()); auth += proxy.user.toUtf8();
        auth += char(proxy.password.toUtf8().size()); auth += proxy.password.toUtf8();
        REQUIRE(exchange(auth, 2) == QByteArray::fromHex("0100"));
        const auto reply = exchange(QByteArray::fromHex("05030001000000000000"), 10);
        REQUIRE(reply.left(4) == QByteArray::fromHex("05000001"));
        const auto port = (uint8_t(reply[8]) << 8) | uint8_t(reply[9]);
        REQUIRE(port >= fixture.info["relay_min"].toInt());
        REQUIRE(port <= fixture.info["relay_max"].toInt());
        QUdpSocket udp;
        REQUIRE(udp.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        auto bytes = dcpp::gost::encodeSocks({"127.0.0.1", uint16_t(fixture.info["udp4"].toInt()), {1, 2, uint8_t(n)}});
        REQUIRE(udp.writeDatagram(reinterpret_cast<const char *>(bytes.data()), bytes.size(), QHostAddress::LocalHost, port) == qint64(bytes.size()));
        REQUIRE(waitFor([&] { return udp.hasPendingDatagrams(); }, 3000));
        const auto packet = udp.receiveDatagram();
        REQUIRE(packet.senderPort() == port);
        dcpp::gost::Datagram decoded;
        REQUIRE(dcpp::gost::decodeSocks(std::span(reinterpret_cast<const uint8_t *>(packet.data().constData()), packet.data().size()), decoded) == dcpp::gost::DecodeResult::Complete);
        REQUIRE(decoded.payload == dcpp::ByteVector{1, 2, uint8_t(n)});
    }
    fixture.control("restart");
    tcpEcho(nativeConfig(proxy), "127.0.0.1", fixture.info["tcp4"].toInt());
}

TEST_CASE("Real GOST adapter isolates UDP sources and rejects unauthenticated clients", "[gost][interop]")
{
    if (qEnvironmentVariableIsEmpty("GOST_BIN")) SKIP("Opt-in: set verified GOST_BIN for real interoperability");
    Fixture fixture;
    const auto upstreamProxy = upstream(fixture.directory.path());
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(upstreamProxy));
    const auto proxy = adapter.endpoint();
    for (const auto &route : {upstreamProxy, proxy}) {
        QTcpSocket anonymous;
        anonymous.setProxy(QNetworkProxy::NoProxy);
        anonymous.connectToHost(route.host, route.port);
        REQUIRE(anonymous.waitForConnected(3000));
        REQUIRE(exchange(anonymous, QByteArray::fromHex("050100"), 2) == QByteArray::fromHex("05ff"));
        QTcpSocket bad;
        bad.setProxy(QNetworkProxy::NoProxy);
        bad.connectToHost(route.host, route.port);
        REQUIRE(bad.waitForConnected(3000));
        REQUIRE(exchange(bad, QByteArray::fromHex("050102"), 2) == QByteArray::fromHex("0502"));
        REQUIRE(exchange(bad, QByteArray::fromHex("0101780178"), 2) == QByteArray::fromHex("0101"));
    }
    QTcpSocket control;
    authenticate(control, proxy);
    QUdpSocket owner, other;
    owner.setProxy(QNetworkProxy::NoProxy); other.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(owner.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    REQUIRE(other.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    auto request = QByteArray::fromHex("050300017f000001");
    request += char(owner.localPort() >> 8); request += char(owner.localPort());
    const auto reply = exchange(control, request, 10);
    REQUIRE(reply.left(8) == QByteArray::fromHex("050000017f000001"));
    const int port = (uint8_t(reply[8]) << 8) | uint8_t(reply[9]);
    const auto wire = dcpp::gost::encodeSocks({"::1", uint16_t(fixture.info["udp6"].toInt()), {1, 0, 255}});
    auto send = [&](QUdpSocket &socket) {
        REQUIRE(socket.writeDatagram(reinterpret_cast<const char *>(wire.data()), wire.size(), QHostAddress::LocalHost, port) == qint64(wire.size()));
    };
    const int before = readJson(fixture.directory.filePath("services.json"))["echo_udp"].toInt();
    send(other);
    REQUIRE_FALSE(waitFor([&] { return other.hasPendingDatagrams() || owner.hasPendingDatagrams(); }, 400));
    REQUIRE(readJson(fixture.directory.filePath("services.json"))["echo_udp"].toInt() == before);
    send(owner);
    REQUIRE(waitFor([&] { return owner.hasPendingDatagrams(); }, 5000));
    const auto datagram = owner.receiveDatagram();
    dcpp::gost::Datagram decoded;
    REQUIRE(dcpp::gost::decodeSocks(std::span(reinterpret_cast<const uint8_t *>(datagram.data().constData()), datagram.data().size()), decoded) == dcpp::gost::DecodeResult::Complete);
    REQUIRE(decoded.host == "::1");
    REQUIRE(decoded.payload == dcpp::ByteVector{1, 0, 255});
    REQUIRE(readJson(fixture.directory.filePath("services.json"))["echo_udp"].toInt() == before + 1);
    control.abort();
    QTest::qWait(150);
    send(owner);
    REQUIRE_FALSE(waitFor([&] { return owner.hasPendingDatagrams(); }, 400));
    REQUIRE(readJson(fixture.directory.filePath("services.json"))["echo_udp"].toInt() == before + 1);
    adapter.stop();
}

TEST_CASE("gost acceptance peer", "[.gost-child]")
{
    const auto root = qEnvironmentVariable("GOST_INTEROP_ROOT");
    if (root.isEmpty()) SKIP("Internal fixture child; use the public [gost][interop] cases");
    const auto mode = qEnvironmentVariable("GOST_INTEROP_MODE");
    const bool utp = mode.contains("utp"), ipv6 = mode.contains("v6");
    contain({}, true);
    QTcpServer tracker, reservation;
    REQUIRE(tracker.listen(QHostAddress::LocalHost));
    REQUIRE(reservation.listen(ipv6 ? QHostAddress::LocalHostIPv6 : QHostAddress::LocalHost));
    const auto port = reservation.serverPort();
    reservation.close();
    const auto info = readJson(root + "/fixture.json");
    dcpp::test::TestContext globalContext;
    auto route = inheritedUpstream(root, mode, *globalContext.ownedCtx);
    TorrentProxyAdapter adapter;
    REQUIRE(adapter.start(route));
    proxy_test::PinnedGostAdapter pinned;
    REQUIRE(pinned.start(adapter.endpoint().port));
    const auto local = adapter.endpoint();
    writeJson(root + "/adapter.json", {{"port", pinned.port()}, {"udp_ports", pinned.udpPorts()},
        {"real_adapter_port", local.port}, {"user", local.user}, {"password", local.password}});
    metadata(root, "download", 31, tracker.serverPort(), info[ipv6 ? "tracker6" : "tracker4"].toInt(), ipv6);
    metadata(root, "upload", 47, tracker.serverPort(), info[ipv6 ? "tracker6" : "tracker4"].toInt(), ipv6);
    metadata(root, "udp-probe", 59, tracker.serverPort(), info[ipv6 ? "tracker6" : "tracker4"].toInt(), ipv6, true);
    int announces = 0;
    QObject::connect(&tracker, &QTcpServer::newConnection, &tracker, [&] {
        while (auto *socket = tracker.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("done").toBool()) return;
                socket->setProperty("done", true);
                ++announces;
                const QUrl url("http://localhost" + QString::fromLatin1(request.split(' ').value(1)));
                QByteArray peers;
                if (QUrlQuery(url).queryItemValue("port").toUInt() != port) {
                    peers = ipv6 ? QByteArray(15, '\0') + '\1' : QByteArray::fromHex("7f000001");
                    peers += char(port >> 8); peers += char(port & 255);
                }
                const auto body = QByteArray("d8:intervali1e") + (ipv6 ? "6:peers6" : "5:peers") +
                    QByteArray::number(peers.size()) + ':' + peers + 'e';
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    TorrentEngine engine(root + "/peer-state");
    engine.configure(isolated(root + "/source", utp, ipv6, port), {});
    const auto seed = engine.add(root + "/peer-download.torrent", root + "/source");
    const auto leech = engine.add(root + "/peer-upload.torrent", root + "/peer-download");
    REQUIRE_FALSE(seed.isEmpty()); REQUIRE_FALSE(leech.isEmpty());
    REQUIRE(waitFor([&] { for (const auto &j : engine.jobs()) if (j.id == seed && j.complete) return true; return false; }));
    writeJson(root + "/peer-ready.json", {{"peer", port}, {"tracker", tracker.serverPort()}});
    QElapsedTimer lifetime; lifetime.start();
    while (!QFile::exists(root + "/peer-stop") && lifetime.elapsed() < 240000) {
        QTest::qWait(100);
        qint64 uploaded = 0, downloaded = 0;
        bool complete = false;
        for (const auto &j : engine.jobs()) {
            if (j.id == seed) uploaded = j.uploaded;
            if (j.id == leech) { downloaded = j.downloaded; complete = j.complete; }
        }
        writeJson(root + "/peer-progress.json", {{"uploaded", uploaded}, {"downloaded", downloaded}, {"complete", complete}, {"announces", announces},
            {"udp_associations", pinned.associations}, {"udp_outbound", pinned.outboundDatagrams}, {"udp_inbound", pinned.inboundDatagrams},
            {"udp_headers", pinned.udpHeaders}});
    }
    engine.shutdown();
}

TEST_CASE("gost acceptance client", "[.gost-child]")
{
    const auto root = qEnvironmentVariable("GOST_INTEROP_ROOT");
    if (root.isEmpty()) SKIP("Internal fixture child; use the public [gost][interop] cases");
    const auto mode = qEnvironmentVariable("GOST_INTEROP_MODE");
    const bool utp = mode.contains("utp"), ipv6 = mode.contains("v6");
    const auto fixture = readJson(root + "/fixture.json"), peer = readJson(root + "/peer-ready.json");
    auto proxy = upstream(root);
    if (mode == "bad-auth") proxy.password += "incorrect";
    if (mode == "bad-trust") proxy.caPem.clear();
    const auto adapterInfo = readJson(root + "/adapter.json");
    ProxyConfig endpoint;
    endpoint.type = ProxyType::Socks5;
    endpoint.host = "127.0.0.1";
    endpoint.port = adapterInfo["port"].toInt();
    endpoint.user = adapterInfo["user"].toString();
    endpoint.password = adapterInfo["password"].toString();
    endpoint.udp = true;
    REQUIRE(endpoint.type == ProxyType::Socks5);
    REQUIRE(endpoint.udp);
    QList<int> allowed{proxy.port, endpoint.port};
    for (const auto value : adapterInfo["udp_ports"].toArray()) allowed << value.toInt();
    contain(allowed);
    deniedDirect(peer["peer"].toInt());
    deniedDirect(fixture["dns"].toInt());
    if (!mode.startsWith("bad-")) {
        // Diagnose the isolation contract without allowing arbitrary localhost UDP.
        QTcpSocket control;
        authenticate(control, endpoint);
        const auto reply = exchange(control, QByteArray::fromHex("050300017f0000010000"), 10);
        REQUIRE(uint8_t(reply[1]) == 0);
        const int udpPort = (uint8_t(reply[8]) << 8) | uint8_t(reply[9]);
        QUdpSocket sender;
        sender.setProxy(QNetworkProxy::NoProxy);
        const auto wire = dcpp::gost::encodeSocks({"127.0.0.1", uint16_t(fixture["udp4"].toInt()), {1, 2, 3}});
        const auto sent = sender.writeDatagram(reinterpret_cast<const char *>(wire.data()), wire.size(), QHostAddress::LocalHost, udpPort);
        writeJson(root + "/udp-isolation.json", {{"tcp_adapter_port", endpoint.port}, {"association_port", udpPort},
            {"bytes_sent", sent}, {"socket_error", sender.errorString()}});
        REQUIRE(sent == qint64(wire.size()));
        REQUIRE(waitFor([&] { return sender.hasPendingDatagrams(); }, 5000));
    }
    auto settings = isolated(root + "/client-download", utp, ipv6, 48999);
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = proxy;
    settings.dht = true;
    settings.bootstrapNodes = QString("dht.fixture.test:%1").arg(fixture["dht"].toInt());
    QStringList errors;
    {
        TorrentEngine engine(root + "/client-state");
        QObject::connect(&engine, &TorrentEngine::error, &engine, [&](const QString &e) { errors << e; });
        engine.configure(settings, endpoint);
        const auto down = engine.add(root + "/download.torrent");
        const auto up = engine.seedCreated(root + "/upload.torrent", root + "/source");
        REQUIRE_FALSE(down.isEmpty()); REQUIRE_FALSE(up.isEmpty());
        engine.selectObservedJob(down);
        if (mode.startsWith("bad-")) {
            REQUIRE(waitFor([&] { return engine.jobs().size() == 2; }));
            QTest::qWait(6000);
            for (const auto &j : engine.jobs()) {
                if (j.id == down) { REQUIRE_FALSE(j.complete); REQUIRE(j.downloaded == 0); }
                if (j.id == up) REQUIRE(j.uploaded == 0);
            }
            deniedDirect(peer["peer"].toInt());
            REQUIRE_FALSE(QFile::exists(root + "/client-download/download.bin"));
            engine.shutdown();
            writeFile(root + "/client-complete", "fail-closed");
            return;
        }
        const auto udpProbe = engine.add(root + "/udp-probe.torrent");
        REQUIRE_FALSE(udpProbe.isEmpty());
        const bool complete = waitFor([&] {
            bool downloaded = false, uploaded = false;
            for (const auto &j : engine.jobs()) {
                if (j.id == down) downloaded = j.complete;
                if (j.id == up) uploaded = j.uploaded >= payload(47).size();
            }
            return downloaded && uploaded && readJson(root + "/peer-progress.json")["complete"].toBool();
        }, 90000);
        QJsonObject result{{"complete", complete}, {"mode", mode}, {"errors", errors.join("; ")},
                           {"direct_tcp_udp_v4_v6_denied", true}, {"adapter_port", endpoint.port}};
        for (const auto &j : engine.jobs()) result[j.name] = QJsonObject{{"downloaded", j.downloaded}, {"uploaded", j.uploaded}, {"error", j.error}, {"state", j.state}};
        writeJson(root + "/client-result.json", result);
        INFO(errors.join("; ").toStdString());
        REQUIRE(complete);
        REQUIRE(readFile(root + "/client-download/download.bin") == payload(31));
        REQUIRE(readFile(root + "/peer-download/upload.bin") == payload(47));
        REQUIRE(waitFor([&] {
            const auto services = readJson(root + "/services.json");
            return services["tracker_announce"].toInt() > 0 && services["dht_queries"].toInt() > 0;
        }, 15000));
        REQUIRE(errors.isEmpty());
        engine.remove(udpProbe);
        REQUIRE(waitFor([&] { return engine.jobs().size() == 2; }));
        engine.shutdown();
    }
    writeFile(root + "/client-ready-restart", "ready");
    REQUIRE(waitFor([&] { return QFile::exists(root + "/server-restarted"); }, 20000));
    deniedDirect(peer["peer"].toInt());
    {
        TorrentEngine resumed(root + "/client-state");
        resumed.configure(settings, endpoint);
        REQUIRE(waitFor([&] { return resumed.jobs().size() == 2; }));
        REQUIRE(waitFor([&] { for (const auto &j : resumed.jobs()) if (!j.complete) return false; return true; }));
        REQUIRE(readFile(root + "/client-download/download.bin") == payload(31));
        // Verify a fresh real Socket/adapter path after restart; pump the adapter's Qt loop.
        std::atomic<bool> done{false}, success{false};
        std::thread thread([&] { try {
            dcpp::Socket socket;
            socket.proxyConnect("restart.fixture.test", std::to_string(fixture["tcp4"].toInt()), nativeConfig(endpoint), 5000);
            socket.writeAll("ok", 2, 2000);
            char reply[2]{};
            success = socket.readAll(reply, 2, 2000) == 2 && std::string(reply, 2) == "ok";
        } catch (...) {} done = true; });
        const bool finished = waitFor([&] { return done.load(); }, 10000);
        thread.join();
        REQUIRE(finished); REQUIRE(success);
        resumed.shutdown();
    }
    writeFile(root + "/client-complete", "verified-resume-and-reconnect");
}

static void applicationAcceptance(const QString &mode)
{
    if (qEnvironmentVariableIsEmpty("GOST_BIN")) SKIP("Opt-in: set verified GOST_BIN for real interoperability");
    Fixture fixture;
    const auto root = fixture.directory.path();
    INFO("private evidence: " << root.toStdString() << " mode=" << mode.toStdString());
    Child peer, client;
    peer.start("peer", root, mode);
    REQUIRE(waitFor([&] { return QFile::exists(root + "/peer-ready.json") || peer.process.state() == QProcess::NotRunning; }));
    REQUIRE(QFile::exists(root + "/peer-ready.json"));
    client.start("client", root, mode);
    REQUIRE(waitFor([&] { return QFile::exists(root + "/client-ready-restart") || client.process.state() == QProcess::NotRunning; }, 110000));
    if (QFile::exists(root + "/client-ready-restart")) {
        fixture.control("restart");
        writeFile(root + "/server-restarted", "ready");
    }
    REQUIRE(waitFor([&] { return client.process.state() == QProcess::NotRunning; }, 40000));
    writeFile(root + "/peer-stop", "stop");
    REQUIRE(waitFor([&] { return peer.process.state() == QProcess::NotRunning; }, 15000));
    CHECK(client.process.exitCode() == 0);
    CHECK(peer.process.exitCode() == 0);
    CHECK(QFile::exists(root + "/client-complete"));
    const auto services = readJson(root + "/services.json");
    CHECK(services["tracker_announce"].toInt() > 0);
    CHECK(services["dht_queries"].toInt() > 0);
    CHECK(services["dht_replies"].toInt() > 0);
    CHECK_FALSE(services["dns"].toArray().isEmpty());
    for (const auto name : {"tracker.fixture.test", "dht.fixture.test", "restart.fixture.test"}) {
        bool observed = false;
        for (const auto entry : services["dns"].toArray())
            observed = observed || entry.toObject()["name"].toString() == name;
        INFO("remote DNS name: " << name);
        CHECK(observed);
    }
}

TEST_CASE("Real GOST application tcp-v4", "[gost][interop][gost-global]") { applicationAcceptance("tcp-v4"); }
TEST_CASE("Real GOST application utp-v4", "[gost][interop][gost-global]") { applicationAcceptance("utp-v4"); }
TEST_CASE("Real GOST application tcp-v6", "[gost][interop][gost-global]") { applicationAcceptance("tcp-v6"); }
TEST_CASE("Real GOST application utp-v6", "[gost][interop][gost-global]") { applicationAcceptance("utp-v6"); }

TEST_CASE("Real GOST application fails closed for bad authentication and trust", "[gost][interop]")
{
    if (qEnvironmentVariableIsEmpty("GOST_BIN")) SKIP("Opt-in: set verified GOST_BIN for real interoperability");
    const auto mode = GENERATE(QString("bad-auth"), QString("bad-trust"));
    Fixture fixture;
    const auto root = fixture.directory.path();
    INFO("private evidence: " << root.toStdString() << " mode=" << mode.toStdString());
    Child peer, client;
    peer.start("peer", root, mode);
    REQUIRE(waitFor([&] { return QFile::exists(root + "/peer-ready.json") || peer.process.state() == QProcess::NotRunning; }));
    REQUIRE(QFile::exists(root + "/peer-ready.json"));
    client.start("client", root, mode);
    REQUIRE(waitFor([&] { return client.process.state() == QProcess::NotRunning; }, 40000));
    writeFile(root + "/peer-stop", "stop");
    REQUIRE(waitFor([&] { return peer.process.state() == QProcess::NotRunning; }, 15000));
    REQUIRE(client.process.exitCode() == 0);
    REQUIRE(QFile::exists(root + "/client-complete"));
    const auto services = readJson(root + "/services.json");
    REQUIRE(services["tracker_announce"].toInt() == 0);
    REQUIRE(services["dht_queries"].toInt() == 0);
    REQUIRE(services["dns"].toArray().isEmpty());
}

TEST_CASE("gost acceptance core", "[.gost-child]")
{
    const auto root = qEnvironmentVariable("GOST_INTEROP_ROOT");
    if(root.isEmpty()) SKIP("Internal disposable fixture child");
    const auto info = readJson(root + "/fixture.json");
    dcpp::test::TestContext tc;
    const auto profile = inheritedUpstream(root, {}, *tc.ownedCtx);
    dcpp::Socket search, dht;
    search.setContext(tc.ownedCtx.get()); dht.setContext(tc.ownedCtx.get());
    auto roundTrip = [&](dcpp::Socket &socket, const char *host, int port, const std::string &payload, dcpp::Socket::UdpSendInfo *sendInfo) {
        socket.writeTo(host, std::to_string(port), payload.data(), payload.size(), false, sendInfo);
        REQUIRE(socket.wait(4000, dcpp::Socket::WAIT_READ) == dcpp::Socket::WAIT_READ);
        char data[128]; sockaddr_storage source{};
        REQUIRE(socket.read(data, sizeof(data), source) == payload.size());
        CHECK(std::string(data, payload.size()) == payload);
    };
    dcpp::Socket::UdpSendInfo searchRoute, dhtRoute;
    roundTrip(search, "127.0.0.1", info["udp4"].toInt(), "initial-search", &searchRoute);
    roundTrip(dht, "::1", info["udp6"].toInt(), "initial-dht", &dhtRoute);
    contain({profile.port, std::stoi(searchRoute.physicalPort), std::stoi(dhtRoute.physicalPort),
             std::stoi(search.getLocalPort()), std::stoi(dht.getLocalPort())});
    deniedDirect(info["tcp4"].toInt()); deniedDirect(info["dns"].toInt());
    for(const char *host : {"127.0.0.1", "::1", "native.fixture.test"}) {
        dcpp::Socket stream; stream.setContext(tc.ownedCtx.get());
        const int port = info[std::string(host) == "::1" ? "tcp6" : "tcp4"].toInt();
        stream.connect(host, std::to_string(port));
        stream.writeAll("dc", 2, 3000);
        char bytes[2]; REQUIRE(stream.readAll(bytes, 2, 3000) == 2);
        CHECK(std::string(bytes, 2) == "dc");
    }
    roundTrip(search, "udp.fixture.test", info["udp4"].toInt(), "search", nullptr);
    roundTrip(dht, "::1", info["udp6"].toInt(), "dht", nullptr);
    search.disconnect();
    roundTrip(dht, "::1", info["udp6"].toInt(), "still-alive", nullptr);
    tc.ownedCtx->getProxyRoute()->stop();
    CHECK_THROWS(dht.writeTo("::1", std::to_string(info["udp6"].toInt()), "revoked", 7, false));
    dcpp::Socket blocked; blocked.setContext(tc.ownedCtx.get());
    CHECK_THROWS(blocked.connect("native.fixture.test", std::to_string(info["tcp4"].toInt())));
    dht.disconnect();
}

TEST_CASE("Real global GOST core traffic remains functional when direct destinations are denied", "[gost-global][interop]")
{
    if(qEnvironmentVariableIsEmpty("GOST_BIN")) SKIP("Set verified GOST_BIN for real interoperability");
    Fixture fixture;
    Child client; client.start("core", fixture.directory.path(), "global");
    REQUIRE(waitFor([&] { return client.process.state() == QProcess::NotRunning; }, 30000));
    CHECK(client.process.exitCode() == 0);
}
