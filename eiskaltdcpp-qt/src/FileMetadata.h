#pragma once

#include <QDateTime>
#include <QList>
#include <QVariant>
#include <limits>

#include "dcpp/DirectoryListing.h"

inline QVariant fileMetadataDate(uint64_t timestamp)
{
    // Qt stores milliseconds in a signed 64-bit value; check before conversion.
    const auto maxSeconds = static_cast<uint64_t>(std::numeric_limits<qint64>::max() / 1000);
    if(timestamp > 0 && timestamp <= maxSeconds) {
        const auto date = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp));
        if(date.isValid())
            return date.toString("yyyy-MM-dd hh:mm");
    }
    return {};
}

inline QList<QVariant> fileMetadataColumns(const dcpp::DirectoryListing::File& file)
{
    QList<QVariant> columns;
    columns << (file.mediaInfo.bitrate > 0 ? QVariant(file.mediaInfo.bitrate) : QVariant())
            << QString::fromStdString(file.mediaInfo.resolution)
            << QString::fromStdString(file.mediaInfo.video_info)
            << QString::fromStdString(file.mediaInfo.audio_info)
            << (file.hasHit() ? QVariant(static_cast<quint64>(file.getHit())) : QVariant())
            << fileMetadataDate(file.getTS())
            << fileMetadataDate(file.getRemoteDate());
    return columns;
}
