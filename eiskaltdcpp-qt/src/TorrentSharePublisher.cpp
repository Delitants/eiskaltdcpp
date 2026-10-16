#include "TorrentSharePublisher.h"

#ifdef USE_TORRENT
#include <QHash>
#include <QMetaObject>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
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
    CheckedRetract checkedRetract;
    QHash<QString, std::shared_ptr<std::atomic<bool>>> tokens;
    QHash<QString, std::shared_ptr<std::atomic<bool>>> quitTokens;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Work> queue;
    bool stopping = false;
    bool stopped = false;
    bool quitFenced = false;
    bool checkedRetirement = false;
    std::atomic<bool> workerFailed{false};
    std::promise<bool> workerReceipt;
    std::future<bool> workerCompletion{workerReceipt.get_future()};
    eiskalt::torrent::QuitRetirementStatus retirement = eiskalt::torrent::QuitRetirementStatus::Pending;
    std::thread worker;
};

TorrentSharePublisher::TorrentSharePublisher(Publish publish, Retract retract, QObject *parent,
                                           CheckedRetract checkedRetract)
    : QObject(parent), d(new Impl)
{
    d->publish = std::move(publish);
    d->retract = std::move(retract);
    d->checkedRetract = std::move(checkedRetract);
    d->worker = std::thread([this] {
        bool succeeded = true;
        try {
            for (;;) {
                Impl::Work work;
                {
                    std::unique_lock lock(d->mutex);
                    d->changed.wait(lock, [this] { return d->stopping || !d->queue.empty(); });
                    if (d->stopping) break;
                    work = std::move(d->queue.front());
                    d->queue.pop_front();
                }
                const auto cancelled = [token = work.cancelled, invalidated = work.invalidated] {
                    try { return token->load() || (invalidated && invalidated()); }
                    catch (...) { return true; }
                };
                if (cancelled()) continue;
                bool published = false;
                try {
                    const auto hashingStarted = [this, id = work.id, cancelled] {
                        QMetaObject::invokeMethod(this, [this, id, cancelled] {
                            if (!cancelled()) emit statusChanged(id, tr("Preparing DC++ sharing"));
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
        } catch (...) {
            succeeded = false;
            d->workerFailed = true;
            // Retain a failed loop until the owner supplies its frozen cleanup
            // intent. An early exit would otherwise skip all later retractions.
            try {
                std::unique_lock lock(d->mutex);
                d->changed.wait(lock, [this] { return d->stopping; });
            } catch (...) {}
        }
        // The stop request publishes quitTokens under this mutex. Destroy cancelled
        // queued captures on the worker, never on the checked GUI request path.
        try {
            std::deque<Impl::Work> cancelledQueue;
            bool checked = false;
            {
                std::lock_guard lock(d->mutex);
                cancelledQueue.swap(d->queue);
                checked = d->checkedRetirement;
            }
            cancelledQueue.clear();
            if (checked) {
                for (auto it = d->quitTokens.cbegin(); it != d->quitTokens.cend(); ++it) {
                    const auto &id = it.key();
                    try {
                        if (!d->checkedRetract || !d->checkedRetract(id)) succeeded = false;
                    } catch (...) { succeeded = false; }
                }
            }
        } catch (...) { succeeded = false; }
        // Includes real thread-local teardown; returning from this body is not
        // enough to authorize an owner-side join or updater handoff.
        try { d->workerReceipt.set_value_at_thread_exit(succeeded); }
        catch (...) {
            try { d->workerReceipt.set_exception_at_thread_exit(std::current_exception()); }
            catch (...) {} // Keep Pending rather than inventing physical readiness.
        }
    });
}

TorrentSharePublisher::~TorrentSharePublisher() { stop(); }

void TorrentSharePublisher::enqueue(const QString &id, const QString &root, const QStringList &files,
                                   std::function<bool()> invalidated)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (d->stopped || d->workerFailed || id.isEmpty())
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
    if (d->quitFenced) return;
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
    if (d->quitFenced) {
        retireForQuitChecked();
        if (d->workerCompletion.valid()) d->workerCompletion.wait();
        retireForQuitChecked();
        return;
    }
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

bool TorrentSharePublisher::fenceForQuitChecked() noexcept
{
    if (QThread::currentThread() != thread()) return false;
    if (d->quitFenced) return true;
    if (d->stopped) return false; // A legacy void retraction has no checked receipt.
    // No allocating snapshot or implicit detach may precede cancellation.
    // Transfer all cleanup identities to the worker even under allocation failure.
    for (auto it = d->tokens.cbegin(); it != d->tokens.cend(); ++it) it.value()->store(true);
    d->quitTokens.swap(d->tokens);
    d->quitFenced = d->stopped = true;
    return true;
}

eiskalt::torrent::QuitRetirementResult TorrentSharePublisher::retireForQuitChecked() noexcept
{
    using eiskalt::torrent::QuitRetirementStatus;
    if (QThread::currentThread() != thread()) return {QuitRetirementStatus::Refused, "publisher"};
    if (d->retirement != QuitRetirementStatus::Pending) return {d->retirement, "publisher"};
    if (!fenceForQuitChecked()) return {QuitRetirementStatus::Refused, "publisher"};
    try {
        {
            std::lock_guard lock(d->mutex);
            if (!d->checkedRetirement) {
                d->checkedRetirement = true;
                d->stopping = true;
                d->changed.notify_all();
            }
        }
        if (d->workerCompletion.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            return {QuitRetirementStatus::Pending, "publisher"};
        if (d->worker.joinable()) d->worker.join();
        bool succeeded = false;
        try { succeeded = d->workerCompletion.get(); } catch (...) {}
        d->retirement = succeeded ? QuitRetirementStatus::Ready : QuitRetirementStatus::Refused;
        return {d->retirement, "publisher"};
    } catch (...) { return {QuitRetirementStatus::Refused, "publisher"}; }
}
#endif
