#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <filesystem>
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
#undef private
#include "TestContext.h"
#include "File.h"
#include "LogManager.h"
#include "ScopedFunctor.h"

using namespace dcpp;
namespace fs = std::filesystem;

namespace {
template<class M> bool insertionsSucceeded(const M& manager) {
    if constexpr(requires { manager.dataInsertionsSucceeded(); }) return manager.dataInsertionsSucceeded();
    else return true;
}
template<class S> bool rebuildChecked(S& store) {
    if constexpr(requires { store.rebuildChecked(); }) return store.rebuildChecked();
    else { store.rebuild(); return true; }
}
template<class S> bool storageReady(const S& store) {
    if constexpr(requires { store.storageReady(); }) return store.storageReady();
    else return true;
}
template<class S> bool rebuildsSucceeded(const S& store) {
    if constexpr(requires { store.rebuildsSucceeded(); }) return store.rebuildsSucceeded();
    else return true;
}
TigerTree tree(char byte) {
    TigerTree result(1024);
    string bytes(3072, byte);
    result.update(bytes.data(), bytes.size()); result.finalize(); return result;
}
string read(const string& path) { return File(path, File::READ, File::OPEN).read(); }
int64_t cursor(const string& path) {
    File input(path, File::READ, File::OPEN);
    int64_t value = 0; size_t length = sizeof(value);
    REQUIRE(input.read(&value, length) == sizeof(value)); return value;
}
struct Listener : HashManagerListener {
    unsigned calls = 0;
    void on(TTHDone, const string&, const TTHValue&) noexcept override { ++calls; }
};
struct Fixture {
    test::TestContext fixture;
    Fixture() {
        REQUIRE(fs::equivalent(fs::u8path(Util::getPath(Util::PATH_USER_CONFIG)), fixture.tmpDir));
        context().timerManager_ = std::make_unique<TimerManager>(context());
        context().hashManager_ = std::make_unique<HashManager>(context());
        hashes().store.load();
    }
    DCContext& context() { return *fixture.ownedCtx; }
    HashManager& hashes() { return *context().getHashManager(); }
    string path(const char* name) { return (fixture.tmpDir / name).string(); }
    string data() { return HashManager::HashStore::getDataFile(); }
    void publish(const char* name, const TigerTree& value) {
        File payload(path(name), File::WRITE, File::CREATE | File::TRUNCATE);
        payload.write(string(3072, 'p')); payload.flush();
        hashes().addTree(path(name), 1700000000, value);
    }
    void reload() {
        hashes().stopForShutdown(); context().hashManager_.reset();
        context().hashManager_ = std::make_unique<HashManager>(context());
        hashes().store.load();
    }
};
struct DenyWrite {
#ifdef _WIN32
    std::unique_ptr<File> held;
    explicit DenyWrite(const string& path) : held(std::make_unique<File>(path, File::READ, File::OPEN)) { }
#else
    string path; fs::perms original;
    explicit DenyWrite(const string& path) : path(path), original(fs::status(path).permissions()) {
        REQUIRE(getuid() != 0);
        fs::permissions(path, fs::perms::owner_read);
    }
    ~DenyWrite() { std::error_code error; fs::permissions(path, original, error); }
#endif

};
struct FlushFile : File {
    string path;
    vector<int64_t> cursors;
    unsigned failAt;
    FlushFile(const string& path, unsigned failAt = 0) : File(path, READ | WRITE, OPEN), path(path), failAt(failAt) { }
    size_t flush() override {
        cursors.push_back(cursor(path));
        if(cursors.size() == failAt) throw FileException("fixture flush refusal");
        return File::flush();
    }
};
struct ShortReadFile : File {
    explicit ShortReadFile(const string& path) : File(path, READ, OPEN) { }
    size_t read(void* buffer, size_t& length) override {
        const size_t requested = length;
        length = min(length, size_t(TTHValue::BYTES));
        const size_t result = File::read(buffer, length);
        // This fixture's leaves are identical. A matching stale buffer tail
        // cannot turn a reported short read into valid persistence evidence.
        if(length == TTHValue::BYTES)
            for(size_t offset = length; offset + length <= requested; offset += length)
                memcpy(static_cast<uint8_t*>(buffer) + offset, buffer, length);
        return result;
    }
};
void checkTree(HashManager& manager, const TigerTree& expected) {
    TigerTree actual;
    REQUIRE(manager.getTree(expected.getRoot(), actual));
    CHECK(actual.getRoot() == expected.getRoot());
    CHECK(actual.getLeaves() == expected.getLeaves());
}
}

TEST_CASE("TestContext replaces initialized paths before constructing hash storage", "[test-isolation][hash-data-persistence]") {
    const auto decoy = fs::temp_directory_path() / "eiskalt-initialized-path-decoy";
    REQUIRE(fs::create_directory(decoy));
    ScopedFunctor cleanup([&] { std::error_code error; fs::remove_all(decoy, error); });
    const auto sentinel = (decoy / "sentinel").string();
    { File output(sentinel, File::WRITE, File::CREATE); output.write("untouched"); output.flush(); }
    Util::PathsMap initialized;
    initialized[Util::PATH_USER_CONFIG] = decoy.string() + PATH_SEPARATOR;
    initialized[Util::PATH_USER_LOCAL] = decoy.string() + PATH_SEPARATOR;
    Util::uninitialize();
    Util::initialize(initialized);
    {
        test::TestContext fixture;
        // Check ownership before any cache construction, including on a broken helper.
        REQUIRE(fs::equivalent(fs::u8path(Util::getPath(Util::PATH_USER_CONFIG)), fixture.tmpDir));
        REQUIRE(fs::equivalent(fs::u8path(Util::getPath(Util::PATH_USER_LOCAL)), fixture.tmpDir));
        fixture.ownedCtx->timerManager_ = std::make_unique<TimerManager>(*fixture.ownedCtx);
        fixture.ownedCtx->hashManager_ = std::make_unique<HashManager>(*fixture.ownedCtx);
        const auto data = HashManager::HashStore::getDataFile();
        CHECK(fs::equivalent(fs::u8path(data).parent_path(), fixture.tmpDir));
        CHECK(cursor(data) == 8);
        fixture.ownedCtx->getHashManager()->addTree(tree('i'));
        CHECK(fixture.ownedCtx->getHashManager()->saveIndexChecked());
        CHECK(fs::is_regular_file(fixture.tmpDir / "HashIndex.xml"));
    }
    CHECK(read(sentinel) == "untouched");
    CHECK(std::distance(fs::directory_iterator(decoy), fs::directory_iterator()) == 1);
}

TEST_CASE("HashData insertion survives index reload and terminal stop", "[hash-data-persistence][shutdown]") {
    Fixture f; auto first = tree('a'), second = tree('b');
    CHECK(insertionsSucceeded(f.hashes()));
    f.publish("first.bin", first); f.publish("second.bin", second);
    REQUIRE(f.hashes().saveIndexChecked());
    CHECK(cursor(f.data()) == 8 + 6 * TTHValue::BYTES);
    f.reload(); checkTree(f.hashes(), first); checkTree(f.hashes(), second);
    REQUIRE(f.hashes().getFileTTHif(f.path("first.bin")));
    CHECK(*f.hashes().getFileTTHif(f.path("first.bin")) == first.getRoot());
    f.hashes().stopForShutdown();
    CHECK(insertionsSucceeded(f.hashes())); checkTree(f.hashes(), second);
}

TEST_CASE("HashData open refusal propagates and latches without a false new tree", "[hash-data-persistence][shutdown]") {
    Fixture f; auto old = tree('a'), rejected = tree('b');
    f.publish("old.bin", old); REQUIRE(f.hashes().saveIndexChecked());
    const auto bytes = read(f.data()), payload = read(f.path("old.bin"));
    const auto logsBefore = f.context().getLogManager()->getLastLogs().size();
    { DenyWrite denied(f.data()); CHECK_THROWS_AS(f.hashes().addTree(rejected), FileException); }
    CHECK(f.context().getLogManager()->getLastLogs().size() == logsBefore + 1);
    CHECK_FALSE(insertionsSucceeded(f.hashes()));
    TigerTree absent; CHECK_FALSE(f.hashes().getTree(rejected.getRoot(), absent));
    CHECK_THROWS_AS(f.hashes().addTree(rejected), HashException);
    CHECK(bool(read(f.data()) == bytes)); CHECK(bool(read(f.path("old.bin")) == payload));
    checkTree(f.hashes(), old); f.hashes().addTree(old); checkTree(f.hashes(), old);
    REQUIRE(f.hashes().saveIndexChecked());
    HashManager::HashStore loaded(f.context()); loaded.load();
    REQUIRE(loaded.getTree(old.getRoot(), absent));
    CHECK(absent.getLeaves() == old.getLeaves());
    CHECK_FALSE(loaded.getTree(rejected.getRoot(), absent));
    f.hashes().stopForShutdown(); CHECK_FALSE(insertionsSucceeded(f.hashes()));
}

TEST_CASE("Failed hash publication retains the old file mapping and emits no completion", "[hash-data-persistence][shutdown]") {
    Fixture f; auto old = tree('a'), rejected = tree('b'); f.publish("same.bin", old);
    REQUIRE(f.hashes().saveIndexChecked());
    const auto bytes = read(f.data());
    Listener listener; f.hashes().addListener(&listener);
    ScopedFunctor([&] { f.hashes().removeListener(&listener); });
    { DenyWrite denied(f.data());
      f.hashes().addTree(f.path("same.bin"), 1700000001, rejected);
      f.hashes().addTree(f.path("new.bin"), 1700000001, rejected); }
    CHECK(listener.calls == 0); CHECK_FALSE(insertionsSucceeded(f.hashes()));
    REQUIRE(f.hashes().getFileTTHif(f.path("same.bin")));
    CHECK(*f.hashes().getFileTTHif(f.path("same.bin")) == old.getRoot());
    CHECK(f.hashes().getFileTTHif(f.path("new.bin")) == nullptr);
    CHECK(bool(read(f.data()) == bytes)); checkTree(f.hashes(), old);
    REQUIRE(f.hashes().saveIndexChecked());
    HashManager::HashStore loaded(f.context()); loaded.load();
    REQUIRE(loaded.getTTH(f.path("same.bin")));
    CHECK(*loaded.getTTH(f.path("same.bin")) == old.getRoot());
    CHECK(loaded.getTTH(f.path("new.bin")) == nullptr);
}

TEST_CASE("HashData flushes leaves before cursor and cursor before accepting insertion", "[hash-data-persistence][hash-data-flush][shutdown]") {
    Fixture f; auto value = tree('a'); FlushFile output(f.data());
    CHECK(f.hashes().store.saveTree(output, value) == 8);
    REQUIRE(output.cursors.size() == 2);
    CHECK(output.cursors[0] == 8);
    CHECK(output.cursors[1] == 8 + 3 * TTHValue::BYTES);
}

TEST_CASE("HashData saveTree reports either physical flush boundary refusal", "[hash-data-persistence][hash-data-flush][shutdown]") {
    Fixture f; unsigned failAt = 1;
    SECTION("leaf flush") { failAt = 1; }
    SECTION("cursor flush") { failAt = 2; }
    FlushFile output(f.data(), failAt);
    CHECK_THROWS_AS(f.hashes().store.saveTree(output, tree('a')), FileException);
    CHECK(output.cursors.size() == failAt);
    CHECK(cursor(f.data()) == (failAt == 1 ? 8 : 8 + 3 * TTHValue::BYTES));
}

TEST_CASE("HashData rejects invalid or overlapping loaded cursors without overwriting old leaves", "[hash-data-persistence][shutdown]") {
    Fixture f; auto old = tree('a'), rejected = tree('b'); f.publish("old.bin", old);
    REQUIRE(f.hashes().saveIndexChecked());
    int64_t invalid = 0;
    SECTION("zero cursor") { invalid = 0; }
    SECTION("negative cursor") { invalid = -1; }
    SECTION("cursor beyond file") { invalid = File::getSize(f.data()) + 1; }
    SECTION("cursor overlaps an indexed tree") { invalid = 8; }
    { File output(f.data(), File::WRITE, File::OPEN); output.write(&invalid, sizeof(invalid)); output.flush(); }
    const auto bytes = read(f.data()); f.reload();
    CHECK_THROWS_AS(f.hashes().addTree(rejected), HashException);
    CHECK_FALSE(insertionsSucceeded(f.hashes()));
    CHECK(bool(read(f.data()) == bytes)); checkTree(f.hashes(), old);
    TigerTree absent; CHECK_FALSE(f.hashes().getTree(rejected.getRoot(), absent));
}

TEST_CASE("HashData refuses short cursor reads without announcing a tree", "[hash-data-persistence][shutdown]") {
    Fixture f; File(f.data(), File::WRITE, File::OPEN).setSize(4);
    CHECK_THROWS_AS(f.hashes().addTree(tree('a')), HashException);
    CHECK_FALSE(insertionsSucceeded(f.hashes()));
    CHECK(File::getSize(f.data()) == 4);
}

TEST_CASE("A short HashData leaf read never assigns partially constructed output", "[hash-data-persistence][shutdown]") {
    Fixture f; auto old = tree('a'), sentinel = tree('z'); f.publish("old.bin", old);
    File(f.data(), File::WRITE, File::OPEN).setSize(8 + TTHValue::BYTES);
    TigerTree output = sentinel;
    CHECK_FALSE(f.hashes().getTree(old.getRoot(), output));
    CHECK(output.getRoot() == sentinel.getRoot()); CHECK(output.getLeaves() == sentinel.getLeaves());
}

TEST_CASE("HashData rejects a short read even when the physical file has enough bytes", "[hash-data-persistence][shutdown]") {
    Fixture f; auto old = tree('a'), sentinel = tree('z'); f.publish("old.bin", old);
    ShortReadFile input(f.data()); TigerTree output = sentinel;
    auto info = f.hashes().store.treeIndex.at(old.getRoot());
    CHECK_FALSE(f.hashes().store.loadTree(input, info, old.getRoot(), output));
    CHECK(output.getRoot() == sentinel.getRoot()); CHECK(output.getLeaves() == sentinel.getLeaves());
}

TEST_CASE("HashData grows safely when a validated cursor reaches physical EOF", "[hash-data-persistence][shutdown]") {
    Fixture f; auto old = tree('a'), next = tree('b'); f.publish("old.bin", old);
    const auto position = cursor(f.data());
    File(f.data(), File::WRITE, File::OPEN).setSize(position);
    f.hashes().addTree(next);
    CHECK(insertionsSucceeded(f.hashes())); CHECK(File::getSize(f.data()) >= position + 3 * TTHValue::BYTES);
    CHECK(cursor(f.data()) == position + 3 * TTHValue::BYTES);
    checkTree(f.hashes(), old); checkTree(f.hashes(), next);
    REQUIRE(f.hashes().saveIndexChecked()); f.reload(); checkTree(f.hashes(), next);
}

#ifndef _WIN32
TEST_CASE("A real partial HashData append failure preserves old trees and rejects publication", "[hash-data-persistence][hash-data-write][shutdown]") {
    Fixture f; auto old = tree('a'), rejected = tree('b'); f.publish("old.bin", old);
    REQUIRE(f.hashes().saveIndexChecked());
    const auto originalCursor = cursor(f.data()); const auto payload = read(f.path("old.bin"));
    // No product workers have started. Only the disposable child has a file
    // size limit; the parent and installed application are unaffected.
    const auto child = fork(); REQUIRE(child >= 0);
    if(child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        const auto maximum = static_cast<rlim_t>(originalCursor + TTHValue::BYTES);
        struct rlimit limit{maximum, maximum};
        if(setrlimit(RLIMIT_FSIZE, &limit) != 0) std::_Exit(3);
        bool threw = false;
        try { f.hashes().addTree(rejected); } catch(const FileException&) { threw = true; }
        TigerTree absent;
        const bool accepted = f.hashes().getTree(rejected.getRoot(), absent);
        std::_Exit(threw && !accepted && !insertionsSucceeded(f.hashes()) ? 0 : 4);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status)); CHECK(WEXITSTATUS(status) == 0);
    CHECK(cursor(f.data()) == originalCursor); CHECK(read(f.path("old.bin")) == payload);
    checkTree(f.hashes(), old); f.reload(); checkTree(f.hashes(), old);
    f.hashes().addTree(rejected); checkTree(f.hashes(), rejected);
    CHECK(insertionsSucceeded(f.hashes()));
}
#endif

TEST_CASE("Hash rebuild XML failure preserves the previous usable pair", "[hash-rebuild][shutdown]") {
    Fixture f; auto dropped = tree('d'), kept = tree('k');
    f.publish("dropped.bin", dropped); f.publish("kept.bin", kept);
    REQUIRE(f.hashes().saveIndexChecked());
    for(auto& [directory, files] : f.hashes().store.fileIndex)
        for(auto& file : files) file.setUsed(file.getFileName() == "kept.bin");
    const auto index = HashManager::HashStore::getIndexFile();
    const auto dataBefore = read(f.data()), indexBefore = read(index), payload = read(f.path("kept.bin"));
    // A directory at the XML staging path permits real data compaction but
    // refuses the index write, exposing the legacy destructive swap.
    fs::create_directory(index + ".tmp");
    CHECK_FALSE(rebuildChecked(f.hashes().store));
    CHECK_FALSE(rebuildsSucceeded(f.hashes().store));
    CHECK(bool(read(f.data()) == dataBefore)); CHECK(read(index) == indexBefore);
    checkTree(f.hashes(), kept); checkTree(f.hashes(), dropped);
    CHECK(read(f.path("kept.bin")) == payload);
    fs::remove(index + ".tmp"); f.reload();
    checkTree(f.hashes(), kept); checkTree(f.hashes(), dropped);
    REQUIRE(f.hashes().getFileTTHif(f.path("kept.bin")));
}

TEST_CASE("Hash startup preserves truncated indexed data and refuses new publication", "[hash-rebuild][shutdown]") {
    Fixture f; f.publish("kept.bin", tree('k')); REQUIRE(f.hashes().saveIndexChecked());
    File(f.data(), File::WRITE, File::OPEN).setSize(2);
    const auto before = read(f.data());
    f.reload();
    CHECK_FALSE(storageReady(f.hashes().store));
    CHECK(bool(read(f.data()) == before));
    CHECK_THROWS_AS(f.hashes().addTree(tree('n')), HashException);
    CHECK_FALSE(f.hashes().saveIndexChecked());
}

TEST_CASE("Hash initial data creation refusal is visible and leaves no false store", "[hash-rebuild][shutdown]") {
    Fixture f; fs::remove(f.data());
    fs::create_directory(f.data());
    HashManager::HashStore rejected(f.context()); rejected.load();
    CHECK_FALSE(storageReady(rejected));
    CHECK_FALSE(rejected.saveChecked());
    CHECK_THROWS_AS(rejected.addTree(tree('n')), HashException);
    CHECK(fs::is_directory(f.data()));
}

TEST_CASE("Hash generations compact used files and reload before and after later appends", "[hash-rebuild][shutdown]") {
    Fixture f; auto dropped = tree('d'), kept = tree('k'), next = tree('n');
    f.publish("dropped.bin", dropped); f.publish("kept.bin", kept);
    REQUIRE(f.hashes().saveIndexChecked()); const auto legacy = read(f.data());
    for(auto& [directory, files] : f.hashes().store.fileIndex)
        for(auto& file : files) file.setUsed(file.getFileName() == "kept.bin");
    REQUIRE(rebuildChecked(f.hashes().store));
    CHECK(f.hashes().dataStorageSucceeded()); CHECK(f.hashes().dataInsertionsSucceeded());
    CHECK(bool(read(f.data()) == legacy));
    const auto first = f.hashes().store.activeDataFile;
    CHECK(cursor(first) == 8 + 3 * TTHValue::BYTES);
    f.reload(); checkTree(f.hashes(), kept);
    TigerTree absent; CHECK_FALSE(f.hashes().getTree(dropped.getRoot(), absent));
    CHECK(f.hashes().getFileTTHif(f.path("dropped.bin")) == nullptr);
    REQUIRE(f.hashes().getFileTTHif(f.path("kept.bin")));
    f.publish("next.bin", next); REQUIRE(f.hashes().saveIndexChecked());
    CHECK(cursor(first) == 8 + 6 * TTHValue::BYTES);
    f.reload(); checkTree(f.hashes(), kept); checkTree(f.hashes(), next);
    REQUIRE(f.hashes().getFileTTHif(f.path("kept.bin")));
    REQUIRE(f.hashes().getFileTTHif(f.path("next.bin")));
    REQUIRE(rebuildChecked(f.hashes().store));
    CHECK(f.hashes().store.activeDataFile != first);
    CHECK_FALSE(fs::exists(first));
    f.reload(); checkTree(f.hashes(), kept); checkTree(f.hashes(), next);
    CHECK(bool(read(f.data()) == legacy));
}

#ifndef _WIN32
TEST_CASE("Hash generation cannot select a symlink outside its profile", "[hash-rebuild][shutdown]") {
    Fixture f; const auto target = f.path("unrelated-data"), generation = f.path("HashData.rebuild.123.dat");
    File::copyFile(f.data(), target); const auto before = read(target);
    fs::create_symlink(target, generation);
    auto index = HashManager::HashStore::getIndexFile();
    { File out(index, File::WRITE, File::CREATE | File::TRUNCATE);
      out.write("<HashStore Version=\"3\" DataFile=\"HashData.rebuild.123.dat\"/>"); out.flush(); }
    f.reload(); CHECK_FALSE(storageReady(f.hashes().store));
    CHECK_THROWS_AS(f.hashes().addTree(tree('n')), HashException);
    CHECK(bool(read(target) == before));
}
#endif

TEST_CASE("Hash rebuild refusal stays visible after a successful retry", "[hash-rebuild][shutdown]") {
    Fixture f; f.publish("kept.bin", tree('k')); REQUIRE(f.hashes().saveIndexChecked());
    auto staging = HashManager::HashStore::getIndexFile() + ".tmp";
    fs::create_directory(staging);
    CHECK_FALSE(rebuildChecked(f.hashes().store));
    CHECK_FALSE(f.hashes().dataStorageSucceeded());
    CHECK(f.hashes().dataInsertionsSucceeded());
    fs::remove(staging); REQUIRE(rebuildChecked(f.hashes().store));
    CHECK_FALSE(f.hashes().dataStorageSucceeded());
    CHECK_FALSE(rebuildsSucceeded(f.hashes().store)); checkTree(f.hashes(), tree('k'));
}

TEST_CASE("Hash rebuild refuses corrupt retained leaves without replacing its index", "[hash-rebuild][shutdown]") {
    Fixture f; f.publish("kept.bin", tree('k')); REQUIRE(f.hashes().saveIndexChecked());
    const auto index = HashManager::HashStore::getIndexFile(), before = read(index);
    { File bad(f.data(), File::WRITE, File::OPEN); bad.setPos(8); bad.write(string(TTHValue::BYTES, 'x')); bad.flush(); }
    const auto data = read(f.data());
    CHECK_FALSE(rebuildChecked(f.hashes().store));
    CHECK(read(index) == before); CHECK(bool(read(f.data()) == data));
    CHECK_FALSE(f.hashes().dataStorageSucceeded());
}

TEST_CASE("Hash rebuild refuses a retained file without its tree instead of dropping it", "[hash-rebuild][shutdown]") {
    Fixture f; auto old = tree('o'); f.publish("old.bin", old); REQUIRE(f.hashes().saveIndexChecked());
    const auto index = HashManager::HashStore::getIndexFile(), before = read(index), bytes = read(f.data());
    f.hashes().store.treeIndex.erase(old.getRoot());
    CHECK_FALSE(rebuildChecked(f.hashes().store));
    CHECK_FALSE(f.hashes().dataStorageSucceeded());
    CHECK(read(index) == before); CHECK(bool(read(f.data()) == bytes));
    f.reload(); checkTree(f.hashes(), old);
}

#ifndef _WIN32
TEST_CASE("Hash rebuild directory lookup refusal is checked and latched", "[hash-rebuild][shutdown]") {
    Fixture f; f.publish("old.bin", tree('o')); REQUIRE(f.hashes().saveIndexChecked());
    REQUIRE(getuid() != 0);
    const auto parent = f.fixture.tmpDir;
    const auto original = fs::status(parent).permissions();
    {
        ScopedFunctor([&] { std::error_code error; fs::permissions(parent, original, error); });
        fs::permissions(parent, fs::perms::owner_read);
        CHECK_FALSE(rebuildChecked(f.hashes().store));
        CHECK_FALSE(f.hashes().dataStorageSucceeded());
    }
    checkTree(f.hashes(), tree('o'));
}
#endif

TEST_CASE("Hash generation selection refuses absent or unsafe references without legacy fallback", "[hash-rebuild][shutdown]") {
    Fixture f; const auto legacy = read(f.data()); string name = "HashData.rebuild.123.dat";
    SECTION("missing generation") { }
    SECTION("relative traversal") { name = "../HashData.dat"; }
    SECTION("absolute path") { name = f.data(); }
    SECTION("unknown XML version") { name = "unknown-version"; }
    SECTION("unrelated XML") { name = "unrelated"; }
    SECTION("incomplete XML") { name = "truncated"; }
    const auto index = HashManager::HashStore::getIndexFile();
    string xml = "<HashStore Version=\"3\" DataFile=\"" + name + "\"/>";
    if(name == "unknown-version") xml = "<HashStore Version=\"4\"/>";
    if(name == "unrelated") xml = "<Unrelated/>";
    if(name == "truncated") xml = "<HashStore Version=\"2\"><Trees>";
    { File out(index, File::WRITE, File::CREATE | File::TRUNCATE); out.write(xml); out.flush(); }
    f.reload();
    CHECK_FALSE(storageReady(f.hashes().store));
    CHECK_FALSE(f.hashes().dataStorageSucceeded());
    CHECK_THROWS_AS(f.hashes().addTree(tree('n')), HashException);
    CHECK_FALSE(f.hashes().saveIndexChecked());
    CHECK(read(index) == xml); CHECK(bool(read(f.data()) == legacy));
}

TEST_CASE("Hash creation preserves a valid minimal legacy cursor", "[hash-rebuild][shutdown]") {
    Fixture f; File(f.data(), File::WRITE, File::OPEN).setSize(8);
    const auto before = read(f.data()); f.reload();
    REQUIRE(storageReady(f.hashes().store));
    CHECK(read(f.data()) == before); CHECK(File::getSize(f.data()) == 8);
    f.publish("next.bin", tree('n')); REQUIRE(f.hashes().saveIndexChecked());
    f.reload(); checkTree(f.hashes(), tree('n'));
}

TEST_CASE("Hash worker startup retains already used cached file mappings", "[hash-rebuild][shutdown]") {
    Fixture f; auto old = tree('o'); f.publish("old.bin", old); REQUIRE(f.hashes().saveIndexChecked());
    f.hashes().stopForShutdown(); f.context().hashManager_.reset();
    f.context().hashManager_ = std::make_unique<HashManager>(f.context());
    // Constructor selects the committed pair, without an extra fixture load.
    checkTree(f.hashes(), old);
    REQUIRE(f.hashes().getFileTTHif(f.path("old.bin")));
    f.hashes().startup(); REQUIRE(rebuildChecked(f.hashes().store));
    checkTree(f.hashes(), old);
    REQUIRE(f.hashes().getFileTTHif(f.path("old.bin")));
}

TEST_CASE("Hash failed generation commit leaves the saved pair and memory unchanged", "[hash-rebuild][shutdown]") {
    Fixture f; auto old = tree('o'); f.publish("old.bin", old); REQUIRE(f.hashes().saveIndexChecked());
    const auto index = HashManager::HashStore::getIndexFile(), before = read(index), bytes = read(f.data());
    auto state = f.hashes().store.prepareRebuild();
    fs::remove(index + ".tmp"); fs::create_directory(index + ".tmp");
    CHECK_THROWS_AS(f.hashes().store.commitRebuild(state), FileException);
    CHECK(read(index) == before); CHECK(bool(read(f.data()) == bytes));
    checkTree(f.hashes(), old); f.reload(); checkTree(f.hashes(), old);
}

#ifndef _WIN32
TEST_CASE("Hash rebuild survives process exit at its staged and committed boundaries", "[hash-rebuild][hash-rebuild-interruption][shutdown]") {
    Fixture f; auto dropped = tree('d'), kept = tree('k');
    f.publish("dropped.bin", dropped); f.publish("kept.bin", kept); REQUIRE(f.hashes().saveIndexChecked());
    for(auto& [directory, files] : f.hashes().store.fileIndex)
        for(auto& file : files) file.setUsed(file.getFileName() == "kept.bin");
    const auto index = HashManager::HashStore::getIndexFile(), before = read(index), bytes = read(f.data());
    bool commit = false;
    SECTION("exit after staging before index switch") { }
    SECTION("exit immediately after index switch") { commit = true; }
    // No fixture workers exist. This abrupt exit affects only the child,
    // exercising actual staged files and the production atomic replacement.
    const auto child = fork(); REQUIRE(child >= 0);
    if(child == 0) {
        try {
            auto state = f.hashes().store.prepareRebuild();
            if(commit) f.hashes().store.commitRebuild(state);
            std::_Exit(0);
        } catch(...) { std::_Exit(2); }
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status)); REQUIRE(WEXITSTATUS(status) == 0);
    CHECK(bool(read(f.data()) == bytes));
    CHECK((read(index) == before) == !commit);
    f.reload(); REQUIRE(storageReady(f.hashes().store)); checkTree(f.hashes(), kept);
    TigerTree unused; CHECK(f.hashes().getTree(dropped.getRoot(), unused) == !commit);
    CHECK((f.hashes().getFileTTHif(f.path("dropped.bin")) != nullptr) == !commit);
    REQUIRE(f.hashes().getFileTTHif(f.path("kept.bin")));
    f.publish("next.bin", tree('n')); REQUIRE(f.hashes().saveIndexChecked());
    f.reload(); checkTree(f.hashes(), kept); checkTree(f.hashes(), tree('n'));
}

TEST_CASE("A native hash creation write refusal cannot certify a usable store", "[hash-rebuild][shutdown]") {
    Fixture f; fs::remove(f.data()); const auto child = fork(); REQUIRE(child >= 0);
    if(child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit limit{0, 0}; if(setrlimit(RLIMIT_FSIZE, &limit) != 0) std::_Exit(3);
        HashManager::HashStore store(f.context());
        std::_Exit(!storageReady(store) && !store.saveChecked() ? 0 : 4);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status)); CHECK(WEXITSTATUS(status) == 0);
    CHECK_FALSE(fs::exists(f.data()));
}
#endif
