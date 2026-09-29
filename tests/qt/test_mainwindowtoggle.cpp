// Full-GUI integration target only: do not link with QT_CONTEXT_MINIMAL.
#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/ClientManager.h"
#include "dcpp/Client.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/Util.h"
#include "ArenaWidgetManager.h"
#include "GlobalTimer.h"
#include "MainWindow.h"
#include "QtContext.h"
#include "SettingsConnection.h"
#include "CertificateSettings.h"
#include "Settings.h"
#include "dcpp/ProxyRoute.h"
#include <QRadioButton>
#include <QMessageBox>
#include "TorrentRuntime.h"
#include "TorrentWindow.h"
#include "TransferView.h"
#include "AutoFitColumns.h"
#include "TabButton.h"
#include "TabNavigation.h"
#include "ToolBar.h"
#include "PublicHubs.h"
#include "FileBrowserModel.h"
#include "ShareBrowser.h"
#include "UserListModel.h"
#include "WulforSettings.h"
#include "WulforUtil.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentSettings.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QThreadPool>
#include <QTabWidget>
#include <QToolButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTreeWidget>
#include <QTranslator>
#include <functional>
#include <iostream>
#include <memory>

namespace {
bool waitFor(const std::function<bool()> &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000)
        QTest::qWait(10);
    return ready();
}

struct IsolatedMainWindow {
    QTemporaryDir home;
    QByteArray previousHome = qgetenv("HOME");
    QByteArray previousConfig = qgetenv("XDG_CONFIG_HOME");
    std::unique_ptr<dcpp::DCContext> core;
    std::unique_ptr<QtContext> gui;

    IsolatedMainWindow()
    {
        REQUIRE(home.isValid());
        qputenv("HOME", home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
        const auto root = (home.path() + QLatin1Char('/')).toStdString();
        dcpp::Util::PathsMap paths;
        paths[dcpp::Util::PATH_USER_CONFIG] = root;
        paths[dcpp::Util::PATH_USER_LOCAL] = root;
        paths[dcpp::Util::PATH_DOWNLOADS] = root + "downloads/";
        dcpp::Util::initialize(paths);
        REQUIRE(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG) == root);

        // Seed a fresh passive profile before full startup can read any settings.
        core = std::make_unique<dcpp::DCContext>();
        core->startupMinimal();
        dcpp::setContext(core.get());
        auto *settings = core->getSettingsManager();
        settings->set(dcpp::SettingsManager::NICK, "offline-toolbar-test");
        settings->set(dcpp::SettingsManager::INCOMING_CONNECTIONS,
                      dcpp::SettingsManager::INCOMING_FIREWALL_PASSIVE);
        settings->set(dcpp::SettingsManager::AUTO_DETECT_CONNECTION, false);
        settings->set(dcpp::SettingsManager::USE_DHT, false);
        settings->set(dcpp::SettingsManager::BIND_ADDRESS, "127.0.0.1");
        settings->set(dcpp::SettingsManager::BIND_ADDRESS6, "::1");
        settings->set(dcpp::SettingsManager::TCP_PORT, 0);
        settings->set(dcpp::SettingsManager::UDP_PORT, 0);
        settings->set(dcpp::SettingsManager::TLS_PORT, 0);
#ifdef LUA_SCRIPT
        settings->set(dcpp::SettingsManager::USE_LUA, false);
#endif
        settings->save();
        core->shutdown();
        // shutdown() uninitializes Util, including every previously set path.
        dcpp::Util::initialize(paths);
        core = std::make_unique<dcpp::DCContext>();
        dcpp::setContext(core.get());
        core->startup();
        REQUIRE(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG) == root);
        REQUIRE(dcpp::Util::getPath(dcpp::Util::PATH_USER_LOCAL) == root);
        REQUIRE_FALSE(core->getClientManager()->isActive());
        REQUIRE_FALSE(core->getSettingsManager()->getBool(dcpp::SettingsManager::USE_DHT));
        REQUIRE(core->getSettingsManager()->get(dcpp::SettingsManager::TCP_PORT) == 0);

        eiskalt::torrent::Settings torrent;
        torrent.downloadPath = home.filePath("downloads");
        torrent.dht = torrent.pex = torrent.localDiscovery = torrent.portMapping = false;
        torrent.shareCompleted = false;
        torrent.bootstrapNodes.clear();
        REQUIRE(eiskalt::torrent::saveSettings(TorrentRuntime::settingsPath(), torrent));

        gui = std::make_unique<QtContext>(*core);
        gui->createSettings();
        gui->settings()->load();
        gui->settings()->setBool(WB_TRAY_ENABLED, false);
        gui->settings()->setBool(WB_EXIT_CONFIRM, false);
        gui->settings()->setBool(WB_ANTISPAM_ENABLED, false);
        const QString layout = qEnvironmentVariable("EISKALT_TEST_TAB_LAYOUT", "multiline");
        REQUIRE((layout == "multiline" || layout == "singleline" || layout == "sidebar"));
        gui->settings()->setBool(WB_MAINWINDOW_USE_SIDEBAR, layout == "sidebar");
        gui->settings()->setBool(WB_MAINWINDOW_USE_M_TABBAR, layout == "multiline");
        gui->createGlobalTimer();
        gui->globalTimer()->stop();
        gui->createWulforUtil();
        gui->wulforUtil()->loadIcons();
        gui->createArenaWidgetManager();
        gui->createMainWindow();
        gui->createHubManager();
        gui->createNotification();
        gui->mainWindow()->resize(1100, 800);
        gui->mainWindow()->show();
        QCoreApplication::processEvents();
        // No autoconnect call, saved hubs, core timer, peers, or resumed jobs.
    }

    ~IsolatedMainWindow()
    {
        gui.reset();
        QtContextAware::setCurrent(nullptr);
        core->shutdown();
        dcpp::setContext(nullptr);
        core.reset();
        if (previousHome.isNull()) qunsetenv("HOME"); else qputenv("HOME", previousHome);
        if (previousConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
        else qputenv("XDG_CONFIG_HOME", previousConfig);
    }
};

template<class T> T *child(QObject *parent, const char *name)
{
    auto *result = parent->findChild<T *>(QString::fromLatin1(name));
    REQUIRE(result);
    return result;
}
}

TEST_CASE("Public hubs keeps its source selector in a compact footer", "[qt][mainwindow][public-hubs-footer]")
{
    IsolatedMainWindow fixture;
    // Deliberately invalid scheme: FavoriteManager rejects it before networking.
    fixture.core->getSettingsManager()->set(dcpp::SettingsManager::HUBLIST_SERVERS,
                                           "qa://offline-only");
    PublicHubs hubs(*fixture.core);
    hubs.resize(900, 550);
    hubs.show();
    QTest::qWait(30);
    auto *table = child<QTreeView>(&hubs, "treeView");
    auto *sources = child<QComboBox>(&hubs, "comboBox_HUBS");
    auto *refresh = child<QPushButton>(&hubs, "pushButton_REFRESH");
    auto *status = child<QLabel>(&hubs, "label_STATUS");
    const auto message = QString("Download failed: test error (%1)").arg(QString(180, 'x'));
    REQUIRE(QMetaObject::invokeMethod(&hubs, "setStatus", Q_ARG(QString, message)));
    QTest::qWait(50);
    CHECK(table->geometry().top() < sources->mapTo(&hubs, QPoint()).y());
    CHECK(table->geometry().bottom() < sources->mapTo(&hubs, QPoint()).y());
    CHECK(status->mapTo(&hubs, QPoint()).y() >= table->geometry().bottom());
    CHECK(status->toolTip() == message);
    CHECK(status->text() != message);
    for (int width : {900, 500, 360}) {
        hubs.resize(width, 550);
        QTest::qWait(30);
        INFO("Requested width " << width << ", actual " << hubs.width());
        CHECK(hubs.width() == width);
        CHECK(sources->isVisible());
        CHECK(refresh->isVisible());
        CHECK(sources->width() >= 100);
        CHECK(refresh->mapTo(&hubs, refresh->rect().bottomRight()).x() < width);
        CHECK(status->mapTo(&hubs, status->rect().bottomRight()).x()
              < sources->mapTo(&hubs, QPoint()).x());
    }
    CHECK_FALSE(fixture.core->getFavoriteManager()->isDownloading());
}

TEST_CASE("Remote file rows retain scalable extension icons", "[qt][mainwindow][file-extension-icons]")
{
    IsolatedMainWindow fixture;
    FileBrowserModel model;
    auto *root = model.getRootElem();
    root->appendChild(new FileBrowserItem({QStringLiteral("movie.mp4")}, root));
    root->appendChild(new FileBrowserItem({QStringLiteral("document.pdf")}, root));
    for (int row = 0; row < 2; ++row) {
        const auto decoration = model.data(model.index(row, COLUMN_FILEBROWSER_NAME), Qt::DecorationRole);
        REQUIRE(decoration.userType() == QMetaType::QIcon);
        const auto icon = qvariant_cast<QIcon>(decoration);
        REQUIRE_FALSE(icon.isNull());
        CHECK_FALSE(icon.pixmap(QSize(32, 32)).isNull());
    }
}

TEST_CASE("Opened file list preserves supplied metadata and leaves unknown cells blank", "[qt][mainwindow][share-browser-metadata]")
{
    IsolatedMainWindow fixture;
    QStandardItemModel oldColumns(0, COLUMN_FILEBROWSER_MODIFIED);
    QHeaderView oldHeader(Qt::Horizontal);
    oldHeader.setModel(&oldColumns);
    oldHeader.resizeSection(COLUMN_FILEBROWSER_NAME, 310);
    oldHeader.resizeSection(COLUMN_FILEBROWSER_TS, 145);
    oldHeader.hideSection(COLUMN_FILEBROWSER_ESIZE);
    fixture.gui->settings()->setStr(WS_SHARE_RPANE_STATE, oldHeader.saveState().toBase64());
    const QString listPath = fixture.home.filePath("OfflineQA.xml");
    QFile listFile(listPath);
    REQUIRE(listFile.open(QIODevice::WriteOnly));
    const QByteArray xml = "<FileListing Version=\"1\" Base=\"/\"><Directory Name=\"Media\">"
        "<Directory Name=\"Subfolder\" Date=\"1690000000\"/>"
        "<File Name=\"Movie.mp4\" Size=\"2400000000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"/>"
        "<File Name=\"Document.pdf\" Size=\"42000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"/>"
        "<File Name=\"Song.mp3\" Size=\"7000000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" BR=\"320\" MA=\"MP3\" HIT=\"0\"/>"
        "<File Name=\"Known-video.mkv\" Size=\"1500000000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" BR=\"8500\" WH=\"1920x1080\" MV=\"H.264\" MA=\"AAC\" TS=\"1700000000\" Date=\"1690000000\" HIT=\"4\"/>"
        "</Directory></FileListing>";
    REQUIRE(listFile.write(xml) == xml.size());
    listFile.close();
    const auto user = fixture.core->getClientManager()->getUser(dcpp::CID(std::string(39, 'A')));
    auto *browser = new ShareBrowser(user, listPath, "Media");
    fixture.gui->arenaWidgetManager()->add(browser);
    QTreeView *files = nullptr;
    REQUIRE(waitFor([&] {
        files = browser->findChild<QTreeView *>("treeView_RPANE");
        return files && files->model() && files->model()->rowCount() == 5;
    }));
    CHECK(files->model()->headerData(COLUMN_FILEBROWSER_MODIFIED, Qt::Horizontal).toString() == "Modified");
    CHECK(files->header()->sectionSize(COLUMN_FILEBROWSER_NAME) == 310);
    CHECK(files->header()->sectionSize(COLUMN_FILEBROWSER_TS) == 145);
    CHECK(files->header()->isSectionHidden(COLUMN_FILEBROWSER_ESIZE));
    CHECK_FALSE(files->header()->isSectionHidden(COLUMN_FILEBROWSER_MODIFIED));
    for (int row = 0; row < files->model()->rowCount(); ++row) {
        auto cell = [&](int column) { return files->model()->index(row, column).data(); };
        const auto name = cell(COLUMN_FILEBROWSER_NAME).toString();
        INFO(name.toStdString());
        if (name == "Subfolder") {
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_MODIFIED).toString().isEmpty());
            CHECK(cell(COLUMN_FILEBROWSER_TS).toString().isEmpty());
        } else if (name == "Movie.mp4" || name == "Document.pdf") {
            for (int column = COLUMN_FILEBROWSER_BR; column <= COLUMN_FILEBROWSER_MODIFIED; ++column)
                CHECK(cell(column).toString().isEmpty());
        } else if (name == "Song.mp3") {
            CHECK(cell(COLUMN_FILEBROWSER_BR).toInt() == 320);
            CHECK(cell(COLUMN_FILEBROWSER_MAUDIO).toString() == "MP3");
            CHECK(cell(COLUMN_FILEBROWSER_HIT).toString() == "0");
            CHECK(cell(COLUMN_FILEBROWSER_TS).toString().isEmpty());
        } else {
            CHECK(cell(COLUMN_FILEBROWSER_WH).toString() == "1920x1080");
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_TS).toString().startsWith("1969"));
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_TS).toString().isEmpty());
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_MODIFIED).toString().isEmpty());
            CHECK(cell(COLUMN_FILEBROWSER_MODIFIED) != cell(COLUMN_FILEBROWSER_TS));
        }
        const auto decoration = files->model()->index(row, 0).data(Qt::DecorationRole);
        REQUIRE(decoration.userType() == QMetaType::QIcon);
        REQUIRE_FALSE(qvariant_cast<QIcon>(decoration).isNull());
    }
    const QString preview = qEnvironmentVariable("EISKALT_QA_SHARE_PREVIEW");
    if (!preview.isEmpty()) {
        fixture.gui->mainWindow()->resize(1450, 720);
        files->header()->resizeSections(QHeaderView::ResizeToContents);
        QTest::qWait(100);
        REQUIRE(fixture.gui->mainWindow()->grab().save(preview));
        files->horizontalScrollBar()->setValue(files->horizontalScrollBar()->maximum());
        QTest::qWait(100);
        REQUIRE(fixture.gui->mainWindow()->grab().save(preview + ".dates.png"));
    }
    fixture.gui->arenaWidgetManager()->rem(browser);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("Close current tab resolves separate arena owners", "[qt][mainwindow][tab-close-owner]")
{
    IsolatedMainWindow fixture;
    auto *manager = fixture.gui->arenaWidgetManager();
    QPointer<ScriptWidget> tab = new ScriptWidget;
    tab->setWidget(new QLabel("Offline separate-owner tab"));
    tab->setArenaShortTitle("Offline QA");
    tab->setArenaTitle("Offline QA");
    manager->add(tab);
    manager->activate(tab);
    REQUIRE(QMetaObject::invokeMethod(fixture.gui->mainWindow(), "slotCloseCurrentWidget"));
    CHECK(tab.isNull());
    if (tab)
        manager->rem(tab);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("DHT controls use inline checkboxes and preserve the advertisement preference", "[qt][dht-advertisement][ui]")
{
#ifdef WITH_DHT
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    sm.set(dcpp::SettingsManager::USE_DHT, true);
    SettingsConnection page;
    page.resize(950, 720);
    page.show();
    auto* tabs = page.findChild<QTabWidget*>();
    REQUIRE(tabs);
    tabs->setCurrentIndex(2);
    QTest::qWait(30);
    auto* group = child<QGroupBox>(&page, "groupBox_DHT");
    REQUIRE_FALSE(group->isCheckable());
    auto* enable = child<QCheckBox>(&page, "checkBox_DHT");
    auto* hide = child<QCheckBox>(&page, "checkBox_HIDE_DHT");
    auto* port = child<QSpinBox>(&page, "spinBox_DHT");
    REQUIRE(enable); REQUIRE(hide); REQUIRE(port);
    CHECK(enable->isChecked());
    CHECK_FALSE(hide->isChecked());
    CHECK(enable->text() == QStringLiteral("Enable DHT"));
    CHECK(hide->toolTip().contains("reconnect", Qt::CaseInsensitive));
    CHECK(enable->geometry().bottom() < hide->geometry().top());
    CHECK(hide->geometry().bottom() < port->geometry().top());
    CHECK(enable->geometry().left() == hide->geometry().left());
    hide->click();
    enable->click();
    CHECK_FALSE(port->isEnabled());
    CHECK(hide->isChecked());
    enable->click();
    CHECK(port->isEnabled());
    page.ok();
    CHECK(sm.getBool(dcpp::SettingsManager::USE_DHT));
    const auto saved = fixture.home.filePath("dht-setting-roundtrip.xml");
    sm.save(saved.toStdString());
    hide->click();
    page.ok();
    sm.load(saved.toStdString());
    SettingsConnection restored;
    CHECK(child<QCheckBox>(&restored, "checkBox_DHT")->isChecked());
    CHECK(child<QCheckBox>(&restored, "checkBox_HIDE_DHT")->isChecked());
    const auto preview = qEnvironmentVariable("EISKALT_DHT_PREVIEW");
    if (!preview.isEmpty()) REQUIRE(group->grab().save(preview));
#endif
}

TEST_CASE("ADC and NMDC hub handshakes honor DHT advertisement without disabling DHT", "[qt][dht-advertisement][network]")
{
#ifdef WITH_DHT
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    for (const bool adc : {true, false}) {
        for (const bool enabled : {false, true}) {
            for (const bool hidden : {false, true}) {
                INFO("ADC=" << adc << " enabled=" << enabled << " hidden=" << hidden);
                const auto path = fixture.home.filePath("dht-advertisement.xml");
                QFile config(path);
                REQUIRE(config.open(QIODevice::WriteOnly));
                config.write("<DCPlusPlus><Settings><HideDHTFromHubs type=\"int\">");
                config.write(hidden ? "1" : "0");
                config.write("</HideDHTFromHubs></Settings></DCPlusPlus>");
                config.close();
                sm.load(path.toStdString());
                sm.set(dcpp::SettingsManager::USE_DHT, enabled);
                QTcpServer hub;
                REQUIRE(hub.listen(QHostAddress::LocalHost));
                auto* manager = fixture.core->getClientManager();
                auto cleanup = [manager](dcpp::Client* client) { manager->putClient(client); };
                std::unique_ptr<dcpp::Client, decltype(cleanup)> client(
                    manager->getClient(std::string(adc ? "adc://127.0.0.1:" : "dchub://127.0.0.1:")
                                       + std::to_string(hub.serverPort())), cleanup);
                client->connect();
                REQUIRE(waitFor([&] { return hub.hasPendingConnections(); }));
                std::unique_ptr<QTcpSocket> peer(hub.nextPendingConnection());
                REQUIRE(peer);
                if (!adc) peer->write("$Lock EXTENDEDPROTOCOLABCABCABCABCABCABC Pk=Fixture|");
                QByteArray received;
                REQUIRE(waitFor([&] {
                    received += peer->readAll();
                    return adc ? received.contains('\n') : received.contains("$ValidateNick");
                }));
                CHECK(received.contains(adc ? "ADDHT0" : "DHT0") == (enabled && !hidden));
                CHECK(received.contains(adc ? "ADBAS0" : "TTHSearch"));
                CHECK(sm.getBool(dcpp::SettingsManager::USE_DHT) == enabled);
            }
        }
    }
#endif
}

TEST_CASE("Torrent runtime uses its override without changing the DC proxy", "[qt][mainwindow][torrent-routing-runtime]")
{
    IsolatedMainWindow fixture;
    QTcpServer dcProxy, torrentProxy;
    REQUIRE(dcProxy.listen(QHostAddress::LocalHost));
    REQUIRE(torrentProxy.listen(QHostAddress::LocalHost));
    int dcConnections = 0, torrentConnections = 0;
    auto countConnections = [](QTcpServer &server, int &counter) {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &counter] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                ++counter;
                socket->abort();
                socket->deleteLater();
            }
        });
    };
    countConnections(dcProxy, dcConnections);
    countConnections(torrentProxy, torrentConnections);
    auto *dc = fixture.core->getSettingsManager();
    dc->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_SOCKS5);
    dc->set(dcpp::SettingsManager::SOCKS_SERVER, "127.0.0.1");
    dc->set(dcpp::SettingsManager::SOCKS_PORT, int(dcProxy.serverPort()));
    dc->set(dcpp::SettingsManager::SOCKS_TLS, false);
    auto settings = eiskalt::torrent::loadSettings(TorrentRuntime::settingsPath());
    settings.proxyMode = eiskalt::torrent::ProxyMode::Custom;
    settings.socks5Proxy.host = "127.0.0.1";
    settings.socks5Proxy.port = torrentProxy.serverPort();
    settings.socks5Proxy.udp = false;
    REQUIRE(eiskalt::torrent::saveSettings(TorrentRuntime::settingsPath(), settings));
    auto *runtime = TorrentRuntime::instance();
    runtime->reloadSettings();
    const auto id = runtime->engine()->add(
        "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&tr=http%3A%2F%2Ffixture.invalid%3A49001%2Fannounce",
        fixture.home.filePath("downloads"));
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(waitFor([&] { return torrentConnections > 0; }));
    REQUIRE(dcConnections == 0);
    REQUIRE(dc->get(dcpp::SettingsManager::SOCKS_PORT) == dcProxy.serverPort());
    const int before = torrentConnections;
    dc->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_DIRECT);
    runtime->reloadSettings();
    REQUIRE(waitFor([&] { return torrentConnections > before; }));
    REQUIRE(dcConnections == 0);
    REQUIRE(dc->get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_DIRECT);
}

TEST_CASE("MainWindow Torrent action toggles the real singleton without losing state", "[qt][torrent-toggle][mainwindow]")
{
    INFO("Tab layout: " << qEnvironmentVariable("EISKALT_TEST_TAB_LAYOUT", "multiline").toStdString());
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    auto *action = child<QAction>(window, "toolsTorrents");
    auto *manager = fixture.gui->arenaWidgetManager();
    auto *engine = TorrentRuntime::instance()->engine();
    auto *arena = qobject_cast<QDockWidget *>(window->centralWidget());
    REQUIRE(arena);
    REQUIRE_FALSE(window->findChild<TorrentWindow *>());
    CHECK(action->isCheckable());
    CHECK_FALSE(action->isChecked());

    action->trigger();
    QPointer<TorrentWindow> tab(window->findChild<TorrentWindow *>());
    REQUIRE(tab);
    REQUIRE(tab->isVisible());
    CHECK_FALSE(tab->state() & ArenaWidget::Hidden);
    CHECK(action->isChecked());
    REQUIRE(arena->widget() == tab);

    QFile metadata(fixture.home.filePath("offline.torrent"));
    REQUIRE(metadata.open(QIODevice::WriteOnly));
    const QByteArray bytes = QByteArray("d4:infod6:lengthi1e4:name11:payload.bin12:piece lengthi16384e6:pieces20:") +
        QCryptographicHash::hash("x", QCryptographicHash::Sha1) + "ee";
    REQUIRE(metadata.write(bytes) == bytes.size());
    metadata.close();
    const QString id = engine->add(metadata.fileName(), fixture.home.filePath("downloads"), {}, true);
    REQUIRE_FALSE(id.isEmpty());
    auto *jobs = child<QTreeWidget>(tab, "torrentJobs");
    auto *files = child<QTreeWidget>(tab, "torrentFiles");
    REQUIRE(waitFor([&] { return jobs->topLevelItemCount() == 1; }));
    tab->selectJob(id);
    REQUIRE(files->topLevelItemCount() == 1);
    jobs->sortItems(1, Qt::DescendingOrder);
    jobs->setColumnWidth(0, 347);
    jobs->header()->moveSection(2, 1);
    files->setColumnWidth(0, 413);
    files->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    auto *selected = jobs->currentItem();
    const QByteArray jobHeader = jobs->header()->saveState();
    const QByteArray fileHeader = files->header()->saveState();

    action->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(tab);
    REQUIRE(tab->state() & ArenaWidget::Hidden);
    CHECK_FALSE(tab->isVisible());
    CHECK_FALSE(action->isChecked());
    REQUIRE(engine == TorrentRuntime::instance()->engine());
    REQUIRE(engine->jobs().size() == 1);
    CHECK(engine->jobs().first().paused);
    // The real worker still accepts edits while its management tab is closed.
    engine->setWantedFiles(id, {});
    REQUIRE(waitFor([&] { return !engine->files(id).first().wanted; }));

    action->trigger();
    REQUIRE(window->findChild<TorrentWindow *>() == tab);
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    CHECK(jobs->currentItem() == selected);
    CHECK(jobs->currentItem()->isSelected());
    CHECK(jobs->header()->saveState() == jobHeader);
    CHECK(files->header()->saveState() == fileHeader);
    CHECK(files->topLevelItem(0)->checkState(0) == Qt::Unchecked);

    // A different singleton makes Torrents a background tab, not a closed tab.
    auto *uploadsAction = child<QAction>(window, "toolsFinishedUploads");
    uploadsAction->trigger();
    auto *uploads = window->widgetForRole(ArenaWidget::FinishedUploads);
    manager->activate(uploads);
    REQUIRE(arena->widget() == uploads->getWidget());
    uploadsAction->trigger();
    REQUIRE(uploads->state() & ArenaWidget::Hidden);
    CHECK_FALSE(uploadsAction->isChecked());
    uploadsAction->trigger();
    REQUIRE(window->widgetForRole(ArenaWidget::FinishedUploads) == uploads);
    REQUIRE(arena->widget() == uploads->getWidget());
    CHECK(uploadsAction->isChecked());
    REQUIRE_FALSE(tab->isVisible());
    REQUIRE_FALSE(tab->state() & ArenaWidget::Hidden);
    CHECK(action->isChecked());
    action->trigger();
    REQUIRE(arena->widget() == tab);
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    action->trigger();
    REQUIRE(tab->state() & ArenaWidget::Hidden);
    CHECK_FALSE(action->isChecked());

    window->showTorrents();
    window->showTorrents();
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    // Exercise the actual transfer-details connection, including repeated shows.
    manager->rem(tab);
    for (int i = 0; i < 2; ++i) {
        REQUIRE(QMetaObject::invokeMethod(fixture.gui->transferView(), "torrentDetailsRequested",
                    Qt::DirectConnection, Q_ARG(QString, id)));
        REQUIRE(tab->isVisible());
        CHECK(jobs->currentItem()->data(0, Qt::UserRole).toString() == id);
        CHECK(action->isChecked());
    }
    // Cancel the production add dialog: opening a source must never close the tab.
    QTimer::singleShot(0, window, [window] {
        if (auto *dialog = window->findChild<QDialog *>("torrentAddDialog"))
            dialog->reject();
    });
    REQUIRE(window->openTorrentSource(metadata.fileName()));
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    manager->rem(tab);
    REQUIRE(tab->state() & ArenaWidget::Hidden);
    CHECK_FALSE(action->isChecked());
    window->showTorrents();
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    CHECK(engine->jobs().size() == 1);
    CHECK(engine->jobs().first().paused);
    CHECK_FALSE(QFile::exists(fixture.home.filePath("downloads/payload.bin")));
}

TEST_CASE("Transfer columns persist manual widths through real view reconstruction", "[qt][transfer-columns][mainwindow]")
{
    QTranslator translation;
    const QString catalog = qEnvironmentVariable("EISKALT_QA_CATALOG");
    if (!catalog.isEmpty()) {
        REQUIRE(translation.load(catalog));
        QCoreApplication::installTranslator(&translation);
    }
    IsolatedMainWindow fixture;
    const QString layoutKey = QStringLiteral("transferview/column-layout-v1");
    fixture.gui->settings()->setStr(WS_TRANSFERS_STATE, "");
    fixture.gui->settings()->setVar(layoutKey, QVariantMap{});
    QVariantMap saved;
    {
        TransferView first(*fixture.core);
        first.resize(1200, 260);
        first.show();
        auto *table = child<QTreeView>(&first, "treeView_TRANSFERS");
        auto *model = qobject_cast<TransferViewModel *>(table->model());
        REQUIRE(model);
        model->addConnection({{"CID", "offline-column-test"}, {"DOWN", true},
            {"USER", "A long example transfer username"}, {"STAT", "Download complete"},
            {"FNAME", "A full readable filename in the real transfer panel.iso"}});
        model->updateTransfer({{"CID", "offline-column-test"}, {"DOWN", true},
            {"ENCRYPTION", "TLS_AES_256_GCM_SHA384"}});
        const int text = table->fontMetrics().horizontalAdvance("TLS_AES_256_GCM_SHA384");
        REQUIRE(waitFor([&] { return table->columnWidth(COLUMN_TRANSFER_ENCRYPTION) >= text + 8; }));
        table->header()->resizeSection(COLUMN_TRANSFER_ENCRYPTION, 93);
        table->header()->moveSection(table->header()->visualIndex(COLUMN_TRANSFER_FNAME), 0);
        first.hide();
        fixture.gui->settings()->save();
        saved = fixture.gui->settings()->getVar(layoutKey).toMap();
        REQUIRE_FALSE(saved.isEmpty());
    }
    TransferView reopened(*fixture.core);
    reopened.resize(1200, 260);
    reopened.show();
    auto *table = child<QTreeView>(&reopened, "treeView_TRANSFERS");
    auto *model = qobject_cast<TransferViewModel *>(table->model());
    REQUIRE(model);
    model->addConnection({{"CID", "offline-reopen-test"}, {"DOWN", true},
        {"USER", "Example peer"}, {"FNAME", "Another readable file after reopening.iso"},
        {"STAT", QCoreApplication::translate("TransferView", "Download complete")}});
    model->updateTransfer({{"CID", "offline-reopen-test"}, {"DOWN", true},
        {"ENCRYPTION", "TLS_AES_256_GCM_SHA384"}});
    QTest::qWait(160);
    REQUIRE(table->columnWidth(COLUMN_TRANSFER_ENCRYPTION) == 93);
    REQUIRE(table->header()->visualIndex(COLUMN_TRANSFER_FNAME) == 0);
    child<QAction>(&reopened, "fitTransferColumns")->trigger();
    REQUIRE(table->columnWidth(COLUMN_TRANSFER_ENCRYPTION) >=
        table->fontMetrics().horizontalAdvance("TLS_AES_256_GCM_SHA384") + 8);
    const QString screenshot = qEnvironmentVariable("EISKALT_QA_COLUMNS_IMAGE");
    if (!screenshot.isEmpty()) {
        reopened.resize(1500, 240);
        QTest::qWait(150);
        REQUIRE(reopened.grab().save(screenshot));
    }
}

TEST_CASE("Open tabs follow repeated application appearance changes", "[qt][tab-navigation-mainwindow][tab-appearance-mainwindow]")
{
    struct RestoreAppearance {
        QPalette palette = qApp->palette();
        QString sheet = qApp->styleSheet();
        ~RestoreAppearance() {
            qApp->setPalette(palette);
            qApp->setStyleSheet(sheet);
        }
    } appearance;
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    auto *manager = fixture.gui->arenaWidgetManager();
    QList<ScriptWidget *> tabs;
    for (const auto &title : {"Public Hubs", "Offline community"}) {
        auto *tab = new ScriptWidget;
        tab->setWidget(new QLabel("Offline appearance test"));
        tab->setArenaShortTitle(title);
        tab->setArenaTitle(title);
        manager->add(tab);
        tabs.append(tab);
    }
    manager->activate(tabs.back());
    const auto *arena = qobject_cast<QDockWidget *>(window->centralWidget());
    REQUIRE(arena);
    auto *single = window->findChild<QTabBar *>("arenaTabbar");
    const auto multiline = window->findChildren<TabButton *>();
    REQUIRE((single || multiline.size() == 2));
    const QString previews = qEnvironmentVariable("EISKALT_QA_APPEARANCE");
    int step = 0;
    for (const bool dark : {true, false, true, false}) {
        INFO("Appearance step " << step << ", dark=" << dark);
        QPalette palette = appearance.palette;
        palette.setColor(QPalette::Window, QColor(dark ? "#292e34" : "#eeeeee"));
        palette.setColor(QPalette::Button, QColor(dark ? "#30363d" : "#d6d6d6"));
        palette.setColor(QPalette::Base, QColor(dark ? "#252a30" : "#ffffff"));
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
            palette.setColor(role, QColor(dark ? "#f0f3f5" : "#121212"));
        qApp->setPalette(palette);
        // Like the macOS input stylesheet, this repolishes widgets without a tab rule.
        qApp->setStyleSheet(appearance.sheet + QStringLiteral(
            "\nQLineEdit { background-color: %1; color: %2; }")
            .arg(palette.color(QPalette::Base).name(), palette.color(QPalette::Text).name()));
        QTest::qWait(40);

        const auto checkSurface = [dark](QWidget *widget, const QRect &rect) {
            const QImage image = widget->grab().toImage();
            const qreal scale = image.devicePixelRatio();
            const QPoint sample(rect.center().x(), rect.top() + 6);
            const auto color = image.pixelColor(qRound(sample.x() * scale), qRound(sample.y() * scale));
            INFO("Rendered tab surface " << color.name().toStdString());
            CHECK((color.lightness() < 128) == dark);
        };
        if (single) {
            REQUIRE(single->count() == 2);
            for (int i = 0; i < single->count(); ++i)
                checkSurface(single, single->tabRect(i));
            const auto before = single->grab().toImage();
            auto *toolbar = window->findChild<ToolBar *>();
            REQUIRE(toolbar);
            toolbar->refreshTabStyle();
            QTest::qWait(20);
            CHECK(single->grab().toImage() == before);
        } else {
            for (auto *button : multiline)
                checkSurface(button, button->rect());
        }
        for (auto *close : window->findChildren<QToolButton *>("tabCloseButton"))
            CHECK((close->palette().color(QPalette::ButtonText).lightness() > 128) == dark);
        CHECK(arena->widget() == tabs.back()->getWidget());
        if (!previews.isEmpty()) {
            QDir().mkpath(previews);
            REQUIRE(window->grab().save(QString("%1/%2-%3-%4.png").arg(previews,
                single ? "singleline" : "multiline").arg(step).arg(dark ? "dark" : "light")));
        }
        ++step;
    }
    for (auto *tab : tabs)
        manager->rem(tab);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("MainWindow keeps forty offline tabs searchable and safely selectable", "[qt][tab-navigation-mainwindow]")
{
    IsolatedMainWindow fixture;
    class OfflineHubTab final : public ScriptWidget {
    public:
        Role role() const override { return ArenaWidget::Hub; }
    };
    auto *window = fixture.gui->mainWindow();
    auto *manager = fixture.gui->arenaWidgetManager();
    auto *arena = qobject_cast<QDockWidget *>(window->centralWidget());
    REQUIRE(arena);
    QList<OfflineHubTab *> tabs;
    for (int i = 0; i < 40; ++i) {
        auto *tab = new OfflineHubTab;
        const auto url = QString("adcs://offline-%1.example.invalid:1511/?kp=SHA256/%2")
            .arg(i).arg(QString(80, 'A'));
        const auto title = i % 3 == 0 ? url : QString("Community %1 with a long readable name").arg(i);
        tab->setWidget(new QLabel("Synthetic offline tab: no network connections"));
        tab->setArenaShortTitle(title);
        tab->setArenaTitle(title + QLatin1Char('\n') + url);
        tabs.append(tab);
        manager->add(tab);
    }
    window->resize(780, 640);
    QTest::qWait(150);
    auto *all = child<QToolButton>(window, "allTabsButton");
    REQUIRE(all->isVisible());
    auto *menu = child<tab_navigation::AllTabsMenu>(window, "allTabsMenu");
    auto *search = child<QLineEdit>(menu, "tabSearch");
    auto *list = child<QListWidget>(menu, "tabSearchResults");
    auto *single = window->findChild<QTabBar *>("arenaTabbar");
    if (single) {
        REQUIRE(single->count() == 40);
        for (int i = 0; i < 40; ++i) {
            CHECK(single->tabRect(i).width() <= tab_navigation::MaximumWidth);
            CHECK_FALSE(single->tabText(i).contains("kp=SHA256/"));
            CHECK(single->tabToolTip(i).contains("kp=SHA256/"));
        }
        single->moveTab(0, 20);
    } else {
        const auto buttons = window->findChildren<TabButton *>();
        REQUIRE(buttons.size() == 40);
        for (auto *button : buttons)
            CHECK(button->width() <= tab_navigation::MaximumWidth);
    }
    all->click();
    QTest::qWait(30);
    REQUIRE(list->count() == 40);
    search->setText("offline-33.example");
    REQUIRE(list->count() == 1);
    const auto staleId = list->item(0)->data(Qt::UserRole).toULongLong();
    QTest::keyClick(search, Qt::Key_Return);
    REQUIRE(waitFor([&] { return arena->widget() == tabs[33]->getWidget(); }));
    manager->activate(tabs[20]);
    manager->rem(tabs.takeAt(33));
    REQUIRE(arena->widget() == tabs[20]->getWidget());
    REQUIRE(QMetaObject::invokeMethod(menu, "selected", Qt::DirectConnection, Q_ARG(quint64, staleId)));
    REQUIRE(arena->widget() == tabs[20]->getWidget());
    all->click();
    QTest::qWait(30);
    search->clear();
    REQUIRE(list->count() == 39);
    menu->hide();
    for (auto *tab : tabs) manager->rem(tab);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("Global GOST preferences preserve native controls and unsaved profiles", "[gost-global-native][gost-global][ui]")
{
    IsolatedMainWindow fixture;
    auto &sm = *fixture.core->getSettingsManager();
    sm.set(dcpp::SettingsManager::AUTO_DETECT_CONNECTION, true);
    sm.set(dcpp::SettingsManager::INCOMING_CONNECTIONS, dcpp::SettingsManager::INCOMING_DIRECT);
    sm.set(dcpp::SettingsManager::SOCKS_SERVER, "socks.invalid");
    sm.set(dcpp::SettingsManager::SOCKS_PASSWORD, "socks-secret");
    sm.set(dcpp::SettingsManager::GOST_SERVER, "gost.invalid");
    sm.set(dcpp::SettingsManager::GOST_PORT, 5541);
    sm.set(dcpp::SettingsManager::GOST_USER, "fixture-user");
    sm.set(dcpp::SettingsManager::GOST_PASSWORD, "fixture-secret");
    const auto generation = fixture.core->getProxyRoute()->snapshot()->generation;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    dialog.resize(1050, 850); dialog.show();
    auto *page = dialog.findChild<SettingsConnection *>();
    REQUIRE(page);
    auto *gost = child<QRadioButton>(page, "globalGostMode");
    auto *socks = child<QRadioButton>(page, "radioButton_SOCKS");
    auto *password = child<QLineEdit>(page, "lineEdit_SPSWD");
    auto *server = child<QLineEdit>(page, "lineEdit_SIP");
    gost->click();
    REQUIRE(gost->isChecked());
    CHECK(server->text() == "gost.invalid");
    CHECK(password->echoMode() == QLineEdit::Password);
    auto *reveal = password->findChild<QAction *>(QStringLiteral("passwordRevealAction"));
    REQUIRE(reveal);
    REQUIRE_FALSE(reveal->icon().isNull());
    // The legacy LineEdit paints a clear label over trailing QLineEdit actions.
    CHECK(password->findChildren<QLabel *>().isEmpty());
    reveal->trigger();
    CHECK(password->echoMode() == QLineEdit::Normal);
    CHECK(password->text() == "fixture-secret");
    reveal->trigger();
    CHECK(password->echoMode() == QLineEdit::Password);
    for (const auto *name : {"globalProxyTestTcpHost", "globalProxyTestDnsQuery"}) {
        CHECK(child<QLineEdit>(page, name)->text() == "google.com");
        CHECK(child<QLineEdit>(page, name)->isReadOnly());
    }
    CHECK_FALSE(child<QCheckBox>(page, "checkBox_RESOLVE")->isEnabled());
    CHECK_FALSE(child<QRadioButton>(page, "radioButton_ACTIVE")->isEnabled());
    CHECK(child<QRadioButton>(page, "radioButton_ACTIVE")->isChecked());
    CHECK(child<QLineEdit>(page, "globalProxyTestDnsResolver")->isEnabled());
    CHECK(child<QLineEdit>(page, "globalGostCaFile")->isVisible());
    password->setText("unsaved-gost-secret");
    reveal->trigger();
    socks->click();
    CHECK(password->echoMode() == QLineEdit::Password);
    CHECK_FALSE(reveal->isChecked());
    CHECK(server->text() == "socks.invalid");
    CHECK(password->text() == "socks-secret");
    gost->click();
    CHECK(password->text() == "unsaved-gost-secret");
    REQUIRE(page->validate());
    CHECK(fixture.core->getProxyRoute()->snapshot()->generation == generation);
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "fixture-secret");
    for (auto *area : dialog.findChildren<QScrollArea *>())
        if (area->widget() && area->widget()->isAncestorOf(password))
            area->ensureWidgetVisible(password, 0, 100);
    QTest::qWait(100);
    QToolButton *eyeButton = nullptr;
    for (auto *button : password->findChildren<QToolButton *>())
        if (button->defaultAction() == reveal) eyeButton = button;
    REQUIRE(eyeButton);
    REQUIRE(eyeButton->isVisible());
    REQUIRE(eyeButton->isEnabled());
    for (auto *button : password->findChildren<QToolButton *>())
        if (button != eyeButton && button->isVisible())
            CHECK_FALSE(button->geometry().intersects(eyeButton->geometry()));
    const auto clickEye = [&] {
        // Native Cocoa exercises window-system hit testing; headless platforms
        // do not reliably route mouse events through a scrolled settings page.
        if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
            QTest::mouseClick(eyeButton, Qt::LeftButton);
        else
            eyeButton->click();
    };
    clickEye();
    CHECK(password->echoMode() == QLineEdit::Normal);
    clickEye();
    CHECK(password->echoMode() == QLineEdit::Password);
    const auto screenshot = qEnvironmentVariable("EISKALT_GOST_UI_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        auto *caField = child<QLineEdit>(page, "globalGostCaFile");
        for (auto *area : dialog.findChildren<QScrollArea *>()) {
            if (area->widget() && area->widget()->isAncestorOf(caField)) {
                area->ensureWidgetVisible(caField, 0, 100);
                QTest::qWait(100);
                REQUIRE(area->viewport()->rect().contains(caField->mapTo(area->viewport(), caField->rect().center())));
            }
        }
        QTest::qWait(100);
        REQUIRE(dialog.grab().save(screenshot));
    }
    reveal->trigger();
    CHECK(password->echoMode() == QLineEdit::Normal);
    dialog.reject();
    CHECK(password->echoMode() == QLineEdit::Password);
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "fixture-secret");
    CHECK(sm.getBool(dcpp::SettingsManager::AUTO_DETECT_CONNECTION));
}

TEST_CASE("DC certificate controls preserve identity on cancel and save new paths only on Apply", "[certificate-ui][ui]")
{
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    const auto oldCert = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE));
    const auto oldKey = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_PRIVATE_KEY_FILE));
    auto read = [](const QString& path) { QFile f(path); REQUIRE(f.open(QIODevice::ReadOnly)); return f.readAll(); };
    const auto certificate = read(oldCert), key = read(oldKey);
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    auto* page = dialog.findChild<SettingsConnection*>();
    REQUIRE(page);
    auto* certField = child<QLineEdit>(page, "dcTlsCertificate");
    auto* keyField = child<QLineEdit>(page, "dcTlsPrivateKey");
    auto* details = child<QLabel>(page, "dcTlsDetails");
    auto* generate = child<QPushButton>(page, "dcTlsGenerate");
    REQUIRE(certField); REQUIRE(keyField); REQUIRE(details); REQUIRE(generate);
    CHECK(certField->text() == oldCert);
    CHECK(keyField->text() == oldKey);
    CHECK(child<QComboBox>(page, "comboBox_TLS")->currentIndex() == 1);
    CHECK(details->text().contains("SHA-256"));
    QTimer confirm;
    QObject::connect(&confirm, &QTimer::timeout, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                if (auto* yes = box->button(QMessageBox::Yes)) yes->click();
    });
    confirm.start(10);
    generate->click();
    confirm.stop();
    REQUIRE(waitFor([&] { return generate->isEnabled(); }));
    REQUIRE(certField->text() != oldCert);
    const auto generated = certField->text();
    CHECK(read(oldCert) == certificate);
    CHECK(read(oldKey) == key);
    CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == oldCert);
    REQUIRE(page->validate());
    SECTION("Cancel retains original settings") {
        dialog.reject();
        CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == oldCert);
    }
    SECTION("Apply saves new paths without replacing the old files") {
        page->ok();
        CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == generated);
        CHECK(read(oldCert) == certificate);
        CHECK(read(oldKey) == key);
    }
}

TEST_CASE("Certificate editor validates imports and manages only its own staged files", "[certificate-ui][ui]")
{
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    const auto oldCert = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE));
    const auto oldKey = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_PRIVATE_KEY_FILE));
    auto panel = std::make_unique<CertificateSettings>(sm,
        QString::fromStdString(fixture.core->getClientManager()->getMyCID().toBase32()),
        fixture.home.filePath("new-identities"));
    auto* cert = child<QLineEdit>(panel.get(), "dcTlsCertificate");
    auto* key = child<QLineEdit>(panel.get(), "dcTlsPrivateKey");
    auto* generate = child<QPushButton>(panel.get(), "dcTlsGenerate");
    cert->setText(fixture.home.filePath("missing.crt"));
    CHECK_FALSE(panel->validationError().isEmpty());
    panel->save();
    CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == oldCert);
    cert->setText(oldCert);
    key->setText(oldCert); // A certificate is not a private key.
    CHECK_FALSE(panel->validationError().isEmpty());
    key->setText(oldKey);
    REQUIRE(panel->validationError().isEmpty());
    QTimer confirm;
    bool yes = true;
    QObject::connect(&confirm, &QTimer::timeout, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                if (auto* button = box->button(yes ? QMessageBox::Yes : QMessageBox::No)) button->click();
    });
    auto run = [&] { confirm.start(10); generate->click(); confirm.stop(); };
    SECTION("Cancel deletes only the unsaved new pair") {
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        const auto path = cert->text();
        REQUIRE(path != oldCert);
        REQUIRE(QFileInfo::exists(path));
        panel.reset();
        CHECK_FALSE(QFileInfo::exists(path));
        CHECK(QFileInfo::exists(oldCert)); CHECK(QFileInfo::exists(oldKey));
    }
    SECTION("Save keeps the new private files") {
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        const auto path = key->text();
        REQUIRE(path != oldKey);
        CHECK_FALSE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther));
        panel->save(); panel.reset();
        CHECK(QFileInfo::exists(path));
        CHECK(QFileInfo::exists(oldKey));
    }
    SECTION("Save through a directory alias retains the generated pair") {
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        const auto generatedCert = cert->text(), generatedKey = key->text();
        REQUIRE(generatedCert != oldCert);
        const auto alias = fixture.home.filePath("identity-alias");
        REQUIRE(QFile::link(QFileInfo(generatedCert).absolutePath(), alias));
        cert->setText(alias + "/client.crt"); key->setText(alias + "/client.key");
        REQUIRE(panel->validationError().isEmpty());
        panel->save(); panel.reset();
        CHECK(QFileInfo::exists(generatedCert));
        CHECK(QFileInfo::exists(generatedKey));
    }
    SECTION("Declining generation changes nothing") {
        yes = false; run();
        CHECK(cert->text() == oldCert);
        CHECK_FALSE(QFileInfo::exists(fixture.home.filePath("new-identities")));
    }
    SECTION("An unwritable destination preserves old paths") {
        QFile blocker(fixture.home.filePath("new-identities"));
        REQUIRE(blocker.open(QIODevice::WriteOnly)); blocker.close();
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        CHECK(cert->text() == oldCert); CHECK(key->text() == oldKey);
        CHECK(child<QLabel>(panel.get(), "dcTlsMessage")->text().contains("failed"));
    }
    SECTION("Closing during generation does not write files") {
        run(); panel.reset();
        QThreadPool::globalInstance()->waitForDone();
        CHECK_FALSE(QFileInfo::exists(fixture.home.filePath("new-identities")));
        CHECK(QFileInfo::exists(oldCert)); CHECK(QFileInfo::exists(oldKey));
    }
}

TEST_CASE("Certificate controls fit the native Advanced page", "[certificate-ui-native][certificate-ui][ui]")
{
    IsolatedMainWindow fixture;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    auto* page = dialog.findChild<SettingsConnection*>();
    REQUIRE(page);
    page->findChild<QTabWidget*>()->setCurrentIndex(2);
    dialog.resize(1050, 900); dialog.show();
    QTest::qWait(100);
    auto* button = child<QPushButton>(page, "dcTlsGenerate");
    for (auto* area : dialog.findChildren<QScrollArea*>())
        if (area->widget() && area->widget()->isAncestorOf(button)) area->ensureWidgetVisible(button);
    QTest::qWait(100);
    CHECK(button->isVisible());
    CHECK(child<QLineEdit>(page, "dcTlsCertificate")->width() > 200);
    const auto screenshot = qEnvironmentVariable("EISKALT_CERT_UI_SCREENSHOT");
    if (!screenshot.isEmpty()) REQUIRE(dialog.grab().save(screenshot));
}

TEST_CASE("Global invalid GOST preferences do not partially persist other pages", "[gost-global-native][gost-global][ui]")
{
    IsolatedMainWindow fixture;
    auto &sm = *fixture.core->getSettingsManager();
    const auto nick = sm.get(dcpp::SettingsManager::NICK);
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    child<QRadioButton>(&dialog, "globalGostMode")->click();
    child<QLineEdit>(&dialog, "lineEdit_SIP")->setText("proxy.invalid");
    child<QLineEdit>(&dialog, "lineEdit_SPORT")->setText("5541");
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *message = qobject_cast<QMessageBox *>(widget)) message->accept();
    });
    dismiss.start(10);
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);
    CHECK(sm.get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_DIRECT);
    CHECK(sm.get(dcpp::SettingsManager::GOST_SERVER).empty());
    CHECK(sm.get(dcpp::SettingsManager::NICK) == nick);
}

TEST_CASE("Global GOST Apply preserves incoming preferences and no-op route generation", "[gost-global-native][gost-global][ui]")
{
    IsolatedMainWindow fixture;
    auto &sm = *fixture.core->getSettingsManager();
    sm.set(dcpp::SettingsManager::AUTO_DETECT_CONNECTION, true);
    sm.set(dcpp::SettingsManager::INCOMING_CONNECTIONS, dcpp::SettingsManager::INCOMING_DIRECT);
    SettingsConnection page;
    child<QRadioButton>(&page, "globalGostMode")->click();
    child<QLineEdit>(&page, "lineEdit_SIP")->setText("proxy.invalid");
    child<QLineEdit>(&page, "lineEdit_SPORT")->setText("5541");
    child<QLineEdit>(&page, "lineEdit_SUSR")->setText("fixture-user");
    child<QLineEdit>(&page, "lineEdit_SPSWD")->setText("fixture-secret");
    REQUIRE(page.validate());
    page.ok();
    CHECK(sm.get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_GOST);
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "fixture-secret");
    CHECK(sm.getBool(dcpp::SettingsManager::AUTO_DETECT_CONNECTION));
    CHECK(sm.get(dcpp::SettingsManager::INCOMING_CONNECTIONS) == dcpp::SettingsManager::INCOMING_DIRECT);
    const auto route = fixture.core->getProxyRoute()->snapshot();
    REQUIRE(route->valid);
    page.ok();
    CHECK(fixture.core->getProxyRoute()->snapshot() == route);
    child<QLineEdit>(&page, "lineEdit_SPSWD")->setText("retained-inactive-secret");
    child<QRadioButton>(&page, "radioButton_DC")->click();
    // Keep this isolated fixture passive instead of starting real incoming probes.
    child<QCheckBox>(&page, "checkBox_AUTO_DETECT_CONNECTION")->setChecked(false);
    child<QRadioButton>(&page, "radioButton_PASSIVE")->setChecked(true);
    page.ok();
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "retained-inactive-secret");
    CHECK(sm.get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_DIRECT);
    CHECK(route->revoked->load());
}

#include "DownloadQueue.h"
#include "DownloadQueueModel.h"
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QPersistentModelIndex>

namespace {
QVariantMap queueRow(const QString &path, const QString &name)
{
    return {{"PATH", path}, {"FNAME", name}, {"STATUS", "1 of 2 user(s) online"},
        {"ESIZE", 4096}, {"DOWN", 1024}, {"PRIO", int(dcpp::QueueItem::NORMAL)},
        {"USERS", "Example peer"}, {"ERRORS", ""}, {"ADDED", "2026-09-27 20:40"},
        {"TTH", "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG"}};
}
}

TEST_CASE("Queue destinations do not bury filenames under absolute path ancestors", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    const auto first = queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "Sample document.txt");
    model.addItem(first);
    const auto group = model.index(0, 0);
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.rowCount(group) == 1);
    CHECK(model.index(0, 0, group).data().toString() == "Sample document.txt");
    const auto decoration = model.index(0, 0, group).data(Qt::DecorationRole);
    const QPixmap icon = decoration.metaType() == QMetaType::fromType<QPixmap>()
        ? decoration.value<QPixmap>() : decoration.value<QIcon>().pixmap(QSize(16, 16), 2.0);
    CHECK(qRound(icon.deviceIndependentSize().width()) >= 16);
    CHECK(group.data(Qt::ToolTipRole).toString().contains("/Users/fixture/.local/share/eiskaltdc++/files"));
    model.addItem(queueRow("/Volumes/Other/files/", "Second document.txt"));
    REQUIRE(model.rowCount() == 2);
    CHECK(model.index(0, 0, model.index(1, 0)).data().toString() == "Second document.txt");
    CHECK(model.remItem(first));
    CHECK(model.rowCount() == 1);
    CHECK_FALSE(model.remItem(queueRow("/not/queued/", "absent.txt")));
    CHECK(model.rowCount() == 1);
}

TEST_CASE("Queue file list unknown sizes and absent hashes are not fake metadata", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    auto row = queueRow("/tmp/queue-fixture/", "Peer file list.xml.bz2");
    row["ESIZE"] = -1;
    row["TTH"] = QString(39, 'A');
    auto *item = model.addItem(row);
    auto index = model.createIndexForItem(item);
    CHECK(index.siblingAtColumn(COLUMN_DOWNLOADQUEUE_SIZE).data().toString().isEmpty());
    CHECK(index.siblingAtColumn(COLUMN_DOWNLOADQUEUE_ESIZE).data().toString().isEmpty());
    CHECK(index.siblingAtColumn(COLUMN_DOWNLOADQUEUE_TTH).data().toString().isEmpty());
}

TEST_CASE("Queue status painting stays inside its cell", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    auto row = queueRow("/tmp/queue-fixture/", "Example.iso");
    row["STATUS"] = QString(300, 'W');
    auto *item = model.addItem(row);
    DownloadQueueDelegate delegate;
    QTreeView view;
    view.setModel(&model);
    QImage canvas(400, 50, QImage::Format_ARGB32);
    canvas.fill(QColor("#d327df"));
    QStyleOptionViewItem option;
    option.initFrom(&view);
    option.widget = &view;
    option.rect = QRect(150, 5, 100, 35);
    QPainter painter(&canvas);
    delegate.paint(&painter, option, model.createIndexForItem(item).siblingAtColumn(COLUMN_DOWNLOADQUEUE_STATUS));
    painter.end();
    bool outsideUnchanged = true;
    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
            if (!option.rect.contains(x, y) && canvas.pixelColor(x, y) != QColor("#d327df"))
                outsideUnchanged = false;
    CHECK(outsideUnchanged);
}

TEST_CASE("Queue columns fit by default and keep explicit widths after reconstruction", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    const QString key = "downloadqueue/column-layout-v1";
    fixture.gui->settings()->setVar(WS_DQUEUE_STATE, QByteArray{});
    fixture.gui->settings()->setVar(key, QVariantMap{});
    {
        DownloadQueue queue(*fixture.core);
        queue.resize(1350, 450);
        queue.show();
        auto *view = child<QTreeView>(&queue, "treeView_TARGET");
        auto *model = qobject_cast<DownloadQueueModel *>(view->model());
        REQUIRE(model);
        model->addItem(queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "A readable file name in the download queue.iso"));
        view->expandAll();
        REQUIRE(waitFor([&] { return view->columnWidth(0) > 340; }));
        view->header()->resizeSection(0, 287);
        view->header()->hideSection(COLUMN_DOWNLOADQUEUE_TTH);
    }
    DownloadQueue reopened(*fixture.core);
    reopened.resize(1350, 450);
    reopened.show();
    auto *view = child<QTreeView>(&reopened, "treeView_TARGET");
    auto *model = qobject_cast<DownloadQueueModel *>(view->model());
    model->addItem(queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "A longer name after reopening must not override the chosen width.iso"));
    view->expandAll();
    QTest::qWait(200);
    CHECK(view->columnWidth(0) == 287);
    CHECK(view->isColumnHidden(COLUMN_DOWNLOADQUEUE_TTH));
    const auto image = qEnvironmentVariable("EISKALT_QUEUE_IMAGE");
    if (!image.isEmpty()) REQUIRE(reopened.grab().save(image));
}

TEST_CASE("Main window warnings are red plain text and recorded in status history", "[qt][queue-warning]")
{
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    auto *logs = fixture.core->getLogManager();
    logs->clearLiveEntries();
    REQUIRE(QMetaObject::invokeMethod(window, "displayStatusMessage", Q_ARG(QString, QString("Normal queue status"))));
    QLabel *banner = nullptr;
    for (auto *label : window->findChildren<QLabel *>())
        if (label->text() == "Normal queue status") banner = label;
    REQUIRE(banner);
    const QColor normalColor = banner->palette().color(QPalette::WindowText);
    const QString message = "Cannot download from passive user while you are in passive mode: <Example peer>";
    REQUIRE(QMetaObject::invokeMethod(window, "displayStatusWarning", Q_ARG(QString, message)));
    CHECK(banner->textFormat() == Qt::PlainText);
    auto color = banner->palette().color(QPalette::WindowText);
    CHECK(color.red() > color.green() * 1.5);
    CHECK(color.red() > color.blue() * 1.5);
    REQUIRE_FALSE(logs->getLiveEntries().empty());
    CHECK(logs->getLiveEntries().back().area == dcpp::LogManager::STATUS);
    REQUIRE(QMetaObject::invokeMethod(window, "displayStatusMessage", Q_ARG(QString, QString("Ready"))));
    CHECK(banner->palette().color(QPalette::WindowText) == normalColor);
}

TEST_CASE("Queue model updates preserve selected file identities and valid row notifications", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    auto a = queueRow("/tmp/model/", "z-last.iso");
    auto b = queueRow("/tmp/model/", "a-first.iso");
    QSignalSpy stats(&model, &DownloadQueueModel::updateStats);
    auto *original = model.addItem(a);
    QPersistentModelIndex selected(model.createIndexForItem(original));
    model.addItem(b);
    model.sort(0, Qt::AscendingOrder);
    CHECK(selected.data().toString() == "z-last.iso");
    CHECK(selected.row() == 1);
    a["DOWN"] = 2048;
    model.updItem(a);
    CHECK(QModelIndex(selected).siblingAtColumn(COLUMN_DOWNLOADQUEUE_DOWN).data().toString().contains("2"));
    CHECK(model.remItem(b));
    CHECK(selected.isValid());
    CHECK(selected.row() == 0);
    CHECK(model.remItem(a));
    CHECK_FALSE(selected.isValid());
    CHECK(model.rowCount() == 0);
    model.addItem(a);
    model.clear();
    CHECK(model.rowCount() == 0);
    REQUIRE_FALSE(stats.isEmpty());
    CHECK(stats.last().at(0).toULongLong() == 0);
    CHECK(stats.last().at(1).toULongLong() == 0);
}

TEST_CASE("Core warning events reach the main window without losing red severity", "[qt][queue-warning]")
{
    IsolatedMainWindow fixture;
    auto *logs = fixture.core->getLogManager();
    const QString text = "Cannot download from passive user while you are in passive mode: Test peer";
    logs->clearLiveEntries();
    logs->warning(text.toStdString());
    QLabel *banner = nullptr;
    REQUIRE(waitFor([&] {
        for (auto *label : fixture.gui->mainWindow()->findChildren<QLabel *>()) {
            if (!label->toolTip().contains(text)) continue;
            const auto color = label->palette().color(QPalette::WindowText);
            if (color.red() > color.green() * 1.5) { banner = label; return true; }
        }
        return false;
    }));
    CHECK(banner->toolTip().count(text) == 1);
    const auto entries = logs->getLiveEntries();
    CHECK(std::count_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return entry.area == dcpp::LogManager::SYSTEM && entry.message.find("Test peer") != std::string::npos;
    }) == 1);
    CHECK(std::count_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return entry.area == dcpp::LogManager::STATUS && entry.message.find("Test peer") != std::string::npos;
    }) == 1);
}

TEST_CASE("Download queue has readable compact light and dark layouts", "[qt][download-queue][queue-preview]")
{
    struct RestorePalette {
        QPalette saved = qApp->palette();
        ~RestorePalette() { qApp->setPalette(saved); }
    } appearance;
    IsolatedMainWindow fixture;
    fixture.gui->settings()->setVar(WS_DQUEUE_STATE, QByteArray{});
    fixture.gui->settings()->setVar("downloadqueue/column-layout-v1", QVariantMap{});
    DownloadQueue queue(*fixture.core);
    queue.resize(1400, 470);
    queue.show();
    auto *view = child<QTreeView>(&queue, "treeView_TARGET");
    auto *model = qobject_cast<DownloadQueueModel *>(view->model());
    auto row = queueRow("/Users/fixture/Downloads/Documentaries/", "Wild Australia - Desert of the Red Kangaroo.mp4");
    row["ESIZE"] = qlonglong(2400000000);
    row["DOWN"] = qlonglong(900000000);
    model->addItem(row);
    row["FNAME"] = "Wild Australia - Jurassic Jungle.mp4";
    row["DOWN"] = 0;
    model->addItem(row);
    row = queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "Example peer.xml.bz2");
    row["ESIZE"] = -1;
    row["TTH"] = QString(39, 'A');
    model->addItem(row);
    view->expandAll();
    child<QAction>(&queue, "fitQueueColumns")->trigger();
    REQUIRE(view->columnWidth(0) > 340);
    CHECK(view->isColumnHidden(COLUMN_DOWNLOADQUEUE_TTH));
    CHECK(view->isColumnHidden(COLUMN_DOWNLOADQUEUE_ESIZE));
    CHECK(view->y() <= 10);
    CHECK(child<QPushButton>(&queue, "pushButton_EXPAND")->y() > view->y() + view->height());
    const auto images = qEnvironmentVariable("EISKALT_QUEUE_PREVIEWS");
    for (const bool dark : {false, true}) {
        QPalette palette = appearance.saved;
        palette.setColor(QPalette::Window, QColor(dark ? "#25282c" : "#f2f3f5"));
        palette.setColor(QPalette::Button, QColor(dark ? "#30343a" : "#eeeeee"));
        palette.setColor(QPalette::Base, QColor(dark ? "#1d2024" : "#ffffff"));
        palette.setColor(QPalette::AlternateBase, QColor(dark ? "#292d32" : "#f3f5f7"));
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
            palette.setColor(role, QColor(dark ? "#f0f3f5" : "#16191d"));
        qApp->setPalette(palette);
        QTest::qWait(180);
        const auto pixels = view->viewport()->grab().toImage();
        const qreal scale = pixels.devicePixelRatio();
        const auto file = model->index(0, 0, model->index(0, 0));
        const int y = qRound(view->visualRect(file).center().y() * scale);
        const int end = view->header()->length();
        REQUIRE(end + 10 < view->viewport()->width());
        INFO("row fill: inside=" << pixels.pixelColor(qRound((end - 8) * scale), y).name().toStdString()
             << ", outside=" << pixels.pixelColor(qRound((end + 8) * scale), y).name().toStdString());
        CHECK(pixels.pixelColor(qRound((end - 8) * scale), y) ==
              pixels.pixelColor(qRound((end + 8) * scale), y));
        if (!images.isEmpty()) {
            QDir().mkpath(images);
            REQUIRE(queue.grab().save(images + (dark ? "/queue-dark.png" : "/queue-light.png")));
        }
    }
}

namespace {
struct HubSelectionFixture {
    IsolatedMainWindow app;
    UserListModel model;
    UserListProxyModel proxy;
    QItemSelectionModel selection{&proxy};
    QList<UserListItem*> items;

    HubSelectionFixture()
    {
        model.sort(COLUMN_NICK, Qt::AscendingOrder);
        proxy.setSourceModel(&model);
        proxy.setFilterKeyColumn(COLUMN_NICK);
        for (int i = 0; i != 4; ++i) {
            UserPtr user(new User(CID::generate()));
            Identity identity(user, i + 1);
            identity.set("NI", "Peer" + std::to_string(i));
            identity.setBytesShared(std::to_string((4 - i) * 100));
            identity.set("DE", std::to_string(4 - i));
            identity.set("I4", "192.0.2." + std::to_string(i + 1));
            identity.set("I6", "2001:db8::" + std::to_string(4 - i));
            identity.set("EM", "peer" + std::to_string(i) + "@invalid.test");
            identity.set("CO", std::to_string((i + 1) * 10));
            identity.set("TA", "Client" + std::to_string(4 - i));
            items << model.addUser(user, identity,
                                   QString::fromStdString(user->getCID().toBase32()), false);
        }
    }

    QModelIndex source(UserListItem* item, int column = 0) {
        return model.index(item->row(), column);
    }
    void select(UserListItem* item) {
        selection.select(proxy.mapFromSource(source(item)),
                         QItemSelectionModel::Select | QItemSelectionModel::Rows);
        selection.setCurrentIndex(proxy.mapFromSource(source(item)), QItemSelectionModel::NoUpdate);
    }
    void checkSelected(const QList<UserListItem*>& expected) {
        const auto rows = selection.selectedRows();
        REQUIRE(rows.size() == expected.size());
        for (const auto& row : rows) {
            const auto mapped = proxy.mapToSource(row);
            CHECK(expected.contains(static_cast<UserListItem*>(mapped.internalPointer())));
            CHECK(mapped == source(static_cast<UserListItem*>(mapped.internalPointer())));
        }
    }
};
}

TEST_CASE("Hub selections retain user identity across every header sort", "[hub-selection]")
{
    HubSelectionFixture f;
    f.proxy.setFilterRegularExpression("Peer[013]");
    f.select(f.items[0]);
    f.select(f.items[3]);
    QItemSelectionModel sourceSelection(&f.model);
    sourceSelection.setCurrentIndex(f.source(f.items[1]),
                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QPersistentModelIndex proxySaved(f.proxy.mapFromSource(f.source(f.items[3], COLUMN_EMAIL)));
    QList<QPersistentModelIndex> saved;
    for (auto* item : f.items)
        for (int column = 0; column != f.model.columnCount(); ++column)
            saved << f.source(item, column);
    for (int column = 0; column != f.model.columnCount(); ++column) {
        for (const auto order : {Qt::DescendingOrder, Qt::AscendingOrder}) {
            f.proxy.sort(column, order);
            int n = 0;
            for (auto* item : f.items)
                for (int c = 0; c != f.model.columnCount(); ++c)
                    CHECK(saved[n++] == f.source(item, c));
            f.checkSelected({f.items[0], f.items[3]});
            CHECK(f.proxy.mapToSource(f.selection.currentIndex()) == f.source(f.items[3]));
            CHECK(f.proxy.mapToSource(proxySaved) == f.source(f.items[3], COLUMN_EMAIL));
            CHECK(sourceSelection.currentIndex() == f.source(f.items[1]));
            REQUIRE(sourceSelection.selectedRows().size() == 1);
            CHECK(sourceSelection.selectedRows().front() == f.source(f.items[1]));
        }
    }
}

TEST_CASE("Hub identity changes preserve selected rows without removal", "[hub-selection]")
{
    HubSelectionFixture f;
    f.select(f.items[1]);
    f.select(f.items[3]);
    QPersistentModelIndex saved(f.source(f.items[1], COLUMN_SHARE));
    QPersistentModelIndex other(f.source(f.items[2], COLUMN_COMMENT));
    QSignalSpy removed(&f.model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&f.model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&f.model, &QAbstractItemModel::dataChanged);
    Identity identity(f.items[1]->getIdentity());
    bool favorite = false;
    SECTION("nickname moves down") { identity.set("NI", "Zulu"); }
    SECTION("nickname moves up") { identity.set("NI", "Alpha"); }
    SECTION("operator promoted") { identity.setOp(true); }
    SECTION("favorite promoted") { favorite = true; }
    SECTION("share changes while share sorted") {
        f.proxy.sort(COLUMN_SHARE, Qt::DescendingOrder);
        identity.setBytesShared("9999");
    }
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), favorite);
    CHECK(removed.count() == 0);
    CHECK(inserted.count() == 0);
    CHECK(changed.count() == 1);
    CHECK(saved == f.source(f.items[1], COLUMN_SHARE));
    CHECK(other == f.source(f.items[2], COLUMN_COMMENT));
    f.checkSelected({f.items[1], f.items[3]});
    CHECK(f.proxy.mapToSource(f.selection.currentIndex()) == f.source(f.items[3]));
    f.model.removeUser(f.items[1]->getUser());
    CHECK_FALSE(saved.isValid());
    f.checkSelected({f.items[3]});
    f.model.clear();
    CHECK_FALSE(other.isValid());
    CHECK(f.selection.selectedRows().isEmpty());
}

TEST_CASE("Unsorted hub identity changes do not move the selected user", "[hub-selection]")
{
    HubSelectionFixture f;
    f.model.sort(-1);
    f.select(f.items[2]);
    QPersistentModelIndex saved(f.source(f.items[2]));
    Identity identity(f.items[2]->getIdentity());
    identity.setOp(true);
    f.model.updateUser(f.items[2], identity, f.items[2]->getCID(), true);
    CHECK(saved == f.source(f.items[2]));
    CHECK(saved.row() == 2);
    f.checkSelected({f.items[2]});
}

TEST_CASE("Filtered hub updates track the current user and invalidate hidden users", "[hub-selection]")
{
    HubSelectionFixture f;
    f.proxy.setFilterRegularExpression("Peer[13]");
    f.select(f.items[3]);
    f.select(f.items[1]);
    QPersistentModelIndex selected(f.proxy.mapFromSource(f.source(f.items[1])));
    QPersistentModelIndex other(f.proxy.mapFromSource(f.source(f.items[3])));
    QPersistentModelIndex source(f.source(f.items[1]));
    Identity identity(f.items[1]->getIdentity());
    identity.setOp(true);
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), false);
    f.checkSelected({f.items[1], f.items[3]});
    CHECK(f.selection.currentIndex() == selected);
    CHECK(f.proxy.mapToSource(selected) == f.source(f.items[1]));

    identity.set("NI", "Hidden");
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), false);
    CHECK_FALSE(selected.isValid());
    CHECK(source == f.source(f.items[1]));
    CHECK(other.isValid());
    f.checkSelected({f.items[3]});
    CHECK_FALSE(f.proxy.mapFromSource(source).isValid());

    identity.set("NI", "Peer1-returned");
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), false);
    CHECK(f.proxy.mapFromSource(source).isValid());
    f.checkSelected({f.items[3]});
    f.select(f.items[1]);
    selected = f.proxy.mapFromSource(source);
    f.model.removeUser(f.items[1]->getUser());
    CHECK_FALSE(selected.isValid());
    CHECK_FALSE(source.isValid());
    CHECK(other.isValid());
    CHECK(f.selection.currentIndex() != selected);
    f.checkSelected({f.items[3]});
    f.model.clear();
    CHECK_FALSE(other.isValid());
    CHECK_FALSE(f.selection.currentIndex().isValid());
}

TEST_CASE("Generated large hub updates preserve selected identities", "[.][hub-scale]")
{
    HubSelectionFixture f;
    for (int count : {1000, 10000}) {
        f.model.clear();
        f.items.clear();
        f.model.sort(-1);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < count; ++i) {
            UserPtr user(new User(CID::generate()));
            Identity identity(user, i + 1);
            identity.set("NI", QString("Peer%1").arg(i, 5, 10, QLatin1Char('0')).toStdString());
            identity.setBytesShared(std::to_string(i * 1024));
            f.items << f.model.addUser(user, identity,
                                      QString::fromStdString(user->getCID().toBase32()), false);
        }
        const double loadMs = timer.nsecsElapsed() / 1e6;
        f.model.sort(COLUMN_SHARE, Qt::DescendingOrder);
        QList<UserListItem*> selected;
        QList<QPersistentModelIndex> sourceSaved, proxySaved;
        for (int i = 0; i < count; i += count / 20) {
            selected << f.items[i];
            f.select(f.items[i]);
            sourceSaved << f.source(f.items[i]);
            proxySaved << f.proxy.mapFromSource(f.source(f.items[i]));
        }
        QSignalSpy reset(&f.model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&f.model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&f.model, &QAbstractItemModel::rowsInserted);
        QSignalSpy layout(&f.model, &QAbstractItemModel::layoutChanged);
        QSignalSpy changed(&f.model, &QAbstractItemModel::dataChanged);
        timer.restart();
        f.proxy.sort(COLUMN_NICK, Qt::AscendingOrder);
        const double sortMs = timer.nsecsElapsed() / 1e6;
        double maxUpdateMs = 0;
        timer.restart();
        for (int i = 0; i != 200; ++i) {
            auto* item = f.items[(i * 97) % count];
            Identity identity(item->getIdentity());
            identity.set("NI", "Updated" + std::to_string(i));
            QElapsedTimer update;
            update.start();
            f.model.updateUser(item, identity, item->getCID(), false);
            maxUpdateMs = std::max(maxUpdateMs, update.nsecsElapsed() / 1e6);
        }
        const double updatesMs = timer.nsecsElapsed() / 1e6;
        CHECK(f.model.rowCount() == count);
        CHECK(f.proxy.rowCount() == count);
        for (int i = 0; i != 200; ++i)
            CHECK(f.items[(i * 97) % count]->getNick() == QString("Updated%1").arg(i));
        for (int row = 1; row < count; ++row)
            CHECK(QString::localeAwareCompare(f.model.index(row - 1, COLUMN_NICK).data().toString(),
                                              f.model.index(row, COLUMN_NICK).data().toString()) <= 0);
        f.checkSelected(selected);
        for (int i = 0; i < selected.size(); ++i) {
            CHECK(sourceSaved[i] == f.source(selected[i]));
            CHECK(f.proxy.mapToSource(proxySaved[i]) == f.source(selected[i]));
        }
        CHECK(f.proxy.mapToSource(f.selection.currentIndex()) == f.source(selected.back()));
        CHECK(reset.count() == 0);
        CHECK(removed.count() == 0);
        CHECK(inserted.count() == 0);
        CHECK(layout.count() == 201);
        CHECK(changed.count() == 200);
        std::cout << "hub-scale users=" << count << " selected=" << selected.size()
                  << " updates=200 load_ms=" << loadMs << " sort_ms=" << sortMs
                  << " updates_ms=" << updatesMs << " max_update_ms=" << maxUpdateMs << '\n';
    }
}
