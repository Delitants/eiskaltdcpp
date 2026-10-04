#include "dcpp/stdinc.h"
#include "dcpp/ProxyRoute.h"
#include "updates/UpdateLoopbackBridge.h"
namespace updates {
struct UpdateLoopbackBridgeTestAccess {
    static QUrl committed(UpdateLoopbackBridge& bridge, const QString& feed) {
        return bridge.beginInstallerSession(feed);
    }
};
}
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QTcpServer>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QRegularExpression>
#include <array>
#ifdef __APPLE__
#include "updates/UpdateArchiveVerifier.h"
#include <QCryptographicHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#endif

namespace {
bool pump(const std::function<bool()>& done, int timeout = 3000) {
    if(done()) return true;
    QEventLoop loop;
    QTimer check, deadline;
    deadline.setSingleShot(true); deadline.setTimerType(Qt::PreciseTimer);
    QObject::connect(&check, &QTimer::timeout, &loop, [&] { if(done()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    check.start(1); deadline.start(timeout);
    loop.exec();
    return done();
}
void writePrivate(const QString& path, const QByteArray& bytes) {
    QFile file(path); REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
    REQUIRE(file.write(bytes) == bytes.size());
}
QByteArray request(const QUrl& endpoint, const QByteArray& bytes) {
    QTcpSocket socket;
    QByteArray result;
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&] { result += socket.readAll(); });
    socket.connectToHost(QHostAddress::LocalHost, endpoint.port());
    if(!pump([&] { return socket.state() != QAbstractSocket::ConnectingState; })) return {};
    if(socket.state() != QAbstractSocket::ConnectedState) return {};
    socket.write(bytes);
    const bool closed = pump([&] { return socket.state() == QAbstractSocket::UnconnectedState; });
    INFO("Bridge received bytes=" << result.size() << ", buffered=" << socket.bytesAvailable()
        << ", socket state=" << socket.state());
    REQUIRE(closed);
    result += socket.readAll();
    return result;
}
QByteArray host(const QUrl& url) { return "127.0.0.1:" + QByteArray::number(url.port()); }
QByteArray get(const QUrl& url, const QByteArray& path = {}, const QByteArray& extra = {}) {
    return "GET " + (path.isEmpty() ? url.path().toLatin1() : path) + " HTTP/1.1\r\nHost: " + host(url) + "\r\n" + extra + "\r\n";
}
struct Fixture {
    QTemporaryDir root;
    QString feed = root.filePath("feed.xml"), archive = root.filePath("archive.zip");
    QByteArray feedBytes = "<rss>original signed bytes</rss>\n<!-- signature -->";
    QByteArray archiveBytes = QByteArray("PK\3\4\0binary archive", 19);
    std::shared_ptr<dcpp::ProxyRouteSnapshot> route = std::make_shared<dcpp::ProxyRouteSnapshot>();
    updates::UpdateLoopbackBridge bridge;
    Fixture() {
        REQUIRE(root.isValid());
        writePrivate(feed, feedBytes); writePrivate(archive, archiveBytes);
    }
    QUrl start() { return bridge.beginSession(feed, route); }
};
}

#ifndef _WIN32
TEST_CASE("Committed installer IPC survives route retirement without reopening discovery", "[updates][update-bridge][update-install-bridge]") {
    Fixture f;
    f.route->revoked->store(true);
    CHECK_FALSE(f.start().isValid());
    const auto feed = updates::UpdateLoopbackBridgeTestAccess::committed(f.bridge, f.feed);
    REQUIRE(feed.isValid());
    const auto archive = f.bridge.mapArchive(f.archive, f.archiveBytes.size());
    REQUIRE(archive.isValid());
    CHECK(request(feed, get(feed)).endsWith(f.feedBytes));
    CHECK(request(archive, get(archive)).endsWith(f.archiveBytes));
    CHECK(f.route->revoked->load());
    f.bridge.stop();
    CHECK_FALSE(f.bridge.mapArchive(f.archive, f.archiveBytes.size()).isValid());
    CHECK_FALSE(f.start().isValid());
}

TEST_CASE("Update bridge serves unchanged staged bytes on capability-scoped IPv4 loopback", "[updates][update-bridge]") {
    Fixture f;
    const auto feed = f.start();
    REQUIRE(feed.isValid());
    CHECK(feed.scheme() == "http"); CHECK(feed.host() == "127.0.0.1");
    CHECK(feed.port() > 0);
    CHECK(QRegularExpression("^/[0-9a-f]{64}/feed.xml$").match(feed.path()).hasMatch());
    const auto reply = request(feed, get(feed));
    CHECK(reply.startsWith("HTTP/1.1 200 OK\r\n"));
    CHECK(reply.contains("Content-Length: " + QByteArray::number(f.feedBytes.size()) + "\r\n"));
    CHECK(reply.mid(reply.indexOf("\r\n\r\n") + 4) == f.feedBytes);
    auto archivePath = feed.path().toLatin1(); archivePath.replace("feed.xml", "update.zip");
    CHECK_FALSE(request(feed, get(feed, archivePath)).startsWith("HTTP/1.1 200"));
    const auto archive = f.bridge.mapArchive(f.archive, f.archiveBytes.size());
    REQUIRE(archive.isValid());
    const auto body = request(archive, get(archive));
    CHECK(body.mid(body.indexOf("\r\n\r\n") + 4) == f.archiveBytes);
    CHECK_FALSE(f.bridge.mapArchive(f.archive, f.archiveBytes.size()).isValid());
}

TEST_CASE("Update bridge rejects malformed unauthorized and proxy requests", "[updates][update-bridge]") {
    Fixture f; const auto url = f.start(); REQUIRE(url.isValid());
    const auto path = url.path().toLatin1();
    const QByteArray prefix = "GET " + path + " HTTP/1.1\r\n";
    for(const auto& message : std::vector<QByteArray>{
        get(url, "/wrong/feed.xml"), get(url, path + "?q=1"), get(url, path + "#x"),
        get(url, "/"), get(url, "/../feed.xml"), get(url, path + "/.."),
        get(url, "/%2e%2e/feed.xml"), get(url, "http://" + host(url) + path),
        "POST " + path + " HTTP/1.1\r\nHost: " + host(url) + "\r\n\r\n",
        "CONNECT example.com:443 HTTP/1.1\r\nHost: " + host(url) + "\r\n\r\n",
        prefix + "Host: localhost:" + QByteArray::number(url.port()) + "\r\n\r\n",
        prefix + "Host: attacker.invalid\r\n\r\n", prefix + "\r\n",
        get(url, {}, "Host: " + host(url) + "\r\n"),
        get(url, {}, "Transfer-Encoding: chunked\r\n"),
        get(url, {}, "Content-Length: 1\r\n") + "x",
        get(url, {}, "Content-Length: 0\r\nContent-Length: 0\r\n"),
        get(url, {}, "Range: bytes=1-4\r\n"),
        get(url, {}, "Upgrade: websocket\r\n"),
        get(url, {}, "Expect: 100-continue\r\n"),
        get(url, {}, "Authorization: secret\r\n"),
        get(url, {}, " Bad: folded\r\n"),
        get(url, {}, "Bad Header: invalid\r\n"),
        get(url) + get(url)
    }) {
        const auto response = request(url, message);
        CHECK_FALSE(response.startsWith("HTTP/1.1 200"));
        CHECK_FALSE(response.contains(f.feedBytes));
        CHECK_FALSE(response.contains("secret"));
    }
    CHECK(request(url, get(url)).startsWith("HTTP/1.1 200"));
}

TEST_CASE("Update bridge invalidates sessions on stop restart and route revocation", "[updates][update-bridge]") {
    Fixture f; const auto first = f.start(); REQUIRE(first.isValid());
    const auto second = f.start(); REQUIRE(second.isValid());
    CHECK(first.path() != second.path());
    CHECK_FALSE(request(second, get(second, first.path().toLatin1())).startsWith("HTTP/1.1 200"));
    SECTION("explicit stop") { f.bridge.stop(); }
    SECTION("revoked route") { f.route->revoked->store(true); }
    SECTION("generation changed") { ++f.route->generation; }
    CHECK_FALSE(request(second, get(second)).startsWith("HTTP/1.1 200"));
    CHECK_FALSE(f.bridge.mapArchive(f.archive, f.archiveBytes.size()).isValid());
}

TEST_CASE("Update bridge requires valid private bounded staged files", "[updates][update-bridge]") {
    Fixture f;
    SECTION("missing feed") { REQUIRE(QFile::remove(f.feed)); CHECK_FALSE(f.start().isValid()); }
    SECTION("empty feed") { writePrivate(f.feed, {}); CHECK_FALSE(f.start().isValid()); }
    SECTION("oversize feed") { writePrivate(f.feed, QByteArray(1024 * 1024 + 1, 'a')); CHECK_FALSE(f.start().isValid()); }
    SECTION("shared feed") { REQUIRE(QFile::setPermissions(f.feed, QFile::ReadOwner | QFile::ReadOther)); CHECK_FALSE(f.start().isValid()); }
    SECTION("symlink feed") {
        auto link = f.root.filePath("link.xml"); REQUIRE(QFile::link(f.feed, link)); f.feed = link;
        CHECK_FALSE(f.start().isValid());
    }
    SECTION("invalid route") { f.route->valid = false; CHECK_FALSE(f.start().isValid()); }
    SECTION("missing route") { CHECK_FALSE(f.bridge.beginSession(f.feed, {}).isValid()); }
    SECTION("archive length mismatch") { REQUIRE(f.start().isValid()); CHECK_FALSE(f.bridge.mapArchive(f.archive, 1).isValid()); }
    SECTION("archive limit") { REQUIRE(f.start().isValid()); CHECK_FALSE(f.bridge.mapArchive(f.archive, 512LL * 1024 * 1024 + 1).isValid()); }
}

TEST_CASE("Update bridge bounds request headers and concurrent clients", "[updates][update-bridge]") {
    Fixture f; const auto url = f.start(); REQUIRE(url.isValid());
    const auto oversized = get(url, {}, "X-Large: " + QByteArray(8192, 'x') + "\r\n");
    CHECK_FALSE(request(url, oversized).startsWith("HTTP/1.1 200"));
    std::array<std::unique_ptr<QTcpSocket>, 8> clients;
    for(auto& socket : clients) {
        socket = std::make_unique<QTcpSocket>();
        socket->connectToHost(QHostAddress::LocalHost, url.port());
        REQUIRE(pump([&] { return socket->state() == QAbstractSocket::ConnectedState; }));
        socket->write("GET ");
        QCoreApplication::processEvents();
    }
    CHECK_FALSE(request(url, get(url)).startsWith("HTTP/1.1 200"));
    f.bridge.stop();
    REQUIRE(pump([&] {
        return std::all_of(clients.begin(), clients.end(), [](const auto& socket) { return socket->state() == QAbstractSocket::UnconnectedState; });
    }));
}

TEST_CASE("Update bridge preserves multi-chunk bytes and rejects changed staged files", "[updates][update-bridge]") {
    Fixture f;
    f.archiveBytes = QByteArray(4 * 1024 * 1024 + 17, '\0');
    for(qsizetype i = 0; i < f.archiveBytes.size(); ++i) f.archiveBytes[i] = static_cast<char>(i % 251);
    writePrivate(f.archive, f.archiveBytes);
    REQUIRE(f.start().isValid());
    const auto archive = f.bridge.mapArchive(f.archive, f.archiveBytes.size()); REQUIRE(archive.isValid());
    SECTION("complete multi-chunk response") {
        const auto response = request(archive, get(archive, {}, "User-Agent: Sparkle fixture\r\nAccept-Encoding: gzip\r\nContent-Length: 0\r\n"));
        REQUIRE(response.startsWith("HTTP/1.1 200"));
        CHECK(response.mid(response.indexOf("\r\n\r\n") + 4) == f.archiveBytes);
    }
    SECTION("archive changed after mapping") {
        f.archiveBytes[0] = 'X'; writePrivate(f.archive, f.archiveBytes);
        CHECK_FALSE(request(archive, get(archive)).startsWith("HTTP/1.1 200"));
    }
}

TEST_CASE("Update bridge closes incomplete requests and invalidated active sessions", "[updates][update-bridge]") {
    Fixture f; const auto url = f.start(); REQUIRE(url.isValid());
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, url.port());
    REQUIRE(pump([&] { return socket.state() == QAbstractSocket::ConnectedState; }));
    socket.write("GET ");
    SECTION("fixed header deadline") {
        REQUIRE(pump([&] { return socket.state() == QAbstractSocket::UnconnectedState; }, 6500));
    }
    SECTION("route revoked without further socket activity") {
        f.route->revoked->store(true);
        REQUIRE(pump([&] { return socket.state() == QAbstractSocket::UnconnectedState; }, 1000));
    }
    SECTION("destroy bridge owner") {
        auto other = std::make_unique<updates::UpdateLoopbackBridge>();
        const auto second = other->beginSession(f.feed, f.route); REQUIRE(second.isValid());
        QTcpSocket child; child.connectToHost(QHostAddress::LocalHost, second.port());
        REQUIRE(pump([&] { return child.state() == QAbstractSocket::ConnectedState; }));
        other.reset();
        REQUIRE(pump([&] { return child.state() == QAbstractSocket::UnconnectedState; }));
    }
}

TEST_CASE("Slow updater bridge readers cannot grow an unbounded socket queue", "[updates][update-bridge]") {
    Fixture f;
    QFile large(f.archive); REQUIRE(large.open(QIODevice::WriteOnly));
    REQUIRE(large.resize(32 * 1024 * 1024)); large.close();
    REQUIRE(f.start().isValid());
    const auto url = f.bridge.mapArchive(f.archive, 32 * 1024 * 1024); REQUIRE(url.isValid());
    const auto server = f.bridge.findChild<QTcpServer*>();
    REQUIRE(server);
    CHECK(server->serverAddress() == QHostAddress::LocalHost);
    QTcpSocket socket; socket.setReadBufferSize(1024);
    socket.connectToHost(QHostAddress::LocalHost, url.port());
    REQUIRE(pump([&] { return socket.state() == QAbstractSocket::ConnectedState; }));
    socket.write(get(url));
    REQUIRE(pump([&] { return socket.bytesAvailable() > 0; }));
    QElapsedTimer timer; timer.start();
    while(timer.elapsed() < 200) {
        QCoreApplication::processEvents();
        for(const auto peer : server->findChildren<QTcpSocket*>()) CHECK(peer->bytesToWrite() <= 65536);
        QThread::msleep(5);
    }
    f.route->revoked->store(true);
    REQUIRE(pump([&] { return !server->isListening(); }, 1000));
}

#ifdef __APPLE__
TEST_CASE("Real Sparkle reads signed staged content through the Qt update bridge", "[.][updates][update-bridge-native]") {
    const auto fixture = qEnvironmentVariable("EISKALT_UPDATER_SIGNATURE_FIXTURE");
    if(fixture.isEmpty()) SKIP("Opt-in: set EISKALT_UPDATER_SIGNATURE_FIXTURE to the pinned signed Sparkle fixture");
    Fixture f;
    QDir source(fixture);
    QString mode = "information-only";
    SECTION("information only") {}
    SECTION("altered feed") { mode = "bad-feed"; }
    SECTION("verified archive handoff in disposable fixture") { mode = "valid"; }
    QFile feed(source.filePath(mode == "bad-feed" ? "bad-feed.xml" : "appcast.xml"));
    REQUIRE(feed.open(QIODevice::ReadOnly)); writePrivate(f.feed, feed.readAll());
    const auto feedUrl = f.start(); REQUIRE(feedUrl.isValid());
    QUrl archiveUrl;
    if(mode == "valid") {
        REQUIRE(QFile::remove(f.archive)); REQUIRE(QFile::copy(source.filePath("update.zip"), f.archive));
        REQUIRE(QFile::setPermissions(f.archive, QFile::ReadOwner | QFile::WriteOwner));
        QFile manifest(source.filePath("fixture.json")); REQUIRE(manifest.open(QIODevice::ReadOnly));
        const auto metadata = QJsonDocument::fromJson(manifest.readAll()).object();
        updates::ArchiveExpectation expected{QFileInfo(f.archive).size(),
            QByteArray::fromHex(metadata["files"].toObject()["update.zip"].toString().toLatin1()),
            metadata["signature"].toString().toLatin1()};
        REQUIRE(updates::verifyUpdateArchive(f.archive, expected, metadata["public_key"].toString().toLatin1()));
        archiveUrl = f.bridge.mapArchive(f.archive, expected.size); REQUIRE(archiveUrl.isValid());
    }
    const auto app = f.root.filePath("UpdaterProbe.app");
    QProcess copy; copy.start("/usr/bin/ditto", {source.filePath("old/UpdaterProbe.app"), app});
    REQUIRE(copy.waitForFinished(10000)); REQUIRE(copy.exitCode() == 0);
    const auto executable = app + "/Contents/MacOS/UpdaterProbe";
    const auto plist = app + "/Contents/Info.plist";
    const auto digest = [](const QString& path) {
        QFile file(path); if(!file.open(QIODevice::ReadOnly)) return QByteArray{};
        QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&file); return hash.result();
    };
    const auto beforeBinary = digest(executable), beforePlist = digest(plist);
    REQUIRE_FALSE(beforeBinary.isEmpty()); REQUIRE_FALSE(beforePlist.isEmpty());
    const auto home = f.root.filePath("home"); REQUIRE(QDir().mkpath(home));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("HOME", home); environment.insert("CFFIXED_USER_HOME", home);
    environment.insert("XDG_CONFIG_HOME", home + "/config");
    environment.insert("EISKALT_PROBE_FEED", feedUrl.toString());
    environment.insert("EISKALT_PROBE_ARCHIVE", archiveUrl.toString());
    environment.insert("EISKALT_PROBE_MODE", mode);
    QProcess probe; probe.setProcessEnvironment(environment);
    const QString sandbox = "(version 1)(allow default)(deny network*)"
        "(allow network-outbound (remote ip \"localhost:*\"))"
        "(allow network-bind (local ip \"localhost:*\"))"
        "(allow network-inbound (local ip \"localhost:*\"))";
    probe.start("/usr/bin/sandbox-exec", {"-p", sandbox, executable, "[bridge-probe]"});
    REQUIRE(probe.waitForStarted(5000));
    REQUIRE(pump([&] { return probe.state() == QProcess::NotRunning; }, 40000));
    const auto output = (probe.readAllStandardOutput() + probe.readAllStandardError()).replace(feedUrl.path().split('/').value(1).toLatin1(), "CAPABILITY");
    INFO(output.toStdString());
    CHECK(probe.exitStatus() == QProcess::NormalExit); CHECK(probe.exitCode() == 0);
    CHECK(output.contains("All tests passed"));
    QElapsedTimer settle; settle.start();
    REQUIRE(pump([&] { return settle.elapsed() >= 2000; }, 2500));
    CHECK(digest(executable) == beforeBinary); CHECK(digest(plist) == beforePlist);
}
#endif
#else
TEST_CASE("Windows update bridge remains unavailable without private staging ACL support", "[updates][update-bridge]") {
    Fixture f; CHECK_FALSE(f.start().isValid());
}
#endif
