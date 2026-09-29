#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TorrentWindow.h"
#include "TransferViewModel.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentCreator.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QMainWindow>
#include <QPersistentModelIndex>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTreeView>
#include <QUrlQuery>
#include <functional>

using namespace eiskalt::torrent;

namespace {
bool eventually(const std::function<bool()> &ready, int timeout = 30000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        QTest::qWait(20);
        if (ready()) return true;
    } while (elapsed.elapsed() < timeout);
    return ready();
}

Settings isolated(const QString &path, quint16 port)
{
    Settings settings;
    settings.downloadPath = path;
    settings.proxyMode = ProxyMode::Direct;
    settings.bindAddress = "127.0.0.1";
    settings.bindAddress6.clear();
    settings.listenPort = port;
    settings.randomizePort = false;
    settings.dht = settings.pex = settings.localDiscovery = settings.portMapping = settings.utp = false;
    settings.bootstrapNodes.clear();
    settings.seedRatio = 0;
    return settings;
}

QModelIndex row(TransferViewModel &model, const QString &id)
{
    const auto key = QString("torrent:%1").arg(id);
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto index = model.index(i, 0);
        if (index.data(Qt::UserRole).toString() == key) return index;
    }
    return {};
}

template<class Model> void attach(Model &model, TorrentEngine *engine)
{
    if constexpr (requires { model.setTorrentEngine(engine); })
        model.setTorrentEngine(engine);
    else
        FAIL("Shared transfer model has no live Torrent subscription");
}
}

TEST_CASE("Shared transfers show real local downloads and uploads while Torrent tab is hidden",
          "[qt][unified-swarm]")
{
#ifdef Q_OS_UNIX
    const bool createHybrid = GENERATE(false, true);
#else
    const bool createHybrid = false;
#endif
    INFO("Creator-generated hybrid metadata=" << createHybrid);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QTcpServer tracker, seedReservation, downloadReservation;
    REQUIRE(tracker.listen(QHostAddress::LocalHost));
    REQUIRE(seedReservation.listen(QHostAddress::LocalHost));
    REQUIRE(downloadReservation.listen(QHostAddress::LocalHost));
    const auto seedPort = seedReservation.serverPort();
    const auto downloadPort = downloadReservation.serverPort();
    seedReservation.close();
    downloadReservation.close();
    int announces = 0;
    QObject::connect(&tracker, &QTcpServer::newConnection, &tracker, [&] {
        while (tracker.hasPendingConnections()) {
            auto *socket = tracker.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) return;
                socket->setProperty("answered", true);
                ++announces;
                const QUrl url("http://localhost" + QString::fromLatin1(request.split(' ').value(1)));
                QByteArray peers;
                // Never return the seeder to itself: a self-connection can ban
                // the shared loopback address and reject the test downloader.
                if (QUrlQuery(url).queryItemValue("port").toUInt() != seedPort) {
                    peers = QByteArray("\x7f\0\0\1", 4);
                    peers.append(char(seedPort >> 8));
                    peers.append(char(seedPort & 255));
                }
                const auto body = QByteArray("d8:intervali60e5:peers") + QByteArray::number(peers.size()) + ':' + peers + 'e';
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                              QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    // Generated private test content; tracker and every peer are loopback-only.
    const auto source = dir.filePath("source");
    REQUIRE(QDir().mkpath(source));
    QByteArray payload(2 * 1024 * 1024, 'x'), pieces;
    for (int i = 0; i < payload.size(); ++i) payload[i] = char((i * 17 + 31) % 251);
    for (int offset = 0; offset < payload.size(); offset += 16384)
        pieces += QCryptographicHash::hash(payload.mid(offset, 16384), QCryptographicHash::Sha1);
    QFile original(QDir(source).filePath("payload.bin"));
    REQUIRE(original.open(QIODevice::WriteOnly));
    REQUIRE(original.write(payload) == payload.size());
    original.close();
    const auto announce = QString("http://127.0.0.1:%1/announce").arg(tracker.serverPort()).toUtf8();
    const auto info = QByteArray("d6:lengthi") + QByteArray::number(payload.size()) +
        "e4:name11:payload.bin12:piece lengthi16384e6:pieces" +
        QByteArray::number(pieces.size()) + ':' + pieces + "7:privatei1ee";
    const auto metadata = QByteArray("d8:announce") + QByteArray::number(announce.size()) + ':' + announce +
        "4:info" + info + 'e';
    QFile torrent(dir.filePath("local.torrent"));
    if (createHybrid) {
        TorrentCreator creator;
        bool finished = false;
        QString creationError;
        QObject::connect(&creator, &TorrentCreator::finished, &tracker,
                         [&](const QString &, const QString &) { finished = true; });
        QObject::connect(&creator, &TorrentCreator::failed, &tracker,
                         [&](const QString &message) { creationError = message; });
        CreateTorrentOptions options;
        options.sourcePath = QFileInfo(original.fileName()).canonicalFilePath();
        options.outputPath = QDir(dir.path()).canonicalPath() + "/local.torrent";
        options.trackers = {QString::fromUtf8(announce)};
        options.privateTorrent = true;
        REQUIRE(creator.start(options));
        REQUIRE(eventually([&] { return finished || !creationError.isEmpty(); }));
        INFO(creationError.toStdString());
        REQUIRE(finished);
    } else {
        REQUIRE(torrent.open(QIODevice::WriteOnly));
        REQUIRE(torrent.write(metadata) == metadata.size());
        torrent.close();
    }

    TorrentEngine seed(dir.filePath("seed-state")), download(dir.filePath("download-state"));
    QStringList errors;
    QObject::connect(&seed, &TorrentEngine::error, &tracker, [&](const QString &message) { errors << "seed: " + message; });
    QObject::connect(&download, &TorrentEngine::error, &tracker, [&](const QString &message) { errors << "download: " + message; });
    auto seedSettings = isolated(source, seedPort);
    if (createHybrid) seedSettings.encryptionMode = EncryptionMode::Required;
    seed.configure(seedSettings, {});
    auto downloadSettings = isolated(dir.filePath("download"), downloadPort);
    if (createHybrid) downloadSettings.encryptionMode = EncryptionMode::Required;
    downloadSettings.downloadLimitKiB = 128;
    download.configure(downloadSettings, {});
    TransferViewModel model, uploads;
    attach(model, &download);
    attach(uploads, &seed);
    const QVariantMap dc{{"CID", "fixture-dc-peer"}, {"DOWN", true}, {"BGROUP", true},
        {"TARGET", dir.filePath("dc/payload.bin")}, {"FNAME", "payload.bin"},
        {"USER", "DC++ fixture peer"}, {"ESIZE", payload.size()}, {"SPEED", 16384}, {"STAT", "Downloading"}};
    model.addConnection(dc);
    model.updateTransfer(dc);
    const auto seedId = createHybrid ? seed.seedCreated(torrent.fileName(), QDir(source).canonicalPath())
                                     : seed.add(torrent.fileName());
    REQUIRE_FALSE(seedId.isEmpty());
    REQUIRE(eventually([&] { return !seed.jobs().isEmpty() && seed.jobs().first().complete; }));
    const auto id = download.add(torrent.fileName());
    REQUIRE_FALSE(id.isEmpty());
    bool sawDownload = false;
    QPersistentModelIndex selected;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, &tracker, [&] {
        sawDownload = sawDownload || row(model, id).isValid();
        if (!selected.isValid()) selected = row(model, id);
    });

    QMainWindow window;
    auto *tabs = new QTabWidget(&window);
    auto *chat = new QTextEdit("Local integration fixture: DC++ and Torrent activity share the panel below.", tabs);
    tabs->addTab(chat, "Chat");
    auto *management = new TorrentWindow(&download, dir.filePath("layout.ini"), tabs);
    tabs->addTab(management, "Torrents");
    window.setCentralWidget(tabs);
    auto *dock = new QDockWidget("Transfers", &window);
    auto *view = new QTreeView(dock);
    view->setModel(&model);
    view->setAlternatingRowColors(true);
    view->header()->setStretchLastSection(false);
    view->header()->moveSection(10, 1);
    for (int i = 0; i < model.columnCount(); ++i) view->setColumnWidth(i, 110);
    view->setColumnWidth(0, 165);
    view->setColumnWidth(2, 210);
    view->setColumnWidth(6, 200);
    dock->setWidget(view);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    window.resize(1200, 640);
    window.show();
    tabs->setCurrentWidget(chat);
    REQUIRE(management->isHidden());
    const bool observedTraffic = eventually([&] {
        const auto down = row(model, id), up = row(uploads, seedId);
        const bool active = down.isValid() && up.isValid() &&
            static_cast<TransferViewItem *>(down.internalPointer())->itemData[1].toLongLong() > 0 &&
            static_cast<TransferViewItem *>(up.internalPointer())->itemData[1].toLongLong() > 0;
        // Loopback peers can bypass rate limits and finish between snapshots.
        // In that case verify actual transmitted bytes, not a transient rate.
        const bool fastCompletion = sawDownload && up.isValid() &&
            download.jobs().value(0).complete &&
            seed.jobs().value(0).uploaded >= payload.size();
        return active || fastCompletion;
    });
    INFO("tracker announces=" << announces << " download rows=" << model.rowCount()
         << " upload rows=" << uploads.rowCount());
    const auto seedSnapshot = seed.jobs().value(0);
    INFO("seed state=" << seedSnapshot.state.toStdString() << " paused=" << seedSnapshot.paused
         << " peers=" << seedSnapshot.peers << " errors=" << errors.join("; ").toStdString());
    for (const auto &job : download.jobs()) {
        INFO("download state=" << job.state.toStdString() << " error=" << job.error.toStdString()
             << " bytes=" << job.downloaded << " rate=" << job.downloadRate << " peers=" << job.peers);
        REQUIRE(observedTraffic);
    }
    REQUIRE(observedTraffic);
    REQUIRE(sawDownload);
    REQUIRE(announces > 0);
    REQUIRE(model.rowCount() == 2);
    REQUIRE(uploads.rowCount() == 1);
    REQUIRE_FALSE(management->isWindow());
    const auto output = qEnvironmentVariable("EISKALT_UNIFIED_QA_OUTPUT");
    if (!output.isEmpty()) {
        REQUIRE(QDir().mkpath(output));
        REQUIRE(window.grab().save(QDir(output).filePath("live-shared-transfers.png")));
        tabs->setCurrentWidget(management);
        QTest::qWait(30);
        REQUIRE(window.grab().save(QDir(output).filePath("embedded-torrents.png")));
        tabs->setCurrentWidget(chat);
    }
    tabs->removeTab(tabs->indexOf(management));
    management->close();
    REQUIRE(eventually([&] { return !download.jobs().isEmpty() && download.jobs().first().complete; }));
    QFile received(dir.filePath("download/payload.bin"));
    REQUIRE(received.open(QIODevice::ReadOnly));
    REQUIRE(QCryptographicHash::hash(received.readAll(), QCryptographicHash::Sha256) ==
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256));
    REQUIRE(row(model, id).isValid());
    REQUIRE(selected == row(model, id));
    REQUIRE(model.rowCount() == 2);
    download.stop(id);
    REQUIRE(eventually([&] { return !row(model, id).isValid(); }));
    REQUIRE(model.rowCount() == 1);
    REQUIRE(download.jobs().first().stopped);
    download.pause(id, false);
    REQUIRE(eventually([&] { return row(model, id).isValid(); }));
    REQUIRE_FALSE(download.jobs().first().stopped);
    REQUIRE(model.rowCount() == 2);
    attach(model, nullptr);
    REQUIRE(model.rowCount() == 1);
    attach(uploads, nullptr);
}
