#pragma once

#include <QList>
#include <QString>
#include <optional>

namespace libtorrent { class torrent_info; }

namespace eiskalt::torrent {

struct RecheckPlan {
    QList<int> selectedFiles, affectedFiles, pieces;
    quint64 revision = 0;
    qint64 totalBytes = 0;
};
struct RecheckPlanResult {
    std::optional<RecheckPlan> plan;
    QString error;
};

// Metadata indices only. This planner performs no storage I/O or mutation.
RecheckPlanResult planSelectedRecheck(const libtorrent::torrent_info &metadata,
    const QList<int> &files, quint64 revision);

} // namespace eiskalt::torrent
