#include "TorrentRuntime.h"

#ifdef USE_TORRENT
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentSettings.h"
#include "torrent/ProxyTrust.h"
#include "TorrentProxyAdapter.h"
#include "TorrentSharePublisher.h"
#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/ProxyRoute.h"
#include "dcpp/ShareManager.h"
#include "dcpp/Util.h"
#include <QDir>
#include <QCryptographicHash>
#include <QSet>
#include <QTimer>
#include <mutex>

using namespace eiskalt::torrent;

struct TorrentCountrySource {
    std::mutex mutex;
    std::string path;
    QString lookup(const QString &ip) {
        std::string snapshot;
        { std::lock_guard lock(mutex); snapshot = path; }
        return QString::fromStdString(dcpp::Util::getIpCountry(ip.toStdString(), snapshot));
    }
};

QPointer<TorrentRuntime> TorrentRuntime::current;

TorrentRuntime::TorrentRuntime(dcpp::DCContext &context, QObject *parent)
    : QObject(parent), context(context),
      countrySource(std::make_shared<TorrentCountrySource>()),
      torrentEngine(new TorrentEngine(QDir(QString::fromStdString(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG))).filePath(QStringLiteral("torrent/state")), this,
          [source = countrySource](const QString &ip) { return source->lookup(ip); })),
      proxyAdapter(new TorrentProxyAdapter(this)),
      sharePublisher(new TorrentSharePublisher(
          [&context](const QString &id, const QString &root, const QStringList &files,
                     const std::function<bool()> &cancelled, const std::function<void()> &hashingStarted) {
              dcpp::StringList paths;
              for (const auto &file : files)
                  paths.push_back(file.toStdString());
              return context.getShareManager()->replaceManagedFiles(
                  "torrent:" + id.toStdString(), root.toStdString(), paths, cancelled, hashingStarted);
          }, [&context](const QString &id) {
              context.getShareManager()->removeManagedFiles("torrent:" + id.toStdString());
          }, this))
{
    Q_ASSERT(parent);
    Q_ASSERT(!current);
    current = this;
    connect(torrentEngine, &TorrentEngine::error, this, [this](const QString &message) {
        latestError = message;
        emit error(message);
    });
    connect(torrentEngine, &TorrentEngine::diagnostic, this, &TorrentRuntime::diagnostic);
    connect(sharePublisher, &TorrentSharePublisher::statusChanged,
            this, &TorrentRuntime::setShareStatus);
    connect(sharePublisher, &TorrentSharePublisher::finished, this, [this](const QString &id, bool published) {
        if (published) {
            publicationFailures.remove(id);
            retryAfter.remove(id);
            return;
        }
        publishedManifests.remove(id);
        const int failures = qMin(publicationFailures.value(id) + 1, 6);
        publicationFailures.insert(id, failures);
        const int delay = 30000 * (1 << (failures - 1));
        retryAfter.insert(id, QDeadlineTimer(delay));
        setShareStatus(id, tr("Not shared: validation or hashing failed; retry in %1 seconds").arg(delay / 1000));
        QTimer::singleShot(delay, this, &TorrentRuntime::refreshPublication);
    });
    connect(torrentEngine, &TorrentEngine::storageInvalidating,
            this, &TorrentRuntime::invalidateShare, Qt::DirectConnection);
    connect(torrentEngine, &TorrentEngine::dcShareExclusionChanged,
            this, &TorrentRuntime::refreshPublication, Qt::DirectConnection);
    connect(torrentEngine, &TorrentEngine::hashRecheckRequested, this, [&context](const QString &id) {
        context.getShareManager()->forgetManagedFileHashes("torrent:" + id.toStdString());
    }, Qt::DirectConnection);
    connect(torrentEngine, &TorrentEngine::configurationApplied, this, [this](quint64 epoch) {
        if (stopped || epoch != requestedConfiguration)
            return;
        publicationArmed = true;
        invalidatedRevisions.clear();
        refreshPublication();
    }, Qt::QueuedConnection);
    connect(torrentEngine, &TorrentEngine::changed, this,
            &TorrentRuntime::refreshPublication, Qt::QueuedConnection);
}

TorrentRuntime::~TorrentRuntime()
{
    shutdown();
    if (current == this)
        current.clear();
}

TorrentRuntime *TorrentRuntime::instance() { return current.data(); }
TorrentEngine *TorrentRuntime::engine() const { return torrentEngine; }
QString TorrentRuntime::lastError() const { return latestError; }
QList<torrent_sharing::FileMagnet> TorrentRuntime::dcMagnets(const QStringList &ids) const
{
    QList<torrent_sharing::FileMagnet> result;
    if (stopped || !publicationArmed || !activeSettings.shareCompleted) return result;
    for (const auto &job : torrentEngine->jobs()) {
        if (!ids.contains(job.id) || !publishedManifests.contains(job.id) ||
            invalidatedRevisions.contains(job.id)) continue;
        const auto hashes = context.getShareManager()->getManagedFileHashes("torrent:" + job.id.toStdString());
        result.append(torrent_sharing::dcFiles(job, [&](const FileEntry &file) {
            const auto path = QDir(job.savePath).filePath(file.path).toStdString();
            const auto hash = hashes.find(path);
            if (hash == hashes.end() || hash->second.first != file.size) return QString();
            return QString::fromStdString(hash->second.second.toBase32());
        }));
    }
    return result;
}
QString TorrentRuntime::shareStatus(const QString &id) const
{
    return sharingStatuses.value(id, tr("Not shared"));
}

void TorrentRuntime::setShareStatus(const QString &id, const QString &status)
{
    if (sharingStatuses.value(id) == status)
        return;
    sharingStatuses.insert(id, status);
    emit shareStatusChanged(id, status);
}

void TorrentRuntime::refreshPublication()
{
    if (stopped || !publicationArmed)
        return;
    QSet<QString> present;
    for (const auto &job : torrentEngine->jobs()) {
        present.insert(job.id);
        if (invalidatedRevisions.contains(job.id) && job.revision <= invalidatedRevisions.value(job.id))
            continue;
        invalidatedRevisions.remove(job.id);
        QString reason;
        if (job.dcShareExcluded)
            reason = tr("Not shared: excluded for this Torrent");
        else if (!activeSettings.enabled || !activeSettings.shareCompleted)
            reason = tr("DC sharing is disabled");
        else if (job.privateTorrent)
            reason = tr("Not shared: private Torrent");
        else if (!job.complete || !job.error.isEmpty())
            reason = tr("Not shared: waiting for verified completion");
        if (!reason.isEmpty()) {
            if (publishedManifests.remove(job.id))
                sharePublisher->invalidate(job.id);
            setShareStatus(job.id, reason);
            continue;
        }
        if (retryAfter.contains(job.id) && !retryAfter.value(job.id).hasExpired())
            continue;
        QStringList paths;
        QByteArray manifest = job.savePath.toUtf8() + '\0' + QByteArray::number(job.revision);
        for (const auto &file : job.files) {
            if (!file.wanted)
                continue;
            paths.append(QDir(job.savePath).filePath(file.path));
            manifest += '\0' + file.path.toUtf8() + '\0' + QByteArray::number(file.size);
        }
        const auto signature = QString::fromLatin1(QCryptographicHash::hash(manifest, QCryptographicHash::Sha256).toHex());
        if (paths.isEmpty() || publishedManifests.value(job.id) == signature)
            continue;
        publishedManifests.insert(job.id, signature);
        // Exact-file managed roots never turn the download directory into a share.
        const auto root = activeSettings.shareName + QStringLiteral(" - ") + job.id;
        sharePublisher->enqueue(job.id, root, paths,
            [engine = torrentEngine, id = job.id, revision = job.revision, epoch = requestedConfiguration] {
                return !engine->publicationValid(id, revision, epoch);
            });
    }
    for (const auto &id : publishedManifests.keys()) {
        if (!present.contains(id)) {
            sharePublisher->invalidate(id);
            publishedManifests.remove(id);
        }
    }
}

QString TorrentRuntime::settingsPath()
{
    return QDir(QString::fromStdString(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG)))
        .filePath(QStringLiteral("torrent/settings.json"));
}

ProxyConfig TorrentRuntime::snapshotProxy(dcpp::DCContext &context)
{
    const auto *settings = context.getSettingsManager();
    ProxyConfig proxy;
    const int mode = settings->get(dcpp::SettingsManager::OUTGOING_CONNECTIONS, true);
    const auto route = context.getProxyRoute()->snapshot();
    if (mode == dcpp::SettingsManager::OUTGOING_GOST || route->mode == dcpp::SettingsManager::OUTGOING_GOST) {
        proxy.type = ProxyType::Gost;
        proxy.revoked = route->revoked;
        if (!route->valid || route->revoked->load() || route->mode != dcpp::SettingsManager::OUTGOING_GOST)
            return proxy; // Invalid inherited route is not direct networking.
        proxy.host = QString::fromStdString(route->proxy.host);
        proxy.port = route->proxy.port;
        proxy.user = QString::fromStdString(route->proxy.user);
        proxy.password = QString::fromStdString(route->proxy.password);
        proxy.remoteDns = true;
        proxy.udp = route->udp;
        proxy.caFile = QString::fromStdString(route->caFile);
        proxy.caPem = QByteArray::fromStdString(route->proxy.caPem);
        return proxy;
    }
    if (mode == dcpp::SettingsManager::OUTGOING_SOCKS5) {
        const bool tls = settings->getBool(dcpp::SettingsManager::SOCKS_TLS, true);
        proxy.type = tls ? ProxyType::Socks5Tls : ProxyType::Socks5;
        proxy.host = QString::fromStdString(settings->get(dcpp::SettingsManager::SOCKS_SERVER, true));
        proxy.port = settings->get(dcpp::SettingsManager::SOCKS_PORT, true);
        proxy.user = QString::fromStdString(settings->get(dcpp::SettingsManager::SOCKS_USER, true));
        proxy.password = QString::fromStdString(settings->get(dcpp::SettingsManager::SOCKS_PASSWORD, true));
        proxy.remoteDns = settings->getBool(dcpp::SettingsManager::SOCKS_RESOLVE, true);
        proxy.udp = !tls;
    } else if (mode == dcpp::SettingsManager::OUTGOING_SHADOWSOCKS) {
        proxy.type = ProxyType::Shadowsocks;
        proxy.host = QString::fromStdString(settings->get(dcpp::SettingsManager::SHADOWSOCKS_SERVER, true));
        proxy.port = settings->get(dcpp::SettingsManager::SHADOWSOCKS_PORT, true);
        proxy.password = QString::fromStdString(settings->get(dcpp::SettingsManager::SHADOWSOCKS_PASSWORD, true));
        proxy.cipher = QString::fromStdString(settings->get(dcpp::SettingsManager::SHADOWSOCKS_METHOD, true));
        proxy.remoteDns = settings->getBool(dcpp::SettingsManager::SOCKS_RESOLVE, true);
        proxy.udp = settings->get(dcpp::SettingsManager::SHADOWSOCKS_TRANSPORT, true) ==
            dcpp::SettingsManager::SHADOWSOCKS_TRANSPORT_TCP_AND_UDP;
    } else if (mode != dcpp::SettingsManager::OUTGOING_DIRECT) {
        // An unknown app routing mode must not silently become direct networking.
        proxy.type = ProxyType::Socks5;
    }
    return proxy;
}

void TorrentRuntime::reloadSettings()
{
    if (stopped)
        return;
    bool countryChanged;
    {
        std::lock_guard lock(countrySource->mutex);
        const auto path = context.getSettingsManager()->get(dcpp::SettingsManager::COUNTRY_DB_PATH);
        countryChanged = countrySource->path != path;
        countrySource->path = path;
    }
    const auto requestedSettings = loadSettings(settingsPath());
    auto proxy = selectedProxy(requestedSettings, snapshotProxy(context));
    QString trustError;
    bool trustValid = true;
    if (proxy.type == ProxyType::Gost) {
        if (requestedSettings.proxyMode == ProxyMode::Custom) {
            trustValid = loadProxyTrust(proxy, &trustError);
        } else {
            trustValid = proxy.revoked && !proxy.revoked->load() && validateProxy(proxy).isEmpty();
            if (!trustValid) trustError = tr("The global GOST route is invalid or revoked. Correct the global proxy settings; there is no direct fallback.");
        }
    }
    // Accepting unrelated Preferences must not tear down a healthy Torrent session.
    // Compare the upstream route, not the encrypted adapter's ephemeral local port.
    if (trustValid && requestedConfiguration && latestError.isEmpty() && !countryChanged &&
        requestedSettings == activeSettings && proxy == upstreamProxy) {
        emit settingsReloaded();
        return;
    }
    activeSettings = requestedSettings;
    upstreamProxy = proxy;
    latestError.clear();
    publicationArmed = false;
    sharePublisher->invalidate();
    publishedManifests.clear();
    retryAfter.clear();
    publicationFailures.clear();
    proxyAdapter->stop();
    if (!trustValid) {
        latestError = trustError;
        emit error(trustError);
        // Keep the unadapted GOST type: configure rejects it and closes the old route.
    } else if (activeSettings.enabled && activeSettings.proxyMode != ProxyMode::Direct &&
        (proxy.type == ProxyType::Socks5Tls || proxy.type == ProxyType::Shadowsocks || proxy.type == ProxyType::Gost)) {
        QString adapterError;
        if (proxyAdapter->start(proxy, &adapterError)) {
            proxy = proxyAdapter->endpoint();
        } else {
            latestError = adapterError;
            emit error(adapterError);
            // Leave the encrypted type intact: the engine rejects it, never direct.
        }
    }
    requestedConfiguration = torrentEngine->configure(activeSettings, proxy);
    emit settingsReloaded();
}

void TorrentRuntime::applySecurity(EncryptionMode encryption, const QStringList &countries, bool blockUnknownClients)
{
    if (stopped) return;
    auto settings = activeSettings;
    settings.encryptionMode = encryption;
    settings.blockedCountries = countries;
    settings.blockUnknownClients = blockUnknownClients;
    QString failure;
    if (!saveSettings(settingsPath(), settings, &failure)) {
        latestError = failure;
        emit error(failure);
        return;
    }
    // Recreate the session, closing existing peers before the new policy runs.
    reloadSettings();
}

void TorrentRuntime::invalidateShare(const QString &id)
{
    for (const auto &job : torrentEngine->jobs()) {
        if (id.isEmpty() || id == job.id)
            invalidatedRevisions.insert(job.id, job.revision);
    }
    sharePublisher->invalidate(id);
    if (id.isEmpty())
        publishedManifests.clear();
    else
        publishedManifests.remove(id);
    if (id.isEmpty()) {
        retryAfter.clear();
        publicationFailures.clear();
    } else {
        retryAfter.remove(id);
        publicationFailures.remove(id);
    }
}

void TorrentRuntime::shutdown()
{
    if (stopped)
        return;
    stopped = true;
    sharePublisher->stop();
    torrentEngine->shutdown();
    proxyAdapter->stop();
}
#endif
