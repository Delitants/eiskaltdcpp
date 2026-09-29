#pragma once

#include "TorrentTypes.h"
#include <QObject>
#include <QMap>
#include <memory>
#include <functional>

namespace eiskalt::torrent {

class TorrentEngine : public QObject {
    Q_OBJECT
public:
    using CountryLookup = std::function<QString(const QString &)>;
    // Resolver must be local-only, thread-safe and outlive engine shutdown.
    explicit TorrentEngine(QString stateDirectory, QObject *parent = nullptr, CountryLookup countryLookup = {});
    ~TorrentEngine() override;

    // All mutating APIs, including shutdown, must run on this QObject's owner thread.
    // Snapshot getters and publicationValid() are thread-safe while the engine lives.
    // The caller selects the profile and prepares any encrypted TCP adapter first.
    quint64 configure(const Settings &, const ProxyConfig &);
    quint64 configurationGeneration() const;
    Settings settings() const;
    // Last confirmed local TCP/UDP binds, e.g. "TCP [::1]:54321". Not live inventory:
    // libtorrent exposes no complete close notification, so interface changes can
    // leave stale entries until configure/session teardown. Empty when idle.
    QStringList effectiveListeners() const;
    // Acceptance is synchronous; metadata parsing and failures are asynchronous.
    QString add(const QString &fileOrMagnet, const QString &savePath = {},
                const QList<int> &wantedFiles = {}, bool startPaused = false);
    // Hash-verified, upload-only seeding of originals; never moves or deletes source files.
    // File selection is fixed to prevent libtorrent from reopening originals writable.
    QString seedCreated(const QString &metadata, const QString &sourceParent);
    QList<Job> jobs() const;
    // Observe only one job. Empty or unknown ids stop observation. This never
    // starts a session or connects a peer; collection runs on the worker.
    void selectObservedJob(const QString &id);
    QString observedJob() const;
    // Cache-only; empty for unobserved jobs and immediately after invalidation.
    QList<Peer> peers(const QString &id) const;
    QList<FileEntry> files(const QString &id) const;
    QList<FileEntry> files(const QString &id, quint64 revision) const;
    bool publicationValid(const QString &id, quint64 revision, quint64 epoch) const;
    // Retracts managed DC publication synchronously; does not change Torrent activity.
    void setDcShareExcluded(const QString &id, bool excluded);
    void setWantedFiles(const QString &id, const QList<int> &);
    void setWantedFiles(const QString &id, const QList<int> &, const QMap<int, int> &priorities);
    void pause(const QString &id, bool);
    void stop(const QString &id);
    void remove(const QString &id, bool deleteFiles = false);
    void recheck(const QString &id);
    // Call before dependent controllers are destroyed. Idempotent; joins worker.
    void shutdown();

signals:
    // Emitted inline before public configure/selection/recheck/remove mutations.
    // Empty id means all jobs. Use DirectConnection for synchronous cancellation;
    // no backend mutex is held while connected slots run.
    void storageInvalidating(QString id);
    void dcShareExclusionChanged(QString id);
    // Explicit payload recheck, after synchronous publication cancellation.
    void hashRecheckRequested(QString id);
    void configurationApplied(quint64 generation);
    void listenersChanged();
    void peersChanged();
    void changed();
    void error(QString);
    // Recoverable transport events belong in diagnostics, not a sticky job failure.
    void diagnostic(QString);
    void completed(QString id, QStringList files, bool privateTorrent);

private:
    void setPaused(const QString &id, bool paused, bool stopped);
    QString enqueueAdd(const QString &, const QString &, const QList<int> &, bool startPaused, bool sourceReadOnly);
    struct Impl;
    std::unique_ptr<Impl> d;
    friend struct TorrentEngineTestAccess;
};

} // namespace eiskalt::torrent
