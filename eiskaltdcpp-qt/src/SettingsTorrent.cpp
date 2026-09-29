#include "SettingsTorrent.h"

#ifdef USE_TORRENT
#include "dcpp/stdinc.h"
#include "ProxyTestRunner.h"
#include "PasswordRevealAction.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentSettings.h"
#include "torrent/ProxyTrust.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QSettings>
#include <QSignalBlocker>
#include <QListView>
#include <QStyledItemDelegate>
#include <climits>

using namespace eiskalt::torrent;

namespace {
class ProxyTypeComboBox : public QComboBox {
public:
    explicit ProxyTypeComboBox(QWidget *parent) : QComboBox(parent) {
        setStyleSheet(QStringLiteral("QComboBox { combobox-popup: 0; }"));
        setView(new QListView(this));
        setItemDelegate(new QStyledItemDelegate(this));
        view()->setTextElideMode(Qt::ElideNone);
    }
    void showPopup() override {
        ensurePolished();
        view()->ensurePolished();
        view()->doItemsLayout();
        int rowsHeight = 0;
        for (int row = 0; row < count(); ++row)
            rowsHeight += qMax(view()->sizeHintForRow(row), view()->fontMetrics().height());
        // Include styled padding/borders in logical pixels, not a fixed row height.
        view()->setMinimumHeight(rowsHeight + 2 * view()->frameWidth());
        view()->setMinimumWidth(qMax(width(), view()->sizeHintForColumn(modelColumn()) + 2 * view()->frameWidth()));
        setMaxVisibleItems(count());
        QComboBox::showPopup();
    }
};
}

struct SettingsTorrent::Controls {
    eiskalt::torrent::Settings values;
    QCheckBox *enabled;
    QTabWidget *tabs;
    QComboBox *proxyMode, *encryptionMode;
    QLabel *proxyWarning, *effectiveListeners, *networkRouteStatus;
    QPushButton *configureProxy;
    QSpinBox *listenPort;
    QLineEdit *bindAddress, *bindAddress6, *bootstrapNodes, *shareName;
    QCheckBox *dht, *utp, *portMapping, *localDiscovery, *shareCompleted, *randomizePort;
    QGroupBox *customProxyBox;
    QFormLayout *proxyForm;
    QComboBox *proxyType, *proxyCipher;
    QLineEdit *proxyHost, *proxyUser, *proxyPassword;
    QLineEdit *gostCaFile;
    QPushButton *gostCaBrowse;
    QWidget *gostCaRow;
    QLabel *gostNotice;
    QSpinBox *proxyPort;
    QCheckBox *proxyTls, *proxyUdp;
    ProxyTestRunner *proxyTester;
    QPushButton *testProxy, *cancelProxyTest;
    QLabel *proxyTestResult;
    QLineEdit *testTcpHost, *testDnsResolver, *testDnsQuery;
    QSpinBox *testTcpPort, *testDnsPort;
    QLabel *testProtocol;
    bool testTcpPortExplicit = false;
    bool proxyTestStale = false;
    bool loadingProxy = false;
    ProxyTestRunner::ProbeTargets targets() const {
        return {QStringLiteral("google.com"), testTcpPort->value(), testDnsResolver->text(),
                testDnsPort->value(), QStringLiteral("google.com")};
    }
};

SettingsTorrent::SettingsTorrent(const QString &settingsPath, const ProxyConfig &proxy, QWidget *parent)
    : QWidget(parent), ui(new Controls), path(settingsPath), applicationProxy(proxy)
{
    ui->values = loadSettings(path);
    auto *layout = new QVBoxLayout(this);
    ui->enabled = new QCheckBox(tr("Enable Torrent support"), this);
    ui->enabled->setObjectName(QStringLiteral("enabled"));
    ui->enabled->setChecked(ui->values.enabled);
    layout->addWidget(ui->enabled);
    auto *idleNotice = new QLabel(tr("Opening preferences does not start Torrent networking. Limits below apply to Torrents only, not DC transfers."), this);
    idleNotice->setWordWrap(true);
    layout->addWidget(idleNotice);
    ui->tabs = new QTabWidget(this);
    layout->addWidget(ui->tabs);

    auto page = [this](const QString &title) {
        auto *scroll = new QScrollArea(ui->tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *widget = new QWidget(scroll);
        auto *column = new QVBoxLayout(widget);
        column->setAlignment(Qt::AlignTop);
        scroll->setWidget(widget);
        ui->tabs->addTab(scroll, title);
        return column;
    };
    auto group = [](QVBoxLayout *column, const QString &title) {
        auto *box = new QGroupBox(title, column->parentWidget());
        auto *form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        column->addWidget(box);
        return form;
    };
    auto text = [this](QFormLayout *form, const QString &label, const char *name,
                       QString eiskalt::torrent::Settings::*member, bool directory = false) {
        auto *edit = new QLineEdit(ui->values.*member, form->parentWidget());
        edit->setObjectName(QString::fromLatin1(name));
        connect(edit, &QLineEdit::textChanged, this, [this, member](const QString &value) {
            ui->values.*member = value;
        });
        if (directory) {
            auto *row = new QWidget(form->parentWidget());
            auto *line = new QHBoxLayout(row);
            line->setContentsMargins(0, 0, 0, 0);
            auto *browse = new QPushButton(tr("Browse..."), row);
            line->addWidget(edit);
            line->addWidget(browse);
            connect(browse, &QPushButton::clicked, this, [this, edit] {
                const auto chosen = QFileDialog::getExistingDirectory(this, tr("Select Torrent directory"), edit->text());
                if (!chosen.isEmpty())
                    edit->setText(chosen);
            });
            auto *buddy = new QLabel(label, row);
            buddy->setBuddy(edit);
            form->addRow(buddy, row);
        } else {
            form->addRow(label, edit);
        }
        return edit;
    };
    auto integer = [this](QFormLayout *form, const QString &label, const char *name,
                          int eiskalt::torrent::Settings::*member, int minimum, int maximum) {
        auto *spin = new QSpinBox(form->parentWidget());
        spin->setObjectName(QString::fromLatin1(name));
        spin->setRange(minimum, maximum);
        spin->setValue(ui->values.*member);
        form->addRow(label, spin);
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this, member](int value) {
            ui->values.*member = value;
        });
        return spin;
    };
    auto check = [this](QFormLayout *form, const QString &label, const char *name,
                        bool eiskalt::torrent::Settings::*member) {
        auto *box = new QCheckBox(label, form->parentWidget());
        box->setObjectName(QString::fromLatin1(name));
        box->setChecked(ui->values.*member);
        form->addRow(box);
        connect(box, &QCheckBox::toggled, this, [this, member](bool value) {
            ui->values.*member = value;
            updateEnabled();
        });
        return box;
    };

    auto *general = page(tr("Downloads"));
    auto *paths = group(general, tr("Storage"));
    text(paths, tr("Download directory:"), "downloadPath", &eiskalt::torrent::Settings::downloadPath, true)
        ->setPlaceholderText(tr("Engine default when empty"));
    text(paths, tr("Completed directory:"), "completedPath", &eiskalt::torrent::Settings::completedPath, true)
        ->setPlaceholderText(tr("Keep in download directory when empty"));
    auto *limits = group(general, tr("Torrent-only limits"));
    integer(limits, tr("Download limit (KiB/s, 0 = unlimited):"), "downloadLimitKiB", &eiskalt::torrent::Settings::downloadLimitKiB, 0, INT_MAX / 1024);
    integer(limits, tr("Upload limit (KiB/s, 0 = unlimited):"), "uploadLimitKiB", &eiskalt::torrent::Settings::uploadLimitKiB, 0, INT_MAX / 1024);
    integer(limits, tr("Active downloads:"), "activeDownloads", &eiskalt::torrent::Settings::activeDownloads, 1, 10000);
    integer(limits, tr("Total connections:"), "connectionLimit", &eiskalt::torrent::Settings::connectionLimit, 1, 100000);
    integer(limits, tr("Connections per Torrent:"), "perTorrentConnections", &eiskalt::torrent::Settings::perTorrentConnections, 1, 100000);
    auto *seeding = group(general, tr("Seeding (first enabled limit stops seeding, never deletes files)"));
    auto *ratio = new QDoubleSpinBox(this);
    ratio->setObjectName(QStringLiteral("seedRatio"));
    ratio->setRange(0, 100000);
    ratio->setDecimals(2);
    ratio->setSingleStep(0.1);
    ratio->setValue(ui->values.seedRatio);
    seeding->addRow(tr("Ratio (0 = unlimited):"), ratio);
    connect(ratio, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) { ui->values.seedRatio = value; });
    integer(seeding, tr("Time (minutes, 0 = unlimited):"), "seedMinutes", &eiskalt::torrent::Settings::seedMinutes, 0, 10000000);

    auto *proxyPage = page(tr("Proxy"));
    auto *routing = group(proxyPage, tr("Torrent routing"));
    ui->proxyMode = new QComboBox(this);
    ui->proxyMode->setObjectName(QStringLiteral("proxyMode"));
    ui->proxyMode->addItem(tr("Follow application routing"), static_cast<int>(ProxyMode::FollowApplication));
    ui->proxyMode->addItem(tr("Require application proxy"), static_cast<int>(ProxyMode::RequireProxy));
    ui->proxyMode->addItem(tr("Use direct Torrent networking"), static_cast<int>(ProxyMode::Direct));
    ui->proxyMode->addItem(tr("Use a Torrent-only proxy"), static_cast<int>(ProxyMode::Custom));
    ui->proxyMode->setCurrentIndex(ui->proxyMode->findData(static_cast<int>(ui->values.proxyMode)));
    routing->addRow(tr("Routing:"), ui->proxyMode);
    ui->proxyWarning = new QLabel(this);
    ui->proxyWarning->setObjectName(QStringLiteral("proxyWarning"));
    ui->proxyWarning->setWordWrap(true);
    ui->proxyWarning->setTextFormat(Qt::PlainText);
    routing->addRow(ui->proxyWarning);
    auto *diagnostics = group(proxyPage, tr("What to test (diagnostics only)"));
    QSettings diagnosticSettings(path + QStringLiteral(".diagnostics.ini"), QSettings::IniFormat);
    diagnosticSettings.beginGroup(QStringLiteral("ProxyTest"));
    auto diagnosticText = [&](const char *name, const char *key, const QString &fallback) {
        auto *edit = new QLineEdit(diagnosticSettings.value(QString::fromLatin1(key), fallback).toString(), this);
        edit->setObjectName(QString::fromLatin1(name));
        edit->setMaxLength(253);
        return edit;
    };
    auto diagnosticPort = [&](const char *name, const char *key, int fallback) {
        auto *port = new QSpinBox(this);
        port->setObjectName(QString::fromLatin1(name));
        port->setRange(1, 65535);
        port->setValue(diagnosticSettings.value(QString::fromLatin1(key), fallback).toInt());
        return port;
    };
    auto diagnosticEndpoint = [&](const QString &label, QLineEdit *host, QSpinBox *port) {
        auto *row = new QWidget(this);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->addWidget(host, 1);
        auto *portLabel = new QLabel(tr("Port:"), row);
        portLabel->setBuddy(port);
        line->addWidget(portLabel);
        port->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        line->addWidget(port);
        diagnostics->addRow(label, row);
    };
    ui->testTcpHost = diagnosticText("torrentProxyTestTcpHost", "tcpHost", QStringLiteral("google.com"));
    ui->testTcpHost->setText(QStringLiteral("google.com"));
    ui->testTcpHost->setReadOnly(true);
    ui->testTcpPort = diagnosticPort("torrentProxyTestTcpPort", "tcpPort", 443);
    diagnosticEndpoint(tr("TCP host / IP:"), ui->testTcpHost, ui->testTcpPort);
    ui->testTcpPortExplicit = diagnosticSettings.value(QStringLiteral("tcpPortExplicit"), false).toBool();
    ui->testDnsResolver = diagnosticText("torrentProxyTestDnsResolver", "dnsResolver", QStringLiteral("1.1.1.1"));
    ui->testDnsPort = diagnosticPort("torrentProxyTestDnsPort", "dnsPort", 53);
    diagnosticEndpoint(tr("UDP DNS resolver (numeric IP):"), ui->testDnsResolver, ui->testDnsPort);
    ui->testDnsQuery = diagnosticText("torrentProxyTestDnsQuery", "dnsQuery", QStringLiteral("google.com"));
    ui->testDnsQuery->setText(QStringLiteral("google.com"));
    ui->testDnsQuery->setReadOnly(true);
    diagnostics->addRow(tr("UDP DNS query name:"), ui->testDnsQuery);
    ui->testProtocol = new QLabel(this);
    ui->testProtocol->setObjectName(QStringLiteral("torrentProxyTestProtocol"));
    ui->testProtocol->setWordWrap(true);
    ui->testProtocol->setTextFormat(Qt::PlainText);
    diagnostics->addRow(ui->testProtocol);
    auto *testRow = new QWidget(this);
    auto *testLayout = new QHBoxLayout(testRow);
    testLayout->setContentsMargins(0, 0, 0, 0);
    ui->testProxy = new QPushButton(tr("Test proxy"), testRow);
    ui->testProxy->setObjectName(QStringLiteral("torrentProxyTest"));
    ui->testProxy->setToolTip(tr("Test the displayed proxy settings without saving them or interrupting transfers. TCP and UDP are checked separately; there is no direct fallback."));
    ui->cancelProxyTest = new QPushButton(tr("Cancel test"), testRow);
    ui->cancelProxyTest->setObjectName(QStringLiteral("torrentProxyTestCancel"));
    testLayout->addWidget(ui->testProxy);
    testLayout->addWidget(ui->cancelProxyTest);
    testLayout->addStretch();
    diagnostics->addRow(testRow);
    ui->proxyTestResult = new QLabel(this);
    ui->proxyTestResult->setObjectName(QStringLiteral("torrentProxyTestResult"));
    ui->proxyTestResult->setTextFormat(Qt::PlainText);
    ui->proxyTestResult->setWordWrap(true);
    ui->proxyTestResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    diagnostics->addRow(ui->proxyTestResult);
    ui->proxyTester = new ProxyTestRunner(this);
    connect(ui->testProxy, &QPushButton::clicked, this, &SettingsTorrent::testProxy);
    connect(ui->cancelProxyTest, &QPushButton::clicked, this, [this] {
        ui->proxyTester->cancel();
        ui->proxyTestResult->setText(tr("Cancelling proxy test..."));
    });
    connect(ui->proxyTester, &ProxyTestRunner::finished, this, [this](const ProxyTestRunner::Result &result) {
        if (!ui->proxyTestStale) {
            const auto udp = result.udp.status == ProxyTestRunner::Status::NotRequested
                ? tr("Not requested for this route") : result.udp.message;
            ui->proxyTestResult->setText(tr("TCP: %1\nUDP: %2").arg(result.tcp.message, udp));
            if (result.tcp.status != ProxyTestRunner::Status::Cancelled &&
                result.udp.status != ProxyTestRunner::Status::Cancelled)
                saveProbeTargets();
        }
        updateEnabled();
    });
    for (auto *edit : {ui->testTcpHost, ui->testDnsResolver, ui->testDnsQuery})
        connect(edit, &QLineEdit::textChanged, this, &SettingsTorrent::invalidateProxyTest);
    connect(ui->testTcpPort, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
        ui->testTcpPortExplicit = true;
        invalidateProxyTest();
    });
    connect(ui->testTcpPort, &QSpinBox::editingFinished, this, [this] { ui->testTcpPortExplicit = true; });
    connect(ui->testDnsPort, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsTorrent::invalidateProxyTest);
    ui->proxyForm = group(proxyPage, tr("Torrent-only proxy"));
    ui->customProxyBox = qobject_cast<QGroupBox *>(ui->proxyForm->parentWidget());
    ui->proxyType = new ProxyTypeComboBox(this);
    ui->proxyType->setObjectName(QStringLiteral("torrentProxyType"));
    ui->proxyType->addItem(QStringLiteral("SOCKS5"), static_cast<int>(ProxyType::Socks5));
    ui->proxyType->addItem(QStringLiteral("Shadowsocks"), static_cast<int>(ProxyType::Shadowsocks));
    ui->proxyType->addItem(QStringLiteral("GOST"), static_cast<int>(ProxyType::Gost));
    ui->proxyType->setCurrentIndex(ui->proxyType->findData(static_cast<int>(ui->values.customProxyType)));
    ui->proxyForm->addRow(tr("Type:"), ui->proxyType);
    auto proxyText = [this](const QString &label, const char *name) {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(QString::fromLatin1(name));
        ui->proxyForm->addRow(label, edit);
        connect(edit, &QLineEdit::textChanged, this, &SettingsTorrent::storeProxyControls);
        return edit;
    };
    ui->proxyHost = proxyText(tr("Server:"), "torrentProxyHost");
    ui->proxyHost->setPlaceholderText(tr("Proxy hostname or IP address"));
    ui->proxyPort = new QSpinBox(this);
    ui->proxyPort->setObjectName(QStringLiteral("torrentProxyPort"));
    ui->proxyPort->setRange(1, 65535);
    ui->proxyForm->addRow(tr("Port:"), ui->proxyPort);
    ui->proxyUser = proxyText(tr("Username:"), "torrentProxyUser");
    ui->proxyPassword = proxyText(tr("Password / key:"), "torrentProxyPassword");
    ui->proxyPassword->setEchoMode(QLineEdit::Password);
    new PasswordRevealAction(ui->proxyPassword);
    ui->proxyCipher = new QComboBox(this);
    ui->proxyCipher->setObjectName(QStringLiteral("torrentProxyCipher"));
    ui->proxyCipher->addItems({"aes-256-gcm", "aes-128-gcm", "chacha20-ietf-poly1305",
        "2022-blake3-aes-128-gcm", "2022-blake3-aes-256-gcm", "2022-blake3-chacha20-poly1305"});
    ui->proxyForm->addRow(tr("Cipher:"), ui->proxyCipher);
    ui->proxyTls = new QCheckBox(tr("Use TLS to the SOCKS5 server"), this);
    ui->proxyTls->setObjectName(QStringLiteral("torrentProxyTls"));
    ui->proxyForm->addRow(ui->proxyTls);
    ui->proxyUdp = new QCheckBox(tr("Use SOCKS5 UDP (requires server support)"), this);
    ui->proxyUdp->setObjectName(QStringLiteral("torrentProxyUdp"));
    ui->proxyUdp->setToolTip(tr("Uses UDP ASSOCIATE for DHT, UDP trackers and uTP peers. If the server rejects UDP, these connections do not fall back to your direct address."));
    ui->proxyForm->addRow(ui->proxyUdp);
    ui->gostNotice = new QLabel(tr("GOST requires authenticated TLS with certificate verification. TCP and UDP use the encrypted tunnel; there is no plain or direct fallback."), this);
    ui->gostNotice->setWordWrap(true);
    ui->gostNotice->setObjectName(QStringLiteral("torrentGostNotice"));
    ui->proxyForm->addRow(ui->gostNotice);
    ui->gostCaRow = new QWidget(this);
    auto *caLayout = new QHBoxLayout(ui->gostCaRow);
    caLayout->setContentsMargins(0, 0, 0, 0);
    ui->gostCaFile = new QLineEdit(ui->gostCaRow);
    ui->gostCaFile->setObjectName(QStringLiteral("torrentGostCaFile"));
    ui->gostCaFile->setPlaceholderText(tr("Empty: system trust roots"));
    ui->gostCaBrowse = new QPushButton(tr("Browse..."), ui->gostCaRow);
    ui->gostCaBrowse->setObjectName(QStringLiteral("torrentGostCaBrowse"));
    caLayout->addWidget(ui->gostCaFile, 1);
    caLayout->addWidget(ui->gostCaBrowse);
    ui->proxyForm->addRow(tr("CA certificate file:"), ui->gostCaRow);
    connect(ui->gostCaFile, &QLineEdit::textChanged, this, &SettingsTorrent::storeProxyControls);
    connect(ui->gostCaBrowse, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getOpenFileName(this, tr("Select CA certificate file"), ui->gostCaFile->text());
        if (!selected.isEmpty()) ui->gostCaFile->setText(selected);
    });
    auto *proxyNotice = new QLabel(tr("This profile applies to Torrent peers, DHT and trackers together. DC connections are unchanged. Hostnames are resolved by the proxy. Credentials are stored in your local user settings."), this);
    proxyNotice->setWordWrap(true);
    proxyPage->addWidget(proxyNotice);
    proxyPage->removeWidget(diagnostics->parentWidget());
    proxyPage->addWidget(diagnostics->parentWidget());
    connect(ui->proxyPort, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsTorrent::storeProxyControls);
    connect(ui->proxyCipher, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsTorrent::storeProxyControls);
    connect(ui->proxyTls, &QCheckBox::toggled, this, &SettingsTorrent::storeProxyControls);
    connect(ui->proxyUdp, &QCheckBox::toggled, this, &SettingsTorrent::storeProxyControls);
    connect(ui->proxyType, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        invalidateProxyTest();
        ui->values.customProxyType = static_cast<ProxyType>(ui->proxyType->currentData().toInt());
        loadProxyControls();
        updateEnabled();
    });
    loadProxyControls();
    auto *network = page(tr("Network"));
    auto *transport = group(network, tr("Transport and discovery"));
    ui->networkRouteStatus = new QLabel(this);
    ui->networkRouteStatus->setObjectName(QStringLiteral("networkRouteStatus"));
    ui->networkRouteStatus->setTextFormat(Qt::PlainText);
    ui->networkRouteStatus->setWordWrap(true);
    transport->addRow(ui->networkRouteStatus);
    ui->configureProxy = new QPushButton(tr("Configure proxy / UDP..."), this);
    ui->configureProxy->setObjectName(QStringLiteral("configureTorrentProxy"));
    transport->addRow(ui->configureProxy);
    connect(ui->configureProxy, &QPushButton::clicked, this, [this] {
        ui->tabs->setCurrentIndex(1);
        if (ui->proxyUdp->isEnabled()) ui->proxyUdp->setFocus();
        else ui->proxyMode->setFocus();
    });
    ui->randomizePort = check(transport, tr("Randomize listening port at application start"), "randomizePort", &eiskalt::torrent::Settings::randomizePort);
    ui->listenPort = integer(transport, tr("Listen port:"), "listenPort", &eiskalt::torrent::Settings::listenPort, 1, 65535);
    ui->effectiveListeners = new QLabel(tr("No confirmed listeners (Torrent networking may be idle)."), this);
    ui->effectiveListeners->setObjectName(QStringLiteral("effectiveListeners"));
    ui->effectiveListeners->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->effectiveListeners->setWordWrap(true);
    ui->effectiveListeners->setTextFormat(Qt::PlainText);
    ui->effectiveListeners->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui->effectiveListeners->setToolTip(tr("Successful bind confirmations for this session, not a live socket inventory. Addresses may be stale after an interface or IP change."));
    transport->addRow(tr("Last confirmed listeners:"), ui->effectiveListeners);
    ui->bindAddress = text(transport, tr("Bind IPv4 address:"), "bindAddress", &eiskalt::torrent::Settings::bindAddress);
    ui->bindAddress6 = text(transport, tr("Bind IPv6 address:"), "bindAddress6", &eiskalt::torrent::Settings::bindAddress6);
    check(transport, tr("TCP"), "tcp", &eiskalt::torrent::Settings::tcp);
    ui->utp = check(transport, tr("uTP (UDP)"), "utp", &eiskalt::torrent::Settings::utp);
    ui->portMapping = check(transport, tr("UPnP / NAT-PMP port mapping"), "portMapping", &eiskalt::torrent::Settings::portMapping);
    ui->dht = check(transport, tr("BitTorrent DHT (separate from DC DHT)"), "dht", &eiskalt::torrent::Settings::dht);
    check(transport, tr("Peer exchange (PEX)"), "pex", &eiskalt::torrent::Settings::pex);
    ui->localDiscovery = check(transport, tr("Local peer discovery"), "localDiscovery", &eiskalt::torrent::Settings::localDiscovery);
    ui->bootstrapNodes = text(transport, tr("BitTorrent bootstrap nodes:"), "bootstrapNodes", &eiskalt::torrent::Settings::bootstrapNodes);
    ui->bootstrapNodes->setPlaceholderText(tr("host:port, host:port (not the DC HTTP bootstrap URL)"));
    ui->encryptionMode = new QComboBox(this);
    ui->encryptionMode->setObjectName(QStringLiteral("encryptionMode"));
    ui->encryptionMode->addItem(tr("Disabled"), static_cast<int>(EncryptionMode::Disabled));
    ui->encryptionMode->addItem(tr("Optional"), static_cast<int>(EncryptionMode::Optional));
    ui->encryptionMode->addItem(tr("Required"), static_cast<int>(EncryptionMode::Required));
    ui->encryptionMode->setCurrentIndex(ui->encryptionMode->findData(static_cast<int>(ui->values.encryptionMode)));
    ui->encryptionMode->setToolTip(tr("BitTorrent peer encryption is not TLS and does not provide anonymity. Required encrypts peer payloads."));
    transport->addRow(tr("Peer encryption:"), ui->encryptionMode);
    connect(ui->encryptionMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        ui->values.encryptionMode = static_cast<EncryptionMode>(ui->encryptionMode->currentData().toInt());
    });

    auto *sharing = page(tr("DC sharing"));
    auto *publication = group(sharing, tr("One-way completed-file publication"));
    ui->shareCompleted = check(publication, tr("Share eligible completed Torrent files in DC"), "shareCompleted", &eiskalt::torrent::Settings::shareCompleted);
    ui->shareName = text(publication, tr("Virtual share name:"), "shareName", &eiskalt::torrent::Settings::shareName);
    auto *status = new QLabel(tr("Publish only verified, completed, selected files after DC hashing, never the entire Torrent directory. Private Torrents are excluded; per-job exceptions are unavailable. Partial and unselected files are not published. Disabling this option retracts Torrent-managed publication, not manual DC shares or copies already downloaded by peers."), this);
    status->setObjectName(QStringLiteral("sharingStatus"));
    status->setWordWrap(true);
    publication->addRow(status);

    connect(ui->enabled, &QCheckBox::toggled, this, [this](bool enabled) {
        invalidateProxyTest();
        ui->values.enabled = enabled;
        updateEnabled();
    });
    connect(ui->proxyMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        invalidateProxyTest();
        ui->values.proxyMode = static_cast<ProxyMode>(ui->proxyMode->currentData().toInt());
        updateEnabled();
    });
    updateEnabled();
}

SettingsTorrent::~SettingsTorrent() { ui->proxyTester->cancel(); }

void SettingsTorrent::invalidateProxyTest()
{
    ui->proxyTestStale = true;
    ui->proxyTester->cancel();
    if (!ui->proxyTestResult->text().isEmpty())
        ui->proxyTestResult->setText(tr("Proxy settings changed. Run the test again."));
}

void SettingsTorrent::testProxy()
{
    auto proxy = selectedProxy(ui->values, applicationProxy);
    if (!ui->values.enabled || ui->proxyTester->isRunning() || !validateProxy(proxy).isEmpty())
        return;
    QString trustError;
    if (proxy.type == ProxyType::Gost && ui->values.proxyMode == ProxyMode::Custom && !loadProxyTrust(proxy, &trustError)) {
        ui->proxyTestResult->setText(trustError);
        return;
    }
    dcpp::Socket::StreamProxyConfig snapshot;
    snapshot.type = proxy.type == ProxyType::Gost ? dcpp::Socket::StreamProxyConfig::Gost : proxy.type == ProxyType::Shadowsocks
        ? dcpp::Socket::StreamProxyConfig::Shadowsocks : dcpp::Socket::StreamProxyConfig::Socks5;
    snapshot.host = proxy.host.toStdString();
    snapshot.port = proxy.port;
    snapshot.user = proxy.user.toStdString();
    snapshot.password = proxy.password.toStdString();
    snapshot.cipher = proxy.cipher.toStdString();
    snapshot.tls = proxy.type == ProxyType::Socks5Tls;
    snapshot.remoteDns = proxy.remoteDns;
    snapshot.caPem = proxy.caPem.toStdString();
    snapshot.cancelled = [token = proxy.revoked] { return token && token->load(); };
    const auto targets = ui->targets();
    const auto error = ProxyTestRunner::validateTargets(targets, proxy.udp && (proxy.type == ProxyType::Socks5 || proxy.type == ProxyType::Gost));
    if (!error.isEmpty()) {
        ui->proxyTestResult->setText(error);
        return;
    }
    ui->proxyTestStale = false;
    if (ui->proxyTester->start(snapshot, targets, proxy.udp)) {
        ui->proxyTestResult->setText(tr("Testing proxy %1:%2... Transfers keep their current settings.")
                                       .arg(proxy.host).arg(proxy.port));
        updateEnabled();
    }
}

void SettingsTorrent::saveProbeTargets()
{
    const auto targets = ui->targets();
    if (!ProxyTestRunner::validateTargets(targets, true).isEmpty()) return;
    QSettings saved(path + QStringLiteral(".diagnostics.ini"), QSettings::IniFormat);
    saved.beginGroup(QStringLiteral("ProxyTest"));
    saved.setValue(QStringLiteral("tcpHost"), targets.tcpHost);
    saved.setValue(QStringLiteral("tcpPort"), targets.tcpPort);
    saved.setValue(QStringLiteral("tcpPortExplicit"), ui->testTcpPortExplicit);
    saved.setValue(QStringLiteral("dnsResolver"), targets.dnsResolver);
    saved.setValue(QStringLiteral("dnsPort"), targets.dnsPort);
    saved.setValue(QStringLiteral("dnsQuery"), targets.dnsQuery);
}

void SettingsTorrent::loadProxyControls()
{
    PasswordRevealAction::mask(ui->proxyPassword);
    ui->loadingProxy = true;
    const bool gost = ui->values.customProxyType == ProxyType::Gost;
    const auto &p = gost ? ui->values.gostProxy : ui->values.customProxyType == ProxyType::Shadowsocks ?
        ui->values.shadowsocksProxy : ui->values.socks5Proxy;
    ui->proxyHost->setText(p.host);
    ui->proxyPort->setValue(p.port);
    ui->proxyUser->setText(p.user);
    ui->proxyPassword->setText(p.password);
    ui->proxyCipher->setCurrentText(p.cipher.isEmpty() ? QStringLiteral("aes-256-gcm") : p.cipher);
    ui->proxyTls->setChecked(ui->values.socks5Proxy.type == ProxyType::Socks5Tls);
    ui->proxyUdp->setChecked(gost ? p.udp : ui->values.socks5Proxy.udp);
    ui->gostCaFile->setText(ui->values.gostProxy.caFile);
    ui->loadingProxy = false;
}

void SettingsTorrent::storeProxyControls()
{
    if (ui->loadingProxy) return;
    invalidateProxyTest();
    const bool ss = ui->values.customProxyType == ProxyType::Shadowsocks;
    const bool gost = ui->values.customProxyType == ProxyType::Gost;
    auto &p = gost ? ui->values.gostProxy : ss ? ui->values.shadowsocksProxy : ui->values.socks5Proxy;
    p.type = gost ? ProxyType::Gost : ss ? ProxyType::Shadowsocks : ui->proxyTls->isChecked() ? ProxyType::Socks5Tls : ProxyType::Socks5;
    p.host = ui->proxyHost->text().trimmed();
    p.port = ui->proxyPort->value();
    p.user = ss ? QString{} : ui->proxyUser->text();
    p.password = ui->proxyPassword->text();
    p.cipher = ss ? ui->proxyCipher->currentText() : QString{};
    p.udp = !ss && ui->proxyUdp->isChecked();
    if (gost) p.caFile = ui->gostCaFile->text();
    p.remoteDns = true;
    updateEnabled();
}

eiskalt::torrent::Settings SettingsTorrent::settings() const { return ui->values; }

bool SettingsTorrent::save(QString *error)
{
    const QString validation = validateSettings(ui->values);
    if (!validation.isEmpty()) {
        if (error)
            *error = validation;
        return false;
    }
    if (!saveSettings(path, ui->values, error))
        return false;
    saveProbeTargets();
    if (error)
        error->clear();
    emit saved();
    return true;
}

void SettingsTorrent::setApplicationProxy(const ProxyConfig &proxy)
{
    invalidateProxyTest();
    applicationProxy = proxy;
    updateEnabled();
}

void SettingsTorrent::setEngine(TorrentEngine *value)
{
    if (engine == value)
        return;
    if (engine)
        disconnect(engine, nullptr, this, nullptr);
    engine = value;
    if (engine) {
        connect(engine, &TorrentEngine::listenersChanged, this, &SettingsTorrent::updateListeners);
        connect(engine, &QObject::destroyed, this, [this] {
            engine = nullptr;
            updateListeners();
        });
    }
    updateListeners();
}

void SettingsTorrent::updateListeners()
{
    const auto listeners = engine ? engine->effectiveListeners() : QStringList{};
    ui->effectiveListeners->setText(listeners.isEmpty() ?
        tr("No confirmed listeners (Torrent networking may be idle).") : listeners.join('\n'));
}

void SettingsTorrent::updateEnabled()
{
    ui->tabs->setEnabled(ui->values.enabled);
    const bool custom = ui->values.proxyMode == ProxyMode::Custom;
    const auto proxy = selectedProxy(ui->values, applicationProxy);
    const bool testing = ui->proxyTester->isRunning();
    const bool httpProbe = proxy.type == ProxyType::Shadowsocks;
    if (!ui->testTcpPortExplicit) {
        const QSignalBlocker blocker(ui->testTcpPort);
        ui->testTcpPort->setValue(httpProbe ? 80 : 443);
    }
    ui->testProtocol->setText(proxy.type == ProxyType::Gost
        ? tr("GOST tests verified proxy TLS and authenticated CONNECT, not destination TLS or application traffic. UDP requires a matching DNS reply through the encrypted tunnel. No direct fallback.") : httpProbe
        ? tr("Shadowsocks tests plain HTTP HEAD at the TCP host and port, not an arbitrary TLS service. UDP testing is unsupported. No direct fallback.")
        : tr("TCP tests SOCKS CONNECT, not a download or TLS handshake. UDP sends a separate DNS A query through the proxy relay; no direct fallback."));
    const bool udpProbe = (proxy.type == ProxyType::Socks5 || proxy.type == ProxyType::Gost) && proxy.udp;
    for (auto *field : QList<QWidget *>{ui->testDnsResolver, ui->testDnsPort, ui->testDnsQuery})
        field->setEnabled(udpProbe);
    ui->testProxy->setEnabled(ui->values.enabled && !testing && validateProxy(proxy).isEmpty());
    ui->cancelProxyTest->setEnabled(testing);
    ui->cancelProxyTest->setVisible(testing);
    const bool direct = ui->values.proxyMode == ProxyMode::Direct ||
        (ui->values.proxyMode == ProxyMode::FollowApplication && applicationProxy.type == ProxyType::Direct);
    const bool encrypted = proxy.type == ProxyType::Socks5Tls || proxy.type == ProxyType::Shadowsocks;
    const auto proxyError = validateProxy(proxy);
    const bool udp = direct || (proxyError.isEmpty() && udpProbe);
    const bool ss = ui->values.customProxyType == ProxyType::Shadowsocks;
    const bool gost = ui->values.customProxyType == ProxyType::Gost;
    ui->customProxyBox->setEnabled(custom);
    auto enableField = [this](QWidget *field, bool enabled) {
        field->setEnabled(enabled);
        if (auto *label = ui->proxyForm->labelForField(field)) label->setEnabled(enabled);
    };
    enableField(ui->proxyUser, !ss);
    enableField(ui->proxyCipher, ss);
    ui->proxyTls->setEnabled(!ss && !gost);
    ui->proxyTls->setVisible(!gost);
    ui->proxyUdp->setEnabled(gost || (!ss && !ui->proxyTls->isChecked()));
    ui->proxyUdp->setText(gost ? tr("Use GOST encrypted UDP") : tr("Use SOCKS5 UDP (requires server support)"));
    ui->proxyUdp->setToolTip(gost ? ui->gostNotice->text() : tr("Uses UDP ASSOCIATE for DHT, UDP trackers and uTP peers. If the server rejects UDP, these connections do not fall back to your direct address."));
    ui->proxyForm->setRowVisible(ui->gostCaRow, gost);
    ui->gostCaRow->setEnabled(gost);
    ui->gostNotice->setVisible(gost);
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    ui->customProxyBox->setStyleSheet(QStringLiteral(
        "QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; }"
        "QLabel:disabled { color: %2; }").arg(dark ? "#363636" : "#cfcfcf", dark ? "#969696" : "#555555", dark ? "#555555" : "#aaaaaa"));
    ui->randomizePort->setEnabled(direct);
    ui->listenPort->setEnabled(direct && !ui->values.randomizePort);
    ui->bindAddress->setEnabled(direct);
    ui->bindAddress6->setEnabled(direct);
    ui->portMapping->setEnabled(direct);
    ui->localDiscovery->setEnabled(direct);
    ui->utp->setEnabled(udp);
    ui->dht->setEnabled(udp);
    ui->bootstrapNodes->setEnabled(udp && ui->values.dht);
    QString discovery;
    if (direct)
        discovery = tr("DHT and bootstrap nodes use the direct connection. Private Torrents never use DHT.");
    else if (!proxyError.isEmpty())
        discovery = tr("Torrent networking is blocked: %1. Correct the proxy settings; saved discovery preferences are retained. No direct fallback is allowed.").arg(proxyError);
    else if (udp && proxy.type == ProxyType::Gost)
        discovery = tr("GOST carries DHT, UDP trackers and uTP through authenticated TLS. Use Test proxy to verify UDP. Private Torrents never use DHT. No direct fallback.");
    else if (udp)
        discovery = tr("DHT, bootstrap hostname lookups and UDP trackers use the SOCKS5 UDP relay. The server must support UDP ASSOCIATE; use Test proxy to verify UDP. Private Torrents never use DHT. No direct fallback is allowed.");
    else if (encrypted || proxy.type == ProxyType::Gost)
        discovery = tr("This proxy route is TCP-only. DHT, bootstrap nodes, uTP and UDP trackers are unavailable; peer uploads and downloads can still use TCP through the proxy. Saved discovery preferences are retained.");
    else
        discovery = tr("SOCKS5 UDP is disabled or no suitable proxy is configured. Enable Use SOCKS5 UDP in the Proxy tab and run Test proxy to verify the server. DHT and bootstrap nodes then become available. No direct fallback is allowed.");
    ui->networkRouteStatus->setText(discovery);
    ui->dht->setToolTip(discovery);
    ui->bootstrapNodes->setToolTip(discovery);
    ui->configureProxy->setVisible(!direct);
    ui->configureProxy->setEnabled(!direct);
    ui->shareName->setEnabled(ui->values.shareCompleted);
    QString warning;
    if (ui->values.proxyMode == ProxyMode::Direct)
        warning = tr("Warning: Torrent peers and trackers can see your direct address, even when DC uses a proxy.");
    else if (direct)
        warning = tr("The application currently uses direct networking.");
    else if (custom && !validateProxy(proxy).isEmpty())
        warning = validateProxy(proxy);
    else if (proxy.type == ProxyType::Direct || proxy.host.trimmed().isEmpty() || proxy.port <= 0)
        warning = tr("Torrent activity is blocked: an application proxy is required but not configured. There is no direct fallback.");
    else if (proxy.type == ProxyType::Gost)
        warning = ui->gostNotice->text();
    else if (encrypted)
        warning = tr("The selected encrypted proxy uses a TCP-only adapter. Adapter failure blocks Torrent activity; there is no direct fallback.");
    else if (custom)
        warning = tr("Torrent peers, DHT and trackers use this Torrent-only proxy. DC connections keep their application settings.");
    else
        warning = tr("All Torrent traffic follows the application proxy, regardless of DC's peer-to-peer proxy checkbox.");
    if (!direct)
        warning += tr(" Incoming listeners, port mapping and local discovery are disabled by the proxy policy.");
    if (!udp)
        warning += tr(" DHT, uTP and UDP trackers are unavailable on this route. Saved preferences are retained.");
    else if (!direct && proxy.type != ProxyType::Gost)
        warning += tr(" UDP requires SOCKS5 UDP ASSOCIATE support on the server; rejection never enables direct networking.");
    ui->proxyWarning->setText(warning);
}
#endif
