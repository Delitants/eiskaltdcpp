#include "dcpp/stdinc.h"
#include "dcpp/CryptoManager.h"
#include "CertificateSettings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSslCertificate>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {
QByteArray readPem(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {};
    return file.readAll();
}
struct GeneratedIdentity { std::string certificate, key; bool ok = false; };
bool writePrivate(const QString& path, const std::string& data)
{
    QFile file(path);
    // Files are new, inside a private QTemporaryDir; never truncate an old pair.
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        && file.write(data.data(), qint64(data.size())) == qint64(data.size()) && file.flush();
}
}

CertificateSettings::CertificateSettings(dcpp::SettingsManager& sm, const QString& identity,
                                         const QString& root, QWidget* parent)
    : QWidget(parent), settings(sm), cid(identity), directory(root)
{
    using S = dcpp::SettingsManager;
    originalCertificate = QString::fromStdString(sm.get(S::TLS_CERTIFICATE_FILE));
    originalKey = QString::fromStdString(sm.get(S::TLS_PRIVATE_KEY_FILE));
    originalTrust = QString::fromStdString(sm.get(S::TLS_TRUSTED_CERTIFICATES_PATH));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    editors = new QWidget(this);
    auto* form = new QFormLayout(editors);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto field = [&](const QString& label, const char* name, const QString& value, bool folder) {
        auto* row = new QWidget(editors);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(value, row);
        edit->setObjectName(QString::fromLatin1(name));
        auto* browse = new QPushButton(tr("Browse..."), row);
        line->addWidget(edit, 1); line->addWidget(browse);
        form->addRow(label, row);
        connect(browse, &QPushButton::clicked, this, [this, edit, folder, label] {
            const auto path = folder ? QFileDialog::getExistingDirectory(this, label, edit->text())
                : QFileDialog::getOpenFileName(this, label, edit->text());
            if (!path.isEmpty()) edit->setText(path);
        });
        return edit;
    };
    certificate = field(tr("Identity certificate:"), "dcTlsCertificate", originalCertificate, false);
    key = field(tr("Private key:"), "dcTlsPrivateKey", originalKey, false);
    trust = field(tr("Trusted certificates folder:"), "dcTlsTrust", originalTrust, true);
    layout->addWidget(editors);
    details = new QLabel(this);
    details->setObjectName(QStringLiteral("dcTlsDetails"));
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(details);
    generateButton = new QPushButton(tr("Generate / Renew..."), this);
    generateButton->setObjectName(QStringLiteral("dcTlsGenerate"));
    layout->addWidget(generateButton, 0, Qt::AlignLeft);
    message = new QLabel(tr("Fresh installs generate a DC++ identity automatically. Changes here apply after Save and restart. GOST server trust is configured separately."), this);
    message->setObjectName(QStringLiteral("dcTlsMessage"));
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(message);
    connect(certificate, &QLineEdit::textChanged, this, &CertificateSettings::updateDetails);
    connect(generateButton, &QPushButton::clicked, this, &CertificateSettings::generate);
    updateDetails();
}

CertificateSettings::~CertificateSettings() = default;

void CertificateSettings::updateDetails()
{
    const QSslCertificate cert(readPem(certificate->text()));
    if (cert.isNull()) {
        details->setText(tr("No readable certificate. A missing default identity is generated at startup."));
        return;
    }
    details->setText(tr("Identity: %1\nValid from: %2\nExpires: %3\nSHA-256: %4")
        .arg(cert.subjectInfo(QSslCertificate::CommonName).join(QStringLiteral(", ")),
             cert.effectiveDate().toLocalTime().toString(Qt::ISODate),
             cert.expiryDate().toLocalTime().toString(Qt::ISODate),
             QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex(' '))));
}

QString CertificateSettings::validationError() const
{
    if (busy) return tr("Wait for certificate generation to finish.");
    if (certificate->text() != originalCertificate || key->text() != originalKey) {
        if (!dcpp::CryptoManager::identityPemMatches(readPem(certificate->text()).toStdString(),
                readPem(key->text()).toStdString(), cid.toStdString()))
            return tr("Select a valid matching certificate and unencrypted private key for this DC++ identity (at least 90 days remaining).");
    }
    if (trust->text() != originalTrust && !QFileInfo(trust->text()).isDir())
        return tr("Select an existing trusted certificates folder.");
    return {};
}

void CertificateSettings::save()
{
    if (!validationError().isEmpty()) return;
    using S = dcpp::SettingsManager;
    const auto absolute = [](const QString& path) {
        return path.isEmpty() ? path : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    };
    settings.set(S::TLS_CERTIFICATE_FILE, absolute(certificate->text()).toStdString());
    settings.set(S::TLS_PRIVATE_KEY_FILE, absolute(key->text()).toStdString());
    auto trustPath = absolute(trust->text());
    if (!trustPath.isEmpty() && !trustPath.endsWith(QLatin1Char('/'))) trustPath += QLatin1Char('/');
    settings.set(S::TLS_TRUSTED_CERTIFICATES_PATH, trustPath.toStdString());
    for (auto& folder : generated) {
        const auto root = QFileInfo(folder->path()).canonicalFilePath() + QLatin1Char('/');
        if (QFileInfo(certificate->text()).canonicalFilePath().startsWith(root)
            || QFileInfo(key->text()).canonicalFilePath().startsWith(root))
            folder->setAutoRemove(false);
    }
}

void CertificateSettings::generate()
{
    if (busy) return;
    if (QMessageBox::question(this, tr("Generate DC++ identity"),
            tr("Generate a new certificate and private key? This changes your keyprint. Existing files are kept for rollback. The new identity is used only after Save and restart."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    busy = true;
    editors->setEnabled(false);
    generateButton->setEnabled(false);
    message->setText(tr("Generating certificate..."));
    auto* watcher = new QFutureWatcher<GeneratedIdentity>(this);
    connect(watcher, &QFutureWatcher<GeneratedIdentity>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy = false;
        editors->setEnabled(true);
        generateButton->setEnabled(true);
        if (result.ok && QDir().mkpath(directory)) {
            auto folder = std::make_unique<QTemporaryDir>(directory + QStringLiteral("/identity-XXXXXX"));
            if (folder->isValid()
                && writePrivate(folder->filePath(QStringLiteral("client.crt")), result.certificate)
                && writePrivate(folder->filePath(QStringLiteral("client.key")), result.key)) {
                certificate->setText(folder->filePath(QStringLiteral("client.crt")));
                key->setText(folder->filePath(QStringLiteral("client.key")));
                generated.push_back(std::move(folder));
                message->setText(tr("New identity ready. Save and restart to use it. Previous certificate and key files are unchanged."));
                return;
            }
        }
        message->setText(tr("Certificate generation failed. Existing files and settings were not changed."));
    });
    const auto identity = cid.toStdString();
    watcher->setFuture(QtConcurrent::run([identity] {
        GeneratedIdentity result;
        try {
            auto pem = dcpp::CryptoManager::createIdentityPem(identity);
            result.certificate = std::move(pem.first);
            result.key = std::move(pem.second);
            result.ok = dcpp::CryptoManager::identityPemMatches(result.certificate, result.key, identity);
        } catch (...) { }
        return result;
    }));
}
