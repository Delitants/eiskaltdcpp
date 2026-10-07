#include <catch2/generators/catch_generators.hpp>

#include <catch2/catch_test_macros.hpp>
#include "TorrentSharePublisher.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

TEST_CASE("Restoring a cached publication does not announce payload preparation", "[torrent][publisher][managedhash-restart]") {
    TorrentSharePublisher publisher(
        [](const QString &, const QString &, const QStringList &, const std::function<bool()> &, const std::function<void()> &) { return true; },
        [](const QString &) {});
    QSignalSpy statuses(&publisher, &TorrentSharePublisher::statusChanged);
    QSignalSpy finished(&publisher, &TorrentSharePublisher::finished);
    publisher.enqueue("cached", "Completed", {"/tmp/data"});
    if(finished.isEmpty()) REQUIRE(finished.wait(2000));
    REQUIRE(finished.front().at(1).toBool());
    for(const auto& signal : statuses)
        REQUIRE(signal.at(1).toString() != "Preparing DC++ sharing");
}

TEST_CASE("Actual hashing reports preparation before publication completes", "[torrent][publisher][managedhash-restart]") {
    TorrentSharePublisher publisher(
        [](const QString &, const QString &, const QStringList &, const std::function<bool()> &,
           const std::function<void()> &hashingStarted) { hashingStarted(); return true; },
        [](const QString &) {});
    QSignalSpy statuses(&publisher, &TorrentSharePublisher::statusChanged);
    QSignalSpy finished(&publisher, &TorrentSharePublisher::finished);
    publisher.enqueue("uncached", "Completed", {"/tmp/data"});
    if(finished.isEmpty()) REQUIRE(finished.wait(2000));
    QStringList messages;
    for(const auto& signal : statuses) messages.append(signal.at(1).toString());
    REQUIRE(messages.contains("Preparing DC++ sharing"));
    REQUIRE(messages.last() == "Shared in DC++");
    REQUIRE(messages.indexOf("Preparing DC++ sharing") < messages.indexOf("Shared in DC++"));
}

TEST_CASE("Torrent publication cancels hashing before retracting a disabled share", "[torrent][publisher]") {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    std::function<bool()> runningCancelled;
    std::atomic<bool> cancellationPrecededRetraction{false};
    std::atomic<int> removed{0};
    TorrentSharePublisher publisher(
        [&](const QString &, const QString &, const QStringList &, const std::function<bool()> &cancelled, const std::function<void()> &) {
            { std::lock_guard lock(mutex); runningCancelled = cancelled; started = true; }
            changed.notify_all();
            while(!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return false;
        }, [&](const QString &) {
            std::lock_guard lock(mutex);
            if (started) {
                cancellationPrecededRetraction = runningCancelled();
                ++removed;
            }
        });
    publisher.enqueue("job-1", "Completed", {"/tmp/test-data"});
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(2), [&] { return started; }));
    }
    publisher.invalidate("job-1");
    publisher.stop();
    REQUIRE(cancellationPrecededRetraction);
    REQUIRE(removed >= 1);
}

TEST_CASE("Torrent publication validates empty identity and does not publish empty file sets", "[torrent][publisher]") {
    std::atomic<int> called{0};
    TorrentSharePublisher publisher(
        [&](const QString &, const QString &, const QStringList &, const std::function<bool()> &, const std::function<void()> &) {
            ++called;
            return true;
        }, [](const QString &) {});
    publisher.enqueue({}, "Completed", {"/tmp/test-data"});
    publisher.enqueue("job-2", "Completed", {});
    publisher.stop();
    REQUIRE(called == 0);
}

TEST_CASE("Disabling publication cancels queued work as well as the running hash", "[torrent][publisher]") {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    std::atomic<int> called{0};
    TorrentSharePublisher publisher(
        [&](const QString &, const QString &, const QStringList &, const std::function<bool()> &cancelled, const std::function<void()> &) {
            ++called;
            { std::lock_guard lock(mutex); started = true; }
            changed.notify_all();
            while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return false;
        }, [](const QString &) {});
    publisher.enqueue("running", "Completed", {"/tmp/data-1"});
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(2), [&] { return started; }));
    }
    publisher.enqueue("queued", "Completed", {"/tmp/data-2"});
    publisher.invalidate();
    publisher.stop();
    REQUIRE(called == 1);
}

TEST_CASE("An engine revision guard cancels publication without processing GUI events", "[torrent][publisher]") {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    std::atomic<bool> invalidated{false}, observed{false};
    TorrentSharePublisher publisher(
        [&](const QString &, const QString &, const QStringList &, const std::function<bool()> &cancelled, const std::function<void()> &) {
            { std::lock_guard lock(mutex); started = true; }
            changed.notify_all();
            while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            observed = true;
            return false;
        }, [](const QString &) {});
    publisher.enqueue("job", "Completed", {"/tmp/data"}, [&] { return invalidated.load(); });
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(2), [&] { return started; }));
    }
    invalidated = true;
    for (int i = 0; i < 2000 && !observed; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(observed);
    publisher.stop();
}

TEST_CASE("A successful worker cannot report a stale Shared state after revocation", "[torrent][publisher]") {
    std::atomic<bool> published{false};
    TorrentSharePublisher publisher(
        [&](const QString &, const QString &, const QStringList &, const std::function<bool()> &, const std::function<void()> &) {
            published = true;
            return true;
        }, [](const QString &) {});
    QSignalSpy statuses(&publisher, &TorrentSharePublisher::statusChanged);
    publisher.enqueue("job", "Completed", {"/tmp/data"});
    for (int i = 0; i < 2000 && !published; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(published);
    publisher.invalidate("job");
    publisher.stop();
    QCoreApplication::processEvents();
    for (const auto &signal : statuses)
        REQUIRE(signal.at(1).toString() != "Shared in DC++");
}

#include "torrent/QuitRetirement.h"
#include <QTimer>
#include <QThread>
#include <future>
#include <stdexcept>
#include "dcpp/stdinc.h"
#define private public
#include "dcpp/DCContext.h"
#undef private
#include "dcpp/DCPlusPlus.h"
#include "dcpp/ClientManager.h"
#include "dcpp/HashManager.h"
#include "dcpp/QueueManager.h"
#include "dcpp/SearchManager.h"
#include "dcpp/ShareManager.h"
#include "dcpp/UploadManager.h"
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace {
using Retirement = eiskalt::torrent::QuitRetirementStatus;
template<class Publisher> constexpr bool hasCheckedPublisher() {
    return requires(Publisher& p) { p.fenceForQuitChecked(); p.retireForQuitChecked(); };
}
template<class Publisher> bool fencePublisher(Publisher& publisher) {
    if constexpr(hasCheckedPublisher<Publisher>()) return publisher.fenceForQuitChecked();
    return false;
}
template<class Publisher> Retirement retirePublisher(Publisher& publisher) {
    if constexpr(hasCheckedPublisher<Publisher>()) return publisher.retireForQuitChecked().status;
    publisher.stop();
    return Retirement::Ready; // Baseline exposes its blocking, unchecked result.
}
template<class Publisher = TorrentSharePublisher>
std::unique_ptr<Publisher> checkedPublisher(typename Publisher::Publish publish,
        typename Publisher::Retract retract, std::function<bool(const QString&)> checked = {}) {
    if constexpr(std::is_constructible_v<Publisher, typename Publisher::Publish,
                  typename Publisher::Retract, QObject*, std::function<bool(const QString&)>>)
        return std::make_unique<Publisher>(std::move(publish), std::move(retract), nullptr, std::move(checked));
    else return std::make_unique<Publisher>(std::move(publish), std::move(retract));
}
bool publisherWait(const std::function<bool()>& predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (!predicate() && std::chrono::steady_clock::now() < end) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}
struct PublisherHold {
    std::atomic<bool> entered{false}, release{false}, exited{false};
    void block() {
        entered = true;
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        exited = true;
    }
};
struct PublisherRelease {
    std::shared_ptr<PublisherHold> hold;
    std::thread watchdog;
    explicit PublisherRelease(std::shared_ptr<PublisherHold> h) : hold(std::move(h)),
        watchdog([h = hold] { std::this_thread::sleep_for(std::chrono::milliseconds(350)); h->release = true; }) {}
    ~PublisherRelease() { hold->release = true; watchdog.join(); }
};
struct PublisherExitHold {
    std::shared_ptr<PublisherHold> hold;
    explicit PublisherExitHold(std::shared_ptr<PublisherHold> h) : hold(std::move(h)) {}
    ~PublisherExitHold() { hold->block(); }
};

// Same real managers as the runtime tests, with disposable paths and no networking startup.
struct PublisherCore {
    QTemporaryDir temp;
    dcpp::Util::PathsMap saved;
    dcpp::DCContext* previous = dcpp::getContext();
    dcpp::DCContext context;
    QString root;
    PublisherCore() {
        REQUIRE(temp.isValid());
        root = QFileInfo(temp.path()).canonicalFilePath();
        dcpp::Util::PathsMap paths;
        for(int i = 0; i < dcpp::Util::PATH_LAST; ++i) {
            const auto key = static_cast<dcpp::Util::Paths>(i);
            saved[key] = dcpp::Util::getPath(key);
            const auto path = root + QString("/dc-%1/").arg(i);
            REQUIRE(QDir().mkpath(path));
            paths[key] = path.toStdString();
        }
        paths[dcpp::Util::PATH_NOTEPAD] = (root + "/notepad.txt").toStdString();
        dcpp::Util::uninitialize();
        dcpp::Util::initialize(paths);
        context.startupMinimal();
        dcpp::setContext(&context);
        context.timerManager_ = std::make_unique<dcpp::TimerManager>(context);
        context.hashManager_ = std::make_unique<dcpp::HashManager>(context);
        context.searchManager_ = std::make_unique<dcpp::SearchManager>(context);
        context.clientManager_ = std::make_unique<dcpp::ClientManager>(context);
        context.queueManager_ = std::make_unique<dcpp::QueueManager>(context);
        context.uploadManager_ = std::make_unique<dcpp::UploadManager>(context);
        context.shareManager_ = std::make_unique<dcpp::ShareManager>(context);
        context.getSettingsManager()->set(dcpp::SettingsManager::LIST_DUPES, true);
        context.getSettingsManager()->set(dcpp::SettingsManager::SHARE_SKIP_ZERO_BYTE, false);
    }
    ~PublisherCore() {
        context.shutdown();
        dcpp::setContext(previous);
        dcpp::Util::uninitialize();
        dcpp::Util::initialize(saved);
    }
};
template<class Manager> bool publisherRemoveChecked(Manager& manager, const std::string& id) {
    if constexpr(requires { manager.removeManagedFilesChecked(id); }) return manager.removeManagedFilesChecked(id);
    manager.removeManagedFiles(id);
    return false;
}
}

TEST_CASE("Publisher checked retirement preserves cancellation and physical retraction", "[torrent-quit-publisher]") {
    SECTION("publication, checked retraction and actual TLS exit stay pending") {
        const auto mode = GENERATE(0, 1, 2);
        INFO("physical hold mode " << mode);
        auto hold = std::make_shared<PublisherHold>();
        std::atomic<bool> started{false}, armed{false}, sawCancellation{false}, offGui{false};
        std::atomic<int> publications{0}, retractions{0}, legacyRetractions{0};
        std::function<bool()> cancellation;
        auto* gui = QThread::currentThread();
        auto publisher = checkedPublisher(
            [&](const QString&, const QString&, const QStringList&, const auto& cancelled, const auto& hashing) {
                ++publications;
                cancellation = cancelled;
                if(mode == 2) {
                    thread_local std::unique_ptr<PublisherExitHold> tail;
                    tail = std::make_unique<PublisherExitHold>(hold);
                }
                started = true;
                hashing();
                if(mode == 0) { hold->block(); sawCancellation = cancelled(); }
                return true;
            }, [&](const QString&) { ++legacyRetractions; if(armed && mode == 1) hold->block(); },
            [&](const QString&) {
                offGui = QThread::currentThread() != gui;
                ++retractions;
                if(mode == 1) hold->block();
                return true;
            });
        // Release even when an assertion unwinds before the normal request.
        struct Cleanup { std::shared_ptr<PublisherHold> h; ~Cleanup() { h->release = true; } } cleanup{hold};
        QSignalSpy statuses(publisher.get(), &TorrentSharePublisher::statusChanged);
        QSignalSpy completed(publisher.get(), &TorrentSharePublisher::finished);
        publisher->enqueue("job", "Completed", {"/tmp/data"});
        REQUIRE(publisherWait([&] { return started.load(); }));
        // The publication hold makes this second accepted job deterministically queued.
        if(mode == 0) publisher->enqueue("queued", "Completed", {"/tmp/queued"});
        const int expectedRetractions = mode == 0 ? 2 : 1;
        legacyRetractions = 0;
        statuses.clear(); completed.clear(); armed = true;
        int heartbeats = 0;
        QTimer timer;
        timer.setInterval(5);
        QObject::connect(&timer, &QTimer::timeout, [&] { if(!hold->release) ++heartbeats; });
        timer.start();
        PublisherRelease release(hold);
        const auto begin = std::chrono::steady_clock::now();
        const bool fenced = fencePublisher(*publisher);
        const auto first = retirePublisher(*publisher);
        const auto requestMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
        const bool stillHeld = !hold->release;
        CHECK(fenced);
        CHECK(requestMs < 100);
        CHECK(first == Retirement::Pending);
        CHECK(stillHeld);
        CHECK(cancellation());
        publisher->enqueue("late", "Completed", {"/tmp/late"});
        REQUIRE(publisherWait([&] { return hold->entered.load(); }));
        const auto pollStart = std::chrono::steady_clock::now();
        const auto heldPoll = retirePublisher(*publisher);
        const auto pollMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - pollStart).count();
        CHECK(heldPoll == Retirement::Pending);
        CHECK(pollMs < 100);
        CHECK_FALSE(hold->release.load());
        Retirement result = Retirement::Pending;
        REQUIRE(publisherWait([&] { result = retirePublisher(*publisher); return result != Retirement::Pending; }));
        CHECK(result == Retirement::Ready);
        CHECK(hold->exited.load());
        CHECK(heartbeats > 0);
        CHECK(offGui.load());
        CHECK(retractions == expectedRetractions);
        CHECK(legacyRetractions == 0);
        if(mode == 0) CHECK(sawCancellation.load());
        CHECK(publications == 1);
        QCoreApplication::processEvents();
        CHECK(statuses.isEmpty());
        CHECK(completed.isEmpty());
        publisher->invalidate("job");
        publisher->stop();
        CHECK(retractions == expectedRetractions);
        CHECK(legacyRetractions == 0);
        CHECK(retirePublisher(*publisher) == Retirement::Ready);
    }
    SECTION("false, throwing and absent callbacks are cached refusals and all IDs drain") {
        const auto failure = GENERATE(0, 1, 2);
        std::atomic<int> retractions{0};
        std::function<bool(const QString&)> checked;
        if(failure != 2) checked = [&](const QString&) {
            ++retractions;
            if(failure == 1) throw std::runtime_error("fixture retraction failed");
            return false;
        };
        auto publisher = checkedPublisher([](const auto&, const auto&, const auto&, const auto&, const auto&) { return true; },
            [](const QString&) {}, checked);
        publisher->enqueue("one", "Completed", {"/tmp/one"});
        publisher->enqueue("two", "Completed", {"/tmp/two"});
        CHECK(fencePublisher(*publisher));
        Retirement result = Retirement::Pending;
        REQUIRE(publisherWait([&] { result = retirePublisher(*publisher); return result != Retirement::Pending; }));
        CHECK(result == Retirement::Refused);
        CHECK(retirePublisher(*publisher) == Retirement::Refused);
        CHECK(retractions == (failure == 2 ? 0 : 2));
        publisher->stop();
        CHECK(retirePublisher(*publisher) == Retirement::Refused);
        CHECK(retractions == (failure == 2 ? 0 : 2));
    }
    SECTION("empty owner set and foreign calls do not poison owner retirement") {
        auto publisher = checkedPublisher([](const auto&, const auto&, const auto&, const auto&, const auto&) { return true; },
            [](const QString&) {});
        bool foreignFence = true;
        Retirement foreign = Retirement::Ready;
        std::thread caller([&] {
            if constexpr(hasCheckedPublisher<TorrentSharePublisher>()) {
                foreignFence = fencePublisher(*publisher);
                foreign = retirePublisher(*publisher);
            } else { foreignFence = false; foreign = Retirement::Refused; }
        });
        caller.join();
        CHECK_FALSE(foreignFence);
        CHECK(foreign == Retirement::Refused);
        CHECK(fencePublisher(*publisher));
        Retirement result = Retirement::Pending;
        REQUIRE(publisherWait([&] { result = retirePublisher(*publisher); return result != Retirement::Pending; }));
        CHECK(result == Retirement::Ready);
        CHECK(retirePublisher(*publisher) == Retirement::Ready);
    }
    SECTION("real retraction completes while retained core admission is open") {
        PublisherCore core;
        auto& shares = *core.context.getShareManager();
        const auto file = core.root + "/payload.txt";
        { QFile out(file); REQUIRE(out.open(QIODevice::WriteOnly)); REQUIRE(out.write("payload") == 7); }
        std::atomic<bool> published{false}, checkedAccepted{false}, offGui{false};
        auto* gui = QThread::currentThread();
        auto publisher = checkedPublisher(
            [&](const QString& id, const QString& root, const QStringList& files, const auto& cancelled, const auto& hashing) {
                const bool ok = shares.replaceManagedFiles(id.toStdString(), root.toStdString(),
                    {files.front().toStdString()}, cancelled, hashing);
                published = ok;
                return ok;
            }, [&](const QString& id) { shares.removeManagedFiles(id.toStdString()); },
            [&](const QString& id) {
                offGui = QThread::currentThread() != gui;
                checkedAccepted = publisherRemoveChecked(shares, id.toStdString());
                return checkedAccepted.load();
            });
        publisher->enqueue("real", "Completed", {file});
        REQUIRE(publisherWait([&] { return published.load(); }));
        const auto tth = shares.getTTH("/Completed/payload.txt");
        REQUIRE(shares.isTTHShared(tth));
        CHECK(fencePublisher(*publisher));
        Retirement result = Retirement::Pending;
        REQUIRE(publisherWait([&] { result = retirePublisher(*publisher); return result != Retirement::Pending; }));
        CHECK(result == Retirement::Ready);
        CHECK(checkedAccepted.load());
        CHECK(offGui.load());
        CHECK_FALSE(shares.isTTHShared(tth));
        CHECK(publisherRemoveChecked(shares, "absent"));
        shares.stopForShutdown();
        CHECK_FALSE(publisherRemoveChecked(shares, "absent"));
    }
}

namespace {
struct PublisherCopyFailure {
    std::atomic<bool> armed{false}, thrown{false}, threadExited{false};
};
struct ThrowingInvalidationCopy {
    std::shared_ptr<PublisherCopyFailure> state;
    explicit ThrowingInvalidationCopy(std::shared_ptr<PublisherCopyFailure> s) : state(std::move(s)) {}
    ThrowingInvalidationCopy(const ThrowingInvalidationCopy& other) : state(other.state) {
        if(state->armed) { state->thrown = true; throw std::runtime_error("fixture invalidation copy failed"); }
    }
    bool operator()() const { return false; }
};
struct PublisherExitMarker {
    std::shared_ptr<PublisherCopyFailure> state;
    explicit PublisherExitMarker(std::shared_ptr<PublisherCopyFailure> s) : state(std::move(s)) {}
    ~PublisherExitMarker() { state->threadExited = true; }
};
}

TEST_CASE("Publisher loop failure retains checked cleanup of already published shares", "[torrent-quit-publisher-failure]") {
    PublisherCore core;
    auto& shares = *core.context.getShareManager();
    const auto file = core.root + "/failure-payload.txt";
    { QFile out(file); REQUIRE(out.open(QIODevice::WriteOnly)); REQUIRE(out.write("payload") == 7); }
    auto state = std::make_shared<PublisherCopyFailure>();
    auto hold = std::make_shared<PublisherHold>();
    std::atomic<bool> published{false};
    std::atomic<int> publications{0}, legacyRetractions{0};
    QStringList checkedIds;
    auto publisher = checkedPublisher(
        [&](const QString& id, const QString& root, const QStringList& files, const auto& cancelled, const auto& hashing) {
            ++publications;
            thread_local std::unique_ptr<PublisherExitMarker> marker;
            marker = std::make_unique<PublisherExitMarker>(state);
            const bool ok = shares.replaceManagedFiles(id.toStdString(), root.toStdString(),
                {files.front().toStdString()}, cancelled, hashing);
            published = ok;
            hold->block();
            return ok;
        }, [&](const QString& id) { ++legacyRetractions; shares.removeManagedFiles(id.toStdString()); },
        [&](const QString& id) { checkedIds.append(id); return publisherRemoveChecked(shares, id.toStdString()); });
    struct Cleanup { std::shared_ptr<PublisherHold> h; ~Cleanup() { h->release = true; } } cleanup{hold};
    publisher->enqueue("retained", "Completed", {file});
    REQUIRE(publisherWait([&] { return published.load() && hold->entered.load(); }));
    const auto tth = shares.getTTH("/Completed/failure-payload.txt");
    REQUIRE(shares.isTTHShared(tth));
    std::function<bool()> invalidated = ThrowingInvalidationCopy(state);
    publisher->enqueue("copy-failure", "Unused", {file}, std::move(invalidated));
    state->armed = true;
    hold->release = true;
    REQUIRE(publisherWait([&] { return state->thrown.load(); }));
    // Allow an incorrectly exited worker to reach its actual TLS marker before
    // asking for retirement. The repaired worker instead retains its stop wait.
    const auto observeUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
    while(std::chrono::steady_clock::now() < observeUntil) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_FALSE(state->threadExited.load());
    publisher->enqueue("unserviceable", "Unused", {file});
    CHECK(legacyRetractions == 2);
    CHECK(fencePublisher(*publisher));
    Retirement result = Retirement::Pending;
    REQUIRE(publisherWait([&] { result = retirePublisher(*publisher); return result != Retirement::Pending; }));
    CHECK(result == Retirement::Refused);
    CHECK(retirePublisher(*publisher) == Retirement::Refused);
    CHECK(state->threadExited.load());
    CHECK(publications == 1);
    checkedIds.sort();
    CHECK(checkedIds == QStringList{"copy-failure", "retained"});
    CHECK_FALSE(shares.isTTHShared(tth));
    CHECK(publisherRemoveChecked(shares, "absent"));
    publisher->stop();
    CHECK(legacyRetractions == 2);
    CHECK(checkedIds.size() == 2);
}
