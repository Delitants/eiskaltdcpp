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
