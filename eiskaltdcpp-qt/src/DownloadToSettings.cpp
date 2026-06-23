/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "DownloadToSettings.h"

#include <QByteArray>
#include <QStringList>

QList<DownloadToEntry> decodeDownloadTo(const QString& encodedPaths,
                                        const QString& encodedAliases)
{
    const QString aliases = QByteArray::fromBase64(encodedAliases.toUtf8());
    const QString paths = QByteArray::fromBase64(encodedPaths.toUtf8());

    const QStringList aliasList = aliases.split('\n', Qt::SkipEmptyParts);
    const QStringList pathList = paths.split('\n', Qt::SkipEmptyParts);

    QList<DownloadToEntry> entries;

    if (aliasList.size() != pathList.size() || aliasList.isEmpty()) {
        return entries;
    }

    entries.reserve(aliasList.size());

    for (int index = 0; index < aliasList.size(); ++index) {
        entries.append({ pathList.at(index), aliasList.at(index) });
    }

    return entries;
}

QPair<QString, QString> encodeDownloadTo(const QList<DownloadToEntry>& entries)
{
    QString paths;
    QString aliases;

    for (const DownloadToEntry& entry : entries) {
        paths += entry.path + '\n';
        aliases += entry.alias + '\n';
    }

    return {
        QString::fromLatin1(paths.toUtf8().toBase64()),
        QString::fromLatin1(aliases.toUtf8().toBase64())
    };
}
