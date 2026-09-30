#pragma once

#include <QList>
#include <QString>
#include <optional>

namespace eiskalt::torrent {

struct RecheckRecord {
    QString jobId, metadataDigest, storageIdentity;
    quint64 token = 0, revision = 0, resumeGeneration = 0;
    QList<int> selectedFiles, pieces;
    bool sourceReadOnly = false;
    bool operator==(const RecheckRecord &) const = default;
};

class RecheckJournal {
public:
    explicit RecheckJournal(QString stateDirectory);
    bool begin(const RecheckRecord &record, QString *error);
    // A nonempty error blocks recovery; it never means "no pending operation".
    std::optional<RecheckRecord> pending(const QString &jobId, QString *error) const;
    // The caller must commit the corresponding resume snapshot first.
    bool finish(const QString &jobId, quint64 token, quint64 durableResumeGeneration, QString *error);
private:
    QString directory;
};

} // namespace eiskalt::torrent
