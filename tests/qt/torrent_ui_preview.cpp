#include "SettingsTorrent.h"
#include "TorrentWindow.h"
#include "TorrentCreateDialog.h"
#include "torrent/TorrentTypes.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDebug>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTranslator>
#include <QTreeWidget>
#include <QLabel>

struct TorrentWindowTestAccess {
    static void render(TorrentWindow &window, const QList<eiskalt::torrent::Peer> &peers)
    {
        window.renderPeers(QStringLiteral("synthetic-no-network"), peers);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 2 && argc != 3)
        return 2;
    QTranslator translator;
    if (argc == 3) {
        if (!translator.load(QString::fromLocal8Bit(argv[2]))) return 2;
        app.installTranslator(&translator);
    }
    QTemporaryDir profile;
    const QString output = QString::fromLocal8Bit(argv[1]);
    if (!profile.isValid() || !QDir().mkpath(output))
        return 2;
    eiskalt::torrent::ProxyConfig proxy;
    proxy.type = eiskalt::torrent::ProxyType::Shadowsocks;
    proxy.host = QStringLiteral("proxy.example");
    proxy.port = 8388;
    SettingsTorrent settings(profile.filePath("settings.json"), proxy);
    settings.resize(950, 680);
    settings.show();
    auto *tabs = settings.findChild<QTabWidget *>();
    for (int page = 0; page < tabs->count(); ++page) {
        tabs->setCurrentIndex(page);
        app.processEvents();
        if (!settings.grab().save(QDir(output).filePath(QStringLiteral("settings-%1.png").arg(page))))
            return 1;
    }
    auto *routing = settings.findChild<QComboBox *>(QStringLiteral("proxyMode"));
    auto *proxyType = settings.findChild<QComboBox *>(QStringLiteral("torrentProxyType"));
    auto *proxyHost = settings.findChild<QLineEdit *>(QStringLiteral("torrentProxyHost"));
    tabs->setCurrentIndex(1);
    routing->setCurrentIndex(routing->findData(3));
    proxyHost->setText(QStringLiteral("proxy.example"));
    settings.findChild<QLineEdit *>(QStringLiteral("torrentProxyUser"))->setText(QStringLiteral("qa-user"));
    settings.findChild<QLineEdit *>(QStringLiteral("torrentProxyPassword"))->setText(QStringLiteral("qa-password"));
    app.processEvents();
    if (!settings.grab().save(QDir(output).filePath(QStringLiteral("proxy-socks5.png")))) return 1;
    proxyType->setCurrentIndex(proxyType->findData(static_cast<int>(eiskalt::torrent::ProxyType::Shadowsocks)));
    proxyHost->setText(QStringLiteral("shadowsocks.example"));
    settings.findChild<QLineEdit *>(QStringLiteral("torrentProxyPassword"))->setText(QStringLiteral("qa-key"));
    app.processEvents();
    if (!settings.grab().save(QDir(output).filePath(QStringLiteral("proxy-shadowsocks.png")))) return 1;
    settings.resize(680, 500);
    app.processEvents();
    if (!settings.grab().save(QDir(output).filePath(QStringLiteral("proxy-compact.png")))) return 1;
    settings.resize(950, 680);
    settings.setApplicationProxy({});
    routing->setCurrentIndex(routing->findData(2));
    tabs->setCurrentIndex(2);
    app.processEvents();
    if (!settings.grab().save(QDir(output).filePath(QStringLiteral("network-direct.png"))))
        return 1;
    TorrentCreateDialog creator(nullptr);
    creator.findChild<QLineEdit *>(QStringLiteral("torrentCreateSource"))->setText(
        QStringLiteral("/Users/preview/Documents/Torrent sources/Example collection"));
    creator.findChild<QLineEdit *>(QStringLiteral("torrentCreateOutput"))->setText(
        QStringLiteral("/Users/preview/Documents/Example collection.torrent"));
    creator.show();
    const auto captureCreator = [&](const QString &name) {
        app.processEvents();
        return creator.grab().save(QDir(output).filePath(name));
    };
    if (!captureCreator(QStringLiteral("create-torrent.png"))) return 1;
    creator.resize(660, 600);
    if (!captureCreator(QStringLiteral("create-torrent-660.png"))) return 1;
    creator.resize(980, 780);
    if (!captureCreator(QStringLiteral("create-torrent-expanded.png"))) return 1;
    creator.resize(creator.minimumSizeHint());
    if (!captureCreator(QStringLiteral("create-torrent-minimum.png"))) return 1;
    creator.resize(740, 620);
    creator.findChild<QPlainTextEdit *>(QStringLiteral("torrentCreateTrackers"))->appendPlainText(
        QStringLiteral("https://private.example/announce?passkey=preview"));
    creator.findChild<QCheckBox *>(QStringLiteral("torrentCreatePrivate"))->setChecked(true);
    if (!captureCreator(QStringLiteral("create-torrent-private.png"))) return 1;
    creator.setLayoutDirection(Qt::RightToLeft);
    if (!captureCreator(QStringLiteral("create-torrent-rtl.png"))) return 1;
    creator.close();
    // No engine exists in this preview. Every peer and policy below is synthetic.
    TorrentWindow window(nullptr, profile.filePath("window.ini"));
    window.showError(QStringLiteral("SYNTHETIC VISUAL QA ONLY / NO NETWORK / no active Torrent engine or applied security policy."));
    window.show();
    app.processEvents();
    const bool saved = window.grab().save(QDir(output).filePath(QStringLiteral("torrents.png")));
    auto *details = window.findChild<QTabWidget *>(QStringLiteral("torrentDetails"));
    auto *peers = window.findChild<QTreeWidget *>(QStringLiteral("torrentPeers"));
    auto *splitter = window.findChild<QSplitter *>();
    auto *countries = window.findChild<QListWidget *>(QStringLiteral("torrentSecurityCountries"));
    if (!details || details->count() < 3 || !peers || !splitter || !countries) return 1;

    eiskalt::torrent::Peer ua, us, unknown;
    ua.id = QStringLiteral("synthetic-ua");
    ua.ip = QStringLiteral("192.0.2.9");
    ua.port = 51413;
    ua.countryCode = QStringLiteral("UA");
    ua.client = QStringLiteral("Synthetic client A");
    ua.progress = 0.625;
    ua.downloadRate = 262144;
    ua.uploadRate = 32768;
    ua.state = QStringLiteral("Connected (synthetic)");
    ua.transport = QStringLiteral("TCP");
    ua.encryption = QStringLiteral("RC4");
    us = ua;
    us.id = QStringLiteral("synthetic-us");
    us.ip = QStringLiteral("198.51.100.10");
    us.port = 6881;
    us.countryCode = QStringLiteral("US");
    us.client = QStringLiteral("Synthetic client B");
    us.progress = 0.987;
    us.downloadRate = 1572864;
    us.uploadRate = 131072;
    unknown = ua;
    unknown.id = QStringLiteral("synthetic-unknown");
    unknown.ip = QStringLiteral("2001:db8::2");
    unknown.port = 443;
    unknown.countryCode.clear();
    unknown.client = QStringLiteral("Unrecognized peer ID (synthetic)");
    unknown.progress = 0.125;
    unknown.downloadRate = 900;
    unknown.uploadRate = 1200;
    unknown.transport = QStringLiteral("uTP");
    unknown.encryption = QStringLiteral("Unknown");
    const QList<eiskalt::torrent::Peer> snapshots{ua, us, unknown};
    eiskalt::torrent::Settings security;
    security.encryptionMode = eiskalt::torrent::EncryptionMode::Required;
    security.blockedCountries = {QStringLiteral("US")};
    security.blockUnknownClients = false;
    window.setSecuritySettings(security);
    peers->sortItems(4, Qt::DescendingOrder);

    const auto captureWindow = [&](const QString &name, const QSize &size, int page) {
        details->setCurrentIndex(page);
        window.resize(size);
        app.processEvents();
        splitter->setSizes({size.height() / 5, size.height() * 4 / 5});
        if (page == 1) {
            TorrentWindowTestAccess::render(window, snapshots);
            if (auto *notice = window.findChild<QLabel *>(QStringLiteral("torrentPeerNotice")))
                notice->setText(QStringLiteral("Synthetic UA / US / unmapped peers on documentation IPs; no network. Unknown country intentionally has no flag."));
        } else {
            for (int i = 0; i < countries->count(); ++i) {
                if (countries->item(i)->data(Qt::UserRole).toString() == QStringLiteral("US"))
                    countries->scrollToItem(countries->item(i), QAbstractItemView::PositionAtCenter);
            }
        }
        app.processEvents();
        if (page == 0)
            window.findChild<QTreeWidget *>(QStringLiteral("torrentFiles"))->setEnabled(true);
        if (window.size() != size)
            qWarning() << "Preview minimum layout exceeds requested size:" << name
                       << "requested" << size << "actual" << window.size();
        return window.grab().save(QDir(output).filePath(name));
    };
    if (!captureWindow(QStringLiteral("peers-fixture.png"), QSize(1100, 650), 1)) return 1;
    if (!captureWindow(QStringLiteral("peers-compact.png"), QSize(720, 500), 1)) return 1;
    if (!captureWindow(QStringLiteral("security-fixture.png"), QSize(1100, 650), 2)) return 1;
    if (!captureWindow(QStringLiteral("security-compact.png"), QSize(720, 500), 2)) return 1;
    auto *files = window.findChild<QTreeWidget *>(QStringLiteral("torrentFiles"));
    files->clear();
    files->setEnabled(true);
    for (int i = 0; i < 8; ++i) {
        auto *item = new QTreeWidgetItem(files);
        item->setText(0, QStringLiteral("Synthetic collection / Example file %1.mkv").arg(i + 1));
        item->setText(1, QStringLiteral("1.00 GiB"));
        item->setCheckState(0, i % 2 ? Qt::Unchecked : Qt::Checked);
        item->setText(2, i % 3 == 0 ? QStringLiteral("High") : i % 3 == 1 ? QStringLiteral("Normal") : QStringLiteral("Low"));
        item->setData(2, Qt::UserRole + 1, i % 3 == 0 ? 7 : i % 3 == 1 ? 4 : 1);
    }
    if (!captureWindow(QStringLiteral("files-fixture.png"), QSize(1100, 650), 0)) return 1;
    QPalette dark = app.palette();
    dark.setColor(QPalette::Window, QColor(40, 40, 40));
    dark.setColor(QPalette::Base, QColor(30, 30, 30));
    dark.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    dark.setColor(QPalette::Text, Qt::white);
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Button, QColor(50, 50, 50));
    dark.setColor(QPalette::ButtonText, Qt::white);
    app.setPalette(dark);
    if (!captureWindow(QStringLiteral("files-dark.png"), QSize(1100, 650), 0)) return 1;
    if (!captureWindow(QStringLiteral("security-dark.png"), QSize(1100, 650), 2)) return 1;
    return saved ? 0 : 1;
}
