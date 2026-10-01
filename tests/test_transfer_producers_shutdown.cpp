#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <ranges>
#define private public
#define protected public
#include "DCContext.h"
#include "DownloadManager.h"
#include "UploadManager.h"
#include "QueueManager.h"
#include "UserConnection.h"
#include "HashManager.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "FinishedManager.h"
#include "SearchManager.h"
#include "ShareManager.h"
#include "Download.h"
#include "Upload.h"
#include "File.h"
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
        FAIL("Transfer producer admission did not close");
    }
}
template<class Manager> void lateDownloadCallbacks(Manager& manager) {
    if constexpr(requires { manager.stopProducersForShutdown(); }) {
        manager.addConnection(nullptr);
        manager.on(UserConnectionListener::Updated(), nullptr);
        manager.on(AdcCommand::SND(), nullptr, AdcCommand(AdcCommand::CMD_SND));
        manager.startData(nullptr, 0, 0, false);
        manager.checkIdle(nullptr);
    }
}
template<class Manager> void lateUploadCallbacks(Manager& manager) {
    if constexpr(requires { manager.stopProducersForShutdown(); }) {
        manager.addConnection(nullptr);
        manager.on(UserConnectionListener::Get(), nullptr, "", 0);
        manager.on(UserConnectionListener::Send(), nullptr);
        manager.on(UserConnectionListener::GetListLength(), nullptr);
        manager.on(AdcCommand::GET(), nullptr, AdcCommand(AdcCommand::CMD_GET));
        manager.on(AdcCommand::GFI(), nullptr, AdcCommand(AdcCommand::CMD_GFI));
        manager.on(ClientManagerListener::UserDisconnected(), nullptr);
    }
}
struct Deadline {
    std::atomic<const char*> stage{"setup"};
    Semaphore finished;
    std::thread watchdog{[this] {
        if(!finished.wait(40000)) {
            std::fprintf(stderr, "Transfer producers fixture exceeded 40 seconds: %s\n", stage.load());
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
    TimerManagerListener* downloadTimer;
    TimerManagerListener* uploadTimer;
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
        c.favoriteManager_ = std::make_unique<FavoriteManager>(c);
        c.downloadManager_ = std::make_unique<DownloadManager>(c);
        downloadTimer = c.getTimerManager()->listeners.back();
        c.uploadManager_ = std::make_unique<UploadManager>(c);
        uploadTimer = c.getTimerManager()->listeners.back();
        c.finishedManager_ = std::make_unique<FinishedManager>(c);
        c.shareManager_ = std::make_unique<ShareManager>(c);
        auto& s = *c.getSettingsManager();
        s.set(SettingsManager::KEEP_FINISHED_FILES, true);
        s.set(SettingsManager::KEEP_LISTS, true);
        s.set(SettingsManager::SFV_CHECK, false);
        s.set(SettingsManager::LOG_DOWNLOADS, false);
        s.set(SettingsManager::LOG_UPLOADS, false);
        s.set(SettingsManager::IPFILTER, false);
        s.set(SettingsManager::REQUIRE_TLS, 0);
        s.set(SettingsManager::SLOTS, 2);
        s.set(SettingsManager::SEGMENTED_DL, false);
    }
    ~Fixture() {
        deadline.stage = "cleanup";
        stopProducers(download()); stopProducers(upload());
        for(auto* source: sources) {
            if(source->isSet(UserConnection::FLAG_DOWNLOAD)) download().onFailed(source, "fixture cleanup");
            else upload().on(UserConnectionListener::Failed(), source, "fixture cleanup");
            delete source;
        }
        BufferedSocket::waitShutdown();
    }
    DCContext& context() { return *fixture.ownedCtx; }
    DownloadManager& download() { return *context().getDownloadManager(); }
    UploadManager& upload() { return *context().getUploadManager(); }
    QueueManager& queue() { return *context().getQueueManager(); }
    string path(const string& name) { return (fixture.tmpDir / name).string(); }
    UserPtr user() { return UserPtr(new User(CID::generate())); }
    UserConnection* source(bool downloading) {
        auto* source = new UserConnection(false, context());
        source->setFlag(downloading ? UserConnection::FLAG_DOWNLOAD : UserConnection::FLAG_UPLOAD);
        source->setFlag(UserConnection::FLAG_SUPPORTS_TTHF | UserConnection::FLAG_SUPPORTS_ADCGET);
        source->setUser(user());
        source->setHubUrl("adc://fixture");
        source->socket = BufferedSocket::getSocket('\n', context());
        auto transport = std::make_unique<Socket>();
        transport->create(Socket::TYPE_TCP, AF_INET);
        source->socket->setSocket(std::move(transport));
        sources.push_back(source);
        return source;
    }
    string queued(UserConnection& source, const string& name, int64_t size = 128 * 1024, int64_t verified = 0) {
        string zero(size, '\0');
        TigerTree tree(64 * 1024);
        tree.update(zero.data(), zero.size()); tree.finalize();
        context().getHashManager()->addTree(tree);
        auto temp = path(name + ".part"), target = path(name + ".bin");
        File(temp, File::WRITE, File::CREATE | File::TRUNCATE).write(zero);
        auto* item = queue().fileQueue.add(target, size, 0, QueueItem::NORMAL, temp, 1700000000, tree.getRoot());
        if(verified) item->addSegment(Segment(0, verified));
        queue().addSource(item, source.getHintedUser(), 0, false);
        return target;
    }
    Download* requested(UserConnection& source) {
        auto* d = queue().getDownload(source, false);
        if(!d) throw std::runtime_error("Fixture download not selected");
        download().downloads.push_back(d);
        source.setState(UserConnection::STATE_SND);
        return d;
    }
    bool prepare(UserConnection& source) {
        return upload().prepareFile(source, Transfer::names[Transfer::TYPE_PARTIAL_LIST], "/", 0, -1, false);
    }
};
struct DownloadListener : DownloadManagerListener {
    std::atomic<unsigned> requesting{0}, starting{0}, ticks{0}, completed{0}, failed{0};
    bool blockStarting = false;
    bool blockFailed = false;
    Semaphore entered, release;
    void on(Requesting, Download*) noexcept override { ++requesting; }
    void on(Starting, Download*) noexcept override {
        ++starting;
        if(blockStarting) { entered.signal(); release.wait(); }
    }
    void on(Tick, const DownloadList&) noexcept override { ++ticks; }
    void on(Complete, Download*) noexcept override { ++completed; }
    void on(Failed, Download*, const string&) noexcept override {
        ++failed;
        if(blockFailed) { entered.signal(); release.wait(); }
    }
};
struct UploadListener : UploadManagerListener {
    std::atomic<unsigned> ticks{0}, completed{0}, failed{0};
    bool blockWaiting = false;
    Semaphore entered, release;
    void on(WaitingRemoveUser, const HintedUser&) noexcept override {
        if(blockWaiting) { entered.signal(); release.wait(); }
    }
    void on(Tick, const UploadList&) noexcept override { ++ticks; }
    void on(Complete, Upload*) noexcept override { ++completed; }
    void on(Failed, Upload*, const string&) noexcept override { ++failed; }
};
}

TEST_CASE("Download producer fence rejects late requests without losing queue state", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    auto target = f.queued(*source, "late");
    DownloadListener listener;
    f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    stopProducers(f.download());
    f.download().addConnection(source);
    CHECK(source->getDownload() == nullptr);
    CHECK(listener.requesting == 0);
    CHECK(f.download().getDownloadCount() == 0);
    CHECK_FALSE(f.download().startDownload(QueueItem::HIGHEST));
    CHECK(f.queue().getSize(target) == 128 * 1024);
    CHECK(f.queue().getPos(target) == 0);
    CHECK_FALSE(fs::exists(target));
    stopProducers(f.download());
    lateDownloadCallbacks(f.download());
}

TEST_CASE("Upload producer fence rejects streams slots and waiting list mutations", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(false);
    auto reserved = f.user();
    f.upload().reserveSlot(HintedUser(reserved, "adc://fixture"));
    f.upload().waitingUsers.emplace_back(source->getHintedUser(), 1000);
    f.upload().waitingFiles[source->getUser()].insert("existing.bin");
    stopProducers(f.upload());
    auto late = f.user();
    f.upload().reserveSlot(HintedUser(late, "adc://fixture"));
    CHECK(f.upload().reservedSlots.count(late) == 0);
    CHECK(f.upload().reservedSlots.count(reserved) == 1);
    f.upload().addConnection(source);
    CHECK(source->getState() == UserConnection::STATE_UNCONNECTED);
    const bool prepared = f.prepare(*source);
    CHECK_FALSE(prepared);
    CHECK(f.upload().getUploadCount() == 0);
    if(prepared) f.upload().on(UserConnectionListener::Failed(), source, "RED cleanup");
    f.upload().setRunning(99);
    CHECK(f.upload().running == 0);
    f.upload().notifyQueuedUsers();
    f.upload().on(ClientManagerListener::UserDisconnected(), source->getUser());
    CHECK(f.upload().getWaitingUsers().size() == 1);
    CHECK(f.upload().waitingFiles.count(source->getUser()) == 1);
    stopProducers(f.upload());
    lateUploadCallbacks(f.upload());
}

TEST_CASE("Download producer fence joins admitted file setup and concurrent callers", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    f.queued(*source, "starting");
    auto* d = f.requested(*source);
    DownloadListener listener;
    listener.blockStarting = true;
    f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    auto producer = std::async(std::launch::async, [&] { f.download().startData(source, 0, d->getSize(), false); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "admitted download file setup";
    auto stop = std::async(std::launch::async, [&] { stopProducers(f.download()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.download());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto other = std::async(std::launch::async, [&] { joining.signal(); stopProducers(f.download()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000));
    CHECK(other.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); other.get(); producer.get();
    CHECK(source->getState() == UserConnection::STATE_RUNNING);
    CHECK(f.download().getDownloadCount() == 1);
    f.download().onFailed(source, "accepted disconnect");
    CHECK(f.download().getDownloadCount() == 0);
    CHECK(source->getDownload() == nullptr);
}

TEST_CASE("Upload producer fence joins admitted slot callbacks without discarding stream", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(false);
    f.upload().waitingUsers.emplace_back(source->getHintedUser(), 1000);
    f.upload().waitingFiles[source->getUser()].insert("existing.bin");
    UploadListener listener;
    listener.blockWaiting = true;
    f.upload().addListener(&listener);
    ScopedFunctor([&] { f.upload().removeListener(&listener); });
    auto producer = std::async(std::launch::async, [&] { return f.prepare(*source); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    f.deadline.stage = "admitted upload slot callback";
    auto stop = std::async(std::launch::async, [&] { stopProducers(f.upload()); });
    ScopedFunctor([&] { listener.release.signal(); });
    awaitClosure(f.upload());
    CHECK(stop.wait_for(30ms) == std::future_status::timeout);
    Semaphore joining;
    auto other = std::async(std::launch::async, [&] { joining.signal(); stopProducers(f.upload()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(joining.wait(5000));
    CHECK(other.wait_for(30ms) == std::future_status::timeout);
    listener.release.signal();
    REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
    stop.get(); other.get(); REQUIRE(producer.get());
    CHECK(source->getUpload() != nullptr);
    CHECK(f.upload().getUploadCount() == 1);
    CHECK(f.upload().running == 1);
    f.upload().on(UserConnectionListener::Failed(), source, "accepted disconnect");
    CHECK(source->getUpload() == nullptr);
    CHECK(f.upload().getUploadCount() == 0);
    CHECK(f.upload().running == 0);
}

TEST_CASE("Transfer producer fence rejects stale timer snapshots", "[transfer-producers-stop][shutdown]") {
    for(bool downloading: {true, false}) {
        Fixture f;
        auto* source = f.source(downloading);
        DownloadListener dl;
        UploadListener ul;
        if(downloading) { f.queued(*source, "timer"); f.requested(*source)->addPos(1, 1); }
        else { REQUIRE(f.prepare(*source)); source->getUpload()->addPos(1, 1); }
        f.download().addListener(&dl); f.upload().addListener(&ul);
        ScopedFunctor([&] { f.download().removeListener(&dl); f.upload().removeListener(&ul); });
        auto& timer = *f.context().getTimerManager();
        auto* target = downloading ? f.downloadTimer : f.uploadTimer;
        struct Blocker : TimerManagerListener {
            Semaphore entered, release;
            void on(Second, uint64_t) noexcept override { entered.signal(); release.wait(); }
        } blocker;
        timer.removeListeners(); timer.addListener(&blocker); timer.addListener(target);
        ScopedFunctor([&] { timer.removeListener(&blocker); });
        auto tick = std::async(std::launch::async, [&] { timer.fire(TimerManagerListener::Second(), 1000); });
        ScopedFunctor([&] { blocker.release.signal(); });
        REQUIRE(blocker.entered.wait(5000));
        f.deadline.stage = "stale transfer timer snapshot";
        auto stop = std::async(std::launch::async, [&] {
            if(downloading) stopProducers(f.download()); else stopProducers(f.upload());
        });
        ScopedFunctor([&] { blocker.release.signal(); });
        if(downloading) awaitClosure(f.download()); else awaitClosure(f.upload());
        blocker.release.signal();
        REQUIRE(stop.wait_for(5000ms) == std::future_status::ready);
        stop.get(); tick.get();
        CHECK(dl.ticks == 0); CHECK(ul.ticks == 0);
    }
}

TEST_CASE("Accepted partial download cleanup preserves verified bytes after producer fence", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    auto target = f.queued(*source, "partial", 192 * 1024, 64 * 1024);
    auto* d = f.requested(*source);
    REQUIRE(d->getStartPos() == 64 * 1024);
    f.download().startData(source, d->getStartPos(), d->getSize(), false);
    string bytes(64 * 1024 + 123, '\0');
    f.download().on(UserConnectionListener::Data(), source, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    stopProducers(f.download());
    REQUIRE(source->getDownload() != nullptr);
    f.download().onFailed(source, "normal quit");
    CHECK(source->getDownload() == nullptr);
    CHECK(f.download().getDownloadCount() == 0);
    CHECK(f.queue().getPos(target) == 128 * 1024);
    CHECK(f.queue().fileQueue.find(target)->getTempTarget() == f.path("partial.part"));
    CHECK(File::getSize(f.path("partial.part")) == 192 * 1024);
    CHECK_FALSE(fs::exists(target));
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Accepted download completion moves its file after producer fence without requesting next job", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    auto target = f.queued(*source, "complete", 128 * 1024, 64 * 1024);
    auto* d = f.requested(*source);
    f.download().startData(source, d->getStartPos(), d->getSize(), false);
    f.queued(*source, "next", 192 * 1024);
    DownloadListener listener;
    f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    stopProducers(f.download());
    string bytes(64 * 1024, '\0');
    f.download().on(UserConnectionListener::Data(), source, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    CHECK(listener.completed == 1);
    CHECK(listener.requesting == 0);
    CHECK(source->getDownload() == nullptr);
    CHECK(f.download().getDownloadCount() == 0);
    CHECK(f.queue().getPos(target) == 128 * 1024);
    CHECK(File::getSize(target) == 128 * 1024);
    CHECK_FALSE(fs::exists(f.path("complete.part")));
    CHECK(f.queue().getPos(f.path("next.bin")) == 0);
    REQUIRE(f.queue().saveQueueChecked(true));
}

TEST_CASE("Accepted upload completion and failure release streams and slots after producer fence", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(false);
    REQUIRE(f.prepare(*source));
    source->setState(UserConnection::STATE_RUNNING);
    UploadListener listener;
    f.upload().addListener(&listener);
    ScopedFunctor([&] { f.upload().removeListener(&listener); });
    stopProducers(f.upload());
    f.upload().on(UserConnectionListener::BytesSent(), source, 10, 12);
    CHECK(source->getUpload()->getPos() == 10);
    CHECK(source->getUpload()->getActual() == 12);
    f.upload().on(UserConnectionListener::TransmitDone(), source);
    CHECK(listener.completed == 1);
    CHECK(source->getUpload() == nullptr);
    CHECK(f.upload().getUploadCount() == 0);
    CHECK(source->getState() == UserConnection::STATE_GET);
    CHECK(f.upload().running == 1);
    f.upload().on(UserConnectionListener::Failed(), source, "normal quit");
    CHECK(f.upload().running == 0);
    CHECK_FALSE(source->isSet(UserConnection::FLAG_HASSLOT));
    CHECK_FALSE(source->isSet(UserConnection::FLAG_HASEXTRASLOT));
}

TEST_CASE("Ordinary transfer setup and waiting list controls remain reusable before producer fence", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    auto target = f.queued(*source, "ordinary");
    DownloadListener listener;
    f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    f.download().addConnection(source);
    REQUIRE(source->getDownload() != nullptr);
    CHECK(listener.requesting == 1);
    CHECK(source->getState() == UserConnection::STATE_SND);
    auto* d = source->getDownload();
    f.download().startData(source, d->getStartPos(), d->getSize(), false);
    CHECK(listener.starting == 1);
    f.download().onFailed(source, "ordinary disconnect");
    CHECK(f.download().getDownloadCount() == 0);
    CHECK(f.queue().getSize(target) == 128 * 1024);
    CHECK(f.download().startDownload(QueueItem::NORMAL));
    auto* upload = f.source(false);
    f.upload().addConnection(upload);
    CHECK(upload->getState() == UserConnection::STATE_GET);
    REQUIRE(f.prepare(*upload));
    CHECK(f.upload().running == 1);
    f.upload().on(UserConnectionListener::Failed(), upload, "ordinary disconnect");
    CHECK(f.upload().running == 0);
    f.upload().waitingUsers.emplace_back(upload->getHintedUser(), 1000);
    f.upload().waitingFiles[upload->getUser()].insert("existing.bin");
    f.upload().clearUserFiles(upload->getUser());
    CHECK(f.upload().getWaitingUsers().empty());
    CHECK(f.upload().waitingFiles.empty());
    f.upload().setRunning(1); CHECK(f.upload().running == 1);
    f.upload().setRunning(0); CHECK(f.upload().running == 0);
}

TEST_CASE("Download producer fence rejects late pending file preparation without creating directories", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    auto target = f.queued(*source, "pending");
    auto absent = f.path("absent/pending.part");
    f.queue().fileQueue.find(target)->setTempTarget(absent);
    auto* d = f.requested(*source);
    stopProducers(f.download());
    f.download().startData(source, d->getStartPos(), d->getSize(), false);
    AdcCommand snd(AdcCommand::CMD_SND);
    snd.addParam("file").addParam("TTH/" + d->getTTH().toBase32()).addParam("0").addParam("131072");
    f.download().on(AdcCommand::SND(), source, snd);
    CHECK(d->getFile() == nullptr);
    CHECK(source->getState() == UserConnection::STATE_SND);
    CHECK_FALSE(fs::exists(f.path("absent")));
    CHECK(f.download().getDownloadCount() == 1);
    f.download().onFailed(source, "accepted cleanup");
    CHECK(source->getDownload() == nullptr);
    CHECK(f.download().getDownloadCount() == 0);
}

TEST_CASE("Empty download list after producer fence is not accepted cleanup completion", "[transfer-producers-stop][shutdown]") {
    Fixture f;
    auto* source = f.source(true);
    auto target = f.queued(*source, "cleanup", 192 * 1024, 64 * 1024);
    auto* d = f.requested(*source);
    f.download().startData(source, d->getStartPos(), d->getSize(), false);
    string bytes(64 * 1024, '\0');
    f.download().on(UserConnectionListener::Data(), source, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    stopProducers(f.download());
    DownloadListener listener;
    listener.blockFailed = true;
    f.download().addListener(&listener);
    ScopedFunctor([&] { f.download().removeListener(&listener); });
    f.deadline.stage = "accepted cleanup after producer fence";
    auto cleanup = std::async(std::launch::async, [&] { f.download().onFailed(source, "normal quit"); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(listener.entered.wait(5000));
    CHECK(f.download().getDownloadCount() == 0);
    CHECK(source->getDownload() != nullptr);
    CHECK(f.queue().getPos(target) == 64 * 1024);
    CHECK(cleanup.wait_for(30ms) == std::future_status::timeout);
    auto repeat = std::async(std::launch::async, [&] { stopProducers(f.download()); });
    ScopedFunctor([&] { listener.release.signal(); });
    REQUIRE(repeat.wait_for(5000ms) == std::future_status::ready);
    repeat.get();
    listener.release.signal();
    REQUIRE(cleanup.wait_for(5000ms) == std::future_status::ready);
    cleanup.get();
    CHECK(source->getDownload() == nullptr);
    CHECK(f.queue().getPos(target) == 128 * 1024);
}
