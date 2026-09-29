#pragma once

#include "AppIconTheme.h"
#include "torrent/TorrentTypes.h"
#include <QAction>
#include <QCoreApplication>
#include <QStyle>
#include <QWidget>
#include <algorithm>

namespace torrent_action_menu {
struct Actions {
    QAction *pause;
    QAction *resume;
    QAction *stop;
};

// The caller owns insertion and connections, so menus and toolbars share policy.
inline Actions create(QObject *parent, QWidget *appearance)
{
    const auto action = [parent, appearance](const char *text, const char *name,
                                           const char *icon, QStyle::StandardPixmap fallback) {
        auto *result = new QAction(app_icon_theme::icon(app_icon_theme::activePath(),
            QString::fromLatin1(icon), appearance->palette(), appearance->style()->standardIcon(fallback)),
            QCoreApplication::translate("TorrentWindow", text), parent);
        result->setObjectName(QString::fromLatin1(name));
        result->setCheckable(true);
        result->setEnabled(false);
        return result;
    };
    return {action(QT_TRANSLATE_NOOP("TorrentWindow", "Pause"), "torrentPause", "media-pause", QStyle::SP_MediaPause),
            action(QT_TRANSLATE_NOOP("TorrentWindow", "Resume"), "torrentResume", "media-play", QStyle::SP_MediaPlay),
            action(QT_TRANSLATE_NOOP("TorrentWindow", "Stop"), "torrentStop", "media-stop", QStyle::SP_MediaStop)};
}

inline void update(Actions actions, const QList<eiskalt::torrent::Job> &jobs, bool available)
{
    const auto active = [](const auto &job) { return !job.paused && !job.stopped; };
    const auto paused = [](const auto &job) { return job.paused && !job.stopped; };
    const auto stopped = [](const auto &job) { return job.stopped; };
    const auto any = [&jobs](auto predicate) { return std::any_of(jobs.cbegin(), jobs.cend(), predicate); };
    const auto all = [&jobs](auto predicate) {
        return !jobs.isEmpty() && std::all_of(jobs.cbegin(), jobs.cend(), predicate);
    };
    // Mixed selections have no misleading single checked state, but allow valid transitions.
    actions.pause->setChecked(all(paused));
    actions.resume->setChecked(all(active));
    actions.stop->setChecked(all(stopped));
    actions.pause->setEnabled(available && any(active));
    actions.resume->setEnabled(available && any([](const auto &job) { return job.paused || job.stopped; }));
    actions.stop->setEnabled(any([](const auto &job) { return !job.stopped; }));
}
} // namespace torrent_action_menu
