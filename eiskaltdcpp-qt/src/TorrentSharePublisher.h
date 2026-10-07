#pragma once

#ifdef USE_TORRENT
#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>
#include "torrent/QuitRetirement.h"

class TorrentSharePublisher : public QObject {
    Q_OBJECT
public:
    using Publish = std::function<bool(const QString &, const QString &, const QStringList &,
                                       const std::function<bool()> &, const std::function<void()> &)>;
    using Retract = std::function<void(const QString &)>;
    using CheckedRetract = std::function<bool(const QString &)>;

    TorrentSharePublisher(Publish publish, Retract retract, QObject *parent = nullptr,
                          CheckedRetract checkedRetract = {});
    ~TorrentSharePublisher() override;
    void enqueue(const QString &id, const QString &root, const QStringList &files,
                 std::function<bool()> invalidated = {});
    void invalidate(const QString &id = {});
    void stop();
    // Owner-only, terminal admission fence and zero-wait physical retirement.
    bool fenceForQuitChecked() noexcept;
    eiskalt::torrent::QuitRetirementResult retireForQuitChecked() noexcept;

signals:
    void statusChanged(const QString &id, const QString &status);
    void finished(const QString &id, bool published);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
#endif
