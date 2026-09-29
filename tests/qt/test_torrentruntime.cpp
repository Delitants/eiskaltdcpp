#ifdef USE_TORRENT
#include <catch2/catch_test_macros.hpp>

#include "dcpp/stdinc.h"
// Assemble real managers without application startup or a live DC profile.
#define private public
#include "dcpp/DCContext.h"
#undef private
#include "dcpp/DCPlusPlus.h"
#include "dcpp/ClientManager.h"
#include "dcpp/HashManager.h"
#include "dcpp/QueueManager.h"
#include "dcpp/SearchManager.h"
#define private public
#include "dcpp/ShareManager.h"
#undef private
#include "dcpp/UploadManager.h"

#include "TorrentRuntime.h"
#include "TorrentSharePublisher.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentSettings.h"
#include "tests/proxy/DisposableGostServer.h"
#include "TorrentProxyAdapter.h"
#include "dcpp/ProxyRoute.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUdpSocket>

using namespace eiskalt::torrent;

namespace {
struct IdleSocksProxy {
    QTcpServer server;
    QUdpSocket relay;
    bool unexpectedTraffic = false;

    IdleSocksProxy()
    {
        REQUIRE(server.listen(QHostAddress::LocalHost, 0));
        REQUIRE(relay.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QObject::connect(&relay, &QUdpSocket::readyRead, &relay, [this] {
            unexpectedTraffic = true;
            while (relay.hasPendingDatagrams()) relay.readDatagram(nullptr, 0);
        });
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                struct Control { QByteArray bytes; int stage = 0; };
                auto control = std::make_shared<Control>();
                const auto consume = [this, socket, control] {
                    auto &bytes = control->bytes;
                    bytes += socket->readAll();
                    const auto reject = [&] {
                        unexpectedTraffic = true;
                        socket->disconnectFromHost();
                    };
                    if (control->stage == 0) {
                        if (bytes.size() < 2) return;
                        const int methods = static_cast<unsigned char>(bytes[1]);
                        if (bytes.size() < 2 + methods) return;
                        if (bytes[0] != char(5) || !bytes.mid(2, methods).contains(char(0))) {
                            reject();
                            return;
                        }
                        bytes.remove(0, 2 + methods);
                        socket->write(QByteArray::fromHex("0500"));
                        control->stage = 1;
                    }
                    if (control->stage == 1) {
                        if (bytes.size() < 4) return;
                        int length = 0;
                        switch (static_cast<unsigned char>(bytes[3])) {
                        case 1: length = 10; break;
                        case 4: length = 22; break;
                        case 3:
                            if (bytes.size() < 5) return;
                            length = 7 + static_cast<unsigned char>(bytes[4]);
                            break;
                        default: reject(); return;
                        }
                        if (bytes.size() < length) return;
                        // Only idle UDP control is allowed; never tunnel CONNECT traffic.
                        if (bytes[0] != char(5) || bytes[1] != char(3) || bytes[2] != char(0)) {
                            reject();
                            return;
                        }
                        bytes.remove(0, length);
                        QByteArray reply = QByteArray::fromHex("050000017f000001");
                        reply.append(char(relay.localPort() >> 8));
                        reply.append(char(relay.localPort() & 0xff));
                        socket->write(reply);
                        control->stage = 2;
                    }
                    // Leave the successful control connection alive until fixture teardown.
                    if (control->stage == 2 && !bytes.isEmpty()) reject();
                };
                QObject::connect(socket, &QTcpSocket::readyRead, socket, consume);
                consume();
            }
        });
    }
};

bool waitFor(const std::function<bool()> &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 10000)
        QTest::qWait(10);
    return ready();
}

void staysTrue(const std::function<bool()> &ready)
{
    // Cover several engine publication ticks, not just the optimistic public snapshot.
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        REQUIRE(ready());
        QTest::qWait(10);
    } while (elapsed.elapsed() < 750);
    REQUIRE(ready());
}

struct IsolatedProfile {
    QTemporaryDir directory;
    dcpp::Util::PathsMap previousPaths;
    dcpp::DCContext *previousContext = dcpp::getContext();
    QString root;

    IsolatedProfile()
    {
        REQUIRE(directory.isValid());
        // ManagedInput rejects symlink ancestors, including macOS's /var alias.
        root = QFileInfo(directory.path()).canonicalFilePath();
        REQUIRE_FALSE(root.isEmpty());
        dcpp::Util::PathsMap isolated;
        for (int i = 0; i < dcpp::Util::PATH_LAST; ++i) {
            const auto key = static_cast<dcpp::Util::Paths>(i);
            previousPaths[key] = dcpp::Util::getPath(key);
            const auto path = root + QStringLiteral("/dc-%1/").arg(i);
            REQUIRE(QDir().mkpath(path));
            isolated[key] = path.toStdString();
        }
        isolated[dcpp::Util::PATH_NOTEPAD] = (root + "/notepad.txt").toStdString();
        dcpp::Util::uninitialize();
        dcpp::Util::initialize(isolated);
    }

    ~IsolatedProfile()
    {
        dcpp::setContext(previousContext);
        dcpp::Util::uninitialize();
        dcpp::Util::initialize(previousPaths);
    }
};

struct RuntimeFixture {
    IsolatedProfile profile;
    dcpp::DCContext context;
    QObject parent;
    std::unique_ptr<TorrentRuntime> runtime;
    std::unique_ptr<QSignalSpy> publications;
    Settings settings;

    RuntimeFixture()
    {
        context.startupMinimal();
        dcpp::setContext(&context);
        context.timerManager_ = std::make_unique<dcpp::TimerManager>(context);
        context.hashManager_ = std::make_unique<dcpp::HashManager>(context);
        context.searchManager_ = std::make_unique<dcpp::SearchManager>(context);
        context.clientManager_ = std::make_unique<dcpp::ClientManager>(context);
        context.queueManager_ = std::make_unique<dcpp::QueueManager>(context);
        context.uploadManager_ = std::make_unique<dcpp::UploadManager>(context);
        context.shareManager_ = std::make_unique<dcpp::ShareManager>(context);
        context.getSettingsManager()->set(dcpp::SettingsManager::LIST_DUPES, true);
        context.getSettingsManager()->set(dcpp::SettingsManager::SHARE_SKIP_ZERO_BYTE, false);
        context.getSettingsManager()->set(dcpp::SettingsManager::COUNTRY_DB_PATH, std::string{});
        context.getSettingsManager()->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS,
                                          dcpp::SettingsManager::OUTGOING_DIRECT);

        settings.downloadPath = profile.root + "/payload";
        settings.completedPath.clear();
        settings.proxyMode = ProxyMode::FollowApplication;
        settings.bindAddress = "127.0.0.1";
        settings.bindAddress6.clear();
        // Validation requires a nominal port; randomizePort requests port 0 at runtime.
        settings.listenPort = 6881;
        settings.randomizePort = true;
        settings.dht = settings.pex = settings.localDiscovery = settings.portMapping = false;
        settings.bootstrapNodes.clear();
        settings.utp = false;
        settings.seedRatio = 0;
        settings.seedMinutes = 0;
        settings.shareName = "Runtime original";
        REQUIRE(QDir().mkpath(settings.downloadPath));
        write(settings.downloadPath + "/payload.bin", "x");

        runtime = std::make_unique<TorrentRuntime>(context, &parent);
        auto *publisher = runtime->findChild<TorrentSharePublisher *>();
        REQUIRE(publisher);
        publications = std::make_unique<QSignalSpy>(publisher, &TorrentSharePublisher::finished);
        REQUIRE(publications->isValid());
        reload();
    }

    ~RuntimeFixture()
    {
        publications.reset();
        // Join both workers before destroying managers or restoring global paths.
        runtime.reset();
        context.shutdown();
    }

    static void write(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(bytes) == bytes.size());
    }

    TorrentEngine &engine() { return *runtime->engine(); }
    dcpp::ShareManager &shares() { return *context.getShareManager(); }
    std::string owner(const QString &id) const { return "torrent:" + id.toStdString(); }
    std::string virtualFile(const QString &id) const
    {
        return ("/" + settings.shareName + " - " + id + "/payload.bin").toStdString();
    }

    void reload()
    {
        QString error;
        REQUIRE(saveSettings(TorrentRuntime::settingsPath(), settings, &error));
        runtime->reloadSettings();
    }

    QString add(bool privateTorrent = false, bool sourceReadOnly = false)
    {
        QByteArray bytes = QByteArray("d4:infod6:lengthi1e4:name11:payload.bin12:piece lengthi16384e6:pieces20:") +
            QCryptographicHash::hash("x", QCryptographicHash::Sha1);
        if (privateTorrent) bytes += "7:privatei1e";
        bytes += "ee";
        const auto path = profile.root + (privateTorrent ? "/private.torrent" : "/public.torrent");
        write(path, bytes);
        const auto id = sourceReadOnly ? engine().seedCreated(path, settings.downloadPath) :
            engine().add(path, settings.downloadPath, {}, false);
        REQUIRE_FALSE(id.isEmpty());
        return id;
    }

    Job job(const QString &id)
    {
        for (const auto &job : engine().jobs())
            if (job.id == id) return job;
        return {};
    }

    dcpp::ShareManager::ManagedFilePtr published(const QString &id)
    {
        dcpp::Lock lock(shares().cs);
        const auto found = shares().managedShares.find(owner(id));
        if (found == shares().managedShares.end() || found->second.files.size() != 1) return {};
        return found->second.files.front();
    }

    dcpp::ShareManager::ManagedFilePtr cached(const QString &id)
    {
        dcpp::Lock lock(shares().cs);
        const auto found = shares().managedVerifiedFiles.find(owner(id));
        if (found == shares().managedVerifiedFiles.end() || found->second.size() != 1) return {};
        return found->second.front();
    }

    void awaitPublished(const QString &id, qsizetype previousPublications = 0)
    {
        const bool ready = waitFor([&] {
            return publications->size() > previousPublications && job(id).complete &&
                published(id) && runtime->dcMagnets({id}).size() == 1;
        });
        const auto snapshot = job(id);
        INFO("publication count=" << publications->size() << " previous=" << previousPublications);
        INFO("complete=" << snapshot.complete << " state=" << snapshot.state.toStdString()
             << " revision=" << snapshot.revision << " error=" << snapshot.error.toStdString());
        INFO("published=" << bool(published(id)) << " magnets=" << runtime->dcMagnets({id}).size()
             << " sharing=" << runtime->shareStatus(id).toStdString()
             << " runtime error=" << runtime->lastError().toStdString());
        REQUIRE(ready);
        REQUIRE(publications->last().at(0).toString() == id);
        REQUIRE(publications->last().at(1).toBool());
        const auto hashes = shares().getManagedFileHashes(owner(id));
        REQUIRE(hashes.size() == 1);
        REQUIRE(hashes.at((settings.downloadPath + "/payload.bin").toStdString()).first == 1);
        REQUIRE(shares().toReal(virtualFile(id)) == (settings.downloadPath + "/payload.bin").toStdString());
    }

    bool hidden(const QString &id)
    {
        return !published(id) && shares().getManagedFileHashes(owner(id)).empty() &&
            runtime->dcMagnets({id}).isEmpty() && shares().getSharedFiles() == 0;
    }

    std::string fileList()
    {
        std::unique_ptr<dcpp::MemoryInputStream> stream(shares().generatePartialList("/", true));
        REQUIRE(stream);
        std::string xml(stream->getSize(), '\0');
        auto size = xml.size();
        stream->read(xml.data(), size);
        xml.resize(size);
        return xml;
    }

    void requireRetracted(const QString &id, const std::string &oldFile,
                          const dcpp::ShareManager::ManagedFilePtr &verified)
    {
        REQUIRE(hidden(id));
        REQUIRE_FALSE(shares().isTTHShared(verified->tth));
        REQUIRE(fileList().find("payload.bin") == std::string::npos);
        REQUIRE_THROWS_AS(shares().toReal(oldFile), dcpp::ShareException);
        REQUIRE_THROWS_AS(shares().toReal("TTH/" + verified->tth.toBase32()), dcpp::ShareException);
    }
};
}

TEST_CASE("Runtime restart restores unchanged sharing without preparing payload again", "[qt][torrent][runtime][managedhash-restart]") {
    RuntimeFixture f;
    const auto id = f.add();
    { INFO("initial publication"); f.awaitPublished(id); }
    const auto hash = f.published(id)->tth;
    f.publications.reset();
    f.runtime.reset();
    f.context.shareManager_.reset();
    f.context.shareManager_ = std::make_unique<dcpp::ShareManager>(f.context);
    REQUIRE(f.shares().getManagedFileHashes(f.owner(id)).empty());
    f.runtime = std::make_unique<TorrentRuntime>(f.context, &f.parent);
    const auto restored = f.cached(id);
    REQUIRE(restored);
    QSignalSpy statuses(f.runtime.get(), &TorrentRuntime::shareStatusChanged);
    auto *publisher = f.runtime->findChild<TorrentSharePublisher *>();
    REQUIRE(publisher);
    f.publications = std::make_unique<QSignalSpy>(publisher, &TorrentSharePublisher::finished);
    f.reload();
    { INFO("restored publication"); f.awaitPublished(id); }
    REQUIRE(f.published(id)->leaves == restored->leaves);
    REQUIRE(f.published(id)->modified > 0);
    REQUIRE(f.published(id)->tth == hash);
    REQUIRE(f.runtime->shareStatus(id) == "Shared in DC++");
    for(const auto& status : statuses)
        REQUIRE(status.at(1).toString() != "Preparing DC++ sharing");
}

TEST_CASE("GOST runtime snapshots trust before equality and stops on invalid replacement", "[qt][gost][routing]")
{
    proxy_test::GostServer server;
    server.start();
    RuntimeFixture f;
    f.settings.proxyMode = ProxyMode::Custom;
    f.settings.customProxyType = ProxyType::Gost;
    f.settings.gostProxy = {ProxyType::Gost, "127.0.0.1", "fixture-user", "fixture-secret", {}, server.port(), true, true};
    f.settings.gostProxy.caFile = f.profile.root + "/ca.pem";
    RuntimeFixture::write(f.settings.gostProxy.caFile, QByteArray::fromStdString(server.ca.pem));
    f.reload();
    const auto id = f.add();
    f.awaitPublished(id);
    REQUIRE(f.runtime->lastError().isEmpty());
    auto *adapter = f.runtime->findChild<TorrentProxyAdapter *>();
    REQUIRE(adapter);
    REQUIRE(adapter->endpoint().type == ProxyType::Socks5);
    REQUIRE(adapter->endpoint().udp);
    const auto endpoint = adapter->endpoint();
    const auto epoch = f.engine().configurationGeneration();
    const auto verified = f.published(id);
    const auto revision = f.job(id).revision;
    f.reload();
    staysTrue([&] { return f.engine().configurationGeneration() == epoch &&
        adapter->endpoint() == endpoint && f.published(id) == verified && f.job(id).revision == revision; });
    f.settings.gostProxy.udp = false;
    f.reload();
    REQUIRE_FALSE(adapter->endpoint().udp);
    f.settings.gostProxy.udp = true;
    f.reload();
    REQUIRE(adapter->endpoint().udp);
    const auto replacementEpoch = f.engine().configurationGeneration();
    const auto replacementEndpoint = adapter->endpoint();
    const auto replacement = proxy_test::Identity::make();
    RuntimeFixture::write(f.settings.gostProxy.caFile, QByteArray::fromStdString(replacement.pem));
    f.reload();
    REQUIRE(f.engine().configurationGeneration() > replacementEpoch);
    REQUIRE_FALSE(adapter->endpoint() == replacementEndpoint);
    REQUIRE(f.runtime->lastError().isEmpty());
    SECTION("invalid PEM") { RuntimeFixture::write(f.settings.gostProxy.caFile, "not a CA"); }
    SECTION("missing file") { REQUIRE(QFile::remove(f.settings.gostProxy.caFile)); }
    f.reload();
    REQUIRE_FALSE(f.runtime->lastError().isEmpty());
    REQUIRE(adapter->endpoint().type == ProxyType::Direct);
    REQUIRE(waitFor([&] { return !f.job(id).error.isEmpty(); }));
    REQUIRE(f.context.getSettingsManager()->get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) ==
        dcpp::SettingsManager::OUTGOING_DIRECT);
}

TEST_CASE("Runtime stop and resume preserve verified publication without rechecks", "[qt][torrent][runtime][managedhash-reuse]")
{
    RuntimeFixture f;
    bool sourceReadOnly = false;
    SECTION("Downloaded payload") {}
    SECTION("Created source seed") { sourceReadOnly = true; }
    const auto id = f.add(false, sourceReadOnly);
    f.awaitPublished(id);
    const auto verified = f.published(id);
    const auto revision = f.job(id).revision;
    const auto epoch = f.engine().configurationGeneration();
    const auto count = f.publications->size();
    QSignalSpy invalidations(&f.engine(), &TorrentEngine::storageInvalidating);
    QSignalSpy rechecks(&f.engine(), &TorrentEngine::hashRecheckRequested);
    QSignalSpy completions(&f.engine(), &TorrentEngine::completed);
    auto *publisher = f.runtime->findChild<TorrentSharePublisher *>();
    QSignalSpy statuses(publisher, &TorrentSharePublisher::statusChanged);

    for (int operation = 0; operation < 4; ++operation) {
        CAPTURE(operation);
        if (operation == 0) f.engine().pause(id, true);
        else if (operation == 2) f.engine().stop(id);
        else f.engine().pause(id, false);
        const bool paused = operation % 2 == 0;
        REQUIRE(f.job(id).paused == paused);
        REQUIRE(f.job(id).stopped == (operation == 2));
        staysTrue([&] {
            return f.job(id).complete && f.job(id).revision == revision &&
                f.engine().configurationGeneration() == epoch && f.published(id) == verified &&
                f.cached(id) == verified && f.runtime->dcMagnets({id}).size() == 1 &&
                invalidations.isEmpty() && rechecks.isEmpty() && completions.isEmpty() &&
                statuses.isEmpty() && f.publications->size() == count;
        });
        REQUIRE(waitFor([&] { return f.job(id).paused == paused; }));
    }
}

TEST_CASE("Runtime unchanged settings proxy and country path do not reconfigure or republish", "[qt][torrent][runtime][managedhash-reuse]")
{
    // Outlive Runtime so its session closes before the idle proxy controls disappear.
    std::unique_ptr<IdleSocksProxy> proxy;
    RuntimeFixture f;
    QSignalSpy diagnostics(f.runtime.get(), &TorrentRuntime::diagnostic);
    QSignalSpy errors(f.runtime.get(), &TorrentRuntime::error);
    bool sourceReadOnly = false;
    SECTION("Downloaded payload") {}
    SECTION("Created source seed") { sourceReadOnly = true; }
    SECTION("Healthy TCP-only local SOCKS5 route") {
        proxy = std::make_unique<IdleSocksProxy>();
        f.settings.proxyMode = ProxyMode::Custom;
        f.settings.customProxyType = ProxyType::Socks5;
        f.settings.socks5Proxy = {ProxyType::Socks5, "127.0.0.1", {}, {}, {},
                                 int(proxy->server.serverPort()), true, false};
        f.reload();
    }
    const auto id = f.add(false, sourceReadOnly);
    f.awaitPublished(id);
    REQUIRE(f.runtime->lastError().isEmpty());
    REQUIRE(errors.isEmpty());
    if (proxy) {
        REQUIRE(f.job(id).proxied);
        REQUIRE(waitFor([&] {
            return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [](const auto &signal) {
                return signal.at(0).toString().contains("Torrent proxy has no UDP support");
            });
        }));
        REQUIRE_FALSE(proxy->unexpectedTraffic);
    }
    const auto verified = f.published(id);
    const auto revision = f.job(id).revision;
    const auto epoch = f.engine().configurationGeneration();
    const auto count = f.publications->size();
    QSignalSpy applied(&f.engine(), &TorrentEngine::configurationApplied);
    QSignalSpy invalidations(&f.engine(), &TorrentEngine::storageInvalidating);
    QSignalSpy rechecks(&f.engine(), &TorrentEngine::hashRecheckRequested);
    QSignalSpy completions(&f.engine(), &TorrentEngine::completed);
    auto *publisher = f.runtime->findChild<TorrentSharePublisher *>();
    QSignalSpy statuses(publisher, &TorrentSharePublisher::statusChanged);

    // Exercise the real persisted-settings/upstream-proxy/country-path snapshot.
    for (int repeat = 0; repeat < 2; ++repeat) {
        f.runtime->reloadSettings();
        staysTrue([&] {
            return f.engine().configurationGeneration() == epoch && f.job(id).revision == revision &&
                f.job(id).complete && f.published(id) == verified && f.cached(id) == verified &&
                f.runtime->dcMagnets({id}).size() == 1 && f.publications->size() == count &&
                applied.isEmpty() && invalidations.isEmpty() && rechecks.isEmpty() &&
                completions.isEmpty() && statuses.isEmpty() && errors.isEmpty() &&
                f.runtime->lastError().isEmpty() && (!proxy || !proxy->unexpectedTraffic);
        });
    }
}

TEST_CASE("Runtime settings reload reuses verified files and removes renamed roots", "[qt][torrent][runtime][managedhash-reuse]")
{
    RuntimeFixture f;
    const auto id = f.add();
    f.awaitPublished(id);
    const auto verified = f.published(id);
    const auto oldFile = f.virtualFile(id);
    const auto count = f.publications->size();
    const auto revision = f.job(id).revision;
    QSignalSpy rechecks(&f.engine(), &TorrentEngine::hashRecheckRequested);
    bool renamed = false;
    SECTION("Share name") { f.settings.shareName = "Runtime renamed"; renamed = true; }
    SECTION("Upload limit") { f.settings.uploadLimitKiB = 32; }
    f.reload();
    f.requireRetracted(id, oldFile, verified);
    REQUIRE(f.cached(id) == verified);
    f.awaitPublished(id, count);
    REQUIRE(f.job(id).revision > revision);
    REQUIRE(f.published(id) == verified);
    REQUIRE(f.cached(id) == verified);
    REQUIRE(rechecks.isEmpty());
    if (renamed) {
        REQUIRE_THROWS_AS(f.shares().toReal(oldFile), dcpp::ShareException);
        REQUIRE(f.fileList().find("Runtime original") == std::string::npos);
        REQUIRE(f.fileList().find("Runtime renamed") != std::string::npos);
    }
    staysTrue([&] { return f.published(id) == verified && f.publications->size() == count + 1; });
}

TEST_CASE("Runtime file selection retracts excluded files and revalidates eligible files", "[qt][torrent][runtime][managedhash-reuse]")
{
    RuntimeFixture f;
    const auto id = f.add();
    f.awaitPublished(id);
    const auto verified = f.published(id);
    const auto oldFile = f.virtualFile(id);
    const auto count = f.publications->size();
    QSignalSpy rechecks(&f.engine(), &TorrentEngine::hashRecheckRequested);
    f.engine().setWantedFiles(id, {});
    f.requireRetracted(id, oldFile, verified);
    REQUIRE(f.cached(id) == verified);
    REQUIRE(waitFor([&] { const auto files = f.engine().files(id); return files.size() == 1 && !files.first().wanted; }));
    staysTrue([&] { return f.hidden(id) && f.cached(id) == verified && f.publications->size() == count; });
    f.engine().setWantedFiles(id, {0});
    f.awaitPublished(id, count);
    const auto reselected = f.published(id);
    REQUIRE(reselected);
    const auto oldIdentity = verified->identity;
    const auto newIdentity = reselected->identity;
    CAPTURE(oldIdentity, newIdentity);
    // Libtorrent may reopen/touch the payload when restoring file priorities.
    // Cache reuse is valid only when the full verified identity remains unchanged.
    REQUIRE(reselected->available());
    REQUIRE(reselected->size == verified->size);
    REQUIRE(reselected->realPath == verified->realPath);
    REQUIRE(reselected->tth == verified->tth);
    REQUIRE(reselected->leaves == verified->leaves);
    if (newIdentity == oldIdentity)
        REQUIRE(reselected == verified);
    else
        REQUIRE(reselected != verified);
    REQUIRE(f.cached(id) == reselected);
    REQUIRE(rechecks.isEmpty());
}

TEST_CASE("Runtime explicit recheck immediately clears reusable hashes before republishing", "[qt][torrent][runtime][managedhash-reuse]")
{
    RuntimeFixture f;
    const auto id = f.add();
    f.awaitPublished(id);
    const auto verified = f.published(id);
    const auto oldFile = f.virtualFile(id);
    const auto count = f.publications->size();
    const auto revision = f.job(id).revision;
    QSignalSpy invalidations(&f.engine(), &TorrentEngine::storageInvalidating);
    QSignalSpy rechecks(&f.engine(), &TorrentEngine::hashRecheckRequested);
    f.engine().recheck(id);
    // No event processing: the real Runtime's direct connections must have run.
    f.requireRetracted(id, oldFile, verified);
    REQUIRE_FALSE(f.cached(id));
    REQUIRE(invalidations.size() == 1);
    REQUIRE(rechecks.size() == 1);
    REQUIRE(rechecks.first().at(0).toString() == id);
    REQUIRE(f.job(id).revision == revision + 1);
    f.awaitPublished(id, count);
    const auto refreshed = f.published(id);
    // Keep the old shared_ptr alive so an allocator cannot fake pointer reuse.
    REQUIRE(refreshed != verified);
    REQUIRE(refreshed->tth == verified->tth);
    REQUIRE(refreshed->leaves == verified->leaves);
    REQUIRE(f.cached(id) == refreshed);
    staysTrue([&] { return f.published(id) == refreshed && f.publications->size() == count + 1; });
}

TEST_CASE("Runtime private and disabled jobs never advertise retained hashes", "[qt][torrent][runtime][managedhash-reuse]")
{
    RuntimeFixture f;
    const auto id = f.add();
    f.awaitPublished(id);
    const auto verified = f.published(id);
    const auto oldFile = f.virtualFile(id);
    const auto count = f.publications->size();

    SECTION("Private metadata cannot reuse the removed public owner's publication") {
        f.engine().remove(id);
        f.requireRetracted(id, oldFile, verified);
        REQUIRE(f.cached(id) == verified);
        // Configuration joins pending removals before reusing this exact payload.
        QSignalSpy applied(&f.engine(), &TorrentEngine::configurationApplied);
        f.settings.uploadLimitKiB = 32;
        f.reload();
        REQUIRE(waitFor([&] { return !applied.isEmpty() && f.engine().jobs().isEmpty(); }));
        const auto privateId = f.add(true);
        REQUIRE(waitFor([&] { return f.job(privateId).complete && f.job(privateId).privateTorrent; }));
        staysTrue([&] {
            return f.hidden(id) && f.hidden(privateId) && f.cached(id) == verified &&
                !f.cached(privateId) && f.publications->size() == count;
        });
        f.requireRetracted(privateId, f.virtualFile(privateId), verified);
    }
    SECTION("Disabling publication or the engine retains only unadvertised cache") {
        SECTION("Sharing off") { f.settings.shareCompleted = false; }
        SECTION("Engine off") { f.settings.enabled = false; }
        QSignalSpy applied(&f.engine(), &TorrentEngine::configurationApplied);
        f.reload();
        f.requireRetracted(id, oldFile, verified);
        REQUIRE(f.cached(id) == verified);
        REQUIRE(waitFor([&] { return !applied.isEmpty(); }));
        staysTrue([&] { return f.hidden(id) && f.cached(id) == verified && f.publications->size() == count; });
        f.settings.enabled = f.settings.shareCompleted = true;
        f.reload();
        f.awaitPublished(id, count);
        REQUIRE(f.published(id) == verified);
    }
}
TEST_CASE("Global GOST inheritance uses validated immutable trust without rereading the file", "[gost-global][inheritance]")
{
    using SM = dcpp::SettingsManager;
    RuntimeFixture f;
    proxy_test::GostServer server;
    auto &sm = *f.context.getSettingsManager();
    const auto path = f.profile.root + "/inherited-ca.pem";
    RuntimeFixture::write(path, QByteArray::fromStdString(server.ca.pem));
    sm.set(SM::OUTGOING_CONNECTIONS, SM::OUTGOING_GOST);
    sm.set(SM::GOST_SERVER, "127.0.0.1"); sm.set(SM::GOST_PORT, server.port());
    sm.set(SM::GOST_USER, "fixture-user"); sm.set(SM::GOST_PASSWORD, "fixture-secret");
    sm.set(SM::GOST_CA_FILE, path.toStdString());
    f.context.getProxyRoute()->reload(sm);
    const auto route = TorrentRuntime::snapshotProxy(f.context);
    REQUIRE(route.type == ProxyType::Gost);
    CHECK(route.host == "127.0.0.1"); CHECK(route.port == server.port());
    CHECK(route.user == "fixture-user"); CHECK(route.password == "fixture-secret");
    CHECK(route.remoteDns); CHECK(route.udp);
    CHECK(route.caPem == QByteArray::fromStdString(server.ca.pem));
    CHECK(route.caFile == path);
    // Until a new batch is published, neither consumer may independently reread trust.
    RuntimeFixture::write(path, "invalid replacement");
    CHECK(TorrentRuntime::snapshotProxy(f.context) == route);
    f.reload();
    REQUIRE(waitFor([&] { return f.engine().configurationGeneration() != 0; }));
    REQUIRE(f.runtime->lastError().isEmpty());
    const auto generation = f.engine().configurationGeneration();
    auto *adapter = f.runtime->findChild<TorrentProxyAdapter *>();
    REQUIRE(adapter);
    const auto endpoint = adapter->endpoint();
    f.reload();
    CHECK(f.engine().configurationGeneration() == generation);
    CHECK(adapter->endpoint() == endpoint);
    f.context.getProxyRoute()->reload(sm);
    f.reload();
    CHECK_FALSE(f.runtime->lastError().isEmpty());
    CHECK(TorrentRuntime::snapshotProxy(f.context).host.isEmpty());
    CHECK(adapter->endpoint().type == ProxyType::Direct);
    // A separately configured Torrent-only route is unaffected by invalid global trust.
    f.settings.proxyMode = ProxyMode::Custom;
    f.settings.customProxyType = ProxyType::Socks5;
    f.settings.socks5Proxy = {ProxyType::Socks5, "127.0.0.1", {}, {}, {}, 1080, true, false};
    CHECK(selectedProxy(f.settings, TorrentRuntime::snapshotProxy(f.context)) == f.settings.socks5Proxy);
}

#endif

#ifdef USE_TORRENT
namespace {
template<class Engine> void runtimeSetExcluded(Engine &engine, const QString &id, bool excluded) {
    if constexpr (requires { engine.setDcShareExcluded(id, excluded); })
        engine.setDcShareExcluded(id, excluded);
    else FAIL("TorrentEngine has no per-job DC sharing exclusion");
}
}
TEST_CASE("Runtime DC exclusion immediately retracts and follow global republishes", "[qt][torrent][runtime][dc-exclusion]") {
    RuntimeFixture f;
    const auto id = f.add();
    f.awaitPublished(id);
    const auto verified = f.published(id);
    const auto oldFile = f.virtualFile(id);
    const auto revision = f.job(id).revision;
    const auto epoch = f.engine().configurationGeneration();
    runtimeSetExcluded(f.engine(), id, true);
    f.requireRetracted(id, oldFile, verified);
    REQUIRE_FALSE(f.engine().publicationValid(id, revision, epoch));
    REQUIRE(f.runtime->shareStatus(id).contains("excluded", Qt::CaseInsensitive));
    staysTrue([&] { return f.hidden(id); });
    auto count = f.publications->size();
    runtimeSetExcluded(f.engine(), id, false);
    f.awaitPublished(id, count);
    REQUIRE(f.settings.shareCompleted);
    f.settings.shareCompleted = false;
    f.reload();
    runtimeSetExcluded(f.engine(), id, true);
    runtimeSetExcluded(f.engine(), id, false);
    staysTrue([&] { return f.hidden(id); });
    f.settings.shareCompleted = true;
    count = f.publications->size();
    f.reload();
    f.awaitPublished(id, count);
}

TEST_CASE("Runtime follow global cannot publish a private torrent", "[qt][torrent][runtime][dc-exclusion]") {
    RuntimeFixture f;
    const auto id = f.add(true);
    REQUIRE(waitFor([&] { return f.job(id).complete; }));
    runtimeSetExcluded(f.engine(), id, true);
    runtimeSetExcluded(f.engine(), id, false);
    staysTrue([&] { return f.hidden(id); });
}
#endif
