#pragma once

#include "TorrentActionMenu.h"
#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>

namespace torrent_action_menu {
using DirectoryProbe = std::function<bool(const QString &)>;

inline void probeDownloadDirectory(QWidget *requester, const eiskalt::torrent::Job &job,
                                   std::function<bool()> current, std::function<void()> error,
                                   DirectoryProbe probe = {})
{
    if (!qApp || QThread::currentThread() != qApp->thread() || !requester)
        return;
    const auto path = downloadDirectory(job);
    const QPointer<QWidget> owner(requester);
    if (path.isEmpty() || !owner->isEnabled() || owner->property("_torrentLocationPending").toBool()
        || !current() || !owner || !owner->isEnabled())
        return;
    // The stable window retains the physical future even if its requesting tab
    // disappears. Checked quit can observe it without waiting on filesystem I/O.
    auto *watcher = new QFutureWatcher<bool>(owner->window());
    owner->setProperty("_torrentLocationPending", true);
    QObject::connect(watcher, &QFutureWatcher<bool>::finished, watcher,
        [owner, watcher, path, current = std::move(current), error = std::move(error)] {
            const bool completed = !watcher->isCanceled() && watcher->future().resultCount() == 1;
            const bool exists = completed && watcher->result();
            if (owner) owner->setProperty("_torrentLocationPending", false);
            watcher->deleteLater();
            if (!completed || !owner || !owner->isEnabled() || !current() || !owner || !owner->isEnabled())
                return;
            if (exists && QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                return;
            if (owner && owner->isEnabled() && current() && owner && owner->isEnabled())
                error();
        });
    if (!probe) probe = [](const QString &value) { return QFileInfo(value).isDir(); };
    // Only copied path/dependency values cross threads, never managers or widgets.
    watcher->setFuture(QtConcurrent::run([path, probe = std::move(probe)] {
        try { return probe(path); }
        catch (...) { return false; }
    }));
}
} // namespace torrent_action_menu
