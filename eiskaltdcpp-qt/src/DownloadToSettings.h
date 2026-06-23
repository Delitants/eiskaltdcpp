/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QList>
#include <QPair>
#include <QString>

struct DownloadToEntry {
    QString path;
    QString alias;

    bool operator==(const DownloadToEntry& other) const
    {
        return path == other.path && alias == other.alias;
    }
};

QList<DownloadToEntry> decodeDownloadTo(const QString& encodedPaths,
                                        const QString& encodedAliases);
QPair<QString, QString> encodeDownloadTo(const QList<DownloadToEntry>& entries);
