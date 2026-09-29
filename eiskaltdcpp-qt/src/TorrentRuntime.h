#pragma once

#ifdef USE_TORRENT
#include <QObject>
#include <QHash>
#include <QDeadlineTimer>
#include <QPointer>
#include <QStringList>
#include <memory>
#include "torrent/TorrentTypes.h"
#include "TorrentSharing.h"

namespace dcpp { class DCContext; }
namespace eiskalt::torrent { class TorrentEngine; }
class TorrentProxyAdapter;
class TorrentSharePublisher;
struct TorrentCountrySource;

// MainWindow owns this object; the static pointer is non-owning and never creates it.
class TorrentRuntime : public QObject
{
    Q_OBJECT
public:
    TorrentRuntime(dcpp::DCContext &context, QObject *parent);
    ~TorrentRuntime() override;
    static TorrentRuntime *instance();
    static QString settingsPath();
    static eiskalt::torrent::ProxyConfig snapshotProxy(dcpp::DCContext &context);
    eiskalt::torrent::TorrentEngine *engine() const;
    QString lastError() const;
    QString shareStatus(const QString &id) const;
    QList<torrent_sharing::FileMagnet> dcMagnets(const QStringList &ids) const;

public slots:
    void reloadSettings();
    void applySecurity(eiskalt::torrent::EncryptionMode encryption, const QStringList &countries, bool blockUnknownClients);
    void shutdown();
    void invalidateShare(const QString &id);

signals:
    void error(const QString &message);
    void diagnostic(const QString &message);
    void settingsReloaded();
    void shareStatusChanged(const QString &id, const QString &status);

private:
    static QPointer<TorrentRuntime> current;
    dcpp::DCContext &context;
    std::shared_ptr<TorrentCountrySource> countrySource;
    eiskalt::torrent::TorrentEngine *torrentEngine;
    TorrentProxyAdapter *proxyAdapter;
    TorrentSharePublisher *sharePublisher;
    eiskalt::torrent::Settings activeSettings;
    eiskalt::torrent::ProxyConfig upstreamProxy;
    QString latestError;
    QHash<QString, QString> publishedManifests, sharingStatuses;
    QHash<QString, quint64> invalidatedRevisions;
    QHash<QString, QDeadlineTimer> retryAfter;
    QHash<QString, int> publicationFailures;
    quint64 requestedConfiguration = 0;
    bool publicationArmed = false;
    bool stopped = false;
    void refreshPublication();
    void setShareStatus(const QString &id, const QString &status);
};
#endif
