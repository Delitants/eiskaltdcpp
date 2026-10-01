#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <atomic>
#include <future>
#include <ranges>
#define private public
#define protected public
#include "DCContext.h"
#include "DownloadManager.h"
#include "UploadManager.h"
#include "QueueManager.h"
#include "ConnectionManager.h"
#include "UserConnection.h"
#include "HashManager.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "FinishedManager.h"
#include "SearchManager.h"
#include "ShareManager.h"
#include "Download.h"
#include "Upload.h"
#include "Streams.h"
#include "MerkleCheckOutputStream.h"
#include "ScopedFunctor.h"

using namespace dcpp;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
template<class Manager> bool stopCleanup(Manager& manager) {
    if constexpr(requires { manager.stopCleanupForShutdown(); }) return manager.stopCleanupForShutdown();
    else return true;
}
template<class Manager> void awaitClosure(Manager& manager) {
    if constexpr(requires { manager.cleanupShutdownRequested; }) {
        for(unsigned i = 0; i < 5000; ++i) {
            { std::lock_guard lock(manager.producerMutex); if(manager.cleanupShutdownRequested) return; }
            Thread::sleep(1);
        }
        FAIL("Accepted cleanup admission did not close");
    }
}
template<class Manager> void lateDownloadCalls(Manager& manager) {
    if constexpr(requires { manager.stopCleanupForShutdown(); }) {
        manager.on(UserConnectionListener::Data(), nullptr, nullptr, 0);
        manager.on(UserConnectionListener::Failed(), nullptr, "late");
        manager.on(UserConnectionListener::ProtocolError(), nullptr, "late");
        manager.on(UserConnectionListener::MaxedOut(), nullptr);
        manager.on(UserConnectionListener::FileNotAvailable(), nullptr);
        manager.on(AdcCommand::STA(), nullptr, AdcCommand(AdcCommand::CMD_STA));
        manager.endData(nullptr);
        manager.failDownload(nullptr, "late");
        manager.removeDownload(nullptr);
        manager.removeConnection(nullptr);
        manager.checkDownloads(nullptr);
    }
}
template<class Manager> void lateUploadCalls(Manager& manager) {
    if constexpr(requires { manager.stopCleanupForShutdown(); }) {
        manager.on(UserConnectionListener::BytesSent(), nullptr, 0, 0);
        manager.on(UserConnectionListener::Failed(), nullptr, "late");
        manager.on(UserConnectionListener::TransmitDone(), nullptr);
        manager.removeUpload(nullptr);
        manager.removeConnection(nullptr);
    }
}
struct Deadline {
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(40000)) {
            std::fprintf(stderr, "Transfer cleanup fixture exceeded 40 seconds\n");
            std::fflush(stderr); std::_Exit(EXIT_FAILURE);
        }
    }};
    ~Deadline() { finished.signal(); watchdog.join(); }
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
        s.set(SettingsManager::KEEP_FINISHED_FILES, true);
        s.set(SettingsManager::KEEP_LISTS, true);
        s.set(SettingsManager::SFV_CHECK, false);
        s.set(SettingsManager::LOG_DOWNLOADS, false);
        s.set(SettingsManager::LOG_UPLOADS, false);
        s.set(SettingsManager::LOG_FINISHED_DOWNLOADS, false);
        s.set(SettingsManager::IPFILTER, false);
        s.set(SettingsManager::REQUIRE_TLS, 0);
        s.set(SettingsManager::SLOTS, 2);
        s.set(SettingsManager::SEGMENTED_DL, false);
#ifdef WITH_DHT
        s.set(SettingsManager::USE_DHT, false);
#endif
    }
    ~Fixture() {
        download().stopProducersForShutdown(); upload().stopProducersForShutdown();
        for(auto* source: sources) {
            if(source->isSet(UserConnection::FLAG_DOWNLOAD)) download().onFailed(source, "fixture cleanup");
            else upload().on(UserConnectionListener::Failed(), source, "fixture cleanup");
            delete source;
        }
        BufferedSocket::waitShutdown();
        queue().stopProducersForShutdown(); queue().stopWorkersForShutdown();
    }
    DCContext& context() { return *fixture.ownedCtx; }
    DownloadManager& download() { return *context().getDownloadManager(); }
    UploadManager& upload() { return *context().getUploadManager(); }
    QueueManager& queue() { return *context().getQueueManager(); }
    string path(const string& name) { return (fixture.tmpDir / name).string(); }
    UserConnection* source(bool downloading, bool registered = false) {
        auto* source = registered ? context().getConnectionManager()->getConnection(false, false) : new UserConnection(false, context());
        source->setFlag(downloading ? UserConnection::FLAG_DOWNLOAD : UserConnection::FLAG_UPLOAD);
        source->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET | UserConnection::FLAG_SUPPORTS_TTHF | UserConnection::FLAG_SUPPORTS_MINISLOTS);
        source->setUser(UserPtr(new User(CID::generate()))); source->setHubUrl("adc://fixture");
        source->socket = BufferedSocket::getSocket('\n', context());
        auto transport = std::make_unique<Socket>(); transport->create(Socket::TYPE_TCP, AF_INET);
        source->socket->setSocket(std::move(transport));
        if(!registered) sources.push_back(source);
        return source;
    }
    string queued(UserConnection& source, const string& name, int64_t size = 192 * 1024) {
        string zero(size, '\0'); TigerTree tree(64 * 1024); tree.update(zero.data(), zero.size()); tree.finalize();
        context().getHashManager()->addTree(tree);
        auto temp = path(name + ".part"), target = path(name + ".bin");
        File(temp, File::WRITE, File::CREATE | File::TRUNCATE).write(zero);
        auto* item = queue().fileQueue.add(target, size, 0, QueueItem::NORMAL, temp, 1700000000, tree.getRoot());
        item->addSegment(Segment(0, 64 * 1024)); queue().addSource(item, source.getHintedUser(), 0, false);
        return target;
    }
    Download* start(UserConnection& source) {
        download().addConnection(&source);
        auto* d = source.getDownload(); if(!d) throw std::runtime_error("Fixture download not selected");
        download().startData(&source, d->getStartPos(), d->getSize(), false);
        return d;
    }
    void bytes(UserConnection& source, size_t count = 64 * 1024) {
        string bytes(count, '\0');
        download().on(UserConnectionListener::Data(), &source, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    }
    bool prepare(UserConnection& source) {
        return upload().prepareFile(source, Transfer::names[Transfer::TYPE_PARTIAL_LIST], "/", 0, -1, false);
    }
};
struct DownloadListener : DownloadManagerListener {
    bool blockComplete = false;
    Semaphore entered, release;
    void on(Failed, Download*, const string&) noexcept override { if(!blockComplete) { entered.signal(); release.wait(); } }
    void on(Complete, Download*) noexcept override { if(blockComplete) { entered.signal(); release.wait(); } }
};
struct UploadListener : UploadManagerListener {
    bool blockComplete = false;
    Semaphore entered, release;
    void on(Failed, Upload*, const string&) noexcept override { if(!blockComplete) { entered.signal(); release.wait(); } }
    void on(Complete, Upload*) noexcept override { if(blockComplete) { entered.signal(); release.wait(); } }
};
}

TEST_CASE("Download cleanup stop joins accepted failure queue tails and concurrent finalizers", "[transfer-cleanup-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    auto target = f.queued(*source, "partial"); f.start(*source); f.bytes(*source);
    f.download().stopProducersForShutdown(); f.queue().stopProducersForShutdown();
    DownloadListener listener; f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    auto cleanup = std::async(std::launch::async, [&] { f.download().onFailed(source, "normal quit"); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    CHECK(f.download().getDownloadCount() == 0); CHECK(f.queue().getPos(target) == 64 * 1024);
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.download()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.download());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto other = std::async(std::launch::async, [&] { joining.signal(); return stopCleanup(f.download()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000)); CHECK(other.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal(); cleanup.get();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    CHECK(stop.get()); CHECK(other.get()); CHECK(stopCleanup(f.download()));
    CHECK(source->getDownload() == nullptr); CHECK(f.queue().getPos(target) == 128 * 1024);
    CHECK(f.queue().userQueue.getRunning().empty()); CHECK(File::getSize(f.path("partial.part")) == 192 * 1024);
    lateDownloadCalls(f.download()); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Download cleanup stop preserves admitted nested completion and target move", "[transfer-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(true);
    auto target = f.queued(*source, "complete", 128 * 1024); f.start(*source);
    f.download().stopProducersForShutdown(); f.queue().stopProducersForShutdown();
    DownloadListener listener; listener.blockComplete = true; f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    auto cleanup = std::async(std::launch::async, [&] { f.bytes(*source); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    CHECK(f.download().getDownloadCount() == 0); CHECK_FALSE(fs::exists(target));
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.download()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.download()); CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal(); cleanup.get();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready); CHECK(stop.get());
    CHECK(source->getDownload() == nullptr); CHECK(File::getSize(target) == 128 * 1024);
    CHECK_FALSE(fs::exists(f.path("complete.part"))); CHECK(f.queue().getPos(target) == 128 * 1024);
    lateDownloadCalls(f.download()); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Download cleanup stop retains flush failure and only prior verified progress", "[transfer-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(true);
    auto target = f.queued(*source, "flush"); auto* d = f.start(*source);
    delete d->getFile();
    d->setFile(new BufferedOutputStream<true>(new File(f.path("flush.part"), File::READ, File::OPEN), 128 * 1024));
    bool complete = false;
    SECTION("disconnect flush") { complete = false; }
    SECTION("endData flush") { complete = true; }
    f.bytes(*source);
    CHECK(d->getPos() == 64 * 1024);
    f.download().stopProducersForShutdown(); f.queue().stopProducersForShutdown();
    if(complete) f.download().endData(source); else f.download().onFailed(source, "normal quit");
    CHECK(source->getDownload() == nullptr); CHECK(f.download().getDownloadCount() == 0);
    CHECK(f.queue().getPos(target) == 64 * 1024); CHECK_FALSE(fs::exists(target));
    CHECK_FALSE(stopCleanup(f.download())); CHECK_FALSE(stopCleanup(f.download()));
    CHECK(File::getSize(f.path("flush.part")) == 192 * 1024);
    lateDownloadCalls(f.download()); REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Upload cleanup stop joins accepted failure and releases normal and extra slots", "[transfer-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(false);
    bool extra = false;
    UserConnection* slotOwner = nullptr;
    SECTION("normal slot") { extra = false; }
    SECTION("extra slot") {
        extra = true;
        f.context().getSettingsManager()->set(SettingsManager::SLOTS, 1);
        f.context().getSettingsManager()->set(SettingsManager::ALLOW_SIM_UPLOADS, true);
        slotOwner = f.source(false); REQUIRE(f.prepare(*slotOwner));
    }
    REQUIRE(f.prepare(*source)); source->setState(UserConnection::STATE_RUNNING);
    CHECK(source->isSet(UserConnection::FLAG_HASEXTRASLOT) == extra);
    if(slotOwner) f.upload().on(UserConnectionListener::Failed(), slotOwner, "release occupied normal slot");
    f.upload().stopProducersForShutdown();
    UploadListener listener; f.upload().addListener(&listener);
    ScopedFunctor([&] { f.upload().removeListener(&listener); });
    auto cleanup = std::async(std::launch::async, [&] { f.upload().on(UserConnectionListener::Failed(), source, "normal quit"); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.upload()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.upload()); CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto other = std::async(std::launch::async, [&] { joining.signal(); return stopCleanup(f.upload()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000)); CHECK(other.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal(); cleanup.get();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready); CHECK(stop.get()); CHECK(other.get());
    CHECK(source->getUpload() == nullptr); CHECK(f.upload().getUploadCount() == 0);
    CHECK(f.upload().running == 0); CHECK(f.upload().getExtra() == 0);
    CHECK_FALSE(source->isSet(UserConnection::FLAG_HASSLOT)); CHECK_FALSE(source->isSet(UserConnection::FLAG_HASEXTRASLOT));
    lateUploadCalls(f.upload()); CHECK(stopCleanup(f.upload()));
}

TEST_CASE("Upload cleanup stop drains completion tail but does not mistake held slot for success", "[transfer-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(false); REQUIRE(f.prepare(*source));
    source->setState(UserConnection::STATE_RUNNING); f.upload().stopProducersForShutdown();
    UploadListener listener; listener.blockComplete = true; f.upload().addListener(&listener);
    ScopedFunctor([&] { f.upload().removeListener(&listener); });
    auto cleanup = std::async(std::launch::async, [&] { f.upload().on(UserConnectionListener::TransmitDone(), source); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    auto stop = std::async(std::launch::async, [&] { return stopCleanup(f.upload()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.upload()); CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal(); cleanup.get();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready); CHECK_FALSE(stop.get());
    CHECK(source->getUpload() == nullptr); CHECK(f.upload().getUploadCount() == 0);
    CHECK(source->isSet(UserConnection::FLAG_HASSLOT)); CHECK(f.upload().running == 1);
    CHECK_FALSE(stopCleanup(f.upload()));
    // Deliberately violated socket-drain prerequisite: fixture owns the retained
    // slot and restores it manually; a production owner must not destroy on false.
    source->unsetFlag(UserConnection::FLAG_HASSLOT); f.upload().running = 0;
    lateUploadCalls(f.upload());
}

TEST_CASE("Download cleanup outcome rejects a retained idle peer", "[transfer-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(true); f.download().addConnection(source);
    REQUIRE(source->getState() == UserConnection::STATE_IDLE);
    CHECK(f.download().getDownloadCount() == 0);
    CHECK_FALSE(stopCleanup(f.download())); CHECK_FALSE(stopCleanup(f.download()));
    CHECK(f.download().idlers.size() == 1);
    // Early finalization is failure, not permission; dispose owned fixture intent.
    f.download().idlers.clear(); source->removeListeners();
    lateDownloadCalls(f.download());
}

TEST_CASE("Real peer failure retains download cleanup through CM retirement and socket drain", "[transfer-cleanup-stop][shutdown]") {
    Fixture f; auto* source = f.source(true, true);
    auto target = f.queued(*source, "peer"); f.start(*source); f.bytes(*source);
    f.download().stopProducersForShutdown(); f.queue().stopProducersForShutdown();
    DownloadListener listener; f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    auto cleanup = std::async(std::launch::async, [&] { source->on(BufferedSocketListener::Failed(), "normal quit"); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.context().getConnectionManager()->stopForShutdown();
    CHECK(f.context().getConnectionManager()->userConnections.empty());
    CHECK(f.download().getDownloadCount() == 0); CHECK(f.queue().getPos(target) == 64 * 1024);
    CHECK_FALSE(BufferedSocket::waitShutdown(30));
    listener.release.signal(); cleanup.get();
    REQUIRE(BufferedSocket::waitShutdown(5000));
    CHECK(stopCleanup(f.download())); CHECK(f.queue().getPos(target) == 128 * 1024);
    CHECK(File::getSize(f.path("peer.part")) == 192 * 1024);
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Ordinary accepted transfer cleanup remains reusable before terminal closure", "[transfer-cleanup-stop][shutdown]") {
    Fixture f;
    for(unsigned i = 0; i < 2; ++i) {
        auto* source = f.source(true); auto target = f.queued(*source, "ordinary" + std::to_string(i));
        f.start(*source); f.bytes(*source); f.download().onFailed(source, "ordinary disconnect");
        CHECK(f.download().getDownloadCount() == 0); CHECK(f.queue().getPos(target) == 128 * 1024);
        auto* upload = f.source(false); REQUIRE(f.prepare(*upload));
        upload->setState(UserConnection::STATE_RUNNING);
        f.upload().on(UserConnectionListener::BytesSent(), upload, 10, 12);
        CHECK(upload->getUpload()->getPos() == 10); CHECK(upload->getUpload()->getActual() == 12);
        f.upload().on(UserConnectionListener::TransmitDone(), upload);
        CHECK(f.upload().running == 1);
        f.upload().on(UserConnectionListener::Failed(), upload, "ordinary disconnect");
        CHECK(f.upload().running == 0); CHECK(upload->getUpload() == nullptr);
    }
    REQUIRE(stopCleanup(f.download())); REQUIRE(stopCleanup(f.upload()));
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Interrupted Merkle flush retains complete leaves and can resume to a short final block", "[transfer-cleanup-stop][merkle-flush][shutdown]") {
    test::TestContext fixture;
    const string contents(192 * 1024 + 123, 'x');
    TigerTree tree(64 * 1024); tree.update(contents.data(), contents.size()); tree.finalize();
    auto path = (fixture.tmpDir / "merkle.part").string();
    auto* file = new File(path, File::WRITE, File::CREATE | File::TRUNCATE);
    file->write(contents.data(), 64 * 1024);
    MerkleCheckOutputStream<TigerTree, true> stream(tree, new BufferedOutputStream<true>(file), 64 * 1024);
    size_t count = 0;
    SECTION("partial hash block") { count = 64 * 1024 + 123; }
    SECTION("complete hash block before EOF") { count = 64 * 1024; }
    SECTION("short EOF block") { count = 128 * 1024 + 123; }
    REQUIRE_NOTHROW(stream.write(contents.data() + 64 * 1024, count));
    REQUIRE_NOTHROW(stream.flush()); REQUIRE_NOTHROW(stream.flush());
    CHECK(File::getSize(path) == static_cast<int64_t>(64 * 1024 + count));
    CHECK(stream.verifiedBytes() == (count == 128 * 1024 + 123 ? 192 * 1024 + 123 : 128 * 1024));
    if(count != 128 * 1024 + 123) {
        const auto position = 64 * 1024 + count;
        REQUIRE_NOTHROW(stream.write(contents.data() + position, contents.size() - position));
        REQUIRE_NOTHROW(stream.flush()); REQUIRE_NOTHROW(stream.flush());
    }
    CHECK(stream.verifiedBytes() == 192 * 1024 + 123);
    CHECK(File(path, File::READ, File::OPEN).read() == contents);
    CHECK_THROWS_AS(stream.write("z", 1), FileException);
}

TEST_CASE("Merkle flush still rejects corrupt complete and short final blocks", "[transfer-cleanup-stop][merkle-flush][shutdown]") {
    test::TestContext fixture;
    const string contents(192 * 1024 + 123, 'x');
    TigerTree tree(64 * 1024); tree.update(contents.data(), contents.size()); tree.finalize();
    auto path = (fixture.tmpDir / "bad-merkle.part").string();
    auto* file = new File(path, File::WRITE, File::CREATE | File::TRUNCATE);
    file->write(contents.data(), 64 * 1024);
    MerkleCheckOutputStream<TigerTree, true> stream(tree, new BufferedOutputStream<true>(file), 64 * 1024);
    SECTION("bad complete leaf") {
        string bad(64 * 1024, 'y');
        CHECK_THROWS_AS(stream.write(bad.data(), bad.size()), FileException);
        CHECK_THROWS_AS(stream.flush(), FileException);
    }
    SECTION("bad short final leaf") {
        auto tail = contents.substr(64 * 1024); tail.back() = 'y';
        REQUIRE_NOTHROW(stream.write(tail.data(), tail.size()));
        CHECK_THROWS_AS(stream.flush(), FileException); CHECK_THROWS_AS(stream.flush(), FileException);
    }
    SECTION("bytes beyond expected EOF") {
        auto tail = contents.substr(64 * 1024); tail.push_back('x');
        CHECK_THROWS_AS(stream.write(tail.data(), tail.size()), FileException);
    }
}

TEST_CASE("Download cleanup accepts a valid short final hash block without a false flush failure", "[transfer-cleanup-stop][merkle-flush][shutdown]") {
    Fixture f; auto* source = f.source(true);
    auto target = f.queued(*source, "short-final", 192 * 1024 + 123); f.start(*source);
    f.download().stopProducersForShutdown(); f.queue().stopProducersForShutdown();
    f.bytes(*source, 128 * 1024 + 123);
    CHECK(source->getDownload() == nullptr); CHECK(f.queue().getPos(target) == 192 * 1024 + 123);
    CHECK(File::getSize(target) == 192 * 1024 + 123); CHECK_FALSE(fs::exists(f.path("short-final.part")));
    CHECK(stopCleanup(f.download())); REQUIRE(f.queue().saveQueueChecked(true));
}
