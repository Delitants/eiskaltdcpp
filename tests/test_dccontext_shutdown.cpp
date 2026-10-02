#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#include <filesystem>
#include <future>
#include <ranges>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
#define private public
#define protected public
#include "DCContext.h"
#include "BufferedSocket.h"
#include "HashManager.h"
#include "DownloadManager.h"
#include "QueueManager.h"
#include "ClientManager.h"
#include "ShareManager.h"
#include "ConnectionManager.h"
#include "UserConnection.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ADLSearch.h"
#include "BufferedSocket.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "File.h"
#include "FinishedManager.h"
#include "Download.h"
#include "Streams.h"
#include "MappingManager.h"
#include "ProxyRoute.h"
#include "ResourceManager.h"
#include "SearchManager.h"
#include "ShareManager.h"
#include "ThrottleManager.h"
#include "TimerManager.h"
#include "UploadManager.h"
#include "UPnP.h"
#include "ScopedFunctor.h"
#include "extra/dyndns.h"
#include "extra/ipfilter.h"
#ifdef WITH_DHT
#include "dht/DHT.h"
#endif
#ifdef LUA_SCRIPT
#include "ScriptManager.h"
#endif

using namespace dcpp;
namespace fs = std::filesystem;
namespace {
enum class Status { Ready, Failed, SocketTimedOut, UnsupportedContext };
struct Result { Status status; string component; };
template<class C> Result prepare(C& c, uint32_t timeout = 3000) {
    if constexpr(requires { c.prepareShutdownChecked(timeout); }) {
        auto r = c.prepareShutdownChecked(timeout);
        return {static_cast<Status>(r.status), r.component};
    } else return {Status::UnsupportedContext, "missing checked shutdown"};
}
template<class C> void armOwner(C& c) {
    if constexpr(requires { c.shutdownOwner_; }) c.shutdownOwner_ = std::this_thread::get_id();
}
template<class C> bool timerAttempted(C& c) {
    if constexpr(requires { c.timerShutdownAttempted_; }) return c.timerShutdownAttempted_;
    else return false;
}
struct SaveListener : SettingsManagerListener {
    unsigned calls = 0;
    bool fail = false;
    void on(Save, SimpleXML&) override {
        ++calls;
        if(fail) throw std::runtime_error("owned save failure");
    }
};
struct Gateway : UPnP {
    bool fail = true;
    unsigned removals = 0;
    bool init() override { return true; }
    string getExternalIP() override { return "192.0.2.1"; }
    const string& getName() const override { static const string name = "owned fixture"; return name; }
    bool add(const string&, Protocol, const string&) override { return true; }
    bool remove(const string&, Protocol) override {
        ++removals;
        if(fail) throw std::runtime_error("owned mapping failure");
        return true;
    }
};
struct Fixture {
    test::TestContext fixture;
    explicit Fixture(bool foreignConstruction = false) {
        if(foreignConstruction) {
            fixture.ownedCtx.reset();
            fixture.ownedCtx = std::async(std::launch::async, [] { return std::make_unique<DCContext>(); }).get();
            const auto dir = fixture.tmpDir.string() + std::string(1, PATH_SEPARATOR);
            Util::PathsMap paths;
            paths[Util::PATH_USER_CONFIG] = dir; paths[Util::PATH_USER_LOCAL] = dir;
            paths[Util::PATH_DOWNLOADS] = dir + "Downloads" + PATH_SEPARATOR;
            Util::initialize(paths);
            fixture.ownedCtx->startupMinimal();
            setContext(fixture.ownedCtx.get());
        }
        auto& c = context();
        REQUIRE(fs::equivalent(fs::u8path(Util::getPath(Util::PATH_USER_CONFIG)), fixture.tmpDir));
        c.timerManager_ = std::make_unique<TimerManager>(c);
        armOwner(c);
        c.hashManager_ = std::make_unique<HashManager>(c);
        c.hashManager_->store.load();
        c.cryptoManager_ = std::make_unique<CryptoManager>(c);
        c.searchManager_ = std::make_unique<SearchManager>(c);
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.connectionManager_ = std::make_unique<ConnectionManager>(c);
        c.downloadManager_ = std::make_unique<DownloadManager>(c);
        c.uploadManager_ = std::make_unique<UploadManager>(c);
        c.throttleManager_ = std::make_unique<ThrottleManager>(c);
        c.queueManager_ = std::make_unique<QueueManager>(c);
        c.shareManager_ = std::make_unique<ShareManager>(c);
        c.favoriteManager_ = std::make_unique<FavoriteManager>(c);
        c.finishedManager_ = std::make_unique<FinishedManager>(c);
        c.adlSearchManager_ = std::make_unique<ADLSearchManager>(c);
        c.connectivityManager_ = std::make_unique<ConnectivityManager>(c);
        c.mappingManager_ = std::make_unique<MappingManager>(c);
        c.debugManager_ = std::make_unique<DebugManager>(c);
        c.dynDNS_ = std::make_unique<DynDNS>(c);
        c.ipFilter_ = std::make_unique<IPFilter>(c);
        c.settingsManager_->set(SettingsManager::KEEP_LISTS, true);
#ifdef WITH_DHT
        c.settingsManager_->set(SettingsManager::USE_DHT, false);
        c.settingsManager_->set(SettingsManager::DHT_BOOTSTRAP_URLS, ";");
        c.dht_ = std::make_unique<dht::DHT>(c);
#endif
#ifdef LUA_SCRIPT
        c.scriptManager_ = std::make_unique<ScriptManager>(c);
#endif
        c.minimalMode_ = false;
        c.timerManager_->start();
    }
    DCContext& context() { return *fixture.ownedCtx; }
    string path(const string& name) { return (fixture.tmpDir / name).string(); }
    // Negative tests explicitly retire their owned resources. This is NOT a
    // production fallback and must never turn a failed preparation into Ready.
    ~Fixture() {
        auto& c = context();
        if(!c.running_) return;
        c.proxyRoute_->stop();
#ifdef LUA_SCRIPT
        c.scriptManager_->stopForShutdown();
#endif
        c.mappingManager_->close();
        c.throttleManager_->shutdown();
        c.downloadManager_->stopProducersForShutdown();
        c.uploadManager_->stopProducersForShutdown();
        c.queueManager_->stopProducersForShutdown();
        c.dynDNS_->stopForShutdown();
        c.favoriteManager_->stopForShutdown();
        c.searchManager_->stopForShutdown();
#ifdef WITH_DHT
        c.dht_->stopForShutdown();
#endif
        c.clientManager_->stopForShutdown();
        Socket::stopSocksUdpForShutdown(c);
        c.connectionManager_->stopForShutdown();
        BufferedSocket::waitShutdown();
        c.downloadManager_->stopCleanupForShutdown();
        c.uploadManager_->stopCleanupForShutdown();
        c.queueManager_->stopCleanupForShutdown();
        c.shareManager_->stopForShutdown();
        c.hashManager_->stopForShutdown();
        if(!timerAttempted(c)) c.timerManager_->shutdown();
#ifdef LUA_SCRIPT
        c.scriptManager_.reset();
#endif
#ifdef WITH_DHT
        c.dht_.reset();
#endif
        c.debugManager_.reset(); c.mappingManager_.reset(); c.connectivityManager_.reset();
        c.dynDNS_.reset(); c.ipFilter_.reset(); c.adlSearchManager_.reset();
        c.finishedManager_.reset(); c.shareManager_.reset(); c.cryptoManager_.reset();
        c.throttleManager_.reset(); c.downloadManager_.reset(); c.uploadManager_.reset();
        c.queueManager_.reset(); c.connectionManager_.reset(); c.searchManager_.reset();
        c.favoriteManager_.reset(); c.clientManager_.reset(); c.hashManager_.reset();
        c.logManager_.reset(); c.settingsManager_.reset(); c.proxyRoute_.reset();
        c.timerManager_.reset(); c.resourceManager_.reset();
        c.running_ = false;
        // All resources above were test-owned and explicitly drained.
        releaseRetired(c);
    }
    template<class C> static void releaseRetired(C& c) {
        if constexpr(requires { c.shutdownPhase_; }) c.shutdownPhase_ = C::ShutdownPhase::Disposed;
    }
};
}

TEST_CASE("Checked core shutdown retains Ready managers and disposes without save replay", "[core-shutdown]") {
    Fixture f; auto& c = f.context();
    SaveListener listener;
    c.settingsManager_->addListener(&listener);
    ScopedFunctor([&] { if(c.settingsManager_) c.settingsManager_->removeListener(&listener); });
    auto* hash = c.getHashManager(); auto* queue = c.getQueueManager();
    auto* timer = c.getTimerManager(); auto* share = c.getShareManager();
    REQUIRE(prepare(c).status == Status::Ready);
    CHECK(c.isRunning()); CHECK(c.getHashManager() == hash); CHECK(c.getQueueManager() == queue);
    CHECK(c.getTimerManager() == timer); CHECK(c.getShareManager() == share);
    CHECK(listener.calls == 1);
    CHECK(std::async(std::launch::async, [&] {
        try { c.shutdown(); } catch(const std::logic_error&) { return true; }
        return false;
    }).get());
    CHECK(c.isRunning());
    c.settingsManager_->set(SettingsManager::NICK, "must not be saved on disposal");
    REQUIRE(prepare(c, 0).status == Status::Ready);
    CHECK(listener.calls == 1);
    auto saved = File(f.path("DCPlusPlus.xml"), File::READ, File::OPEN).read();
    c.shutdown();
    CHECK_FALSE(c.isRunning()); CHECK(c.getTimerManager() == nullptr);
    CHECK(File(f.path("DCPlusPlus.xml"), File::READ, File::OPEN).read() == saved);
    CHECK(listener.calls == 1);
    c.shutdown();
}

TEST_CASE("Checked core shutdown rejects unsupported callers without consuming owner attempt", "[core-shutdown]") {
    SECTION("unstarted") { DCContext c; CHECK(prepare(c).status == Status::UnsupportedContext); }
    SECTION("minimal") { test::TestContext f; CHECK(prepare(*f.ownedCtx).status == Status::UnsupportedContext); }
    SECTION("wrong owner") {
        Fixture f; auto& c = f.context();
        auto route = c.getProxyRoute()->snapshot();
        auto rejected = std::async(std::launch::async, [&] { return prepare(c); }).get();
        CHECK(rejected.status == Status::UnsupportedContext);
        CHECK(c.getProxyRoute()->snapshot() == route);
        REQUIRE(prepare(c).status == Status::Ready);
        c.shutdown();
    }
    SECTION("incomplete") {
        Fixture f; auto& c = f.context(); auto held = std::move(c.debugManager_);
        CHECK(prepare(c).status == Status::UnsupportedContext);
        c.debugManager_ = std::move(held);
        REQUIRE(prepare(c).status == Status::Ready); c.shutdown();
    }
    SECTION("constructed elsewhere but Timer owned here") {
        Fixture f(true); auto& c = f.context();
        REQUIRE(prepare(c).status == Status::Ready); c.shutdown();
    }
}

TEST_CASE("Checked core save failure is sticky with independent snapshots and retained owners", "[core-shutdown]") {
    Fixture f; auto& c = f.context();
    SaveListener listener; listener.fail = true;
    c.settingsManager_->addListener(&listener);
    ScopedFunctor([&] { c.settingsManager_->removeListener(&listener); });
    auto result = prepare(c);
    REQUIRE(result.status == Status::Failed); CHECK(result.component == "Settings");
    CHECK(c.isRunning()); CHECK(c.getQueueManager() != nullptr);
    CHECK(fs::exists(f.path("Favorites.xml"))); CHECK(fs::exists(f.path("Queue.xml")));
    CHECK(listener.calls == 1);
    CHECK_THROWS(c.shutdown());
    listener.fail = false;
    CHECK(prepare(c).component == result.component); CHECK(prepare(c).status == Status::Failed);
    CHECK(listener.calls == 1);
}

TEST_CASE("Checked core stage failure preserves old snapshot and attempts later saves", "[core-shutdown]") {
    Fixture f; auto& c = f.context();
    REQUIRE(c.favoriteManager_->saveChecked());
    auto old = File(f.path("Favorites.xml"), File::READ, File::OPEN).read();
    fs::create_directory(f.path("Favorites.xml.tmp"));
    SaveListener listener; c.settingsManager_->addListener(&listener);
    ScopedFunctor([&] { c.settingsManager_->removeListener(&listener); });
    auto result = prepare(c);
    REQUIRE(result.status == Status::Failed); CHECK(result.component == "Favorites");
    CHECK(listener.calls == 1); CHECK(fs::exists(f.path("DCPlusPlus.xml")));
    CHECK(File(f.path("Favorites.xml"), File::READ, File::OPEN).read() == old);
    fs::remove(f.path("Favorites.xml.tmp"));
    CHECK(prepare(c).status == Status::Failed); CHECK(listener.calls == 1);
}

TEST_CASE("Checked core socket timeout remains cached after physical resource release", "[core-shutdown]") {
    Fixture f; auto& c = f.context();
    Semaphore entered, release;
    struct SlowTransport : Socket {
        Semaphore& entered; Semaphore& release;
        SlowTransport(Semaphore& a, Semaphore& b) : entered(a), release(b) {}
        ~SlowTransport() override { entered.signal(); release.wait(); }
    };
    auto* socket = BufferedSocket::getSocket('\n', c);
    auto transport = std::make_unique<SlowTransport>(entered, release);
    transport->create(Socket::TYPE_TCP, AF_INET); socket->setSocket(std::move(transport));
    BufferedSocket::putSocket(socket);
    ScopedFunctor([&] { release.signal(); BufferedSocket::waitShutdown(); });
    REQUIRE(entered.wait(3000));
    REQUIRE(prepare(c, 25).status == Status::SocketTimedOut);
    CHECK(c.isRunning()); CHECK_FALSE(fs::exists(f.path("DCPlusPlus.xml")));
    CHECK_THROWS(c.shutdown());
    release.signal(); REQUIRE(BufferedSocket::waitShutdown(3000));
    CHECK(prepare(c, 5000).status == Status::SocketTimedOut);
    CHECK_FALSE(fs::exists(f.path("DCPlusPlus.xml")));
}

TEST_CASE("Checked core mapping exception is cached before timer shutdown or save", "[core-shutdown]") {
    Fixture f; auto& c = f.context(); auto* gateway = new Gateway;
    c.mappingManager_->addImplementation(gateway);
    REQUIRE(gateway->open("1234", UPnP::PROTOCOL_TCP, "owned"));
    ScopedFunctor([&] { gateway->fail = false; });
    auto result = prepare(c);
    REQUIRE(result.status == Status::Failed); CHECK(result.component == "Mapping");
    CHECK(gateway->removals == 1); CHECK_FALSE(timerAttempted(c));
    CHECK(prepare(c).status == Status::Failed); CHECK(gateway->removals == 1);
    CHECK_FALSE(fs::exists(f.path("DCPlusPlus.xml")));
}

TEST_CASE("Checked core transfer cleanup failure prevents dependent snapshots", "[core-shutdown]") {
    Fixture f; auto& c = f.context();
    auto* source = c.connectionManager_->getConnection(false, false);
    source->setFlag(UserConnection::FLAG_DOWNLOAD | UserConnection::FLAG_SUPPORTS_ADCGET |
                    UserConnection::FLAG_SUPPORTS_TTHF | UserConnection::FLAG_SUPPORTS_MINISLOTS);
    source->setUser(UserPtr(new User(CID::generate()))); source->setHubUrl("adc://fixture");
    source->socket = BufferedSocket::getSocket('\n', c);
    auto transport = std::make_unique<Socket>(); transport->create(Socket::TYPE_TCP, AF_INET);
    source->socket->setSocket(std::move(transport));
    const string bytes(192 * 1024, '\0'); TigerTree tree(64 * 1024);
    tree.update(bytes.data(), bytes.size()); tree.finalize(); c.hashManager_->addTree(tree);
    const auto part = f.path("flush.part"), target = f.path("flush.bin");
    File(part, File::WRITE, File::CREATE | File::TRUNCATE).write(bytes);
    auto* item = c.queueManager_->fileQueue.add(target, bytes.size(), 0, QueueItem::NORMAL, part, 1700000000, tree.getRoot());
    item->addSegment(Segment(0, 64 * 1024));
    c.queueManager_->addSource(item, source->getHintedUser(), 0, false);
    c.downloadManager_->addConnection(source);
    auto* download = source->getDownload(); REQUIRE(download != nullptr);
    c.downloadManager_->startData(source, download->getStartPos(), download->getSize(), false);
    delete download->getFile();
    // An actual read-only native file accepts buffered bytes but rejects the
    // eventual write. The accepted peer failure must not certify these bytes.
    download->setFile(new BufferedOutputStream<true>(new File(part, File::READ, File::OPEN), 128 * 1024));
    c.downloadManager_->on(UserConnectionListener::Data(), source,
                          reinterpret_cast<const uint8_t*>(bytes.data()), 64 * 1024);
    source->on(BufferedSocketListener::Failed(), "owned disconnect");
    REQUIRE(BufferedSocket::waitShutdown(3000));
    auto result = prepare(c);
    REQUIRE(result.status == Status::Failed); CHECK(result.component == "Download cleanup");
    CHECK_FALSE(fs::exists(f.path("DCPlusPlus.xml")));
    CHECK(c.isRunning()); CHECK_FALSE(timerAttempted(c));
    CHECK(c.queueManager_->getPos(target) == 64 * 1024); CHECK_FALSE(fs::exists(target));
    c.downloadManager_->flushFailed = false;
    CHECK(prepare(c).status == Status::Failed);
}

TEST_CASE("Checked core snapshots preserve the queue CID tail through the Users loader", "[core-shutdown]") {
    Fixture f; auto& c = f.context();
    const CID queued(string(39, 'B')), unrelated(string(39, 'D'));
    c.clientManager_->nicks[queued] = {"Queue source", false};
    c.clientManager_->nicks[unrelated] = {"Not queued", false};
    TigerTree tree(1024); const string bytes = "queue";
    tree.update(bytes.data(), bytes.size()); tree.finalize();
    auto* item = c.queueManager_->fileQueue.add(f.path("queued.bin"), 65536, 0, QueueItem::NORMAL, {}, 1700000000, tree.getRoot());
    item->addSource(HintedUser(c.clientManager_->getUser(queued), "adc://fixture"));
    REQUIRE(prepare(c).status == Status::Ready);
    c.clientManager_->nicks.clear(); c.clientManager_->loadUsers();
    CHECK(c.clientManager_->getNicks(queued, {}) == StringList{"Queue source"});
    CHECK(c.clientManager_->nicks.count(unrelated) == 0);
    const auto saved = File(f.path("Users.xml"), File::READ, File::OPEN).read();
    c.shutdown();
    CHECK(File(f.path("Users.xml"), File::READ, File::OPEN).read() == saved);
}

TEST_CASE("Checked core mandatory data and cache outcomes gate Ready without skipping snapshots", "[core-shutdown]") {
    Fixture f; auto& c = f.context(); const char* expected;
    SECTION("Hash native storage receipt") { c.hashManager_->store.dataInsertionFailed = true; expected = "Hash data"; }
    SECTION("Share cache revocation receipt") { c.shareManager_->managedCacheRevocationFailed = true; expected = "Share cache"; }
    SaveListener listener; c.settingsManager_->addListener(&listener);
    ScopedFunctor([&] { c.settingsManager_->removeListener(&listener); });
    const auto result = prepare(c);
    REQUIRE(result.status == Status::Failed); CHECK(result.component == expected);
    CHECK(listener.calls == 1); CHECK(timerAttempted(c));
    CHECK(fs::exists(f.path("Queue.xml"))); CHECK(fs::exists(f.path("Favorites.xml")));
    c.hashManager_->store.dataInsertionFailed = false;
    c.shareManager_->managedCacheRevocationFailed = false;
    CHECK(prepare(c).status == Status::Failed); CHECK(listener.calls == 1);
}

TEST_CASE("Checked core rejects reentrant preparation and destruction from a Save callback", "[core-shutdown]") {
    Fixture f; auto& c = f.context();
    struct Reentrant : SettingsManagerListener {
        DCContext& c; bool rejected = false; bool retained = false;
        explicit Reentrant(DCContext& context) : c(context) {}
        void on(Save, SimpleXML&) override {
            rejected = prepare(c).status == Status::UnsupportedContext;
            try { c.shutdown(); } catch(const std::logic_error&) { retained = c.isRunning(); }
        }
    } listener(c);
    c.settingsManager_->addListener(&listener);
    ScopedFunctor([&] { if(c.settingsManager_) c.settingsManager_->removeListener(&listener); });
    REQUIRE(prepare(c).status == Status::Ready);
    CHECK(listener.rejected); CHECK(listener.retained);
    c.shutdown();
}

#ifndef _WIN32
TEST_CASE("Checked core failed-context destructor cannot fall through to member destruction", "[core-shutdown]") {
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if(child == 0) {
        std::set_terminate([] { std::_Exit(81); });
        auto* c = new DCContext;
        c->shutdownPhase_ = DCContext::ShutdownPhase::Finished;
        c->shutdownResult_ = {DCContext::ShutdownStatus::Failed, "owned death fixture"};
        delete c;
        std::_Exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 81);
}
#endif
