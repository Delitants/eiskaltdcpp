#pragma once

#include <QObject>
#include <QStringList>
#include <memory>

namespace eiskalt::torrent {

struct CreateTorrentOptions {
    QString sourcePath;
    QString outputPath;
    QStringList trackers;
    bool privateTorrent = false;
};

class TorrentCreator : public QObject {
    Q_OBJECT
public:
    explicit TorrentCreator(QObject *parent = nullptr);
    ~TorrentCreator() override;

    // Owner-thread APIs. One operation at a time; invalid requests fail asynchronously.
    // Existing output paths are never overwritten. Metadata is always hybrid v1/v2.
    // Secure descriptor-based creation is available on POSIX platforms; other
    // platforms report unsupported. The UI disables creation there.
    // Scan bounds: 10000 entries, depth 128, 1 TiB, 16 MiB of path names;
    // engine-compatible metadata/layout limits are preflighted before file reads.
    bool start(const CreateTorrentOptions &options);
    bool isRunning() const;

public slots:
    // Thread-safe. Cancellation before atomic publication leaves no output.
    // Destruction on the owner thread cancels and joins the worker.
    void cancel();

signals:
    // Emitted from the worker; use Auto/QueuedConnection for UI consumers.
    // 0/0 means scanning; otherwise these are source bytes (excluding pad files).
    void progress(qint64 completed, qint64 total);
    // Exactly one terminal signal per accepted start, on the owner thread.
    void failed(QString message);
    void cancelled();
    void finished(QString metadataPath, QString sourceParentDirectory);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace eiskalt::torrent
