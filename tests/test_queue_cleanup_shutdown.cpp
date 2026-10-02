#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <atomic>
#include <future>
#include <ranges>
#define private public
#define protected public
#include "DCContext.h"
#include "QueueManager.h"
#include "HashManager.h"
#include "UserConnection.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "DownloadManager.h"
#include "UploadManager.h"
#include "FavoriteManager.h"
#include "FinishedManager.h"
#include "SearchManager.h"
#include "ShareManager.h"
#include "Download.h"
#include "LogManagerListener.h"
#include "ScopedFunctor.h"

using namespace dcpp;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
template<class Manager> bool stopCleanup(Manager& manager) {
    if constexpr(requires { manager.stopCleanupForShutdown(); }) return manager.stopCleanupForShutdown();
    else { manager.stopProducersForShutdown(); manager.stopWorkersForShutdown(); return true; }
}
template<class Manager> void awaitClosure(Manager& manager) {
    if constexpr(requires { manager.cleanupShutdownRequested; }) {
        for(unsigned i = 0; i < 5000; ++i) {
            { std::lock_guard lock(manager.producerMutex); if(manager.cleanupShutdownRequested) return; }
            Thread::sleep(1);
        }
        FAIL("Queue cleanup admission did not close");
    }
}
template<class Manager> void lateRawCalls(Manager& manager, const string& target, const UserPtr& user) {
    if constexpr(requires { manager.stopCleanupForShutdown(); }) {
        manager.putDownload(nullptr, true);
        manager.remove(target);
        manager.removeSource(target, user, QueueItem::Source::FLAG_REMOVED);
        manager.removeSource(nullptr, QueueItem::Source::FLAG_REMOVED);
        manager.clear();
        manager.moveStuckFile(nullptr);
        manager.rechecked(nullptr);
    }
}
struct Deadline {
    std::atomic<const char*> stage{"setup"};
    Semaphore done;
    std::thread watchdog{[this] {
        if(!done.wait(40000)) {
            std::fprintf(stderr, "Queue cleanup fixture exceeded 40 seconds: %s\n", stage.load());
            std::fflush(stderr); std::_Exit(EXIT_FAILURE);
        }
    }};
    ~Deadline() { done.signal(); watchdog.join(); }
};
struct Fixture {
    Deadline deadline;
    test::TestContext fixture;
    UserConnectionList sources;
    Fixture() {
        auto& c = context();
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.hashManager_ = std::make_unique<HashManager>(c); c.hashManager_->store.load();
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.cryptoManager_ = std::make_unique<CryptoManager>(c);
        c.debugManager_ = std::make_unique<DebugManager>(c);
        c.searchManager_ = std::make_unique<SearchManager>(c);
        c.connectionManager_ = std::make_unique<ConnectionManager>(c);
        c.queueManager_ = std::make_unique<QueueManager>(c);
        c.favoriteManager_ = std::make_unique<FavoriteManager>(c);
        c.downloadManager_ = std::make_unique<DownloadManager>(c);
        c.uploadManager_ = std::make_unique<UploadManager>(c);
        c.finishedManager_ = std::make_unique<FinishedManager>(c);
        c.shareManager_ = std::make_unique<ShareManager>(c);
        auto& s = *c.getSettingsManager();
        s.set(SettingsManager::KEEP_LISTS, true); s.set(SettingsManager::KEEP_FINISHED_FILES, true);
        s.set(SettingsManager::SFV_CHECK, false); s.set(SettingsManager::LOG_FINISHED_DOWNLOADS, false);
        s.set(SettingsManager::IPFILTER, false); s.set(SettingsManager::SEGMENTED_DL, false);
        s.set(SettingsManager::AUTO_SEARCH_AUTO_MATCH, false);
#ifdef WITH_DHT
        s.set(SettingsManager::USE_DHT, false);
#endif
    }
    ~Fixture() {
        deadline.stage = "owned fixture cleanup";
        queue().stopProducersForShutdown(); queue().stopWorkersForShutdown();
        // Wrong-order tests deliberately retain raw resources. Release only the
        // fixture's ownership; a failed product finalizer cannot authorize this.
        for(auto& entry: queue().fileQueue.getQueue()) entry.second->getDownloads().clear();
        queue().userQueue.running.clear();
        for(auto* source: sources) {
            if(auto* d = source->getDownload()) { delete d->getFile(); d->setFile(nullptr); delete d; }
            delete source;
        }
        BufferedSocket::waitShutdown();
    }
    DCContext& context() { return *fixture.ownedCtx; }
    QueueManager& queue() { return *context().getQueueManager(); }
    string path(const string& name) { return (fixture.tmpDir / name).string(); }
    string file(const string& name, int64_t size) {
        auto p = path(name); File(p, File::WRITE, File::CREATE | File::TRUNCATE).setSize(size); return p;
    }
    UserConnection* source() {
        auto* s = new UserConnection(false, context());
        s->setFlag(UserConnection::FLAG_DOWNLOAD | UserConnection::FLAG_SUPPORTS_ADCGET | UserConnection::FLAG_SUPPORTS_TTHF);
        s->setUser(UserPtr(new User(CID::generate()))); s->setHubUrl("adc://fixture");
        s->socket = BufferedSocket::getSocket('\n', context());
        auto transport = std::make_unique<Socket>(); transport->create(Socket::TYPE_TCP, AF_INET);
        s->socket->setSocket(std::move(transport)); sources.push_back(s); return s;
    }
    string queued(UserConnection& source, const string& name, int64_t size = 192 * 1024, bool cached = true) {
        TigerTree tree(64 * 1024); string zero(size, '\0'); tree.update(zero.data(), zero.size()); tree.finalize();
        if(cached) context().getHashManager()->addTree(tree);
        auto temp = file(name + ".part", size), target = path(name + ".bin");
        auto* q = queue().fileQueue.add(target, size, 0, QueueItem::NORMAL, temp, 1700000000, tree.getRoot());
        q->addSegment(Segment(0, 64 * 1024)); queue().addSource(q, source.getHintedUser(), 0, false); return target;
    }
    Download* download(UserConnection& source, size_t bytes) {
        auto* d = queue().getDownload(source, false);
        if(!d) throw std::runtime_error("No fixture queue download");
        queue().setFile(d); d->setStart(GET_TICK());
        string zero(bytes, '\0'); d->getFile()->write(zero.data(), zero.size());
        d->getFile()->flush(); d->addPos(bytes, bytes); return d;
    }
};
struct Listener : QueueManagerListener {
    enum class Block { None, Status, Finished, Sources, Removed, Move, Recheck } block = Block::None;
    std::atomic<unsigned> status{0}, finished{0}, sources{0}, removed{0}, moved{0}, started{0}, checked{0};
    Semaphore entered, release;
    QueueManager* nestedQueue = nullptr;
    string nestedRemove;
    void hold(Block event, unsigned count) { if(event == block && count == 1) { entered.signal(); release.wait(); } }
    void on(StatusUpdated, QueueItem*) noexcept override { hold(Block::Status, ++status); }
    void on(Finished, QueueItem*, const string&, int64_t) noexcept override { hold(Block::Finished, ++finished); }
    void on(SourcesUpdated, QueueItem*) noexcept override { hold(Block::Sources, ++sources); }
    void on(Removed, QueueItem*) noexcept override { hold(Block::Removed, ++removed); }
    void on(FileMoved, const string&) noexcept override {
        auto n = ++moved; hold(Block::Move, n);
        if(n == 1 && nestedQueue) nestedQueue->remove(nestedRemove);
    }
    void on(RecheckStarted, const string&) noexcept override { hold(Block::Recheck, ++started); }
    void on(RecheckAlreadyFinished, const string&) noexcept override { ++checked; }
    void on(RecheckDone, const string&) noexcept override { ++checked; }
};
struct LogListener : LogManagerListener {
    Semaphore entered, release;
    std::atomic<unsigned> messages{0};
    void on(Message, time_t, const string&) noexcept override {
        if(++messages == 1) { entered.signal(); release.wait(); }
    }
};
}

TEST_CASE("Queue cleanup stop joins accepted partial progress and concurrent finalizers", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(); auto target = f.queued(*source, "partial");
    auto* d = f.download(*source, 64 * 1024);
    Listener listener; listener.block = Listener::Block::Status; f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    auto put = std::async(std::launch::async, [&] { f.queue().putDownload(d, false); });
    ScopedFunctor([&] { listener.release.signal(); }); REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "partial callback drain";
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); }); awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    auto other = std::async(std::launch::async, [&] { return stopCleanup(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); }); CHECK(other.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal(); put.get(); REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    CHECK(stop.get()); CHECK(other.get()); CHECK(stopCleanup(f.queue()));
    CHECK(f.queue().getPos(target) == 128 * 1024); CHECK(source->getDownload() == nullptr);
    lateRawCalls(f.queue(), target, source->getUser());
    CHECK(f.queue().getSize(target) == 192 * 1024); CHECK(f.queue().getPos(target) == 128 * 1024);
    CHECK(File::getSize(f.path("partial.part")) == 192 * 1024); CHECK_FALSE(fs::exists(target));
    CHECK(listener.status == 1); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Queue cleanup stop retains accepted completion through finished callbacks", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(); auto target = f.queued(*source, "complete");
    auto* d = f.download(*source, 128 * 1024);
    Listener listener; listener.block = Listener::Block::Finished; f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    auto put = std::async(std::launch::async, [&] { f.queue().putDownload(d, true); });
    ScopedFunctor([&] { listener.release.signal(); }); REQUIRE(listener.entered.wait(5000));
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); }); awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal(); put.get(); CHECK(stop.get());
    CHECK(f.queue().getPos(target) == 192 * 1024); CHECK(File::getSize(target) == 192 * 1024);
    CHECK_FALSE(fs::exists(f.path("complete.part"))); CHECK(listener.finished == 1); CHECK(listener.status == 1);
    CHECK(source->getDownload() == nullptr); CHECK(stopCleanup(f.queue())); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Queue cleanup stop preserves admitted nested clear and source removal", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(); auto first = f.queued(*source, "first");
    auto second = f.queued(*source, "second");
    Listener listener; bool clearing = false;
    SECTION("clear continues removing all admitted targets") { clearing = true; listener.block = Listener::Block::Removed; }
    SECTION("remove source drains actual source callbacks") { listener.block = Listener::Block::Sources; }
    f.queue().addListener(&listener); ScopedFunctor([&] { f.queue().removeListener(&listener); });
    auto mutation = std::async(std::launch::async, [&] {
        if(clearing) f.queue().clear(); else f.queue().removeSource(source->getUser(), QueueItem::Source::FLAG_REMOVED);
    });
    ScopedFunctor([&] { listener.release.signal(); }); REQUIRE(listener.entered.wait(5000));
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); }); awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout); listener.release.signal(); mutation.get(); CHECK(stop.get());
    if(clearing) {
        CHECK(f.queue().getSize(first) == -1); CHECK(f.queue().getSize(second) == -1);
        CHECK(listener.removed == 2); CHECK_FALSE(fs::exists(f.path("first.part"))); CHECK_FALSE(fs::exists(f.path("second.part")));
    } else {
        CHECK(f.queue().getSize(first) == 192 * 1024); CHECK(f.queue().getSize(second) == 192 * 1024);
        CHECK(f.queue().getQueued(source->getUser()) == 0); CHECK(listener.sources == 2);
    }
    CHECK(stopCleanup(f.queue())); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Queue cleanup stop drains accepted mover jobs and nested worker callbacks", "[queue-cleanup-stop][shutdown]") {
    Fixture f; bool large = false;
    SECTION("small synchronous move") { large = false; }
    SECTION("large mover with a queued successor") { large = true; }
    auto size = large ? 16 * 1024 * 1024 : 1024;
    auto* sourceUser = f.source(); auto remove = f.queued(*sourceUser, "nested");
    auto source = f.file("source", size), target = f.path("moved/source");
    Listener listener; listener.block = Listener::Block::Move; listener.nestedQueue = &f.queue(); listener.nestedRemove = remove;
    f.queue().addListener(&listener); ScopedFunctor([&] { f.queue().removeListener(&listener); });
    auto move = std::async(std::launch::async, [&] { f.queue().moveFile(source, target); });
    ScopedFunctor([&] { listener.release.signal(); }); REQUIRE(listener.entered.wait(5000));
    string pending, next;
    if(large) { pending = f.file("pending", size); next = f.path("moved/pending"); f.queue().moveFile(pending, next); }
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); }); awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    auto late = f.file("late", size); f.queue().moveFile(late, f.path("late-dir/file"));
    listener.release.signal(); move.get(); CHECK(stop.get());
    CHECK(File::getSize(target) == size); CHECK_FALSE(fs::exists(source)); CHECK(f.queue().getSize(remove) == -1);
    CHECK_FALSE(fs::exists(f.path("nested.part"))); CHECK(fs::exists(late)); CHECK_FALSE(fs::exists(f.path("late-dir")));
    CHECK(listener.removed == 1);
    if(large) { CHECK(File::getSize(next) == size); CHECK_FALSE(fs::exists(pending)); CHECK(listener.moved == 2); }
    else CHECK(listener.moved == 1);
    CHECK(stopCleanup(f.queue()));
}

TEST_CASE("Queue cleanup outcome retains requested rename failure despite fallback recovery", "[queue-cleanup-stop][shutdown]") {
    Fixture f; bool large = false, fallback = true;
    SECTION("small fallback success") { }
    SECTION("small fallback failure") { fallback = false; }
    SECTION("large fallback success") { large = true; }
    SECTION("large fallback failure") { large = true; fallback = false; }
    auto size = large ? 16 * 1024 * 1024 : 1024;
    fs::create_directories(f.path("storage")); fs::create_directories(f.path("requested/final"));
    auto source = f.file("storage/original.part", size), target = f.path("requested/final");
    if(!fallback) fs::create_directory(f.path("storage/final"));
    f.queue().moveFile(source, target); CHECK_FALSE(stopCleanup(f.queue())); CHECK_FALSE(stopCleanup(f.queue()));
    CHECK(fs::is_directory(target));
    if(fallback) { CHECK(File::getSize(f.path("storage/final")) == size); CHECK_FALSE(fs::exists(source)); }
    else { CHECK(File::getSize(source) == size); CHECK(fs::is_directory(f.path("storage/final"))); }
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Queue cleanup stop joins failed move logging and retains failure", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto source = f.file("data.part", 1024); fs::create_directories(f.path("requested/final"));
    LogListener listener; f.context().getLogManager()->addListener(&listener);
    ScopedFunctor([&] { f.context().getLogManager()->removeListener(&listener); });
    auto move = std::async(std::launch::async, [&] { f.queue().moveFile(source, f.path("requested/final")); });
    ScopedFunctor([&] { listener.release.signal(); }); REQUIRE(listener.entered.wait(5000));
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); }); awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout); listener.release.signal(); move.get();
    CHECK_FALSE(stop.get()); CHECK_FALSE(stopCleanup(f.queue())); CHECK(listener.messages == 1);
    CHECK(File::getSize(f.path("final")) == 1024); CHECK_FALSE(fs::exists(source));
}

TEST_CASE("Queue cleanup outcome rejects completed move admission after wrong order worker stop", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(); auto target = f.queued(*source, "wrong-order");
    auto* d = f.download(*source, 128 * 1024);
    f.queue().stopProducersForShutdown(); f.queue().stopWorkersForShutdown(); f.queue().putDownload(d, true);
    CHECK_FALSE(stopCleanup(f.queue())); CHECK_FALSE(stopCleanup(f.queue()));
    CHECK_FALSE(fs::exists(target)); CHECK(File::getSize(f.path("wrong-order.part")) == 192 * 1024);
    CHECK(source->getDownload() == nullptr);
}

TEST_CASE("Queue cleanup outcome rejects a retained running download without consuming it", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(); auto target = f.queued(*source, "retained");
    auto* d = f.download(*source, 64 * 1024);
    CHECK_FALSE(stopCleanup(f.queue())); CHECK_FALSE(stopCleanup(f.queue()));
    CHECK(source->getDownload() == d); CHECK(f.queue().getPos(target) == 64 * 1024);
    lateRawCalls(f.queue(), target, source->getUser());
    CHECK(source->getDownload() == d); CHECK(f.queue().getSize(target) == 192 * 1024);
    CHECK(File::getSize(f.path("retained.part")) == 192 * 1024);
}

TEST_CASE("Queue cleanup outcome retains failure when the requested parent is a file", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto source = f.file("original", 1024); f.file("not-a-directory", 1);
    CHECK_NOTHROW(f.queue().moveFile(source, f.path("not-a-directory/final")));
    CHECK_FALSE(stopCleanup(f.queue())); CHECK_FALSE(stopCleanup(f.queue()));
    CHECK_FALSE(fs::exists(source)); CHECK(File::getSize(f.path("final")) == 1024);
    CHECK(File::getSize(f.path("not-a-directory")) == 1);
}

TEST_CASE("Queue cleanup stop retains rechecker cancellation and admitted commit boundaries", "[queue-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(); auto target = f.queued(*source, "recheck", 128 * 1024);
    Listener listener; bool cancel = false;
    SECTION("cancel before file resize or commit") { cancel = true; listener.block = Listener::Block::Recheck; }
    SECTION("join an admitted completed file move") { listener.block = Listener::Block::Move; }
    f.queue().addListener(&listener); ScopedFunctor([&] { f.queue().removeListener(&listener); });
    f.queue().recheck(target); ScopedFunctor([&] { listener.release.signal(); }); REQUIRE(listener.entered.wait(5000));
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); }); awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout); listener.release.signal(); CHECK(stop.get());
    if(cancel) {
        CHECK(f.queue().getPos(target) == 64 * 1024); CHECK(File::getSize(f.path("recheck.part")) == 128 * 1024);
        CHECK_FALSE(fs::exists(target)); CHECK(listener.checked == 0); CHECK(listener.moved == 0);
    } else {
        CHECK(f.queue().getPos(target) == 128 * 1024); CHECK(File::getSize(target) == 128 * 1024);
        CHECK_FALSE(fs::exists(f.path("recheck.part"))); CHECK(listener.checked == 1); CHECK(listener.moved == 1);
    }
    CHECK(stopCleanup(f.queue())); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Ordinary queue cleanup remains reusable before terminal closure", "[queue-cleanup-stop][shutdown]") {
    Fixture f;
    for(unsigned i = 0; i < 2; ++i) {
        auto* source = f.source(); auto target = f.queued(*source, "reuse" + std::to_string(i));
        auto* d = f.download(*source, 64 * 1024); f.queue().putDownload(d, false);
        CHECK(f.queue().getPos(target) == 128 * 1024); f.queue().removeSource(target, source->getUser(), QueueItem::Source::FLAG_REMOVED);
        CHECK(f.queue().getQueued(source->getUser()) == 0); f.queue().remove(target); CHECK(f.queue().getSize(target) == -1);
    }
    CHECK(stopCleanup(f.queue())); CHECK(stopCleanup(f.queue())); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Queue cleanup retains an accepted hash storage exception and retires its download", "[queue-cleanup-stop][queue-cleanup-exception][shutdown]") {
    Fixture f; auto* source = f.source(); source->setFlag(UserConnection::FLAG_SUPPORTS_TTHL);
    auto target = f.queued(*source, "tree", 192 * 1024, false);
    auto* d = f.queue().getDownload(*source, true); REQUIRE(d);
    REQUIRE(d->getType() == Transfer::TYPE_TREE);
    TigerTree tree(64 * 1024); string bytes(192 * 1024, '\0'); tree.update(bytes.data(), bytes.size()); tree.finalize();
    d->getTigerTree() = tree; d->setTreeValid(true);
    CHECK(tree.getRoot() == d->getTTH());
    SECTION("truncated cursor") {
        File(HashManager::HashStore::getDataFile(), File::WRITE, File::OPEN).setSize(0);
        CHECK_THROWS_AS(f.queue().putDownload(d, true), HashException);
    }
#ifndef _WIN32
    SECTION("real data open refusal") {
        auto path = HashManager::HashStore::getDataFile();
        auto original = fs::status(path).permissions();
        ScopedFunctor([&] { fs::permissions(path, original); });
        fs::permissions(path, fs::perms::owner_read);
        CHECK_THROWS_AS(f.queue().putDownload(d, true), FileException);
    }
#endif
    CHECK(source->getDownload() == nullptr);
    { Lock lock(f.queue().cs);
      CHECK(f.queue().fileQueue.find(target)->getDownloads().empty());
      CHECK(f.queue().userQueue.running.empty());
      // Baseline cleanup only: prevent a regression from leaving a destroyed
      // Download in this disposable fixture's destructor.
      f.queue().fileQueue.find(target)->getDownloads().clear(); f.queue().userQueue.running.clear(); }
    CHECK(f.queue().getPos(target) == 64 * 1024);
    CHECK_FALSE(stopCleanup(f.queue())); CHECK_FALSE(stopCleanup(f.queue()));
    CHECK(File::getSize(f.path("tree.part")) == 192 * 1024);
    CHECK_FALSE(fs::exists(target));
}
