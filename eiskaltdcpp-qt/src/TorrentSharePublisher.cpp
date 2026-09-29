#include "TorrentSharePublisher.h"

#ifdef USE_TORRENT
#include <QHash>
#include <QMetaObject>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

struct TorrentSharePublisher::Impl {
    struct Work {
        QString id, root;
        QStringList files;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::function<bool()> invalidated;
    };
    Publish publish;
    Retract retract;
    QHash<QString, std::shared_ptr<std::atomic<bool>>> tokens;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Work> queue;
    bool stopping = false;
    bool stopped = false;
    std::thread worker;
};

TorrentSharePublisher::TorrentSharePublisher(Publish publish, Retract retract, QObject *parent)
    : QObject(parent), d(new Impl)
{
    d->publish = std::move(publish);
    d->retract = std::move(retract);
    d->worker = std::thread([this] {
        for (;;) {
            Impl::Work work;
            {
                std::unique_lock lock(d->mutex);
                d->changed.wait(lock, [this] { return d->stopping || !d->queue.empty(); });
                if (d->stopping)
                    return;
                work = std::move(d->queue.front());
                d->queue.pop_front();
            }
            const auto cancelled = [token = work.cancelled, invalidated = work.invalidated] {
                return token->load() || (invalidated && invalidated());
            };
            if (cancelled())
                continue;
            bool published = false;
            try {
                const auto hashingStarted = [this, id = work.id, cancelled] {
                    QMetaObject::invokeMethod(this, [this, id, cancelled] {
                        if(!cancelled()) emit statusChanged(id, tr("Preparing DC++ sharing"));
                    }, Qt::QueuedConnection);
                };
                published = d->publish(work.id, work.root, work.files, cancelled, hashingStarted);
            } catch (...) {
                // A failed hash/publication must never terminate the GUI process.
            }
            QMetaObject::invokeMethod(this, [this, work, published, cancelled] {
                if (!cancelled()) {
                    emit statusChanged(work.id, published ? tr("Shared in DC++") : tr("Not shared: file validation or hashing failed"));
                    emit finished(work.id, published);
                }
            }, Qt::QueuedConnection);
        }
    });
}

TorrentSharePublisher::~TorrentSharePublisher() { stop(); }

void TorrentSharePublisher::enqueue(const QString &id, const QString &root, const QStringList &files,
                                   std::function<bool()> invalidated)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (d->stopped || id.isEmpty())
        return;
    invalidate(id);
    if (root.isEmpty() || files.isEmpty())
        return;
    auto token = std::make_shared<std::atomic<bool>>(false);
    d->tokens.insert(id, token);
    {
        std::lock_guard lock(d->mutex);
        d->queue.push_back({id, root, files, token, std::move(invalidated)});
    }
    d->changed.notify_one();
}

void TorrentSharePublisher::invalidate(const QString &id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto ids = id.isEmpty() ? d->tokens.keys() : QList<QString>{id};
    for (const auto &owner : ids) {
        auto it = d->tokens.find(owner);
        if (it != d->tokens.end()) {
            // Cancellation precedes retraction, including work not yet registered
            // in ShareManager. Its commit checks this same token under its lock.
            it.value()->store(true);
            d->tokens.erase(it);
        }
        d->retract(owner);
        emit statusChanged(owner, tr("Not shared"));
    }
    {
        std::lock_guard lock(d->mutex);
        for (auto it = d->queue.begin(); it != d->queue.end();) {
            if (it->cancelled->load())
                it = d->queue.erase(it);
            else
                ++it;
        }
    }
}

void TorrentSharePublisher::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (d->stopped)
        return;
    d->stopped = true;
    invalidate();
    {
        std::lock_guard lock(d->mutex);
        d->stopping = true;
        d->queue.clear();
    }
    d->changed.notify_all();
    if (d->worker.joinable())
        d->worker.join();
}
#endif
