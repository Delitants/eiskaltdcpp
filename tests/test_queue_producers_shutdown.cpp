#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <atomic>
#include <condition_variable>
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
#include "SearchResult.h"
#include "ShareManager.h"
#include "Download.h"
#include "LogManagerListener.h"
#include "ScopedFunctor.h"

using namespace dcpp;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
template<class Manager> void stopProducers(Manager& manager) {
    if constexpr(requires { manager.stopProducersForShutdown(); }) manager.stopProducersForShutdown();
}
template<class Manager> void awaitClosure(Manager& manager) {
    if constexpr(requires { manager.producerShutdownRequested; }) {
        for(unsigned i = 0; i < 5000; ++i) {
            { std::lock_guard lock(manager.producerMutex); if(manager.producerShutdownRequested) return; }
            Thread::sleep(1);
        }
        FAIL("Queue producer admission did not close");
    }
}
template<class Manager> void lateRawCallbacks(Manager& manager) {
    if constexpr(requires { manager.stopProducersForShutdown(); }) {
        manager.on(SearchManagerListener::SR(), nullptr);
        manager.on(ClientManagerListener::UserConnected(), nullptr);
        manager.on(ClientManagerListener::UserDisconnected(), nullptr);
        manager.addDirectory("Pack\\", {}, "");
        manager.addList({}, 0);
        manager.readd("", {});
        PartsInfo parts;
        CHECK_FALSE(manager.handlePartialResult(nullptr, "", {}, {"", "", "", ""}, parts));
        CHECK_THROWS_AS(manager.setFile(nullptr), QueueException);
    }
}
struct Deadline {
    std::atomic<const char*> stage{"setup"};
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(40000)) {
            std::fprintf(stderr, "Queue producers fixture exceeded 40 seconds: %s\n", stage.load());
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }};
    ~Deadline() { finished.signal(); watchdog.join(); }
};
struct Fixture {
    Deadline deadline;
    test::TestContext fixture;
    UserConnectionList sources;
    TimerManagerListener* timerSource;
    Fixture() {
        auto& c = context();
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.hashManager_ = std::make_unique<HashManager>(c);
        c.hashManager_->store.load();
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.cryptoManager_ = std::make_unique<CryptoManager>(c);
        c.debugManager_ = std::make_unique<DebugManager>(c);
        c.searchManager_ = std::make_unique<SearchManager>(c);
        c.connectionManager_ = std::make_unique<ConnectionManager>(c);
        c.queueManager_ = std::make_unique<QueueManager>(c);
        timerSource = c.getTimerManager()->listeners.back();
        c.favoriteManager_ = std::make_unique<FavoriteManager>(c);
        c.downloadManager_ = std::make_unique<DownloadManager>(c);
        c.uploadManager_ = std::make_unique<UploadManager>(c);
        c.finishedManager_ = std::make_unique<FinishedManager>(c);
        c.shareManager_ = std::make_unique<ShareManager>(c);
        auto& s = *c.getSettingsManager();
        s.set(SettingsManager::KEEP_LISTS, true);
        s.set(SettingsManager::KEEP_FINISHED_FILES, true);
        s.set(SettingsManager::SFV_CHECK, false);
        s.set(SettingsManager::LOG_FINISHED_DOWNLOADS, false);
        s.set(SettingsManager::IPFILTER, false);
        s.set(SettingsManager::SEGMENTED_DL, false);
        s.set(SettingsManager::DONT_DL_ALREADY_SHARED, false);
        s.set(SettingsManager::DONT_DL_ALREADY_QUEUED, false);
        s.set(SettingsManager::AUTO_SEARCH_AUTO_MATCH, false);
        s.set(SettingsManager::CHECK_TARGETS_PATHS_ON_START, false);
#ifdef WITH_DHT
        s.set(SettingsManager::USE_DHT, false);
#endif
    }
    ~Fixture() {
        deadline.stage = "cleanup";
        stopProducers(queue());
        context().getDownloadManager()->stopProducersForShutdown();
        context().getUploadManager()->stopProducersForShutdown();
        for(auto* source: sources) {
            if(source->getDownload()) queue().putDownload(source->getDownload(), false);
            delete source;
        }
        BufferedSocket::waitShutdown();
        queue().stopWorkersForShutdown();
        for(auto& entry: queue().directories) delete entry.second;
        queue().directories.clear();
    }
    DCContext& context() { return *fixture.ownedCtx; }
    QueueManager& queue() { return *context().getQueueManager(); }
    string path(const string& name) { return (fixture.tmpDir / name).string(); }
    UserPtr user() { return UserPtr(new User(CID::generate())); }
    HintedUser hinted(const UserPtr& u) { return {u, "adc://fixture"}; }
    UserConnection* source() {
        auto* source = new UserConnection(false, context());
        source->setFlag(UserConnection::FLAG_DOWNLOAD | UserConnection::FLAG_SUPPORTS_ADCGET | UserConnection::FLAG_SUPPORTS_TTHF);
        source->setUser(user()); source->setHubUrl("adc://fixture");
        source->socket = BufferedSocket::getSocket('\n', context());
        auto transport = std::make_unique<Socket>();
        transport->create(Socket::TYPE_TCP, AF_INET);
        source->socket->setSocket(std::move(transport));
        sources.push_back(source);
        return source;
    }
    TTHValue root(const string& name) {
        TigerHash hash; hash.update(name.data(), name.size()); return TTHValue(hash.finalize());
    }
    string queued(UserConnection& source, const string& name, int64_t size = 192 * 1024) {
        string zero(size, '\0');
        TigerTree tree(64 * 1024); tree.update(zero.data(), zero.size()); tree.finalize();
        context().getHashManager()->addTree(tree);
        auto temp = path(name + ".part"), target = path(name + ".bin");
        File(temp, File::WRITE, File::CREATE | File::TRUNCATE).write(zero);
        auto* item = queue().fileQueue.add(target, size, 0, QueueItem::NORMAL, temp, 1700000000, tree.getRoot());
        item->addSegment(Segment(0, 64 * 1024));
        queue().addSource(item, source.getHintedUser(), 0, false);
        return target;
    }
    string listing(const string& name = "listing.xml") {
        auto xml = "<FileListing Version=\"1\" Base=\"/\"><Directory Name=\"Pack\">"
            "<File Name=\"A.bin\" Size=\"1024\" TTH=\"" + root("A").toBase32() + "\"/>"
            "<File Name=\"B.bin\" Size=\"2048\" TTH=\"" + root("B").toBase32() + "\"/>"
            "</Directory></FileListing>";
        File(path(name), File::WRITE, File::CREATE | File::TRUNCATE).write(xml);
        return path(name);
    }
};
struct Listener : QueueManagerListener {
    std::atomic<unsigned> added{0}, status{0}, sources{0}, rechecks{0};
    bool blockAdded = false, blockSources = false;
    Semaphore entered, release;
    void on(Added, QueueItem*) noexcept override {
        if(++added == 1 && blockAdded) { entered.signal(); release.wait(); }
    }
    void on(SourcesUpdated, QueueItem*) noexcept override {
        if(++sources == 1 && blockSources) { entered.signal(); release.wait(); }
    }
    void on(StatusUpdated, QueueItem*) noexcept override { ++status; }
    void on(RecheckStarted, const string&) noexcept override { ++rechecks; }
};
}

TEST_CASE("Queue producer fence rejects late file list directory and manual producer effects", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto user = f.user();
    auto target = f.path("retained.bin");
    f.queue().add(target, 4096, f.root("retained"), f.hinted(user));
    f.queue().setPriority(target, QueueItem::NORMAL);
    REQUIRE(f.queue().saveQueueChecked(true));
    auto restore = f.path("restore.xml");
    File(restore, File::WRITE, File::CREATE | File::TRUNCATE).write(
        "<Downloads><Download Target=\"" + f.path("restored.bin") + "\" Size=\"4096\" TTH=\"" + f.root("restore").toBase32() + "\"/></Downloads>");
    f.queue().setQueueFile(restore);
    f.context().getSettingsManager()->set(SettingsManager::KEEP_LISTS, false);
    Listener listener;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    stopProducers(f.queue());
    f.queue().add(f.path("absent/zero"), 0, f.root("zero"));
    f.queue().add(f.path("absent/hinted-zero"), 0, f.root("zero2"), f.hinted(user));
    f.queue().add(f.path("late.bin"), 1024, f.root("late"));
    f.queue().addList(f.hinted(user), 0);
    f.queue().addDirectory("Pack\\", f.hinted(user), f.path("folder/"));
    f.queue().setPriority(target, QueueItem::HIGHEST);
    f.queue().move(target, f.path("renamed.bin"));
    f.queue().noDeleteFileList("late-protected-list");
    f.queue().loadQueue();
    f.queue().recheck(target);
    stopProducers(f.queue());
    f.queue().stopWorkersForShutdown();
    CHECK_FALSE(fs::exists(f.path("absent")));
    CHECK(f.queue().fileQueue.getSize() == 1);
    auto* retained = f.queue().fileQueue.find(target);
    CHECK(retained != nullptr);
    if(retained) CHECK(retained->getPriority() == QueueItem::NORMAL);
    CHECK(f.queue().directories.empty());
    CHECK(f.queue().protectedFileLists.empty());
    CHECK(listener.added == 0); CHECK(listener.status == 0); CHECK(listener.rechecks == 0);
    CHECK(f.queue().getSize(target) == 4096);
    CHECK(f.queue().getPos(target) == 0);
    f.context().getSettingsManager()->set(SettingsManager::KEEP_LISTS, true);
    REQUIRE(f.queue().saveQueueChecked(true));
    lateRawCallbacks(f.queue());
}

TEST_CASE("Queue producer fence rejects late selection and file setup without resetting progress", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source();
    auto target = f.queued(*source, "select");
    auto temp = f.path("select.part");
    fs::remove(temp);
    stopProducers(f.queue());
    auto* late = f.queue().getDownload(*source, false);
    CHECK(late == nullptr);
    CHECK(f.queue().getPos(target) == 64 * 1024);
    if(late) f.queue().putDownload(late, false);

}

TEST_CASE("Queue producer fence rejects pending file setup before creating its directory", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source();
    auto target = f.queued(*source, "setup");
    auto* item = f.queue().fileQueue.find(target);
    item->resetDownloaded(); item->setTempTarget(f.path("absent/setup.part"));
    auto* d = f.queue().getDownload(*source, false);
    REQUIRE(d);
    stopProducers(f.queue());
    CHECK_THROWS_AS(f.queue().setFile(d), QueueException);
    CHECK(d->getFile() == nullptr);
    CHECK_FALSE(fs::exists(f.path("absent")));
    f.queue().putDownload(d, false);
    CHECK(source->getDownload() == nullptr);
    CHECK(f.queue().getPos(target) == 0);
}

TEST_CASE("Queue producer fence rejects search partial and client callbacks without mutating sources", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source();
    auto target = f.queued(*source, "sources", 2 * 1024 * 1024);
    auto* item = f.queue().fileQueue.find(target);
    auto late = f.user();
    SearchResultPtr result(new SearchResult(late, SearchResult::TYPE_FILE, 1, 1, item->getSize(), "sources.bin", "fixture", "adc://fixture", "127.0.0.1", item->getTTH(), "test"));
    DirectoryListing listing(f.context(), f.hinted(late));
    listing.getRoot()->files.insert(new DirectoryListing::File(listing.getRoot(), "sources.bin", item->getSize(), item->getTTH()));
    Listener listener;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    stopProducers(f.queue());
    f.queue().on(SearchManagerListener::SR(), result);
    CHECK(f.queue().matchListing(listing) == 0);
    QueueItem::PartialSource partial("late", "127.0.0.1:411", "127.0.0.1", "0");
    partial.setPartialInfo({0, 32});
    PartsInfo parts;
    CHECK_FALSE(f.queue().handlePartialResult(late, "adc://fixture", item->getTTH(), partial, parts));
    CHECK(parts.empty());
    CHECK_FALSE(f.queue().handlePartialSearch(item->getTTH(), parts));
    CHECK(parts.empty());
    f.queue().on(ClientManagerListener::UserConnected(), source->getUser());
    f.queue().on(ClientManagerListener::UserDisconnected(), source->getUser());
    f.queue().removeSource(target, source->getUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
    f.queue().readd(target, source->getHintedUser());
    CHECK_FALSE(item->isSource(source->getUser()));
    CHECK_FALSE(item->isSource(late));
    CHECK(listener.status == 0);
    CHECK(listener.sources == 1);
    lateRawCallbacks(f.queue());
}

TEST_CASE("Queue producer fence joins admitted source callbacks and concurrent stops", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto user = f.user();
    Listener listener;
    listener.blockSources = true;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    auto target = f.path("admitted.bin");
    auto producer = std::async(std::launch::async, [&] { f.queue().add(target, 4096, f.root("admitted"), f.hinted(user)); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "admitted queue callback";
    auto stop = std::async(std::launch::async, [&] { stopProducers(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto other = std::async(std::launch::async, [&] { joining.signal(); stopProducers(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000));
    CHECK(other.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); other.get(); producer.get();
    REQUIRE(f.queue().fileQueue.find(target));
    CHECK(f.queue().fileQueue.find(target)->isSource(user));
    CHECK(listener.added == 1); CHECK(listener.sources == 1);
}

TEST_CASE("Queue producer fence lets an admitted nested directory pipeline finish", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto user = f.user();
    auto list = f.listing();
    f.queue().directories.emplace(user, new DirectoryItem(user, "Pack\\", f.path("files/") , QueueItem::NORMAL));
    Listener listener;
    listener.blockAdded = true;
    f.queue().addListener(&listener);
    ScopedFunctor([&] { f.queue().removeListener(&listener); });
    auto producer = std::async(std::launch::async, [&] { f.queue().processList(list, f.hinted(user), QueueItem::FLAG_DIRECTORY_DOWNLOAD); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "nested directory queue adds";
    auto stop = std::async(std::launch::async, [&] { stopProducers(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); producer.get();
    CHECK(listener.added == 2);
    CHECK(f.queue().getSize(f.path("files/Pack/A.bin")) == 1024);
    CHECK(f.queue().getSize(f.path("files/Pack/B.bin")) == 2048);
    CHECK(f.queue().directories.empty());
    CHECK_FALSE(fs::exists(f.path("files")));
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Queue producer fence rejects a stale timer snapshot before search state changes", "[queue-producers-stop][shutdown]") {
    Fixture f;
    f.queue().add(f.path("search.bin"), 4096, f.root("search"));
    f.context().getSettingsManager()->set(SettingsManager::AUTO_SEARCH, true);
    auto& timer = *f.context().getTimerManager();
    struct Blocker : TimerManagerListener {
        Semaphore entered, release;
        void on(Minute, uint64_t) noexcept override { entered.signal(); release.wait(); }
    } blocker;
    timer.removeListeners(); timer.addListener(&blocker); timer.addListener(f.timerSource);
    ScopedFunctor([&] { timer.removeListener(&blocker); });
    auto tick = std::async(std::launch::async, [&] { timer.fire(TimerManagerListener::Minute(), 1000000); });
    ScopedFunctor([&] { blocker.release.signal(); });
    REQUIRE(blocker.entered.wait(5000));
    f.deadline.stage = "stale queue timer snapshot";
    auto stop = std::async(std::launch::async, [&] { stopProducers(f.queue()); });
    ScopedFunctor([&] { blocker.release.signal(); });
    awaitClosure(f.queue());
    blocker.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); tick.get();
    CHECK(f.queue().recent.empty()); CHECK(f.queue().nextSearch == 0);
}

TEST_CASE("Completed filelist cleanup after queue producer fence preserves pending directory intent", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source();
    auto list = f.queue().getListPath(source->getHintedUser());
    auto* item = f.queue().fileQueue.add(list, -1, QueueItem::FLAG_USER_LIST | QueueItem::FLAG_DIRECTORY_DOWNLOAD, QueueItem::NORMAL, {}, 1700000000, {});
    f.queue().addSource(item, source->getHintedUser(), 0, false);
    f.queue().directories.emplace(source->getUser(), new DirectoryItem(source->getUser(), "Pack\\", f.path("files/"), QueueItem::NORMAL));
    auto* d = f.queue().getDownload(*source, false);
    REQUIRE(d); REQUIRE(d->getType() == Transfer::TYPE_FULL_LIST);
    f.queue().setFile(d);
    auto contents = File(f.listing(), File::READ, File::OPEN).read();
    d->setSize(contents.size()); item->setSize(contents.size());
    d->getFile()->write(contents.data(), contents.size());
    d->addPos(contents.size(), contents.size());
    stopProducers(f.queue());
    f.queue().putDownload(d, true);
    CHECK(source->getDownload() == nullptr);
    CHECK(f.queue().fileQueue.getSize() == 0);
    CHECK(f.queue().directories.count(source->getUser()) == 1);
    CHECK_FALSE(fs::exists(f.path("files")));
    CHECK(fs::exists(list + ".xml"));
}

TEST_CASE("Accepted file cleanup and moves remain live after queue producer fence", "[queue-producers-stop][shutdown]") {
    Fixture f;
    bool completed = false;
    SECTION("partial segments") { completed = false; }
    SECTION("completed target move") { completed = true; }
    auto* source = f.source();
    auto target = f.queued(*source, "accepted", completed ? 128 * 1024 : 192 * 1024);
    auto* d = f.queue().getDownload(*source, false);
    REQUIRE(d); REQUIRE(d->getStartPos() == 64 * 1024);
    f.queue().setFile(d);
    string zero(64 * 1024, '\0'); d->getFile()->write(zero.data(), zero.size()); d->addPos(zero.size(), zero.size());
    stopProducers(f.queue());
    f.queue().putDownload(d, completed);
    CHECK(source->getDownload() == nullptr);
    CHECK(f.queue().getPos(target) == 128 * 1024);
    CHECK(f.queue().userQueue.getRunning().empty());
    if(completed) {
        CHECK(File::getSize(target) == 128 * 1024);
        CHECK_FALSE(fs::exists(f.path("accepted.part")));
    } else {
        CHECK(File::getSize(f.path("accepted.part")) == 192 * 1024);
        CHECK_FALSE(fs::exists(target));
    }
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Ordinary queue producer controls and checkpoint remain reusable before closure", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto user = f.user();
    auto original = f.path("ordinary.bin"), moved = f.path("moved.bin");
    f.queue().add(original, 4096, f.root("ordinary"), f.hinted(user));
    f.queue().setPriority(original, QueueItem::HIGH);
    CHECK(f.queue().fileQueue.find(original)->getPriority() == QueueItem::HIGH);
    f.queue().move(original, moved);
    CHECK(f.queue().getSize(moved) == 4096);
    CHECK(f.queue().getSize(original) == -1);
    f.queue().removeSource(moved, user, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
    f.queue().readd(moved, f.hinted(user));
    CHECK(f.queue().fileQueue.find(moved)->isSource(user));
    f.queue().add(f.path("created/zero"), 0, f.root("empty"));
    CHECK(File::getSize(f.path("created/zero")) == 0);
    f.queue().addDirectory("Pack\\", f.hinted(user), f.path("files/"));
    CHECK(f.queue().directories.count(user) == 1);
    CHECK(f.queue().fileQueue.getSize() == 2);
    REQUIRE(f.queue().saveQueueChecked(true));
    stopProducers(f.queue());
    CHECK(f.queue().getSize(moved) == 4096);
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Queue producer fence joins the background filelist matcher log callback", "[queue-producers-stop][shutdown]") {
    Fixture f;
    auto user = f.context().getClientManager()->getUser(CID::generate());
    auto source = f.listing();
    auto list = Util::getListPath() + "fixture." + user->getCID().toBase32() + ".xml";
    File::renameFile(source, list);
    f.queue().add(f.path("match.bin"), 1024, f.root("A"));
    struct LogListener : LogManagerListener {
        Semaphore entered, release;
        std::atomic<unsigned> matched{0};
        void on(Message, time_t, const string& message) noexcept override {
            if(message.find("Matched") != string::npos) {
                ++matched; entered.signal(); release.wait();
            }
        }
    } listener;
    auto& log = *f.context().getLogManager();
    log.addListener(&listener);
    ScopedFunctor([&] { log.removeListener(&listener); });
    f.queue().matchAllListings();
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "background filelist matcher callback";
    auto stop = std::async(std::launch::async, [&] { stopProducers(f.queue()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.queue());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get();
    CHECK(listener.matched == 1);
    CHECK(f.queue().fileQueue.find(f.path("match.bin"))->isSource(user));
    f.queue().matchAllListings();
    CHECK(listener.matched == 1);
}
