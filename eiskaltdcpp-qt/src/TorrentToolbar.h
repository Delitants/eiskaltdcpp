#pragma once

#include "AppIconTheme.h"
#include <QApplication>
#include <QIcon>
#include <QStringList>

namespace torrent_toolbar {
inline QString introducedKey()
{
    return QStringLiteral("mainwindow-torrents-toolbar-introduced");
}

inline QStringList restoreActions(const QString &encoded, bool introduced)
{
    QStringList actions = QString::fromUtf8(QByteArray::fromBase64(encoded.toUtf8()))
        .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const QString torrentAction = QStringLiteral("toolsTorrents");
    // Empty settings use the complete default toolbar; a later user opt-out wins.
    if (!introduced && !actions.isEmpty() && !actions.contains(torrentAction)) {
        const int queue = actions.indexOf(QStringLiteral("toolsDownloadQueue"));
        actions.insert(queue < 0 ? actions.size() : queue + 1, torrentAction);
    }
    return actions;
}

inline QIcon icon()
{
    return app_icon_theme::icon(app_icon_theme::activePath(), QStringLiteral("torrent"),
        QApplication::palette(), QIcon(QStringLiteral(":/torrent/torrent.png")));
}
}
