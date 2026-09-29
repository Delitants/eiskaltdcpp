#ifdef USE_TORRENT
#include <catch2/catch_test_macros.hpp>
#include "SettingsTorrent.h"
#include "TorrentWindow.h"
#include "TorrentSource.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentSettings.h"
#include "MacInputStyle.h"
#include "ProxyTestRunner.h"
#include "tests/proxy/DisposableGostServer.h"
#include <QTranslator>
#include <QFileDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QSettings>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QTest>
#include <functional>

using namespace eiskalt::torrent;

namespace {
template<class T> T *control(SettingsTorrent &page, const char *name)
{
    auto *result = page.findChild<T *>(QString::fromLatin1(name));
    REQUIRE(result);
    return result;
}

bool waitForSnapshot(const std::function<bool()> &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return ready();
}

QString offlineTorrent(QTemporaryDir &dir)
{
    // One-byte v1 metadata, with no trackers or bootstrap nodes. No payload is
    // created: tests add this with startPaused=true and never resume networking.
    const auto path = dir.filePath("offline.torrent");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    const QByteArray metadata = QByteArray("d4:infod6:lengthi1e4:name11:payload.bin12:piece lengthi16384e6:pieces20:") +
        QCryptographicHash::hash("x", QCryptographicHash::Sha1) + "ee";
    REQUIRE(file.write(metadata) == metadata.size());
    return path;
}
}

TEST_CASE("Torrent preferences are read-only until saved", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    {
        SettingsTorrent page(path, ProxyConfig{});
        control<QSpinBox>(page, "downloadLimitKiB")->setValue(128);
        REQUIRE_FALSE(QFileInfo::exists(path));
    }
    REQUIRE_FALSE(QFileInfo::exists(path));
}

TEST_CASE("Torrent proxy password reveal is transient and diagnostics use Google", "[qt][torrent-settings][proxy-ui-fixed]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    QSettings old(path + ".diagnostics.ini", QSettings::IniFormat);
    old.setValue("ProxyTest/tcpHost", "previous.invalid");
    old.setValue("ProxyTest/dnsQuery", "previous.invalid");
    old.sync();
    SettingsTorrent page(path, ProxyConfig{});
    control<QComboBox>(page, "proxyMode")->setCurrentIndex(
        control<QComboBox>(page, "proxyMode")->findData(int(ProxyMode::Custom)));
    auto *password = control<QLineEdit>(page, "torrentProxyPassword");
    password->setText("disposable-test-password");
    auto *reveal = password->findChild<QAction *>(QStringLiteral("passwordRevealAction"));
    REQUIRE(reveal);
    CHECK_FALSE(reveal->icon().isNull());
    CHECK(password->echoMode() == QLineEdit::Password);
    reveal->trigger();
    CHECK(password->echoMode() == QLineEdit::Normal);
    CHECK(password->text() == "disposable-test-password");
    control<QComboBox>(page, "torrentProxyType")->setCurrentIndex(1);
    CHECK(password->echoMode() == QLineEdit::Password);
    CHECK_FALSE(reveal->isChecked());
    page.show();
    page.findChild<QTabWidget *>()->setCurrentIndex(1);
    reveal->trigger();
    page.hide();
    CHECK(password->echoMode() == QLineEdit::Password);
    CHECK_FALSE(reveal->isChecked());
    for (const auto *name : {"torrentProxyTestTcpHost", "torrentProxyTestDnsQuery"}) {
        CHECK(control<QLineEdit>(page, name)->text() == "google.com");
        CHECK(control<QLineEdit>(page, name)->isReadOnly());
    }
    CHECK_FALSE(QFileInfo::exists(path));
}

TEST_CASE("Torrent proxy test follows the selected unsaved route without starting networking", "[qt][torrent-settings][torrent-proxy-test-ui]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    SettingsTorrent page(path, ProxyConfig{});
    auto *test = control<QPushButton>(page, "torrentProxyTest");
    auto *result = control<QLabel>(page, "torrentProxyTestResult");
    REQUIRE(result->textFormat() == Qt::PlainText);
    REQUIRE_FALSE(test->isEnabled());
    auto *routing = control<QComboBox>(page, "proxyMode");
    routing->setCurrentIndex(routing->findData(static_cast<int>(ProxyMode::Custom)));
    REQUIRE_FALSE(test->isEnabled());
    control<QLineEdit>(page, "torrentProxyHost")->setText("127.0.0.1");
    control<QSpinBox>(page, "torrentProxyPort")->setValue(1080);
    REQUIRE(test->isEnabled());
    routing->setCurrentIndex(routing->findData(static_cast<int>(ProxyMode::Direct)));
    REQUIRE_FALSE(test->isEnabled());
    ProxyConfig appProxy{ProxyType::Socks5, "127.0.0.1", {}, {}, {}, 1081, true, true};
    page.setApplicationProxy(appProxy);
    routing->setCurrentIndex(routing->findData(static_cast<int>(ProxyMode::FollowApplication)));
    REQUIRE(test->isEnabled());
    control<QCheckBox>(page, "enabled")->setChecked(false);
    REQUIRE_FALSE(test->isEnabled());
    REQUIRE_FALSE(QFileInfo::exists(path));
}

TEST_CASE("Torrent proxy tests stay responsive and do not save settings", "[qt][torrent-settings][torrent-proxy-test-ui]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    QTcpServer silent;
    silent.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(silent.listen(QHostAddress::LocalHost));
    Settings config;
    config.proxyMode = ProxyMode::Custom;
    config.socks5Proxy.host = "127.0.0.1";
    config.socks5Proxy.port = silent.serverPort();
    REQUIRE(saveSettings(path, config));
    QFile settingsFile(path);
    REQUIRE(settingsFile.open(QIODevice::ReadOnly));
    const auto before = settingsFile.readAll();
    settingsFile.close();
    auto page = std::make_unique<SettingsTorrent>(path, ProxyConfig{});
    auto *button = control<QPushButton>(*page, "torrentProxyTest");
    auto *cancel = control<QPushButton>(*page, "torrentProxyTestCancel");
    bool tick = false;
    QTimer::singleShot(0, page.get(), [&] { tick = true; });
    QElapsedTimer elapsed;
    elapsed.start();
    button->click();
    CHECK(elapsed.elapsed() < 250);
    REQUIRE_FALSE(button->isEnabled());
    REQUIRE(cancel->isEnabled());
    REQUIRE(waitForSnapshot([&] { return tick && silent.hasPendingConnections(); }));
    std::unique_ptr<QTcpSocket> peer(silent.nextPendingConnection());
    REQUIRE(peer);
    SECTION("Cancel reports without blocking") {
        cancel->click();
        REQUIRE(waitForSnapshot([&] { return button->isEnabled(); }));
        CHECK(control<QLabel>(*page, "torrentProxyTestResult")->text().contains("cancel", Qt::CaseInsensitive));
    }
    SECTION("Editing invalidates old results") {
        control<QLineEdit>(*page, "torrentProxyHost")->setText("127.0.0.2");
        REQUIRE(waitForSnapshot([&] { return button->isEnabled(); }));
        CHECK(control<QLabel>(*page, "torrentProxyTestResult")->text().contains("settings changed"));
    }
    SECTION("Editing test destination invalidates old results") {
        control<QLineEdit>(*page, "torrentProxyTestTcpHost")->setText("changed.invalid");
        REQUIRE(waitForSnapshot([&] { return button->isEnabled(); }));
        CHECK(control<QLabel>(*page, "torrentProxyTestResult")->text().contains("settings changed"));
    }
    SECTION("Closing does not wait for the silent proxy") {
        elapsed.restart();
        page.reset();
        CHECK(elapsed.elapsed() < 250);
        QCoreApplication::processEvents();
    }
    REQUIRE(settingsFile.open(QIODevice::ReadOnly));
    CHECK(settingsFile.readAll() == before);
}

TEST_CASE("Torrent proxy diagnostic targets are independent unsaved choices", "[qt][torrent-settings][torrent-proxy-targets]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    ProxyConfig proxy{ProxyType::Socks5, "127.0.0.1", {}, {}, {}, 1080, true, true};
    SettingsTorrent page(path, proxy);
    auto *tcpHost = control<QLineEdit>(page, "torrentProxyTestTcpHost");
    auto *tcpPort = control<QSpinBox>(page, "torrentProxyTestTcpPort");
    auto *resolver = control<QLineEdit>(page, "torrentProxyTestDnsResolver");
    auto *dnsPort = control<QSpinBox>(page, "torrentProxyTestDnsPort");
    auto *query = control<QLineEdit>(page, "torrentProxyTestDnsQuery");
    auto *notice = control<QLabel>(page, "torrentProxyTestProtocol");
    REQUIRE(tcpPort->value() == 443);
    REQUIRE(dnsPort->value() == 53);
    tcpHost->setText("service.invalid");
    tcpPort->setValue(8443);
    resolver->setText("2001:db8::53");
    dnsPort->setValue(5353);
    query->setText("question.invalid");
    REQUIRE(control<QPushButton>(page, "torrentProxyTest")->isEnabled());
    proxy.type = ProxyType::Shadowsocks;
    proxy.password = "secret";
    proxy.cipher = "aes-256-gcm";
    page.setApplicationProxy(proxy);
    REQUIRE(tcpPort->value() == 8443);
    REQUIRE(tcpHost->text() == "service.invalid");
    REQUIRE(resolver->text() == "2001:db8::53");
    REQUIRE(dnsPort->value() == 5353);
    REQUIRE(query->text() == "question.invalid");
    REQUIRE(notice->text().contains("HTTP"));
    REQUIRE(notice->text().contains("TLS"));
    REQUIRE_FALSE(QFileInfo::exists(path));
    REQUIRE(page.settings().socks5Proxy.host.isEmpty());
}

TEST_CASE("Torrent proxy type popup shows three rows and supports keyboard selection", "[qt][torrent-settings][torrent-proxy-popup][gost][ui]")
{
    QTemporaryDir dir;
    SettingsTorrent page(dir.filePath("settings.json"), ProxyConfig{});
    // Match the application's padded settings popup, including large UI fonts.
    page.setStyleSheet("QComboBox { combobox-popup: 0; } "
                       "QComboBox QAbstractItemView { border: 1px solid #777; padding: 4px; "
                       "background: #303030; color: #eee; selection-background-color: #456; } "
                       "QComboBox QAbstractItemView::item { min-height: 22px; padding: 5px 10px; }");
    SECTION("normal font") {}
    SECTION("large font") {
        QFont font = page.font();
        font.setPointSizeF(20);
        page.setFont(font);
    }
    page.resize(700, 700);
    page.findChild<QTabWidget *>()->setCurrentIndex(1);
    auto *routing = control<QComboBox>(page, "proxyMode");
    routing->setCurrentIndex(routing->findData(static_cast<int>(ProxyMode::Custom)));
    auto *type = control<QComboBox>(page, "torrentProxyType");
    page.show();
    QCoreApplication::processEvents();
    auto *scroll = qobject_cast<QScrollArea *>(page.findChild<QTabWidget *>()->currentWidget());
    REQUIRE(scroll);
    scroll->ensureWidgetVisible(type);
    type->setFocus();
    type->showPopup();
    REQUIRE(waitForSnapshot([&] { return type->view()->isVisible(); }));
    auto *view = type->view();
    REQUIRE(type->count() == 3);
    CHECK_FALSE(view->verticalScrollBar()->isVisible());
    for (int row = 0; row < type->count(); ++row) {
        const auto rect = view->visualRect(type->model()->index(row, type->modelColumn()));
        CHECK(rect.height() >= view->fontMetrics().height());
        CHECK(view->viewport()->rect().contains(rect));
    }
    QTest::keyClick(view, Qt::Key_Down);
    QTest::keyClick(view, Qt::Key_Down);
    QTest::keyClick(view, Qt::Key_Return);
    CHECK(type->currentData().toInt() == static_cast<int>(ProxyType::Gost));
    // Cocoa's selected-item flash queues hidePopup through timers; Enter need
    // not hide synchronously. Still require closure within a bounded interval.
    QElapsedTimer closing;
    closing.start();
    const bool closed = QTest::qWaitFor([&] { return !view->isVisible(); }, 500);
    INFO("Popup close after Enter: " << closing.elapsed() << " ms");
    CHECK(closed);
}

TEST_CASE("GOST controls preserve independent profiles and mandatory encrypted routing", "[qt][gost][ui]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.socks5Proxy = {ProxyType::Socks5Tls, "socks.invalid", "socks-user", "socks-secret", {}, 1080, true, false};
    settings.shadowsocksProxy = {ProxyType::Shadowsocks, "ss.invalid", {}, "ss-secret", "aes-256-gcm", 8388, true, false};
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = {ProxyType::Gost, "gost.invalid", "gost-user", "gost-secret", {}, 1080, true, true};
    REQUIRE(saveSettings(path, settings));
    SettingsTorrent page(path, {});
    auto *type = control<QComboBox>(page, "torrentProxyType");
    REQUIRE(type->currentData().toInt() == int(ProxyType::Gost));
    auto *ca = control<QLineEdit>(page, "torrentGostCaFile");
    REQUIRE(control<QPushButton>(page, "torrentGostCaBrowse")->isEnabled());
    REQUIRE(control<QLineEdit>(page, "torrentProxyPassword")->echoMode() == QLineEdit::Password);
    REQUIRE_FALSE(control<QCheckBox>(page, "torrentProxyTls")->isEnabled());
    REQUIRE(control<QCheckBox>(page, "torrentProxyTls")->isChecked());
    REQUIRE(control<QCheckBox>(page, "dht")->isEnabled());
    REQUIRE(control<QCheckBox>(page, "utp")->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(page, "portMapping")->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(page, "localDiscovery")->isEnabled());
    ca->setText(dir.filePath("ca.pem"));
    control<QCheckBox>(page, "torrentProxyUdp")->setChecked(false);
    control<QLineEdit>(page, "torrentProxyHost")->setText("edited.invalid");
    for (const auto value : {ProxyType::Socks5, ProxyType::Shadowsocks, ProxyType::Gost})
        type->setCurrentIndex(type->findData(int(value)));
    REQUIRE(control<QLineEdit>(page, "torrentProxyHost")->text() == "edited.invalid");
    REQUIRE(ca->text() == dir.filePath("ca.pem"));
    REQUIRE_FALSE(control<QCheckBox>(page, "torrentProxyUdp")->isChecked());
    REQUIRE_FALSE(page.settings().gostProxy.udp);
    REQUIRE(page.settings().socks5Proxy == settings.socks5Proxy);
    REQUIRE(page.settings().shadowsocksProxy == settings.shadowsocksProxy);
    REQUIRE(page.save());
    SettingsTorrent reopened(path, {});
    REQUIRE(control<QComboBox>(reopened, "torrentProxyType")->currentData().toInt() == int(ProxyType::Gost));
    REQUIRE(control<QLineEdit>(reopened, "torrentGostCaFile")->text() == ca->text());
    const auto warning = control<QLabel>(reopened, "proxyWarning")->text();
    REQUIRE(warning.contains("GOST"));
    REQUIRE_FALSE(warning.contains("TCP-only"));
    REQUIRE_FALSE(warning.contains("UDP ASSOCIATE"));
    REQUIRE_FALSE(control<QLabel>(reopened, "networkRouteStatus")->text().contains("SOCKS5"));
}

TEST_CASE("GOST form uses application input styling and translated encrypted explanation", "[qt][gost][ui][gost-native]")
{
    QTemporaryDir dir;
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = {ProxyType::Gost, "proxy.test", "disposable-user", "disposable-secret", {}, 1080, true, true};
    const auto path = dir.filePath("settings.json");
    REQUIRE(saveSettings(path, settings));
    const auto localePath = qEnvironmentVariable("EISKALT_GOST_TRANSLATIONS");
    QTranslator translator;
    if (!localePath.isEmpty()) {
        REQUIRE(translator.load(localePath + "/de.qm"));
        qApp->installTranslator(&translator);
    }
    struct Restore {
        QString style = qApp->styleSheet();
        ~Restore() { qApp->setStyleSheet(style); }
    } restore;
    qApp->setStyleSheet(mac_input_style::macInputContrastStyle(qApp->palette()));
    SettingsTorrent page(path, {});
    page.resize(780, 900);
    page.findChild<QTabWidget *>()->setCurrentIndex(1);
    page.show();
    REQUIRE(waitForSnapshot([&] { return page.isVisible(); }));
    auto *scroll = qobject_cast<QScrollArea *>(page.findChild<QTabWidget *>()->currentWidget());
    REQUIRE(scroll);
    auto *ca = control<QLineEdit>(page, "torrentGostCaFile");
    scroll->ensureWidgetVisible(ca);
    QTest::qWait(50);
    REQUIRE(ca->isVisible());
    REQUIRE(ca->height() >= ca->fontMetrics().height());
    REQUIRE(control<QLineEdit>(page, "torrentProxyPassword")->echoMode() == QLineEdit::Password);
    auto *notice = control<QLabel>(page, "torrentGostNotice");
    REQUIRE(notice->text().contains("GOST"));
    if (!localePath.isEmpty()) REQUIRE_FALSE(notice->text().contains("requires authenticated"));
    const auto screenshots = qEnvironmentVariable("EISKALT_GOST_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        REQUIRE(page.grab().save(screenshots + "/gost-form-" + qEnvironmentVariable("QT_SCALE_FACTOR", "1") + ".png"));
    }
    auto *testButton = control<QPushButton>(page, "torrentProxyTest");
    scroll->ensureWidgetVisible(testButton);
    QTest::qWait(50);
    REQUIRE(scroll->viewport()->rect().contains(QRect(testButton->mapTo(scroll->viewport(), QPoint{}), testButton->size())));
    if (!screenshots.isEmpty())
        REQUIRE(page.grab().save(screenshots + "/gost-diagnostics-" + qEnvironmentVariable("QT_SCALE_FACTOR", "1") + ".png"));
    auto *type = control<QComboBox>(page, "torrentProxyType");
    scroll->ensureWidgetVisible(type);
    type->showPopup();
    REQUIRE(waitForSnapshot([&] { return type->view()->isVisible(); }));
    REQUIRE(type->count() == 3);
    REQUIRE_FALSE(type->view()->verticalScrollBar()->isVisible());
    for (int row = 0; row < 3; ++row)
        REQUIRE(type->view()->viewport()->rect().contains(type->view()->visualRect(type->model()->index(row, 0))));
    if (!screenshots.isEmpty())
        REQUIRE(type->view()->window()->grab().save(screenshots + "/gost-popup-" + qEnvironmentVariable("QT_SCALE_FACTOR", "1") + ".png"));
    type->hidePopup();
}

TEST_CASE("GOST CA picker selects only the independent profile file", "[qt][gost][ui]")
{
    QTemporaryDir dir;
    const auto certificate = dir.filePath("public-ca.pem");
    QFile file(certificate);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("disposable picker file");
    file.close();
    SettingsTorrent page(dir.filePath("settings.json"), {});
    auto *mode = control<QComboBox>(page, "proxyMode");
    mode->setCurrentIndex(mode->findData(int(ProxyMode::Custom)));
    auto *type = control<QComboBox>(page, "torrentProxyType");
    type->setCurrentIndex(type->findData(int(ProxyType::Gost)));
    struct DialogPolicy {
        bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        DialogPolicy() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true); }
        ~DialogPolicy() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); }
    } policy;
    bool selected = false;
    QTimer::singleShot(50, &page, [&] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                dialog->setDirectory(dir.path());
                dialog->selectFile(certificate);
                // The file model loads asynchronously. Enter the full path just
                // as a user can, instead of depending on a populated file view.
                if (auto *name = dialog->findChild<QLineEdit *>("fileNameEdit")) name->setText(certificate);
                selected = QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        }
    });
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, &page, [] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QFileDialog *>(widget)) dialog->reject();
    });
    watchdog.start(1500);
    control<QPushButton>(page, "torrentGostCaBrowse")->click();
    REQUIRE(selected);
    REQUIRE(control<QLineEdit>(page, "torrentGostCaFile")->text() == certificate);
    REQUIRE(page.settings().gostProxy.caFile == certificate);
    REQUIRE(page.settings().socks5Proxy.caFile.isEmpty());
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("settings.json")));
}

TEST_CASE("GOST settings diagnostic loads CA and sends unsaved targets without saving", "[qt][gost][ui][routing]")
{
    QTemporaryDir dir;
    proxy_test::GostServer server;
    server.start();
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = {ProxyType::Gost, "127.0.0.1", "fixture-user", "fixture-secret", {}, server.port(), true, false};
    settings.gostProxy.caFile = dir.filePath("ca.pem");
    QFile ca(settings.gostProxy.caFile);
    REQUIRE(ca.open(QIODevice::WriteOnly));
    ca.write(QByteArray::fromStdString(server.ca.pem));
    ca.close();
    const auto path = dir.filePath("settings.json");
    REQUIRE(saveSettings(path, settings));
    QFile stored(path);
    REQUIRE(stored.open(QIODevice::ReadOnly));
    const auto before = stored.readAll();
    stored.close();
    SettingsTorrent page(path, {});
    control<QLineEdit>(page, "torrentProxyTestTcpHost")->setText("ui-target.invalid");
    control<QSpinBox>(page, "torrentProxyTestTcpPort")->setValue(32123);
    bool invalid = false;
    SECTION("trusted CA") {}
    SECTION("missing CA fails closed") { invalid = true; REQUIRE(QFile::remove(ca.fileName())); }
    auto *runner = page.findChild<ProxyTestRunner *>();
    REQUIRE(runner);
    QSignalSpy done(runner, &ProxyTestRunner::finished);
    control<QPushButton>(page, "torrentProxyTest")->click();
    if (invalid) {
        REQUIRE_FALSE(runner->isRunning());
        REQUIRE(done.isEmpty());
        REQUIRE_FALSE(control<QLabel>(page, "torrentProxyTestResult")->text().isEmpty());
    } else {
        REQUIRE(waitForSnapshot([&] { return !done.isEmpty(); }));
        const auto result = qvariant_cast<ProxyTestRunner::Result>(done.first().first());
        REQUIRE(result.tcp.status == ProxyTestRunner::Status::Success);
        REQUIRE(result.udp.status == ProxyTestRunner::Status::NotRequested);
    }
    server.requestStop(); server.join();
    if (invalid) REQUIRE_FALSE(server.authReceived);
    else {
        REQUIRE(server.target == "google.com");
        REQUIRE(server.targetPort == 32123);
    }
    REQUIRE(stored.open(QIODevice::ReadOnly));
    REQUIRE(stored.readAll() == before);
    // Completed diagnostics already persist their own destinations, not the route.
    REQUIRE(QFileInfo::exists(path + ".diagnostics.ini") == !invalid);
}

TEST_CASE("Torrent diagnostic targets persist separately only when explicitly saved", "[qt][torrent-settings][torrent-proxy-targets]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    {
        SettingsTorrent page(path, ProxyConfig{});
        control<QLineEdit>(page, "torrentProxyTestTcpHost")->setText("discard.invalid");
    }
    SettingsTorrent page(path, ProxyConfig{});
    REQUIRE(control<QLineEdit>(page, "torrentProxyTestTcpHost")->text() != "discard.invalid");
    control<QLineEdit>(page, "torrentProxyTestTcpHost")->setText("persist.invalid");
    control<QSpinBox>(page, "torrentProxyTestTcpPort")->setValue(8080);
    control<QLineEdit>(page, "torrentProxyTestDnsResolver")->setText("2001:db8::53");
    control<QSpinBox>(page, "torrentProxyTestDnsPort")->setValue(5353);
    control<QLineEdit>(page, "torrentProxyTestDnsQuery")->setText("saved.invalid");
    REQUIRE(page.save());
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    REQUIRE_FALSE(bytes.contains("persist.invalid"));
    REQUIRE_FALSE(bytes.contains("saved.invalid"));
    SettingsTorrent reopened(path, ProxyConfig{});
    REQUIRE(control<QLineEdit>(reopened, "torrentProxyTestTcpHost")->text() == "google.com");
    REQUIRE(control<QSpinBox>(reopened, "torrentProxyTestTcpPort")->value() == 8080);
    REQUIRE(control<QLineEdit>(reopened, "torrentProxyTestDnsResolver")->text() == "2001:db8::53");
    REQUIRE(control<QSpinBox>(reopened, "torrentProxyTestDnsPort")->value() == 5353);
    REQUIRE(control<QLineEdit>(reopened, "torrentProxyTestDnsQuery")->text() == "google.com");
}

TEST_CASE("Torrent diagnostic TCP port defaults follow protocol until explicitly edited", "[qt][torrent-settings][torrent-proxy-targets]")
{
    QTemporaryDir dir;
    ProxyConfig proxy{ProxyType::Socks5, "127.0.0.1", {}, {}, {}, 1080, true, true};
    SettingsTorrent page(dir.filePath("settings.json"), proxy);
    auto *port = control<QSpinBox>(page, "torrentProxyTestTcpPort");
    proxy.type = ProxyType::Shadowsocks;
    page.setApplicationProxy(proxy);
    REQUIRE(port->value() == 80);
    proxy.type = ProxyType::Socks5;
    page.setApplicationProxy(proxy);
    REQUIRE(port->value() == 443);
    port->setValue(80);
    proxy.type = ProxyType::Shadowsocks;
    page.setApplicationProxy(proxy);
    proxy.type = ProxyType::Socks5;
    page.setApplicationProxy(proxy);
    REQUIRE(port->value() == 80);
}

TEST_CASE("Torrent diagnostic rejects invalid targets before connecting", "[qt][torrent-settings][torrent-proxy-targets]")
{
    QTemporaryDir dir;
    QTcpServer proxy;
    proxy.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(proxy.listen(QHostAddress::LocalHost));
    ProxyConfig config{ProxyType::Socks5, "127.0.0.1", {}, {}, {}, proxy.serverPort(), true, true};
    SettingsTorrent page(dir.filePath("settings.json"), config);
    // Host and query are fixed; the remaining editable resolver must be numeric.
    control<QLineEdit>(page, "torrentProxyTestDnsResolver")->setText("resolver.invalid");
    control<QPushButton>(page, "torrentProxyTest")->click();
    QCoreApplication::processEvents();
    REQUIRE_FALSE(proxy.hasPendingConnections());
    REQUIRE_FALSE(control<QLabel>(page, "torrentProxyTestResult")->text().isEmpty());
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("settings.json")));
}

TEST_CASE("Torrent proxy test sends fixed Google hostname with the chosen port", "[qt][torrent-settings][torrent-proxy-targets][network]")
{
    QTemporaryDir dir;
    QTcpServer proxy;
    proxy.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(proxy.listen(QHostAddress::LocalHost));
    ProxyConfig config{ProxyType::Socks5, "127.0.0.1", {}, {}, {}, proxy.serverPort(), true, false};
    SettingsTorrent page(dir.filePath("settings.json"), config);
    control<QLineEdit>(page, "torrentProxyTestTcpHost")->setText("chosen.invalid");
    control<QSpinBox>(page, "torrentProxyTestTcpPort")->setValue(8443);
    control<QPushButton>(page, "torrentProxyTest")->click();
    REQUIRE(waitForSnapshot([&] { return proxy.hasPendingConnections(); }));
    std::unique_ptr<QTcpSocket> peer(proxy.nextPendingConnection());
    REQUIRE(waitForSnapshot([&] { return peer->bytesAvailable() >= 3; }));
    REQUIRE(peer->read(3) == QByteArray::fromHex("050100"));
    peer->write(QByteArray::fromHex("0500"));
    const QByteArray expected = QByteArray::fromHex("050100030a") + "google.com" + QByteArray::fromHex("20fb");
    REQUIRE(waitForSnapshot([&] { return peer->bytesAvailable() >= expected.size(); }));
    REQUIRE(peer->read(expected.size()) == expected);
    peer->write(QByteArray::fromHex("050000017f0000010001"));
    REQUIRE(waitForSnapshot([&] { return control<QPushButton>(page, "torrentProxyTest")->isEnabled(); }));
    REQUIRE(control<QLabel>(page, "torrentProxyTestResult")->text().contains("google.com:8443"));
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("settings.json")));
}

TEST_CASE("Torrent-only proxy retains its own credentials and UDP choice", "[qt][torrent-settings][torrent-custom-proxy]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    ProxyConfig application;
    application.type = ProxyType::Socks5;
    application.host = "dc-proxy.invalid";
    application.port = 9000;
    SettingsTorrent page(path, application);
    auto *routing = control<QComboBox>(page, "proxyMode");
    REQUIRE(routing->findData(3) >= 0);
    routing->setCurrentIndex(routing->findData(3));
    auto *host = control<QLineEdit>(page, "torrentProxyHost");
    auto *port = control<QSpinBox>(page, "torrentProxyPort");
    auto *user = control<QLineEdit>(page, "torrentProxyUser");
    auto *password = control<QLineEdit>(page, "torrentProxyPassword");
    auto *type = control<QComboBox>(page, "torrentProxyType");
    auto *udp = control<QCheckBox>(page, "torrentProxyUdp");
    host->setText("torrent-proxy.invalid");
    port->setValue(1081);
    user->setText("torrent-user");
    password->setText("torrent-secret");
    REQUIRE(udp->isChecked());
    REQUIRE(control<QCheckBox>(page, "dht")->isEnabled());
    udp->setChecked(false);
    REQUIRE_FALSE(control<QCheckBox>(page, "dht")->isEnabled());
    type->setCurrentIndex(type->findData(static_cast<int>(ProxyType::Shadowsocks)));
    REQUIRE_FALSE(user->isEnabled());
    REQUIRE(control<QComboBox>(page, "torrentProxyCipher")->isEnabled());
    host->setText("ss-proxy.invalid");
    password->setText("ss-secret");
    type->setCurrentIndex(type->findData(static_cast<int>(ProxyType::Socks5)));
    REQUIRE(host->text() == "torrent-proxy.invalid");
    REQUIRE(port->value() == 1081);
    REQUIRE(user->text() == "torrent-user");
    REQUIRE(password->text() == "torrent-secret");
    REQUIRE_FALSE(udp->isChecked());
    REQUIRE_FALSE(control<QComboBox>(page, "torrentProxyCipher")->isEnabled());
    page.setApplicationProxy({});
    REQUIRE(host->text() == "torrent-proxy.invalid");
    REQUIRE_FALSE(control<QCheckBox>(page, "localDiscovery")->isEnabled());
    REQUIRE(page.save());
    SettingsTorrent reopened(path, application);
    REQUIRE(control<QComboBox>(reopened, "proxyMode")->currentData().toInt() == 3);
    REQUIRE(control<QLineEdit>(reopened, "torrentProxyHost")->text() == "torrent-proxy.invalid");
    REQUIRE(control<QLineEdit>(reopened, "torrentProxyPassword")->text() == "torrent-secret");
    REQUIRE_FALSE(control<QCheckBox>(reopened, "torrentProxyUdp")->isChecked());
    auto *tls = control<QCheckBox>(reopened, "torrentProxyTls");
    control<QCheckBox>(reopened, "torrentProxyUdp")->setChecked(true);
    tls->setChecked(true);
    REQUIRE_FALSE(control<QCheckBox>(reopened, "torrentProxyUdp")->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(reopened, "dht")->isEnabled());
    tls->setChecked(false);
    REQUIRE(control<QCheckBox>(reopened, "dht")->isEnabled());
}

TEST_CASE("Torrent proxy profiles are isolated and invalid documents fail closed", "[qt][torrent-settings][torrent-custom-proxy]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    eiskalt::torrent::Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.socks5Proxy.host = "torrent.invalid";
    settings.socks5Proxy.user = "torrent-user";
    settings.socks5Proxy.password = "torrent-password";
    ProxyConfig application;
    application.type = ProxyType::Socks5;
    application.host = "dc.invalid";
    application.password = "dc-private-password";
    REQUIRE(selectedProxy(settings, application).host == "torrent.invalid");
    REQUIRE(saveSettings(path, settings));
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    REQUIRE_FALSE(bytes.contains("dc.invalid"));
    REQUIRE_FALSE(bytes.contains("dc-private-password"));
#ifndef Q_OS_WIN
    REQUIRE_FALSE(file.permissions().testFlag(QFileDevice::ReadGroup));
    REQUIRE_FALSE(file.permissions().testFlag(QFileDevice::ReadOther));
#endif
    file.close();
    auto json = QJsonDocument::fromJson(bytes).object();
    auto profiles = json["torrentProxy"].toObject();
    auto socks = profiles["socks5"].toObject();
    socks["port"] = "1080";
    profiles["socks5"] = socks;
    json["torrentProxy"] = profiles;
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(json).toJson());
    file.close();
    const auto invalid = loadSettings(path);
    REQUIRE_FALSE(invalid.enabled);
    REQUIRE(invalid.proxyMode == ProxyMode::RequireProxy);
    settings.proxyMode = ProxyMode::Direct;
    REQUIRE(selectedProxy(settings, application).type == ProxyType::Direct);
    settings.proxyMode = ProxyMode::FollowApplication;
    REQUIRE(selectedProxy(settings, application).host == "dc.invalid");
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Shadowsocks;
    settings.shadowsocksProxy.host = "ss.invalid";
    settings.shadowsocksProxy.password = QString::fromLatin1(QByteArray(16, 'x').toBase64());
    settings.shadowsocksProxy.cipher = "2022-blake3-aes-128-gcm";
    REQUIRE(validateSettings(settings).isEmpty());
    settings.shadowsocksProxy.password = "secret-that-is-not-base64";
    const auto error = validateSettings(settings);
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(error.contains(settings.shadowsocksProxy.password));
    REQUIRE_FALSE(saveSettings(path, settings));
}

TEST_CASE("Torrent port and encryption controls preserve explicit choices", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    SettingsTorrent page(path, ProxyConfig{});
    auto *random = control<QCheckBox>(page, "randomizePort");
    auto *port = control<QSpinBox>(page, "listenPort");
    auto *encryption = control<QComboBox>(page, "encryptionMode");
    REQUIRE(random->isChecked());
    REQUIRE_FALSE(port->isEnabled());
    REQUIRE(control<QCheckBox>(page, "localDiscovery")->isChecked());
    REQUIRE(encryption->count() == 3);
    REQUIRE(encryption->currentData().toInt() == 1);
    REQUIRE_FALSE(control<QLabel>(page, "effectiveListeners")->text().contains("6881"));
    random->setChecked(false);
    REQUIRE(port->isEnabled());
    port->setValue(49011);
    encryption->setCurrentIndex(encryption->findData(2));
    control<QCheckBox>(page, "localDiscovery")->setChecked(false);
    REQUIRE(page.save());
    SettingsTorrent reopened(path, ProxyConfig{});
    REQUIRE_FALSE(control<QCheckBox>(reopened, "randomizePort")->isChecked());
    REQUIRE(control<QSpinBox>(reopened, "listenPort")->isEnabled());
    REQUIRE(control<QSpinBox>(reopened, "listenPort")->value() == 49011);
    REQUIRE(control<QComboBox>(reopened, "encryptionMode")->currentData().toInt() == 2);
    REQUIRE_FALSE(control<QCheckBox>(reopened, "localDiscovery")->isChecked());
    random->setChecked(true);
    REQUIRE_FALSE(port->isEnabled());
    REQUIRE(port->value() == 49011);
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = 9;
    page.setApplicationProxy(proxy);
    REQUIRE_FALSE(random->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(page, "localDiscovery")->isEnabled());
    REQUIRE(encryption->isEnabled());
    REQUIRE(random->isChecked());
    page.setApplicationProxy({});
    REQUIRE(random->isEnabled());
    REQUIRE_FALSE(port->isEnabled());
}

TEST_CASE("Torrent preferences observe confirmed runtime listeners without starting networking", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    SettingsTorrent page(path, ProxyConfig{});
    auto *label = control<QLabel>(page, "effectiveListeners");
    REQUIRE(label->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding);
    REQUIRE(label->toolTip().contains("not a live socket inventory"));
    const auto idleText = label->text();
    auto engine = std::make_unique<TorrentEngine>(dir.filePath("state"));
    Settings settings;
    settings.bindAddress = "127.0.0.1";
    settings.bindAddress6.clear();
    settings.bootstrapNodes.clear();
    settings.dht = settings.pex = settings.localDiscovery = settings.portMapping = false;
    engine->configure(settings, {});
    page.setEngine(engine.get());
    REQUIRE(engine->effectiveListeners().isEmpty());
    REQUIRE(label->text() == idleText);
    const auto id = engine->add(offlineTorrent(dir), dir.filePath("downloads"), {}, true);
    REQUIRE(waitForSnapshot([&] { return !engine->files(id).isEmpty(); }));
    REQUIRE(engine->effectiveListeners().isEmpty());
    engine->pause(id, false);
    REQUIRE(waitForSnapshot([&] { return !engine->effectiveListeners().isEmpty(); }));
    REQUIRE(waitForSnapshot([&] { return label->text() == engine->effectiveListeners().join('\n'); }));
    SettingsTorrent reopened(path, ProxyConfig{});
    reopened.setEngine(engine.get());
    REQUIRE(control<QLabel>(reopened, "effectiveListeners")->text() == engine->effectiveListeners().join('\n'));
    control<QCheckBox>(page, "randomizePort")->setChecked(false);
    control<QSpinBox>(page, "listenPort")->setValue(12345);
    REQUIRE(label->text() == engine->effectiveListeners().join('\n'));
    REQUIRE_FALSE(QFileInfo::exists(path));
    engine.reset();
    REQUIRE(waitForSnapshot([&] { return label->text() == idleText; }));
    REQUIRE(control<QLabel>(reopened, "effectiveListeners")->text() == idleText);
}

TEST_CASE("Torrent preferences persist edited values and notify only after success", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    SettingsTorrent page(path, ProxyConfig{});
    QSignalSpy saved(&page, &SettingsTorrent::saved);
    control<QLineEdit>(page, "downloadPath")->setText(dir.filePath("incoming"));
    control<QLineEdit>(page, "completedPath")->setText(dir.filePath("complete"));
    control<QSpinBox>(page, "downloadLimitKiB")->setValue(128);
    control<QSpinBox>(page, "uploadLimitKiB")->setValue(64);
    control<QSpinBox>(page, "listenPort")->setValue(49001);
    control<QComboBox>(page, "proxyMode")->setCurrentIndex(2);
    QString error;
    REQUIRE(page.save(&error));
    REQUIRE(error.isEmpty());
    REQUIRE(saved.count() == 1);
    const auto restored = loadSettings(path);
    REQUIRE(restored.downloadLimitKiB == 128);
    REQUIRE(restored.uploadLimitKiB == 64);
    REQUIRE(restored.listenPort == 49001);
    REQUIRE(restored.downloadPath == dir.filePath("incoming"));
    REQUIRE(restored.completedPath == dir.filePath("complete"));
    REQUIRE(restored.proxyMode == ProxyMode::Direct);
    SettingsTorrent reopened(path, ProxyConfig{});
    REQUIRE(control<QSpinBox>(reopened, "downloadLimitKiB")->value() == 128);
}

TEST_CASE("Torrent preferences reject invalid settings without overwriting", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    SettingsTorrent page(path, ProxyConfig{});
    REQUIRE(page.save());
    QSignalSpy saved(&page, &SettingsTorrent::saved);
    control<QLineEdit>(page, "bindAddress")->setText("not-an-address");
    QString error;
    REQUIRE_FALSE(page.save(&error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(saved.count() == 0);
    REQUIRE(loadSettings(path).bindAddress == "0.0.0.0");
}

TEST_CASE("Disabled Torrent preferences retain visible controls and saved values", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    SettingsTorrent page(dir.filePath("settings.json"), ProxyConfig{});
    auto *rate = control<QSpinBox>(page, "downloadLimitKiB");
    rate->setValue(200);
    control<QCheckBox>(page, "enabled")->setChecked(false);
    REQUIRE_FALSE(rate->isEnabled());
    REQUIRE_FALSE(rate->isHidden());
    REQUIRE(page.save());
    REQUIRE(page.settings().downloadLimitKiB == 200);
    control<QCheckBox>(page, "enabled")->setChecked(true);
    REQUIRE(rate->isEnabled());
    REQUIRE(rate->value() == 200);
}

TEST_CASE("Torrent proxy choices explain effective restrictions without erasing preferences", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    SettingsTorrent page(dir.filePath("settings.json"), ProxyConfig{});
    auto *mode = control<QComboBox>(page, "proxyMode");
    auto *warning = control<QLabel>(page, "proxyWarning");
    mode->setCurrentIndex(1);
    REQUIRE_FALSE(warning->text().isEmpty());
    REQUIRE_FALSE(control<QSpinBox>(page, "listenPort")->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(page, "dht")->isEnabled());
    REQUIRE(page.settings().dht);
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = 1080;
    proxy.udp = true;
    page.setApplicationProxy(proxy);
    REQUIRE(control<QCheckBox>(page, "dht")->isEnabled());
    mode->setCurrentIndex(2);
    REQUIRE_FALSE(warning->text().isEmpty());
    control<QCheckBox>(page, "randomizePort")->setChecked(false);
    REQUIRE(control<QSpinBox>(page, "listenPort")->isEnabled());
    REQUIRE(control<QCheckBox>(page, "portMapping")->isEnabled());
    proxy.type = ProxyType::Shadowsocks;
    page.setApplicationProxy(proxy);
    mode->setCurrentIndex(0);
    REQUIRE_FALSE(control<QCheckBox>(page, "utp")->isEnabled());
    REQUIRE_FALSE(warning->text().isEmpty());
}

TEST_CASE("Torrent discovery explains UDP restrictions and offers a proxy shortcut", "[qt][torrent-settings][torrent-discovery-ui]")
{
    QTemporaryDir dir;
    SettingsTorrent page(dir.filePath("settings.json"), ProxyConfig{});
    auto *mode = control<QComboBox>(page, "proxyMode");
    mode->setCurrentIndex(mode->findData(static_cast<int>(ProxyMode::Custom)));
    control<QLineEdit>(page, "torrentProxyHost")->setText("127.0.0.1");
    auto *udp = control<QCheckBox>(page, "torrentProxyUdp");
    udp->setChecked(false);
    auto *status = control<QLabel>(page, "networkRouteStatus");
    auto *configure = control<QPushButton>(page, "configureTorrentProxy");
    REQUIRE(status->text().contains("UDP"));
    REQUIRE(configure->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(page, "dht")->isEnabled());
    REQUIRE_FALSE(control<QLineEdit>(page, "bootstrapNodes")->isEnabled());
    REQUIRE(page.settings().dht);
    configure->click();
    REQUIRE_FALSE(udp->isChecked());
    udp->setChecked(true);
    REQUIRE(control<QCheckBox>(page, "dht")->isEnabled());
    REQUIRE(control<QLineEdit>(page, "bootstrapNodes")->isEnabled());
    REQUIRE(status->text().contains("relay"));
    control<QLineEdit>(page, "torrentProxyHost")->clear();
    REQUIRE_FALSE(control<QCheckBox>(page, "dht")->isEnabled());
    REQUIRE_FALSE(control<QLineEdit>(page, "bootstrapNodes")->isEnabled());
    REQUIRE(status->text().contains("blocked"));
    control<QLineEdit>(page, "torrentProxyHost")->setText("127.0.0.1");
    control<QCheckBox>(page, "torrentProxyTls")->setChecked(true);
    REQUIRE_FALSE(control<QCheckBox>(page, "dht")->isEnabled());
    REQUIRE(status->text().contains("TCP-only"));
    REQUIRE(udp->isChecked());
    control<QCheckBox>(page, "torrentProxyTls")->setChecked(false);
    REQUIRE(control<QCheckBox>(page, "dht")->isEnabled());
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("settings.json")));
}

TEST_CASE("Managed Torrent sharing defaults ON and preserves saved OFF", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    SettingsTorrent page(path, ProxyConfig{});
    auto *sharing = control<QCheckBox>(page, "shareCompleted");
    auto *name = control<QLineEdit>(page, "shareName");
    REQUIRE(sharing->isEnabled());
    REQUIRE(sharing->isChecked());
    REQUIRE(name->isEnabled());
    REQUIRE_FALSE(sharing->isHidden());
    REQUIRE_FALSE(control<QLabel>(page, "sharingStatus")->text().isEmpty());
    sharing->setChecked(true);
    REQUIRE(name->isEnabled());
    name->setText("Verified Torrents");
    QSignalSpy saved(&page, &SettingsTorrent::saved);
    REQUIRE(page.save());
    REQUIRE(saved.count() == 1);
    REQUIRE(loadSettings(path).shareCompleted);
    REQUIRE(loadSettings(path).shareName == "Verified Torrents");
    SettingsTorrent reopened(path, ProxyConfig{});
    REQUIRE(control<QCheckBox>(reopened, "shareCompleted")->isChecked());
    control<QCheckBox>(reopened, "shareCompleted")->setChecked(false);
    REQUIRE_FALSE(control<QLineEdit>(reopened, "shareName")->isEnabled());
    REQUIRE(reopened.save());
    REQUIRE_FALSE(loadSettings(path).shareCompleted);
    REQUIRE(loadSettings(path).shareName == "Verified Torrents");
}

TEST_CASE("Torrent save failure does not notify runtime to reconfigure", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    SettingsTorrent page(dir.path(), ProxyConfig{});
    QSignalSpy saved(&page, &SettingsTorrent::saved);
    QString error;
    REQUIRE_FALSE(page.save(&error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(saved.isEmpty());
}

TEST_CASE("Torrent preferences apply queue seeding and discovery edits", "[qt][torrent-settings]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    SettingsTorrent page(path, ProxyConfig{});
    control<QSpinBox>(page, "activeDownloads")->setValue(4);
    control<QSpinBox>(page, "connectionLimit")->setValue(300);
    control<QSpinBox>(page, "perTorrentConnections")->setValue(75);
    control<QSpinBox>(page, "seedMinutes")->setValue(120);
    control<QDoubleSpinBox>(page, "seedRatio")->setValue(2.5);
    control<QCheckBox>(page, "dht")->setChecked(false);
    control<QCheckBox>(page, "pex")->setChecked(false);
    control<QCheckBox>(page, "localDiscovery")->setChecked(true);
    control<QCheckBox>(page, "utp")->setChecked(false);
    control<QCheckBox>(page, "portMapping")->setChecked(false);
    control<QLineEdit>(page, "bootstrapNodes")->setText("127.0.0.1:49000");
    control<QLineEdit>(page, "bindAddress6")->setText("::1");
    REQUIRE(page.save());
    const auto saved = loadSettings(path);
    REQUIRE(saved.activeDownloads == 4);
    REQUIRE(saved.connectionLimit == 300);
    REQUIRE(saved.perTorrentConnections == 75);
    REQUIRE(saved.seedMinutes == 120);
    REQUIRE(saved.seedRatio == 2.5);
    REQUIRE_FALSE(saved.dht);
    REQUIRE_FALSE(saved.pex);
    REQUIRE(saved.localDiscovery);
    REQUIRE_FALSE(saved.utp);
    REQUIRE_FALSE(saved.portMapping);
    REQUIRE(saved.bootstrapNodes == "127.0.0.1:49000");
    REQUIRE(saved.bindAddress6 == "::1");
}

TEST_CASE("Closing and reopening the Torrent window preserves column widths without jobs", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    const auto path = dir.filePath("window.ini");
    {
        TorrentWindow window(&engine, path);
        auto *jobs = window.findChild<QTreeWidget *>("torrentJobs");
        REQUIRE(jobs);
        jobs->setColumnWidth(0, 371);
        jobs->setColumnWidth(1, 247);
        window.close();
        REQUIRE(engine.jobs().isEmpty());
    }
    TorrentWindow reopened(&engine, path);
    const auto *jobs = reopened.findChild<QTreeWidget *>("torrentJobs");
    REQUIRE(jobs);
    REQUIRE(jobs->columnWidth(0) == 371);
    REQUIRE(jobs->columnWidth(1) == 247);
    REQUIRE(engine.jobs().isEmpty());
}

TEST_CASE("Cancelling add Torrent leaves the engine untouched", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    TorrentWindow window(&engine, dir.filePath("window.ini"));
    bool sawDialog = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            sawDialog = dialog->objectName() == "torrentAddDialog";
            dialog->reject();
        }
    });
    window.addSource("magnet:?xt=urn:btih:0123456789012345678901234567890123456789");
    REQUIRE(sawDialog);
    REQUIRE(engine.jobs().isEmpty());
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("state/downloads")));
}

TEST_CASE("Torrent add requires explicit acknowledgement of a manual DC share", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    TorrentWindow window(&engine, dir.filePath("window.ini"));
    window.setManuallySharedDirectories({dir.path()});
    bool blocked = false, allowedAfterAcknowledgement = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        auto *destination = dialog->findChild<QLineEdit *>("torrentDestination");
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        if (destination && buttons) {
            destination->setText(dir.filePath("incoming"));
            blocked = !buttons->button(QDialogButtonBox::Ok)->isEnabled();
            const auto checks = dialog->findChildren<QCheckBox *>();
            for (auto *check : checks) {
                if (!check->isChecked())
                    check->setChecked(true);
            }
            allowedAfterAcknowledgement = buttons->button(QDialogButtonBox::Ok)->isEnabled();
        }
        dialog->reject();
    });
    window.addSource("magnet:?xt=urn:btih:0123456789012345678901234567890123456789");
    REQUIRE(blocked);
    REQUIRE(allowedAfterAcknowledgement);
    REQUIRE(engine.jobs().isEmpty());
}

TEST_CASE("Torrent destructive controls request share invalidation only after confirmation", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    Settings settings;
    settings.dht = settings.pex = settings.localDiscovery = settings.portMapping = false;
    settings.bootstrapNodes.clear();
    engine.configure(settings, ProxyConfig{});
    // A real deferred paused job validates action eligibility without a network
    // session or payload. This test covers confirmation, not backend deletion.
    const auto id = engine.add(offlineTorrent(dir), dir.filePath("downloads"), {}, true);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(waitForSnapshot([&] { return !engine.files(id).isEmpty(); }));
    TorrentWindow window(&engine, dir.filePath("window.ini"));
    auto *jobs = window.findChild<QTreeWidget *>("torrentJobs");
    REQUIRE(jobs);
    window.selectJob(id);
    QSignalSpy invalidated(&window, &TorrentWindow::shareInvalidationRequested);
    QSignalSpy storageInvalidated(&engine, &TorrentEngine::storageInvalidating);
    bool publicationInvalidatedFirst = false;
    QObject::connect(&window, &TorrentWindow::shareInvalidationRequested, &window,
                     [&](const QString &jobId) {
        publicationInvalidatedFirst = jobId == id && storageInvalidated.isEmpty();
    });
    QString actionName;
    SECTION("Remove keeps data and requires confirmation") { actionName = "torrentRemove"; }
    SECTION("Delete data is a separate confirmed action") { actionName = "torrentDelete"; }
    SECTION("Recheck requires confirmation") { actionName = "torrentRecheck"; }
    auto *action = window.findChild<QAction *>(actionName);
    REQUIRE(action);
    REQUIRE(action->isEnabled());
    bool cancelWasDefault = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            cancelWasDefault = box->defaultButton() == box->button(QMessageBox::Cancel);
            box->button(QMessageBox::Cancel)->click();
        }
    });
    action->trigger();
    REQUIRE(cancelWasDefault);
    REQUIRE(invalidated.isEmpty());
    REQUIRE(storageInvalidated.isEmpty());
    REQUIRE(engine.jobs().size() == 1);
    REQUIRE(engine.jobs().first().id == id);
    QTimer::singleShot(0, &window, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Yes)->click();
    });
    action->trigger();
    REQUIRE(invalidated.count() == 1);
    REQUIRE(invalidated.first().first().toString() == id);
    REQUIRE(publicationInvalidatedFirst);
    REQUIRE(storageInvalidated.count() == 1);
    REQUIRE(engine.effectiveListeners().isEmpty());
}

TEST_CASE("A stale Pause action cannot undo an immediate Torrent Stop", "[qt][torrent-window][torrent-ui-fixes]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    Settings settings;
    settings.dht = settings.pex = settings.localDiscovery = settings.portMapping = false;
    settings.bootstrapNodes.clear();
    settings.bindAddress = "127.0.0.1";
    settings.bindAddress6 = "::1";
    engine.configure(settings, ProxyConfig{});
    // Tracker-free metadata and loopback listeners: no external peer discovery.
    const auto id = engine.add(offlineTorrent(dir), dir.filePath("downloads"), {}, false);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(waitForSnapshot([&] { return !engine.files(id).isEmpty() && !engine.jobs().first().paused; }));
    TorrentWindow window(&engine, dir.filePath("window.ini"));
    window.selectJob(id);
    auto *pause = window.findChild<QAction *>("torrentPause");
    auto *stop = window.findChild<QAction *>("torrentStop");
    REQUIRE(pause);
    REQUIRE(stop);
    REQUIRE(pause->isEnabled());
    stop->trigger();
    REQUIRE(engine.jobs().first().stopped);
    // No event processing: this is before the 100ms coalesced refresh.
    pause->trigger();
    REQUIRE(engine.jobs().first().stopped);
    // Also cover a signal already queued before the control was disabled.
    REQUIRE(QMetaObject::invokeMethod(pause, "triggered", Qt::DirectConnection, Q_ARG(bool, false)));
    REQUIRE(engine.jobs().first().stopped);
    REQUIRE(engine.jobs().first().paused);
    REQUIRE_FALSE(pause->isEnabled());
}

TEST_CASE("Torrent source routing recognizes v1 v2 and encoded exact topics", "[qt][torrent-source]")
{
    const QString v1 = "magnet:?xt=urn:btih:0123456789012345678901234567890123456789&dn=example";
    const QString v2 = "MAGNET:?xt=urn:btmh:12200123456789012345678901234567890123456789012345678901234567890123";
    const QString encoded = "magnet:?xt=urn%3Abtih%3A0123456789012345678901234567890123456789";
    REQUIRE(torrent_source::normalize(v1) == v1);
    REQUIRE(torrent_source::normalize("  " + v2 + "  ") == v2);
    REQUIRE(torrent_source::normalize(encoded) == encoded);
    REQUIRE(torrent_source::normalize("magnet:?xt=urn:btih:malformed") == "magnet:?xt=urn:btih:malformed");
}

TEST_CASE("Torrent source routing never hijacks DC magnets or unrelated URLs", "[qt][torrent-source]")
{
    const QString tiger = "magnet:?xt=urn:tree:tiger:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG";
    REQUIRE(torrent_source::normalize(tiger).isEmpty());
    REQUIRE(torrent_source::normalize(tiger + "&xt=urn:btih:0123456789012345678901234567890123456789").isEmpty());
    REQUIRE(torrent_source::normalize("magnet:?xt=urn:btih:abc&xt=urn%3Abitprint%3Aabc.def").isEmpty());
    REQUIRE(torrent_source::normalize("magnet:?xt=urn:tree:tiger/1024:abc&xt=urn:btmh:abc").isEmpty());
    REQUIRE(torrent_source::normalize("magnet:?kt=some+keywords&xs=dchub://example.test").isEmpty());
    REQUIRE(torrent_source::normalize("magnet:?dn=urn:btih:abc&tr=https://example.test/urn:btmh:abc").isEmpty());
    REQUIRE(torrent_source::normalize("https://example.test/download.torrent").isEmpty());
    REQUIRE(torrent_source::normalize("https://example.test/?xt=urn:btih:abc").isEmpty());
    REQUIRE(torrent_source::normalize("adc://example.test").isEmpty());
    REQUIRE(torrent_source::normalize("--version").isEmpty());
}

TEST_CASE("Torrent source routing decodes local file URLs but preserves literal path characters", "[qt][torrent-source]")
{
    const QString path = "/tmp/space # 100%.ToRrEnT";
    REQUIRE(torrent_source::normalize(path) == path);
    REQUIRE(torrent_source::normalize(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)) == path);
    REQUIRE(torrent_source::normalize("C:\\Downloads\\Example.torrent") == "C:\\Downloads\\Example.torrent");
    REQUIRE(torrent_source::normalize("/tmp/ordinary.xml.bz2").isEmpty());
    REQUIRE(torrent_source::normalize("file:///tmp/example.torrent?remote=1").isEmpty());
    REQUIRE(torrent_source::normalize(QString("/tmp/bad") + QChar(0) + ".torrent").isEmpty());
}

TEST_CASE("Torrent sharing status updates the matching job independently of row order", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    TorrentWindow window(&engine, dir.filePath("window.ini"));
    auto *jobs = window.findChild<QTreeWidget *>("torrentJobs");
    REQUIRE(jobs);
    REQUIRE(jobs->columnCount() == 9);
    REQUIRE(jobs->header()->visualIndex(8) == 2);
    auto *second = new QTreeWidgetItem(jobs);
    second->setData(0, Qt::UserRole, "second");
    auto *first = new QTreeWidgetItem(jobs);
    first->setData(0, Qt::UserRole, "first");
    QMetaObject::invokeMethod(&window, "setSharingStatus", Q_ARG(QString, "first"), Q_ARG(QString, "Pending hash"));
    REQUIRE(first->text(8) == "Pending hash");
    REQUIRE(second->text(8).isEmpty());
    QMetaObject::invokeMethod(&window, "setSharingStatus", Q_ARG(QString, "second"), Q_ARG(QString, "Blocked: private Torrent"));
    QMetaObject::invokeMethod(&window, "setSharingStatus", Q_ARG(QString, "first"), Q_ARG(QString, "Shared in DC"));
    REQUIRE(first->text(8) == "Shared in DC");
    REQUIRE(second->text(8) == "Blocked: private Torrent");
    QMetaObject::invokeMethod(&window, "setSharingStatus", Q_ARG(QString, "first"), Q_ARG(QString, QString()));
    REQUIRE_FALSE(first->text(8).isEmpty());
    REQUIRE(first->text(8) != "Shared in DC");
}

TEST_CASE("Torrent share status column preserves older saved transfer column widths", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    const auto path = dir.filePath("window.ini");
    {
        QTreeWidget previous;
        previous.setColumnCount(8);
        previous.header()->setStretchLastSection(false);
        previous.setColumnWidth(0, 371);
        previous.setColumnWidth(1, 247);
        QSettings saved(path, QSettings::IniFormat);
        saved.setValue("jobs/header", previous.header()->saveState());
    }
    TorrentEngine engine(dir.filePath("state"));
    TorrentWindow window(&engine, path);
    auto *jobs = window.findChild<QTreeWidget *>("torrentJobs");
    REQUIRE(jobs);
    REQUIRE(jobs->columnCount() == 9);
    REQUIRE(jobs->columnWidth(0) == 371);
    REQUIRE(jobs->columnWidth(1) == 247);
    REQUIRE_FALSE(jobs->isColumnHidden(8));
    REQUIRE(jobs->columnWidth(8) > 0);
}

TEST_CASE("Start-paused add exposes real metadata and applies file selection without payload", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    const auto source = offlineTorrent(dir);
    const auto destinationPath = dir.filePath("downloads");
    TorrentEngine engine(dir.filePath("state"));
    Settings settings;
    settings.downloadPath = destinationPath;
    settings.dht = false;
    settings.localDiscovery = false;
    settings.portMapping = false;
    engine.configure(settings, ProxyConfig{});
    TorrentWindow window(&engine, dir.filePath("window.ini"));
    bool startsPausedByDefault = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        auto *pause = dialog->findChild<QCheckBox *>("torrentStartPaused");
        auto *destination = dialog->findChild<QLineEdit *>("torrentDestination");
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        startsPausedByDefault = pause && pause->isChecked();
        if (destination && buttons && startsPausedByDefault) {
            destination->setText(destinationPath);
            buttons->button(QDialogButtonBox::Ok)->click();
        } else {
            dialog->reject();
        }
    });
    window.addSource(source);
    REQUIRE(startsPausedByDefault);
    REQUIRE(waitForSnapshot([&] { return engine.jobs().size() == 1 && !engine.files(engine.jobs().first().id).isEmpty(); }));
    const auto id = engine.jobs().first().id;
    REQUIRE(engine.jobs().first().paused);
    REQUIRE(engine.jobs().first().downloaded == 0);
    REQUIRE(engine.files(id).first().wanted);
    auto *files = window.findChild<QTreeWidget *>("torrentFiles");
    auto *apply = window.findChild<QPushButton *>("torrentApplyFiles");
    auto *jobs = window.findChild<QTreeWidget *>("torrentJobs");
    REQUIRE(files);
    REQUIRE(apply);
    REQUIRE(jobs);
    REQUIRE(waitForSnapshot([&] { return files->topLevelItemCount() == 1; }));
    REQUIRE(files->topLevelItem(0)->text(0) == "payload.bin");
    REQUIRE(files->topLevelItem(0)->checkState(0) == Qt::Checked);
    window.setSharingStatus(id, "Not shared: waiting for selected files");
    files->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    REQUIRE(apply->isEnabled());
    QSignalSpy invalidated(&window, &TorrentWindow::shareInvalidationRequested);
    QTimer::singleShot(0, &window, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Yes)->click();
    });
    apply->click();
    REQUIRE(invalidated.count() == 1);
    REQUIRE(invalidated.first().first().toString() == id);
    REQUIRE(waitForSnapshot([&] { return !engine.files(id).first().wanted; }));
    REQUIRE(waitForSnapshot([&] { return files->topLevelItem(0)->checkState(0) == Qt::Unchecked; }));
    REQUIRE(engine.jobs().first().paused);
    REQUIRE(engine.jobs().first().downloaded == 0);
    REQUIRE(jobs->topLevelItem(0)->text(8) == "Not shared: waiting for selected files");
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("downloads/payload.bin")));
}

TEST_CASE("Sharing status received before the first snapshot appears when the job arrives", "[qt][torrent-window]")
{
    QTemporaryDir dir;
    const auto source = offlineTorrent(dir);
    TorrentEngine engine(dir.filePath("state"));
    Settings settings;
    settings.dht = false;
    settings.localDiscovery = false;
    settings.portMapping = false;
    engine.configure(settings, ProxyConfig{});
    TorrentWindow window(&engine, dir.filePath("window.ini"));
    const auto id = engine.add(source, dir.filePath("downloads"), {}, true);
    REQUIRE_FALSE(id.isEmpty());
    window.setSharingStatus(id, "Pending hash");
    auto *jobs = window.findChild<QTreeWidget *>("torrentJobs");
    REQUIRE(jobs);
    REQUIRE(waitForSnapshot([&] { return jobs->topLevelItemCount() == 1; }));
    REQUIRE(jobs->topLevelItem(0)->data(0, Qt::UserRole).toString() == id);
    REQUIRE(jobs->topLevelItem(0)->text(8) == "Pending hash");
    engine.remove(id);
    REQUIRE(waitForSnapshot([&] { return jobs->topLevelItemCount() == 0; }));
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("downloads/payload.bin")));
}
#endif
