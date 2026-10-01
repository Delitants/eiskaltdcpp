#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <ranges>
#define private public
#include "DCContext.h"
#include "QueueManager.h"
#include "HashManager.h"
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "SearchManager.h"
#include "ScopedFunctor.h"
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace dcpp;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
template<class Manager> void stopWorkers(Manager& manager) {
    if constexpr(requires { manager.stopWorkersForShutdown(); }) manager.stopWorkersForShutdown();
    else { manager.rechecker.join(); manager.mover.join(); }
}
template<class Manager> void awaitClosure(Manager& manager) {
    if constexpr(requires { manager.workerShutdownRequested; }) {
        for(unsigned i = 0; i < 5000; ++i) {
            { std::lock_guard lock(manager.workerMutex); if(manager.workerShutdownRequested) return; }
            Thread::sleep(1);
        }
        FAIL("Queue worker admission did not close");
    }
}
template<class Manager> void lateMoveWhileDraining(Manager& manager, const string& source, const string& target) {
    if constexpr(requires { manager.stopWorkersForShutdown(); }) manager.moveFile(source, target);
}
struct Deadline {
    std::atomic<const char*> stage{"setup"};
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(40000)) {
            std::fprintf(stderr, "Queue workers fixture exceeded 40 seconds: %s\n", stage.load());
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }};
    ~Deadline() { finished.signal(); watchdog.join(); }
};
struct Fixture {
    Deadline deadline;
    test::TestContext fixture;
    Fixture() {
        auto& c = *fixture.ownedCtx;
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.hashManager_ = std::make_unique<HashManager>(c);
        c.hashManager_->store.load();
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.searchManager_ = std::make_unique<SearchManager>(c);
        c.queueManager_ = std::make_unique<QueueManager>(c);
        c.getSettingsManager()->set(SettingsManager::KEEP_LISTS, true);
        c.getSettingsManager()->set(SettingsManager::KEEP_FINISHED_FILES, true);
    }
    ~Fixture() { deadline.stage = "cleanup"; stopWorkers(queue()); }
    QueueManager& queue() { return *fixture.ownedCtx->getQueueManager(); }
    string path(const string& name) { return (fixture.tmpDir / name).string(); }
    string file(const string& name, int64_t size) {
        auto p = path(name);
        File out(p, File::WRITE, File::CREATE | File::TRUNCATE);
        out.setSize(size);
        return p;
    }
    string queued(const string& name, int64_t actualSize = 128 * 1024) {
        constexpr int64_t size = 128 * 1024;
        auto temp = file(name + ".part", actualSize);
        TigerTree tree(64 * 1024);
        string zero(size, '\0');
        tree.update(zero.data(), zero.size()); tree.finalize();
        fixture.ownedCtx->getHashManager()->addTree(tree);
        auto target = path(name + ".bin");
        auto* item = queue().fileQueue.add(target, size, 0, QueueItem::NORMAL, temp, 1700000000, tree.getRoot());
        item->addSegment(Segment(0, 64 * 1024));
        return target;
    }
    string temp(const string& target) {
        Lock lock(queue().cs);
        return queue().fileQueue.find(target)->getTempTarget();
    }
};
struct Listener : QueueManagerListener {
    bool blockRecheck = false, blockMove = false;
    std::atomic<unsigned> started{0}, completed{0}, moved{0};
    Semaphore entered, release;
    void on(RecheckStarted, const string&) noexcept override {
        if(++started == 1 && blockRecheck) { entered.signal(); release.wait(); }
    }
    void on(RecheckDone, const string&) noexcept override { ++completed; }
    void on(RecheckAlreadyFinished, const string&) noexcept override { ++completed; }
    void on(FileMoved, const string&) noexcept override {
        if(++moved == 1 && blockMove) { entered.signal(); release.wait(); }
    }
};
}

TEST_CASE("Terminal queue worker stop rejects late rechecks and all physical move sizes", "[queue-workers-stop][shutdown]") {
    Fixture f;
    auto target = f.queued("late");
    auto temp = f.temp(target);
    Listener listener;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    stopWorkers(f.queue());
    f.queue().recheck(target);
    auto small = f.file("small", 1024), large = f.file("large", 16 * 1024 * 1024);
    f.queue().moveFile(small, f.path("not-created/small"));
    f.queue().moveFile(large, f.path("not-created/large"));
    stopWorkers(f.queue());
    CHECK(listener.started == 0);
    CHECK(listener.moved == 0);
    CHECK(f.queue().getPos(target) == 64 * 1024);
    CHECK(File::getSize(temp) == 128 * 1024);
    CHECK_FALSE(fs::exists(target));
    CHECK(fs::exists(small));
    CHECK(fs::exists(large));
    CHECK_FALSE(fs::exists(f.path("not-created")));
    REQUIRE(f.queue().saveQueueChecked(true));
    CHECK(f.queue().getSize(target) == 128 * 1024);
}

TEST_CASE("Terminal rechecker cancellation drains callbacks without resizing or committing queued files", "[queue-workers-stop][shutdown]") {
    Fixture f;
    auto first = f.queued("first", 192 * 1024), second = f.queued("second");
    auto firstTemp = f.temp(first), secondTemp = f.temp(second);
    Listener listener;
    listener.blockRecheck = true;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    f.queue().recheck(first);
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.queue().recheck(second);
    f.deadline.stage = "cancel before resize";
    auto stop = std::async(std::launch::async, [&] { stopWorkers(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto otherStop = std::async(std::launch::async, [&] {
        joining.signal();
        stopWorkers(f.queue());
    });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000));
    CHECK(otherStop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); otherStop.get();
    CHECK(listener.started == 1);
    CHECK(listener.completed == 0);
    CHECK(listener.moved == 0);
    CHECK(File::getSize(firstTemp) == 192 * 1024);
    CHECK(File::getSize(secondTemp) == 128 * 1024);
    CHECK(f.queue().getPos(first) == 64 * 1024);
    CHECK(f.queue().getPos(second) == 64 * 1024);
    CHECK_FALSE(fs::exists(first));
    CHECK_FALSE(fs::exists(second));
    REQUIRE(f.queue().saveQueueChecked(true));
    stopWorkers(f.queue());
}

TEST_CASE("Terminal queue worker stop drains direct and accepted queued move callbacks", "[queue-workers-stop][shutdown]") {
    Fixture f;
    bool worker = false;
    SECTION("small synchronous move") { worker = false; }
    SECTION("large mover and pending accepted move") { worker = true; }
    auto size = worker ? 16 * 1024 * 1024 : 1024;
    auto source = f.file("source", size), target = f.path("moved/source");
    Listener listener;
    listener.blockMove = true;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    auto move = std::async(std::launch::async, [&] { f.queue().moveFile(source, target); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    string nextSource, nextTarget;
    if(worker) {
        nextSource = f.file("pending", size);
        nextTarget = f.path("moved/pending");
        f.queue().moveFile(nextSource, nextTarget);
    }
    f.deadline.stage = "drain move callback";
    auto stop = std::async(std::launch::async, [&] { stopWorkers(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    auto late = f.file("late-move", size);
    // Without terminal admission this would block on the held Speaker lock.
    // The separate late-work case exercises the missing API without that lock.
    lateMoveWhileDraining(f.queue(), late, f.path("late-dir/data"));
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); move.get();
    stopWorkers(f.queue());
    CHECK(File::getSize(target) == size);
    CHECK_FALSE(fs::exists(source));
    if(worker) {
        CHECK(File::getSize(nextTarget) == size);
        CHECK_FALSE(fs::exists(nextSource));
    }
    CHECK(listener.moved == (worker ? 2 : 1));
    CHECK(fs::exists(late));
    CHECK_FALSE(fs::exists(f.path("late-dir")));
}

TEST_CASE("Cancellation during recheck I/O preserves previously verified segments", "[queue-workers-stop][shutdown]") {
#ifdef _WIN32
    SKIP("The blocked physical-read fixture uses POSIX FIFO and symlink facilities");
#else
    Fixture f;
    auto target = f.queued("blocked"), temp = f.temp(target);
    auto& hashes = *f.fixture.ownedCtx->getHashManager();
    std::unique_lock hashLock(hashes.cs);
    Listener listener;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    f.queue().recheck(target);
    // Synchronize the completed metadata phase with the real queue lock.
    for(unsigned i = 0; i < 5000 && listener.started == 0; ++i) Thread::sleep(1);
    REQUIRE(listener.started == 1);
    { Lock lock(f.queue().cs); }
    auto fifo = f.path("read-pipe");
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    fs::rename(temp, temp + ".payload");
    fs::create_symlink(fifo, temp);
    Semaphore connected, finish;
    auto writer = std::async(std::launch::async, [&] {
        int fd = -1;
        for(unsigned i = 0; i < 5000 && fd == -1; ++i) {
            fd = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
            if(fd == -1) Thread::sleep(1);
        }
        if(fd == -1) throw std::runtime_error("Reader did not open the fixture pipe");
        connected.signal();
        finish.wait();
        ::close(fd);
    });
    ScopedFunctor([&] { finish.signal(); });
    hashLock.unlock();
    f.deadline.stage = "blocked physical read";
    REQUIRE(connected.wait(5000));
    Thread::sleep(20);
    CHECK(f.queue().getPos(target) == 64 * 1024);
    auto stop = std::async(std::launch::async, [&] { stopWorkers(f.queue()); });
    ScopedFunctor([&] { finish.signal(); });
    awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    finish.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); writer.get();
    CHECK(f.queue().getPos(target) == 64 * 1024);
    CHECK(listener.completed == 0);
    CHECK(listener.moved == 0);
    CHECK_FALSE(fs::exists(target));
    CHECK(File::getSize(temp + ".payload") == 128 * 1024);
#endif
}

TEST_CASE("Recheck results do not overwrite progress changed while verification was in flight", "[queue-workers-stop][shutdown]") {
    Fixture f;
    auto target = f.queued("changed"), temp = f.temp(target);
    { File out(temp, File::WRITE, File::OPEN); out.setPos(64 * 1024); out.write("x", 1); }
    auto& hashes = *f.fixture.ownedCtx->getHashManager();
    std::unique_lock hashLock(hashes.cs);
    Listener listener;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    f.queue().recheck(target);
    for(unsigned i = 0; i < 5000 && listener.started == 0; ++i) Thread::sleep(1);
    REQUIRE(listener.started == 1);
    {
        Lock lock(f.queue().cs);
        f.queue().fileQueue.find(target)->addSegment(Segment(64 * 1024, 64 * 1024));
    }
    hashLock.unlock();
    f.deadline.stage = "stale progress result";
    f.queue().rechecker.join();
    CHECK(f.queue().getPos(target) == 128 * 1024);
    CHECK(listener.completed == 0);
    CHECK(listener.moved == 0);
    CHECK(File::getSize(temp) == 128 * 1024);
    CHECK_FALSE(fs::exists(target));
}

TEST_CASE("A failed recheck read retains progress and permits the next queued job", "[queue-workers-stop][shutdown]") {
    bool eof = false;
    SECTION("file vanished before opening") { eof = false; }
    SECTION("file truncated before reading") { eof = true; }
    Fixture f;
    auto first = f.queued("unreadable"), second = f.queued("readable");
    auto firstTemp = f.temp(first);
    auto& hashes = *f.fixture.ownedCtx->getHashManager();
    std::unique_lock hashLock(hashes.cs);
    Listener listener;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    f.queue().recheck(first);
    for(unsigned i = 0; i < 5000 && listener.started == 0; ++i) Thread::sleep(1);
    REQUIRE(listener.started == 1);
    { Lock lock(f.queue().cs); }
    fs::rename(firstTemp, firstTemp + ".payload");
    if(eof) { File empty(firstTemp, File::WRITE, File::CREATE | File::TRUNCATE); }
    f.queue().recheck(second);
    hashLock.unlock();
    f.deadline.stage = "failed read then next queued job";
    f.queue().rechecker.join();
    CHECK(f.queue().getPos(first) == 64 * 1024);
    CHECK(File::getSize(firstTemp + ".payload") == 128 * 1024);
    CHECK(listener.started == 2);
    CHECK(listener.completed == 1);
    CHECK(f.queue().getPos(second) == 128 * 1024);
    CHECK(File::getSize(second) == 128 * 1024);
    CHECK_FALSE(fs::exists(first));
}

TEST_CASE("Ordinary rechecks commit complete results and preserve restartable workers", "[queue-workers-stop][shutdown]") {
    Fixture f;
    bool bad = false;
    SECTION("all pieces verified") { bad = false; }
    SECTION("bad second piece and repeated worker starts") { bad = true; }
    auto target = f.queued("ordinary"), temp = f.temp(target);
    if(bad) { File out(temp, File::WRITE, File::OPEN); out.setPos(64 * 1024); out.write("x", 1); }
    Listener listener;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    const unsigned repeats = bad ? 20 : 1;
    for(unsigned i = 0; i < repeats; ++i) {
        f.queue().recheck(target);
        f.queue().rechecker.join();
        CHECK(listener.started == i + 1);
        CHECK(listener.completed == i + 1);
        CHECK(f.queue().getPos(target) == (bad ? 64 : 128) * 1024);
    }
    if(bad) {
        CHECK(listener.moved == 0);
        CHECK_FALSE(fs::exists(target));
        CHECK(File::getSize(temp) == 128 * 1024);
    } else {
        CHECK(listener.moved == 1);
        CHECK_FALSE(fs::exists(temp));
        CHECK(File::getSize(target) == 128 * 1024);
    }
    for(unsigned i = 0; i < 20; ++i) {
        auto source = f.file("restart-source-" + Util::toString(i), 16 * 1024 * 1024);
        auto destination = f.path("restart-destination-" + Util::toString(i));
        f.queue().moveFile(source, destination);
        f.queue().mover.join();
        CHECK(File::getSize(destination) == 16 * 1024 * 1024);
        CHECK_FALSE(fs::exists(source));
    }
    stopWorkers(f.queue());
    REQUIRE(f.queue().saveQueueChecked(true));
}
