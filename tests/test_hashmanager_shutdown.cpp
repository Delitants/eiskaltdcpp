#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <ranges>
#define private public
#include "DCContext.h"
#include "HashManager.h"
#undef private
#include "TestContext.h"
#include "File.h"
#include "ScopedFunctor.h"

using namespace dcpp;
using namespace std::chrono_literals;

namespace {
template<class Manager> void stopHash(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) manager.stopForShutdown();
    else manager.shutdown();
}
template<class Manager> void lateSecond(Manager& manager) {
    if constexpr(requires { manager.stopForShutdown(); })
        manager.on(TimerManagerListener::Second(), 0);
}

struct Deadline {
    std::atomic<const char*> stage{"setup"};
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(30000)) {
            std::fprintf(stderr, "Hash stop fixture exceeded 30 seconds: %s\n", stage.load());
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
        hashes().store.load();
        c.getSettingsManager()->set(SettingsManager::MAX_HASH_SPEED, 64);
    }
    ~Fixture() { deadline.stage = "fixture cleanup"; stopHash(hashes()); }
    HashManager& hashes() { return *fixture.ownedCtx->getHashManager(); }
    string file(const char* name, int64_t size = 0) {
        auto path = (fixture.tmpDir / name).string();
        File out(path, File::WRITE, File::CREATE | File::TRUNCATE);
        out.setSize(size);
        return path;
    }
    uint32_t stamp(const string& path) {
        File input(path, File::READ, File::OPEN);
        return input.getLastModified();
    }
    void start() {
        hashes().startup();
        for(unsigned i = 0; i < 5000 && !hashes().isHashingPaused(); ++i) Thread::sleep(1);
        REQUIRE(hashes().isHashingPaused());
        hashes().resumeHashing();
    }
    bool idle() {
        string current; uint64_t bytes; size_t files;
        hashes().getStats(current, bytes, files);
        return current.empty() && bytes == 0 && files == 0;
    }
};

struct HashListener : HashManagerListener {
    std::atomic<unsigned> calls{0};
    bool block = false;
    Semaphore entered, release;
    void on(TTHDone, const string&, const TTHValue&) noexcept override {
        ++calls;
        if(block) { entered.signal(); release.wait(); }
    }
};

TigerTree emptyTree() {
    TigerTree tree(HashManager::MIN_BLOCK_SIZE);
    tree.finalize();
    return tree;
}
}

TEST_CASE("Terminal hash stop clears queued work and denies all late producers", "[hash-stop][shutdown]") {
    Fixture f;
    auto path = f.file("queued.bin", 1);
    CHECK_FALSE(f.hashes().checkTTH(path, 1, f.stamp(path)));
    f.deadline.stage = "queued stop";
    stopHash(f.hashes());
    CHECK(f.idle());
    CHECK_FALSE(f.hashes().checkTTH(path, 1, f.stamp(path)));
    CHECK_THROWS_AS(f.hashes().getTTH(path, 1), HashException);
    f.hashes().addTree(path, f.stamp(path), emptyTree());
    f.hashes().addTree(emptyTree());
    f.hashes().rebuild();
    f.hashes().resumeHashing();
    f.hashes().pauseHashing();
    f.hashes().startup();
    CHECK(f.idle());
    CHECK_FALSE(f.hashes().isHashingPaused());
    CHECK(f.hashes().getFileTTHif(path) == nullptr);
    TigerTree tree;
    CHECK_FALSE(f.hashes().getTree(TTHValue("LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ"), tree));
}

TEST_CASE("Terminal hash cancellation never publishes a partially read file", "[hash-stop][shutdown]") {
    Fixture f;
    bool fast = false;
    SECTION("slow reads") { fast = false; }
    SECTION("fast mapped or overlapped reads") { fast = true; }
    f.fixture.ownedCtx->getSettingsManager()->set(SettingsManager::FAST_HASH, fast);
    constexpr int64_t size = 64 * 1024 * 1024;
    auto path = f.file("cancel.bin", size);
    HashListener listener;
    f.hashes().addListener(&listener);
    ScopedFunctor([&] { f.hashes().removeListener(&listener); });
    CHECK_FALSE(f.hashes().checkTTH(path, size, f.stamp(path)));
    f.start();
    f.deadline.stage = "wait partial read";
    string current; uint64_t left = size; size_t files = 0;
    for(unsigned i = 0; i < 5000; ++i) {
        f.hashes().getStats(current, left, files);
        if(current == path && left < size && left > 0) break;
        Thread::sleep(1);
    }
    REQUIRE(current == path);
    REQUIRE(left > 0);
    REQUIRE(left < size);
    f.hashes().pauseHashing();
    f.deadline.stage = "cancel partial read";
    stopHash(f.hashes());
    CHECK(listener.calls == 0);
    CHECK(f.hashes().getFileTTHif(path) == nullptr);
    CHECK(f.idle());
    CHECK(File::getSize(path) == size);
}

TEST_CASE("Terminal hash stop drains admitted direct and worker callbacks", "[hash-stop][shutdown]") {
    Fixture f;
    bool worker = false;
    SECTION("direct publication") { worker = false; }
    SECTION("worker publication") { worker = true; }
    auto path = f.file("complete.bin");
    auto stamp = f.stamp(path);
    HashListener listener;
    listener.block = true;
    f.hashes().addListener(&listener);
    ScopedFunctor([&] { f.hashes().removeListener(&listener); });
    std::future<void> publish;
    if(worker) {
        CHECK_FALSE(f.hashes().checkTTH(path, 0, stamp));
        f.start();
    } else {
        publish = std::async(std::launch::async, [&] { f.hashes().addTree(path, stamp, emptyTree()); });
    }
    ScopedFunctor([&] { listener.release.signal(); });
    f.deadline.stage = "wait publication";
    REQUIRE(listener.entered.wait(5000));
    Semaphore stopping;
    auto stop = std::async(std::launch::async, [&] { stopping.signal(); stopHash(f.hashes()); });
    // Release before future destruction even if an assertion aborts the case.
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(stopping.wait(5000));
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto otherStop = std::async(std::launch::async, [&] { joining.signal(); stopHash(f.hashes()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000));
    CHECK(otherStop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    f.deadline.stage = "join publication";
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get();
    REQUIRE(otherStop.wait_for(5000ms) == std::future_status::ready);
    otherStop.get();
    if(publish.valid()) publish.get();
    CHECK(listener.calls == 1);
    CHECK(f.idle());
    REQUIRE(f.hashes().getFileTTHif(path));
    CHECK(f.hashes().getTTH(path, 0).toBase32() == "LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ");
    CHECK(f.hashes().checkTTH(path, 0, stamp));
    CHECK_FALSE(f.hashes().checkTTH(path, 1, stamp));
    REQUIRE(f.hashes().getFileTTHif(path));
    TigerTree tree;
    CHECK(f.hashes().getTree(f.hashes().getTTH(path, 0), tree));
    auto late = f.file("late.bin");
    f.hashes().addTree(late, f.stamp(late), emptyTree());
    CHECK(f.hashes().getFileTTHif(late) == nullptr);
    CHECK(listener.calls == 1);
    stopHash(f.hashes());
}

TEST_CASE("A truncated hash read rejects partial evidence and clears worker status", "[hash-stop][shutdown]") {
    Fixture f;
    f.fixture.ownedCtx->getSettingsManager()->set(SettingsManager::FAST_HASH, false);
    constexpr int64_t size = 64 * 1024 * 1024;
    auto path = f.file("truncated.bin", size);
    HashListener listener;
    f.hashes().addListener(&listener);
    ScopedFunctor([&] { f.hashes().removeListener(&listener); });
    CHECK_FALSE(f.hashes().checkTTH(path, size, f.stamp(path)));
    f.start();
    string current; uint64_t left = size; size_t files;
    f.deadline.stage = "truncate during hashing";
    for(unsigned i = 0; i < 5000; ++i) {
        f.hashes().getStats(current, left, files);
        if(current == path && left < size && left > 0) break;
        Thread::sleep(1);
    }
    REQUIRE(current == path);
    REQUIRE(left > 0);
    REQUIRE(left < size);
    f.hashes().pauseHashing();
    { File out(path, File::WRITE, File::OPEN); out.setSize(0); }
    f.hashes().resumeHashing();
    for(unsigned i = 0; i < 5000 && !f.idle(); ++i) Thread::sleep(1);
    CHECK(f.idle());
    CHECK(listener.calls == 0);
    CHECK(f.hashes().getFileTTHif(path) == nullptr);
    CHECK(File::getSize(path) == 0);
}

TEST_CASE("Ordinary hashing completes before the terminal barrier on both paths", "[hash-stop][shutdown]") {
    Fixture f;
    bool fast = false;
    SECTION("slow reads") { fast = false; }
    SECTION("fast mapped or overlapped reads") { fast = true; }
    f.fixture.ownedCtx->getSettingsManager()->set(SettingsManager::FAST_HASH, fast);
    constexpr int64_t size = 16 * 1024 * 1024;
    auto path = f.file("finished.bin", size);
    auto stamp = f.stamp(path);
    HashListener listener;
    f.hashes().addListener(&listener);
    ScopedFunctor([&] { f.hashes().removeListener(&listener); });
    CHECK_FALSE(f.hashes().checkTTH(path, size, stamp));
    f.start();
    f.deadline.stage = "ordinary hashing completion";
    for(unsigned i = 0; i < 5000 && !f.idle(); ++i) Thread::sleep(1);
    REQUIRE(f.idle());
    REQUIRE(listener.calls == 1);
    REQUIRE(f.hashes().getFileTTHif(path));
    auto root = f.hashes().getTTH(path, size);
    stopHash(f.hashes());
    TigerTree tree;
    REQUIRE(f.hashes().getTree(root, tree));
    CHECK(tree.getFileSize() == size);
    CHECK(f.hashes().checkTTH(path, size, stamp));
    CHECK(f.hashes().getBlockSize(root) == HashManager::MIN_BLOCK_SIZE);
    CHECK(listener.calls == 1);
}

TEST_CASE("Terminal hash stop drains timer snapshots without persisting or late dependency access", "[hash-stop][shutdown]") {
    Fixture f;
    auto& timer = *f.fixture.ownedCtx->getTimerManager();
    struct Blocker : TimerManagerListener {
        Semaphore entered, release;
        void on(Minute, uint64_t) noexcept override { entered.signal(); release.wait(); }
    } blocker;
    timer.removeListener(&f.hashes());
    timer.addListener(&blocker);
    timer.addListener(&f.hashes());
    ScopedFunctor([&] { timer.removeListener(&blocker); });
    auto path = f.file("cached.bin");
    f.hashes().addTree(path, f.stamp(path), emptyTree());
    auto tick = std::async(std::launch::async, [&] { timer.fire(TimerManagerListener::Minute(), 0); });
    ScopedFunctor([&] { blocker.release.signal(); });
    REQUIRE(blocker.entered.wait(5000));
    Semaphore stopping;
    auto stop = std::async(std::launch::async, [&] { stopping.signal(); stopHash(f.hashes()); });
    ScopedFunctor([&] { blocker.release.signal(); });
    REQUIRE(stopping.wait(5000));
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    blocker.release.signal();
    f.deadline.stage = "join timer";
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); tick.get();
    f.hashes().on(TimerManagerListener::Minute(), 0);
    // There is deliberately no ShareManager in this fixture.
    lateSecond(f.hashes());
    CHECK_FALSE(std::filesystem::exists(f.fixture.tmpDir / "HashIndex.xml"));
    REQUIRE(f.hashes().getFileTTHif(path));
}
