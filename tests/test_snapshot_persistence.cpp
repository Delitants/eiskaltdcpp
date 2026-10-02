#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <filesystem>
#include <future>
#include <ranges>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#define private public
#include "DCContext.h"
#include "HashManager.h"
#include "ClientManager.h"
#include "QueueManager.h"
#ifdef WITH_DHT
#include "dht/DHT.h"
#include "dht/KBucket.h"
#include "dht/BootstrapManager.h"
#include "dht/IndexManager.h"
#endif
#undef private
#include "TestContext.h"
#include "File.h"
#include "SearchManager.h"
#include "ScopedFunctor.h"
#include "Semaphore.h"
#include "SimpleXML.h"

using namespace dcpp;
namespace fs = std::filesystem;

namespace {
template<class M> bool saveHash(M& m) {
    if constexpr(requires { m.saveIndexChecked(); }) return m.saveIndexChecked();
    else { m.store.save(); return true; }
}
template<class M> bool saveUsers(M& m) {
    if constexpr(requires { m.saveUsersChecked(); }) return m.saveUsersChecked();
    else { m.saveUsers(); return true; }
}
#ifdef WITH_DHT
template<class M> bool saveDht(M& m) {
    if constexpr(requires { m.saveDataChecked(); }) return m.saveDataChecked();
    // Legacy saveData dereferences a dirty, removed bucket. Exercise ordinary
    // legacy writes but reserve absent-bucket acceptance for the new API.
    else { if(m.bucket) m.saveData(); return true; }
}
#endif
void write(const string& path, const string& bytes) {
    File file(path, File::WRITE, File::CREATE | File::TRUNCATE);
    file.write(bytes); file.flush();
}
string read(const string& path) { return File(path, File::READ, File::OPEN).read(); }
TigerTree tree(const string& bytes) {
    TigerTree result(1024); result.update(bytes.data(), bytes.size()); result.finalize(); return result;
}

// Permit staging I/O, but deny replacing/deleting the existing destination.
// POSIX needs an unprivileged runner; Windows uses native deny-delete sharing.
struct DenyReplace {
    fs::path parent;
#ifdef _WIN32
    std::unique_ptr<File> held;
    explicit DenyReplace(const string& target) : held(std::make_unique<File>(target, File::READ, File::OPEN)) { }
#else
    fs::perms original;
    explicit DenyReplace(const string& target) : parent(fs::path(target).parent_path()), original(fs::status(parent).permissions()) {
        fs::permissions(parent, fs::perms::owner_read | fs::perms::owner_exec);
    }
    ~DenyReplace() { std::error_code e; fs::permissions(parent, original, e); }
#endif
};
struct Fixture {
    test::TestContext fixture;
    Fixture() {
        auto& c = context();
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.hashManager_ = std::make_unique<HashManager>(c);
        c.hashManager_->store.load();
        c.clientManager_ = std::make_unique<ClientManager>(c);
        auto& s = *c.getSettingsManager();
        s.set(SettingsManager::KEEP_LISTS, true);
#ifdef WITH_DHT
        s.set(SettingsManager::USE_DHT, false);
        s.set(SettingsManager::DHT_BOOTSTRAP_URLS, ";");
        c.dht_ = std::make_unique<dht::DHT>(c);
#endif
    }
    DCContext& context() { return *fixture.ownedCtx; }
    HashManager& hashes() { return *context().getHashManager(); }
    ClientManager& users() { return *context().getClientManager(); }
    string path(const string& name) { return (fixture.tmpDir / name).string(); }
    void hash(const char* name, const string& bytes) { hashes().addTree(path(name), 1700000000, tree(bytes)); }
    void user(const char* nick, char cid = 'B') {
        write(path("Users.xml"), string("<Users><User CID=\"") + string(39, cid) + "\" Nick=\"" + nick + "\"/></Users>");
        users().loadUsers(); users().saveUser(CID(string(39, cid)));
    }
    void queue() {
        auto& c = context();
        c.searchManager_ = std::make_unique<SearchManager>(c);
        c.queueManager_ = std::make_unique<QueueManager>(c);
    }
#ifdef WITH_DHT
    dht::DHT& network() { return *context().getDHT(); }
    void routing() { network().bucket = new dht::KBucket(network()); }
    void node() {
        auto n = network().createNode(CID(string(39, 'C')), "8.8.4.4", "6250", false, true);
        REQUIRE(n); REQUIRE(network().addNode(n, false));
        network().getIndexManager().addSource(tree("network-content").getRoot(), n, 8192, false);
    }
#endif
};
}

#ifndef _WIN32
TEST_CASE("Snapshot buffered flush failure cannot replace a valid previous file", "[snapshot-save][snapshot-flush]") {
    Fixture f; string name, root; std::function<bool()> save;
    SECTION("hash index") {
        f.hash("first.bin", "first"); REQUIRE(saveHash(f.hashes())); f.hash("second.bin", "second");
        name = "HashIndex.xml"; root = "HashStore"; save = [&] { return saveHash(f.hashes()); };
    }
    SECTION("users") {
        f.user("Original"); REQUIRE(saveUsers(f.users()));
        f.users().nicks[CID(string(39, 'B'))].first = "Updated";
        name = "Users.xml"; root = "Users"; save = [&] { return saveUsers(f.users()); };
    }
#ifdef WITH_DHT
    SECTION("DHT") {
        f.routing(); f.node(); REQUIRE(saveDht(f.network())); f.network().setDirty();
        name = "dht.xml"; root = "DHT"; save = [&] { return saveDht(f.network()); };
    }
#endif
    const auto previous = read(f.path(name));
    SimpleXML valid; REQUIRE_NOTHROW(valid.fromXML(previous)); REQUIRE(valid.findChild(root));
    // This fixture has no running workers. Only a disposable child gets a
    // zero file-size limit: physical writes during buffered flush fail EFBIG.
    // _Exit belongs solely to the fixture child, not application shutdown.
    auto child = ::fork(); REQUIRE(child >= 0);
    if(child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit limit{0, 0};
        if(::setrlimit(RLIMIT_FSIZE, &limit)) std::_Exit(3);
        std::_Exit(save() ? 1 : 0);
    }
    int status = 0; REQUIRE(::waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status)); CHECK(WEXITSTATUS(status) == 0);
    CHECK(read(f.path(name)) == previous); REQUIRE(save());
}
#endif

TEST_CASE("Hash index staging failure retains the previous snapshot and pending entries", "[snapshot-save][hash-index-save]") {
    Fixture f; f.hash("first.bin", "first"); REQUIRE(saveHash(f.hashes()));
    const auto previous = read(f.path("HashIndex.xml"));
    f.hash("second.bin", "second"); fs::create_directory(f.path("HashIndex.xml.tmp"));
    CHECK_FALSE(saveHash(f.hashes())); CHECK(read(f.path("HashIndex.xml")) == previous);
    CHECK(f.hashes().store.isDirty());
    fs::remove(f.path("HashIndex.xml.tmp")); REQUIRE(saveHash(f.hashes()));
    CHECK_FALSE(f.hashes().store.isDirty());
    HashManager::HashStore loaded(f.context()); loaded.load();
    REQUIRE(loaded.getTTH(f.path("first.bin"))); REQUIRE(loaded.getTTH(f.path("second.bin")));
    CHECK(*loaded.getTTH(f.path("second.bin")) == tree("second").getRoot());
    CHECK(loaded.checkTTH(f.path("second.bin"), 6, 1700000000));
}

TEST_CASE("Hash index replacement failure is reported without losing the old index", "[snapshot-save][hash-index-save]") {
    Fixture f; f.hash("first.bin", "first"); REQUIRE(saveHash(f.hashes()));
    const auto previous = read(f.path("HashIndex.xml")); f.hash("second.bin", "second");
    write(f.path("HashIndex.xml.tmp"), "staging");
    {
        DenyReplace deny(f.path("HashIndex.xml"));
#ifndef _WIN32
        REQUIRE_THROWS_AS(File(f.path("permission-probe"), File::WRITE, File::CREATE), FileException);
#endif
        CHECK_FALSE(saveHash(f.hashes())); CHECK(read(f.path("HashIndex.xml")) == previous);
        CHECK(f.hashes().store.isDirty());
        CHECK(read(f.path("HashIndex.xml.tmp")).find("second.bin") != string::npos);
    }
    REQUIRE(saveHash(f.hashes())); CHECK_FALSE(f.hashes().store.isDirty());
}

TEST_CASE("Clean hash index remains readable and needs no write after terminal stop", "[snapshot-save][hash-index-save]") {
    Fixture f; f.hash("first.bin", "first"); REQUIRE(saveHash(f.hashes()));
    const auto previous = read(f.path("HashIndex.xml")); f.hashes().stopForShutdown();
    fs::create_directory(f.path("HashIndex.xml.tmp"));
    CHECK(saveHash(f.hashes())); CHECK(read(f.path("HashIndex.xml")) == previous);
    CHECK(f.hashes().checkTTH(f.path("first.bin"), 5, 1700000000));
}

TEST_CASE("Saved user staging failure preserves nick intent for a checked retry", "[snapshot-save][users-save]") {
    Fixture f; f.user("Original"); REQUIRE(saveUsers(f.users()));
    const auto previous = read(f.path("Users.xml"));
    f.users().nicks[CID(string(39, 'B'))].first = "Updated & saved";
    fs::create_directory(f.path("Users.xml.tmp"));
    CHECK_FALSE(saveUsers(f.users())); CHECK(read(f.path("Users.xml")) == previous);
    fs::remove(f.path("Users.xml.tmp")); REQUIRE(saveUsers(f.users()));
    f.users().nicks.clear(); f.users().loadUsers();
    CHECK(f.users().getNicks(CID(string(39, 'B')), {}) == StringList{"Updated & saved"});
}

TEST_CASE("Saved user replacement failure preserves previous regular file and retries", "[snapshot-save][users-save]") {
    Fixture f; f.user("Original"); REQUIRE(saveUsers(f.users()));
    const auto previous = read(f.path("Users.xml"));
    f.users().nicks[CID(string(39, 'B'))].first = "Updated"; write(f.path("Users.xml.tmp"), "staging");
    {
        DenyReplace deny(f.path("Users.xml"));
        CHECK_FALSE(saveUsers(f.users())); CHECK(read(f.path("Users.xml")) == previous);
        CHECK(read(f.path("Users.xml.tmp")).find("Updated") != string::npos);
    }
    REQUIRE(saveUsers(f.users()));
    f.users().nicks.clear(); f.users().loadUsers();
    CHECK(f.users().getNicks(CID(string(39, 'B')), {}) == StringList{"Updated"});
}

TEST_CASE("Final user cache includes the queue source save tail but omits unrelated nicks", "[snapshot-save][users-save]") {
    Fixture f; f.queue(); f.user("Queue source"); f.user("Not queued", 'D');
    f.users().nicks[CID(string(39, 'B'))].second = false;
    f.users().nicks[CID(string(39, 'D'))].second = false;
    auto& q = *f.context().getQueueManager();
    auto* item = q.fileQueue.add(f.path("queued.bin"), 65536, 0, QueueItem::NORMAL, {}, 1700000000, tree("queue").getRoot());
    item->addSource(HintedUser(f.users().getUser(CID(string(39, 'B'))), "adc://fixture"));
    CHECK(q.stopCleanupForShutdown()); f.users().stopForShutdown();
    REQUIRE(q.saveQueueChecked(true)); REQUIRE(saveUsers(f.users()));
    f.users().nicks.clear(); f.users().loadUsers();
    CHECK(f.users().getNicks(CID(string(39, 'B')), {}) == StringList{"Queue source"});
    CHECK(f.users().nicks.count(CID(string(39, 'D'))) == 0);
    CHECK(read(f.path("Queue.xml")).find("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBA") != string::npos);
}

TEST_CASE("Concurrent saved user snapshots do not collide on one staging file", "[snapshot-save][users-save]") {
    Fixture f; f.user("One saved nick");
    auto first = std::async(std::launch::async, [&] { return saveUsers(f.users()); });
    auto second = std::async(std::launch::async, [&] { return saveUsers(f.users()); });
    CHECK(first.get()); CHECK(second.get());
    f.users().nicks.clear(); f.users().loadUsers();
    CHECK(f.users().getNicks(CID(string(39, 'B')), {}) == StringList{"One saved nick"});
}

#ifdef WITH_DHT
TEST_CASE("DHT mutation marked during a snapshot stays pending for the next save", "[snapshot-save][dht-save][dht-dirty-race]") {
    Fixture f; f.routing(); f.node(); REQUIRE(saveDht(f.network())); f.network().setDirty();
    Semaphore done;
    std::thread watchdog([&] {
        if(!done.wait(15000)) { std::fputs("DHT dirty fixture exceeded 15 seconds\n", stderr); std::fflush(stderr); std::_Exit(EXIT_FAILURE); }
    });
    auto finish = makeScopedFunctor([&] { done.signal(); watchdog.join(); });
    std::unique_lock<CriticalSection> indexLock(f.network().getIndexManager().cs);
    auto saving = std::async(std::launch::async, [&] { return saveDht(f.network()); });
    auto release = makeScopedFunctor([&] { if(indexLock.owns_lock()) indexLock.unlock(); });
    for(unsigned i = 0; i < 2000 && f.network().dirty.load(); ++i) Thread::sleep(1);
    CHECK_FALSE(f.network().dirty.load());
    CHECK(saving.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    // This producer already owns the index lock. A cs-taking setDirty would
    // invert the writer lock order; a clear-at-end save would lose this mark.
    f.network().setDirty(); indexLock.unlock(); REQUIRE(saving.get());
    fs::create_directory(f.path("dht.xml.tmp")); CHECK_FALSE(saveDht(f.network()));
    CHECK(f.network().dirty); fs::remove(f.path("dht.xml.tmp"));
    REQUIRE(saveDht(f.network())); CHECK_FALSE(f.network().dirty);
}

TEST_CASE("DHT snapshot loader accepts emitted files and legacy indexes", "[snapshot-save][dht-save]") {
    Fixture f; f.routing();
    f.network().bootstrapMgr_ = std::make_unique<dht::BootstrapManager>(f.network());
    string open, close;
    SECTION("emitted file container") { open = "<Files><File"; close = "</File></Files>"; }
    SECTION("legacy index container") { open = "<Indexes><Index"; close = "</Index></Indexes>"; }
    write(f.path("dht.xml"), "<DHT><Nodes/>" + open +
        " TTH=\"LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ\">"
        "<Source CID=\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBA\" I4=\"8.8.4.4\" U4=\"6250\" SI=\"8192\" EX=\"999999\"/>" + close + "</DHT>");
    f.network().loadData();
    dht::IndexManager::SourceList sources;
    REQUIRE(f.network().getIndexManager().findResult(TTHValue("LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ"), sources));
    REQUIRE(sources.size() == 1); CHECK(sources.front().getIp() == "8.8.4.4");
    CHECK(sources.front().getSize() == 8192); CHECK(sources.front().getExpires() == 999999);
}

TEST_CASE("DHT staging failure retains pending routing state and supports loader readback", "[snapshot-save][dht-save]") {
    Fixture f; f.routing(); f.node(); REQUIRE(saveDht(f.network()));
    const auto previous = read(f.path("dht.xml"));
    f.network().setDirty(); fs::create_directory(f.path("dht.xml.tmp"));
    CHECK_FALSE(saveDht(f.network())); CHECK(read(f.path("dht.xml")) == previous);
    CHECK(f.network().dirty); fs::remove(f.path("dht.xml.tmp"));
    f.network().stopForShutdown(); REQUIRE(saveDht(f.network())); CHECK_FALSE(f.network().dirty);
    dht::DHT loaded(f.context()); loaded.bucket = new dht::KBucket(loaded);
    loaded.bootstrapMgr_ = std::make_unique<dht::BootstrapManager>(loaded); loaded.loadData();
    REQUIRE(loaded.getBootstrapManager().bootstrapNodes.size() == 1);
    const auto& n = loaded.getBootstrapManager().bootstrapNodes.front();
    CHECK(n.ip == "8.8.4.4"); CHECK(n.udpPort == "6250"); CHECK(n.cid == CID(string(39, 'C')));
    dht::IndexManager::SourceList sources;
    REQUIRE(loaded.getIndexManager().findResult(tree("network-content").getRoot(), sources));
    REQUIRE(sources.size() == 1); CHECK(sources.front().getSize() == 8192);
}

TEST_CASE("DHT replacement failure keeps dirty state and does not overwrite prior bytes", "[snapshot-save][dht-save]") {
    Fixture f; f.routing(); f.node(); REQUIRE(saveDht(f.network()));
    const auto previous = read(f.path("dht.xml")); f.network().setDirty(); write(f.path("dht.xml.tmp"), "staging");
    {
        DenyReplace deny(f.path("dht.xml")); CHECK_FALSE(saveDht(f.network()));
        CHECK(read(f.path("dht.xml")) == previous); CHECK(f.network().dirty);
        CHECK(read(f.path("dht.xml.tmp")).find("8.8.4.4") != string::npos);
    }
    REQUIRE(saveDht(f.network())); CHECK_FALSE(f.network().dirty);
}

TEST_CASE("Fresh disabled DHT needs no save but lost dirty routing cannot certify success", "[snapshot-save][dht-save]") {
    Fixture f; fs::create_directory(f.path("dht.xml.tmp")); CHECK(saveDht(f.network()));
    CHECK_FALSE(fs::exists(f.path("dht.xml")));
    f.routing(); f.node(); f.network().stop(true);
    REQUIRE(f.network().bucket == nullptr); CHECK(f.network().dirty);
    CHECK_FALSE(saveDht(f.network())); CHECK_FALSE(saveDht(f.network()));
}

TEST_CASE("Successfully saved DHT needs no repeated write without a new mutation", "[snapshot-save][dht-save]") {
    Fixture f; f.routing(); f.node(); REQUIRE(saveDht(f.network())); CHECK_FALSE(f.network().dirty);
    const auto previous = read(f.path("dht.xml")); fs::create_directory(f.path("dht.xml.tmp"));
    CHECK(saveDht(f.network())); CHECK(read(f.path("dht.xml")) == previous);
}
#endif
