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
