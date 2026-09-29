#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
// Assemble real managers without the application's networking/startup side effects.
#define private public
#include "DCContext.h"
#undef private
#include "DCPlusPlus.h"
#include "ClientManager.h"
#include "File.h"
#include "LogManager.h"
#define private public
#include "HashManager.h"
#undef private
#include "QueueManager.h"
#include "SearchManager.h"
#include "SearchResult.h"
#define private public
#include "ShareManager.h"
#undef private
#include "UploadManager.h"
#include "BZUtils.h"
#include "FilteredFile.h"
#include "SimpleXML.h"
#include "DirectoryListing.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <atomic>
#include <thread>
#ifndef _WIN32
#include <sys/resource.h>
#include <utime.h>
#endif

using namespace dcpp;
namespace fs = std::filesystem;

namespace {
struct ManagedShareFixture {
    DCContext context;
    DCContext* previous = getContext();
    Util::PathsMap paths;
    fs::path temp;

    ManagedShareFixture() {
        for (int i = 0; i < Util::PATH_LAST; ++i)
            paths[static_cast<Util::Paths>(i)] = Util::getPath(static_cast<Util::Paths>(i));
        temp = fs::canonical(fs::temp_directory_path()) /
            ("eiskalt_managedshare_" + Util::toString(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(temp / "config");
        Util::uninitialize();
        auto overrides = paths;
        overrides[Util::PATH_USER_CONFIG] = (temp / "config").string() + PATH_SEPARATOR;
        overrides[Util::PATH_USER_LOCAL] = overrides[Util::PATH_USER_CONFIG];
        overrides[Util::PATH_DOWNLOADS] = temp.string() + PATH_SEPARATOR;
        overrides[Util::PATH_FILE_LISTS] = (temp / "lists").string() + PATH_SEPARATOR;
        overrides[Util::PATH_HUB_LISTS] = (temp / "hubs").string() + PATH_SEPARATOR;
        overrides[Util::PATH_NOTEPAD] = (temp / "notepad.txt").string();
        Util::initialize(overrides);
        context.startupMinimal();
        setContext(&context);
        context.timerManager_ = std::make_unique<TimerManager>(context);
        context.hashManager_ = std::make_unique<HashManager>(context);
        context.searchManager_ = std::make_unique<SearchManager>(context);
        context.clientManager_ = std::make_unique<ClientManager>(context);
        context.queueManager_ = std::make_unique<QueueManager>(context);
        context.uploadManager_ = std::make_unique<UploadManager>(context);
        context.shareManager_ = std::make_unique<ShareManager>(context);
        context.getSettingsManager()->set(SettingsManager::LIST_DUPES, true);
        context.getSettingsManager()->set(SettingsManager::SHARE_SKIP_ZERO_BYTE, false);
    }

    ~ManagedShareFixture() {
        context.shutdown();
        setContext(previous);
        Util::initialize(paths);
        std::error_code ec;
        fs::remove_all(temp, ec);
    }

    ShareManager& shares() { return *context.getShareManager(); }

    string file(const string& name, const string& data = "completed payload") {
        const auto path = temp / name;
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << data;
        out.close();
        return path.string();
    }

    string list() {
        return partial("/");
    }

    string partial(const string& base, bool recurse = true) {
        std::unique_ptr<MemoryInputStream> stream(shares().generatePartialList(base, recurse));
        REQUIRE(stream);
        string xml(stream->getSize(), '\0');
        auto size = xml.size();
        stream->read(&xml[0], size);
        return xml;
    }

    string fullList() {
        shares().forceXmlRefresh = true;
        File file(shares().getOwnListFile(), File::READ, File::OPEN);
        FilteredInputStream<UnBZFilter, false> input(&file);
        string xml;
        char buffer[4096];
        for (;;) {
            size_t size = sizeof(buffer);
            size = input.read(buffer, size);
            if (!size) break;
            xml.append(buffer, size);
        }
        return xml;
    }

    void modified(const string& path, int64_t seconds) {
#ifdef _WIN32
        auto handle = CreateFileW(fs::path(path).wstring().c_str(), FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        REQUIRE(handle != INVALID_HANDLE_VALUE);
        ULARGE_INTEGER ticks;
        ticks.QuadPart = (seconds + 11644473600LL) * 10000000ULL;
        FILETIME time{ticks.LowPart, ticks.HighPart};
        const bool ok = SetFileTime(handle, nullptr, nullptr, &time);
        CloseHandle(handle);
        REQUIRE(ok);
#else
        utimbuf times{static_cast<time_t>(seconds), static_cast<time_t>(seconds)};
        REQUIRE(::utime(path.c_str(), &times) == 0);
#endif
    }

    string manual(const string& name, const string& data, int64_t date) {
        if(!fs::exists(temp / "config/HashData.dat"))
            context.getHashManager()->store.load();
        auto path = file(name, data);
        modified(path, date);
        TigerTree tree(HashManager::MIN_BLOCK_SIZE);
        tree.update(data.data(), data.size()); tree.finalize();
        File input(path, File::READ, File::OPEN);
        context.getHashManager()->addTree(path, input.getLastModified(), tree);
        return path;
    }

    void cache(const string& xml) {
        File output((temp / "config/files.xml.bz2").string(), File::WRITE, File::CREATE | File::TRUNCATE);
        FilteredOutputStream<BZFilter, false> bzip(&output);
        bzip.write(xml); bzip.flush();
    }
};

struct DuringRefresh : LogManagerListener {
    LogManager& log;
    std::function<void()> action;
    std::exception_ptr error;

    DuringRefresh(LogManager& manager, std::function<void()> callback) :
        log(manager), action(std::move(callback)) { log.addListener(this); }
    ~DuringRefresh() { log.removeListener(this); }

    void on(Message, time_t, const string& message) noexcept override {
        if(message != _("File list refresh initiated") || !action) return;
        // The real refresh has captured its roots but has not committed its scan.
        auto callback = std::move(action);
        try { callback(); } catch(...) { error = std::current_exception(); }
    }
};
}

TEST_CASE("Manual share dates round trip through full and partial lists", "[share-dates]") {
    ManagedShareFixture f;
    f.manual("manual/sub/file.txt", "dated", 1700000011);
    f.modified((f.temp / "manual/sub").string(), 1700000022);
    f.modified((f.temp / "manual").string(), 1700000033);
    f.shares().addDirectory((f.temp / "manual").string() + PATH_SEPARATOR, "Manual");
    for(const auto& xml : {f.list(), f.fullList()}) {
        DirectoryListing receiver(f.context, HintedUser());
        MemoryInputStream input(xml); receiver.loadXML(input, false);
        REQUIRE(receiver.getRoot()->getRemoteDate() == 0);
        REQUIRE(receiver.getRoot()->directories.size() == 1);
        auto root = *receiver.getRoot()->directories.begin();
        CHECK(root->getRemoteDate() == 1700000033);
        auto sub = *root->directories.begin();
        CHECK(sub->getRemoteDate() == 1700000022);
        REQUIRE(sub->files.size() == 1);
        CHECK((*sub->files.begin())->getRemoteDate() == 1700000011);
        CHECK((*sub->files.begin())->getTS() == 0);
    }
    CHECK(f.partial("/Manual/").find("BaseDate=\"1700000033\"") != string::npos);
    CHECK(f.partial("/Manual/sub/").find("BaseDate=\"1700000022\"") != string::npos);
    DirectoryListing partialReceiver(f.context, HintedUser());
    partialReceiver.updateXML(f.partial("/Manual/sub/"));
    auto partialRoot = *partialReceiver.getRoot()->directories.begin();
    CHECK(partialRoot->getRemoteDate() == 0);
    auto partialSub = *partialRoot->directories.begin();
    CHECK(partialSub->getRemoteDate() == 1700000022);
    CHECK((*partialSub->files.begin())->getRemoteDate() == 1700000011);
    CHECK(f.partial("/", false).find("Date=\"1700000033\"") != string::npos);
    CHECK(f.list().find("BaseDate=") == string::npos);
}

TEST_CASE("Managed dates survive retraction and restart without hashing unchanged data", "[share-dates]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/file.txt", "dated");
    f.modified(path, 1700000044);
    REQUIRE(f.shares().replaceManagedFiles("torrent:dates", "Completed", {path}));
    const auto hash = f.shares().getTTH("/Completed/file.txt");
    for(bool restart : {false, true}) {
        if(restart) {
            f.context.shareManager_.reset();
            f.context.shareManager_ = std::make_unique<ShareManager>(f.context);
        } else f.shares().removeManagedFiles("torrent:dates");
        int hashes = 0;
        REQUIRE(f.shares().replaceManagedFiles("torrent:dates", "Completed", {path}, {}, [&] { ++hashes; }));
        CHECK(hashes == 0);
        CHECK(f.shares().getTTH("/Completed/file.txt") == hash);
        for(const auto& xml : {f.list(), f.fullList()}) {
            DirectoryListing receiver(f.context, HintedUser());
            MemoryInputStream input(xml); receiver.loadXML(input, false);
            auto root = *receiver.getRoot()->directories.begin();
            CHECK(root->getRemoteDate() == 0);
            CHECK((*root->files.begin())->getRemoteDate() == 1700000044);
        }
        CHECK(f.partial("/Completed/").find("BaseDate=") == string::npos);
    }
}

TEST_CASE("XML share cache preserves only valid dates independently of TS", "[share-dates]") {
    ManagedShareFixture f;
    fs::create_directories(f.temp / "manual");
    f.shares().addDirectory((f.temp / "manual").string() + PATH_SEPARATOR, "Manual");
    for(const auto& date : {string("1700000055"), string(""), string("0"), string("-1"),
                           string("18446744073709551615"), string("17x"), string("+17")}) {
        CAPTURE(date);
        f.shares().directories.front()->files.clear();
        auto attr = date.empty() ? "" : " Date=\"" + date + "\"";
        f.cache("<FileListing Base=\"/\"><Directory Name=\"Manual\"" + attr + ">"
            "<File Name=\"file.txt\" Size=\"5\" TTH=\"" + string(39, 'A') + "\" TS=\"1700000099\"" + attr +
            "/></Directory></FileListing>");
        REQUIRE(f.shares().loadCache());
        DirectoryListing receiver(f.context, HintedUser());
        MemoryInputStream input(f.list()); receiver.loadXML(input, false);
        auto root = *receiver.getRoot()->directories.begin();
        const uint64_t expected = date == "1700000055" ? 1700000055 : 0;
        CHECK(root->getRemoteDate() == expected);
        CHECK((*root->files.begin())->getRemoteDate() == expected);
    }
}

TEST_CASE("Share date copies survive while merged directories omit ambiguous dates", "[share-dates]") {
    ManagedShareFixture f;
    f.manual("one/sub/first.txt", "first", 1700000011);
    f.manual("two/sub/second.txt", "second", 1700000022);
    f.manual("two/unique/third.txt", "third", 1700000033);
    f.modified((f.temp / "two/unique").string(), 1700000044);
    f.shares().addDirectory((f.temp / "one").string() + PATH_SEPARATOR, "Merged");
    const auto& original = *f.shares().directories.front()->directories.at("sub")->files.begin();
    ShareManager::Directory::File copy(original), assigned;
    assigned = original;
    CHECK(copy.modified == 1700000011);
    CHECK(assigned.modified == 1700000011);
    f.shares().addDirectory((f.temp / "two").string() + PATH_SEPARATOR, "Merged");
    CHECK(f.partial("/Merged/").find("BaseDate=") == string::npos);
    CHECK(f.partial("/Merged/sub/").find("BaseDate=") == string::npos);
    CHECK(f.partial("/Merged/unique/").find("BaseDate=\"1700000044\"") != string::npos);
    for(const auto& xml : {f.list(), f.fullList()}) {
        DirectoryListing receiver(f.context, HintedUser());
        MemoryInputStream input(xml); receiver.loadXML(input, false);
        auto root = *receiver.getRoot()->directories.begin();
        CHECK(root->getRemoteDate() == 0);
        for(auto child : root->directories) {
            CHECK(child->getRemoteDate() == (child->getName() == "unique" ? 1700000044 : 0));
            for(auto file : child->files)
                CHECK(file->getRemoteDate() == (file->getName() == "first.txt" ? 1700000011 :
                    file->getName() == "second.txt" ? 1700000022 : 1700000033));
        }
    }
}

TEST_CASE("Unmerged descendants keep dates through a merged XML cache round trip", "[share-dates][share-dates-review]") {
    ManagedShareFixture f;
    f.manual("one/shared/first.txt", "first", 1700000011);
    f.manual("two/shared/second.txt", "second", 1700000022);
    f.manual("two/unique/third.txt", "third", 1700000033);
    f.modified((f.temp / "two/unique").string(), 1700000044);
    f.shares().addDirectory((f.temp / "one").string() + PATH_SEPARATOR, "Merged");
    f.shares().addDirectory((f.temp / "two").string() + PATH_SEPARATOR, "Merged");
    const auto saved = f.fullList();
    f.shares().directories.front()->directories.clear();
    f.cache(saved);
    REQUIRE(f.shares().loadCache());
    CHECK(f.partial("/Merged/").find("BaseDate=") == string::npos);
    CHECK(f.partial("/Merged/shared/").find("BaseDate=") == string::npos);
    CHECK(f.partial("/Merged/unique/").find("BaseDate=\"1700000044\"") != string::npos);
    CHECK(f.partial("/Merged/unique/").find("Date=\"1700000033\"") != string::npos);
}

#ifndef _WIN32
TEST_CASE("Manual dates follow the scanner's native filename encoding", "[share-dates][share-dates-review]") {
    ManagedShareFixture f;
    // Native UTF-8 bytes are legal on macOS too. In a Latin-1 process charset,
    // the internal UTF-8 spelling differs, just as FileFindIter converts it.
    const auto nativeFile = f.manual("caf\xc3\xa9/file.txt", "payload", 1700000011);
    const auto nativeDir = fs::path(nativeFile).parent_path().string() + PATH_SEPARATOR;
    f.modified(nativeDir, 1700000022);
    struct CharsetGuard {
        string previous = Text::systemCharset;
        ~CharsetGuard() { Text::systemCharset = previous; }
    } restore;
    Text::systemCharset = "ISO-8859-1";
    const auto internalFile = Text::toUtf8(nativeFile);
    const auto internalDir = Text::toUtf8(nativeDir);
    REQUIRE(internalDir != nativeDir);
    TigerTree tree(HashManager::MIN_BLOCK_SIZE);
    tree.update("payload", 7); tree.finalize();
    File input(internalFile, File::READ, File::OPEN);
    f.context.getHashManager()->addTree(internalFile, input.getLastModified(), tree);
    f.shares().addDirectory(internalDir, "Manual");
    auto root = f.shares().directories.front();
    REQUIRE(root->files.size() == 1);
    CHECK(root->modified == 1700000022);
    CHECK(root->files.begin()->modified == 1700000011);
}
#endif

TEST_CASE("Hash completion cannot attach a current date to an earlier file identity", "[share-dates]") {
    ManagedShareFixture f;
    const auto path = f.manual("manual/file.txt", "old payload", 1700000011);
    f.shares().addDirectory((f.temp / "manual").string() + PATH_SEPARATOR, "Manual");
    auto dir = f.shares().directories.front();
    const auto oldHash = dir->files.begin()->getTTH();
    REQUIRE(dir->files.begin()->modified == 1700000011);
    f.file("manual/file.txt", "new payload");
    f.modified(path, 1700000022);
    f.shares().on(HashManagerListener::TTHDone(), path, oldHash);
    CHECK(dir->files.begin()->modified == 0);
    DirectoryListing receiver(f.context, HintedUser());
    MemoryInputStream input(f.list()); receiver.loadXML(input, false);
    CHECK((*(*receiver.getRoot()->directories.begin())->files.begin())->getRemoteDate() == 0);
    f.shares().on(HashManagerListener::TTHDone(), f.file("manual/new.txt"), oldHash);
    CHECK(dir->findFile("new.txt")->modified == 0);
}

TEST_CASE("Manual directory metadata refresh does not queue file rehashing", "[share-dates]") {
    ManagedShareFixture f;
    f.manual("manual/file.txt", "payload", 1700000011);
    const auto path = (f.temp / "manual").string() + PATH_SEPARATOR;
    f.shares().addDirectory(path, "Manual");
    const auto hash = f.shares().getTTH("/Manual/file.txt");
    f.modified(path, 1700000022);
    auto refreshed = f.shares().buildTree(path, {});
    CHECK(refreshed->modified == 1700000022);
    REQUIRE(refreshed->files.size() == 1);
    CHECK(refreshed->files.begin()->modified == 1700000011);
    CHECK(refreshed->files.begin()->getTTH() == hash);
    CHECK(f.context.getHashManager()->hasher.w.empty());
}

TEST_CASE("Nonpositive observed file dates stay absent instead of wrapping", "[share-dates]") {
    for(int64_t date : {-1, 0}) {
        CAPTURE(date);
        ManagedShareFixture f;
        const auto path = f.manual("manual/file.txt", "payload", date);
        f.shares().addDirectory((f.temp / "manual").string() + PATH_SEPARATOR, "Manual");
        REQUIRE(f.shares().directories.front()->files.size() == 1);
        CHECK(f.shares().directories.front()->files.begin()->modified == 0);
        REQUIRE(f.shares().replaceManagedFiles("torrent:unknown", "Managed", {path}));
        for(const auto& xml : {f.list(), f.fullList()}) {
            DirectoryListing receiver(f.context, HintedUser());
            MemoryInputStream input(xml); receiver.loadXML(input, false);
            for(auto root : receiver.getRoot()->directories)
                CHECK((*root->files.begin())->getRemoteDate() == 0);
        }
    }
}

TEST_CASE("Managed replacement captures its new date without weakening identity validation", "[share-dates]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/file.txt", "old payload");
    f.modified(path, 1700000011);
    REQUIRE(f.shares().replaceManagedFiles("torrent:replace", "Managed", {path}));
    auto before = f.shares().getTTH("/Managed/file.txt");
    f.file("payload/file.txt", "new payload");
    f.modified(path, 1700000022);
    int hashes = 0;
    REQUIRE(f.shares().replaceManagedFiles("torrent:replace", "Managed", {path}, {}, [&] { ++hashes; }));
    CHECK(hashes == 1);
    CHECK(f.shares().getTTH("/Managed/file.txt") != before);
    CHECK(f.partial("/Managed/").find("Date=\"1700000022\"") != string::npos);
    // A touch changes the existing cache identity and must still be validated.
    f.modified(path, 1700000033);
    hashes = 0;
    REQUIRE(f.shares().replaceManagedFiles("torrent:replace", "Managed", {path}, {}, [&] { ++hashes; }));
    CHECK(hashes == 1);
    CHECK(f.partial("/Managed/").find("Date=\"1700000033\"") != string::npos);
}

TEST_CASE("Old single-root cache dates cannot date newly merged virtual directories", "[share-dates]") {
    ManagedShareFixture f;
    for(const auto* name : {"one", "two"}) {
        fs::create_directories(f.temp / name);
        f.shares().addDirectory((f.temp / name).string() + PATH_SEPARATOR, "Merged");
    }
    f.cache("<FileListing Base=\"/\"><Directory Name=\"Merged\" Date=\"1700000011\">"
        "<Directory Name=\"Sub\" Date=\"1700000022\"><File Name=\"file.txt\" Size=\"1\" TTH=\"" +
        string(39, 'A') + "\" Date=\"1700000033\"/></Directory></Directory></FileListing>");
    REQUIRE(f.shares().loadCache());
    CHECK(f.partial("/Merged/").find("BaseDate=") == string::npos);
    CHECK(f.partial("/Merged/Sub/").find("BaseDate=") == string::npos);
    CHECK(f.list().find("Date=\"1700000033\"") != string::npos);
}

TEST_CASE("Managed shares publish only exact hashed files and resolve downloads", "[managedshare]") {
    ManagedShareFixture f;
    const auto selected = f.file("payload/selected.txt", "selected payload");
    f.file("payload/unrelated.txt");
    f.file("payload/private.txt");
    f.file("payload/pending.part");
    f.file("payload/job.torrent");
    REQUIRE(f.shares().replaceManagedFiles("torrent:one", "Completed", {selected}));
    REQUIRE(f.shares().getDirectories().empty());
    REQUIRE(f.shares().getSharedFiles() == 1);
    REQUIRE(f.shares().getShareSize() == 16);
    const string virtualFile = "/Completed/selected.txt";
    const auto tth = f.shares().getTTH(virtualFile);
    TigerTree expected(HashManager::MIN_BLOCK_SIZE);
    expected.update("selected payload", 16);
    expected.finalize();
    REQUIRE(tth == expected.getRoot());
    REQUIRE(f.shares().toVirtual(tth) == virtualFile);
    REQUIRE(f.shares().toReal(virtualFile) == selected);
    REQUIRE(f.shares().toReal("TTH/" + tth.toBase32()) == selected);
    REQUIRE(File(f.shares().toReal(virtualFile), File::READ, File::OPEN).read() == "selected payload");
    REQUIRE(f.shares().getRealPaths(virtualFile) == StringList{selected});
    REQUIRE(f.shares().getRealPaths("/Completed/").empty());
    std::unique_ptr<MemoryInputStream> tree(f.shares().getTree(virtualFile));
    REQUIRE(tree);
    REQUIRE(tree->getSize() > 0);
    SearchResultList adc;
    f.shares().search(adc, StringList{"ANselected"}, 10);
    REQUIRE(adc.size() == 1);
    SearchResultList nmdc;
    f.shares().search(nmdc, "selected", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, nullptr, 10);
    REQUIRE(nmdc.size() == 1);
    REQUIRE(nmdc.front()->getFile() == "Completed\\selected.txt");
    REQUIRE(f.shares().toReal(Util::toAdcFile(nmdc.front()->getFile())) == selected);
    for (const auto& xml : {f.list(), f.fullList()}) {
        REQUIRE(xml.find("selected.txt") != string::npos);
        REQUIRE(xml.find(tth.toBase32()) != string::npos);
        REQUIRE(xml.find("unrelated") == string::npos);
        REQUIRE(xml.find("private") == string::npos);
        REQUIRE(xml.find("pending") == string::npos);
        REQUIRE(xml.find("job.torrent") == string::npos);
    }
    f.file("payload/late.txt");
    f.shares().refresh(true, false, true);
    while (f.shares().isRefreshing()) Thread::sleep(1);
    REQUIRE(f.list().find("selected.txt") != string::npos);
    REQUIRE(f.list().find("late.txt") == string::npos);
    f.shares().removeManagedFiles("torrent:one");
    REQUIRE(f.shares().getSharedFiles() == 0);
    REQUIRE(f.fullList().find("selected.txt") == string::npos);
    REQUIRE_THROWS_AS(f.shares().toReal("TTH/" + tth.toBase32()), ShareException);
    REQUIRE(fs::exists(selected));
    REQUIRE(fs::hard_link_count(selected) == 1);
}

TEST_CASE("Managed sharing exposes cached hashes without reopening payload files", "[managedshare][torrent-sharing]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin", "payload");
    REQUIRE(f.shares().replaceManagedFiles("torrent:cached", "Cached", {path}));
    const auto expected = f.shares().getTTH("/Cached/shared.bin");
    fs::rename(path, path + ".moved");
    const auto cached = f.shares().getManagedFileHashes("torrent:cached");
    REQUIRE(cached.size() == 1);
    REQUIRE(cached.at(path).first == 7);
    REQUIRE(cached.at(path).second == expected);
    std::future<ShareManager::ManagedFileHashes> reader;
    bool ready = false;
    {
        Lock held(f.shares().cs);
        reader = std::async(std::launch::async, [&] {
            return f.shares().getManagedFileHashes("torrent:cached");
        });
        ready = reader.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    }
    REQUIRE(ready);
    REQUIRE(reader.get().at(path).second == expected);
    REQUIRE_THROWS_AS(f.shares().getTTH("/Cached/shared.bin"), ShareException);
    REQUIRE(f.shares().getManagedFileHashes("another-owner").empty());
    f.shares().removeManagedFiles("torrent:cached");
    REQUIRE(f.shares().getManagedFileHashes("torrent:cached").empty());
}

TEST_CASE("Managed publication reuses verified unchanged payload after retraction", "[managedshare][managedhash-reuse]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin", "payload");
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Original", {path}));
    const auto verified = f.shares().managedShares.at("torrent:reuse").files.front();
    f.shares().removeManagedFiles("torrent:reuse");
    REQUIRE(f.shares().getManagedFileHashes("torrent:reuse").empty());
    REQUIRE_THROWS_AS(f.shares().toReal("/Original/shared.bin"), ShareException);
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Renamed", {path}));
    // The immutable verified record (including TTH leaves) is reused, not rebuilt.
    REQUIRE(f.shares().managedShares.at("torrent:reuse").files.front() == verified);
    REQUIRE(f.shares().toReal("/Renamed/shared.bin") == path);
}

TEST_CASE("Managed hash reuse rejects changed payload even with restored modification time", "[managedshare][managedhash-reuse]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin", "before!");
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {path}));
    const auto verified = f.shares().managedShares.at("torrent:reuse").files.front();
    const auto modified = fs::last_write_time(path);
    f.shares().removeManagedFiles("torrent:reuse");
    f.file("payload/shared.bin", "after!!");
    fs::last_write_time(path, modified);
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {path}));
    const auto updated = f.shares().managedShares.at("torrent:reuse").files.front();
    REQUIRE(updated != verified);
    REQUIRE(updated->tth != verified->tth);
}

TEST_CASE("Managed hash cache survives restart without restoring publication", "[managedshare][managedhash-restart]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}));
    const auto hash = f.shares().getManagedFileHashes("torrent:restart").at(path).second;
    f.context.shareManager_.reset();
    f.context.shareManager_ = std::make_unique<ShareManager>(f.context);
    REQUIRE(f.shares().getManagedFileHashes("torrent:restart").empty());
    REQUIRE_THROWS_AS(f.shares().toReal("/Cached/shared.bin"), ShareException);
    REQUIRE(f.shares().managedVerifiedFiles.count("torrent:restart") == 1);
    const auto restored = f.shares().managedVerifiedFiles.at("torrent:restart").front();
    int hashes = 0;
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}, {}, [&] { ++hashes; }));
    REQUIRE(hashes == 0);
    REQUIRE(f.shares().managedShares.at("torrent:restart").files.front()->leaves == restored->leaves);
    REQUIRE(f.shares().getManagedFileHashes("torrent:restart").at(path).second == hash);
}

TEST_CASE("Restart cache revalidates identities and explicit rechecks", "[managedshare][managedhash-restart]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin", "original");
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}));
    const auto oldHash = f.shares().getManagedFileHashes("torrent:restart").at(path).second;
    bool invalidated = false;
    SECTION("same size with restored mtime still checks ctime") {
        const auto modified = fs::last_write_time(path);
        REQUIRE(f.file("payload/shared.bin", "modified") == path);
        fs::last_write_time(path, modified);
    }
    SECTION("different size") { f.file("payload/shared.bin", "longer replacement"); }
    SECTION("explicit recheck survives another process lifetime") {
        f.shares().forgetManagedFileHashes("torrent:restart");
        invalidated = true;
    }
    f.context.shareManager_.reset();
    f.context.shareManager_ = std::make_unique<ShareManager>(f.context);
    int hashes = 0;
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}, {}, [&] { ++hashes; }));
    REQUIRE(hashes == 1);
    const auto newHash = f.shares().getManagedFileHashes("torrent:restart").at(path).second;
    REQUIRE((newHash == oldHash) == invalidated);
}

TEST_CASE("Corrupt restart caches are a safe hash miss", "[managedshare][managedhash-restart]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}));
    const auto cache = f.temp / "config" / "ManagedHashes-v1.bin";
    REQUIRE(fs::is_regular_file(cache));
    f.context.shareManager_.reset();
    SECTION("truncated cache") { fs::resize_file(cache, 8); }
    SECTION("checksum failure") {
        std::fstream stream(cache, std::ios::in | std::ios::out | std::ios::binary);
        stream.seekp(-1, std::ios::end);
        stream.put('\x7f');
    }
    SECTION("symlink cache") {
        fs::rename(cache, cache.string() + ".saved");
        fs::create_symlink(cache.string() + ".saved", cache);
    }
    f.context.shareManager_ = std::make_unique<ShareManager>(f.context);
    REQUIRE(f.shares().managedVerifiedFiles.empty());
    REQUIRE(f.shares().getManagedFileHashes("torrent:restart").empty());
    int hashes = 0;
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}, {}, [&] { ++hashes; }));
    REQUIRE(hashes == 1);
}

TEST_CASE("Restart cache never authorizes changed selection or symlink payloads", "[managedshare][managedhash-restart]") {
    ManagedShareFixture f;
    const auto first = f.file("payload/first.bin"), second = f.file("payload/second.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {first, second}));
    f.context.shareManager_.reset();
    f.context.shareManager_ = std::make_unique<ShareManager>(f.context);
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {first}));
    REQUIRE_THROWS_AS(f.shares().toReal("/Cached/second.bin"), ShareException);
    f.shares().removeManagedFiles("torrent:restart");
    fs::rename(first, first + ".saved");
    fs::create_symlink(first + ".saved", first);
    REQUIRE_FALSE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {first}));
    REQUIRE(f.shares().getManagedFileHashes("torrent:restart").empty());
}

#ifndef _WIN32
TEST_CASE("Failed cache rewrite cannot undo explicit recheck after restart", "[managedshare][managedhash-restart]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}));
    struct LimitGuard {
        rlimit old{};
        bool active = false;
        ~LimitGuard() { if(active) setrlimit(RLIMIT_NOFILE, &old); }
    } limit;
    REQUIRE(getrlimit(RLIMIT_NOFILE, &limit.old) == 0);
    const rlimit exhausted{0, limit.old.rlim_max};
    REQUIRE(setrlimit(RLIMIT_NOFILE, &exhausted) == 0);
    limit.active = true;
    // File creation fails with EMFILE, but unlink still works without a new fd.
    f.shares().forgetManagedFileHashes("torrent:restart");
    REQUIRE(setrlimit(RLIMIT_NOFILE, &limit.old) == 0);
    limit.active = false;
    f.context.shareManager_.reset();
    f.context.shareManager_ = std::make_unique<ShareManager>(f.context);
    int hashes = 0;
    REQUIRE(f.shares().replaceManagedFiles("torrent:restart", "Cached", {path}, {}, [&] { ++hashes; }));
    REQUIRE(hashes == 1);
}
#endif

TEST_CASE("Explicit managed hash invalidation forces fresh verification", "[managedshare][managedhash-reuse]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {path}));
    const auto verified = f.shares().managedShares.at("torrent:reuse").files.front();
    f.shares().removeManagedFiles("torrent:reuse");
    f.shares().forgetManagedFileHashes("torrent:reuse");
    REQUIRE(f.shares().managedVerifiedFiles.empty());
    REQUIRE(f.shares().managedVerifiedBytes == 0);
    REQUIRE(f.shares().managedVerifiedCount == 0);
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {path}));
    const auto updated = f.shares().managedShares.at("torrent:reuse").files.front();
    REQUIRE(updated != verified);
    REQUIRE(updated->tth == verified->tth);
}

TEST_CASE("Reusable managed hashes never restore unselected or unsafe files", "[managedshare][managedhash-reuse]") {
    ManagedShareFixture f;
    const auto first = f.file("payload/first.bin"), second = f.file("payload/second.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {first, second}));
    const auto verified = f.shares().managedShares.at("torrent:reuse").files.front();
    f.shares().removeManagedFiles("torrent:reuse");
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {first}));
    REQUIRE(f.shares().managedShares.at("torrent:reuse").files.front() == verified);
    REQUIRE(f.shares().getManagedFileHashes("torrent:reuse").size() == 1);
    REQUIRE_THROWS_AS(f.shares().toReal("/Cached/second.bin"), ShareException);
    f.shares().removeManagedFiles("torrent:reuse");
    fs::rename(first, first + ".moved");
    fs::create_symlink(first + ".moved", first);
    REQUIRE_FALSE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {first}));
    REQUIRE(f.shares().getManagedFileHashes("torrent:reuse").empty());
}

TEST_CASE("Managed verified cache is bounded and eviction does not unshare files", "[managedshare][managedhash-reuse]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:old", "Cached", {path}));
    const auto verified = f.shares().managedShares.at("torrent:old").files.front();
    // Populate records directly to exercise the cap without hashing thousands of files.
    auto large = std::make_shared<ShareManager::ManagedFile>(*verified);
    large->leaves.resize(ShareManager::managedVerifiedByteLimit / 2);
    ShareManager::ManagedShare snapshot;
    snapshot.files = {large};
    f.shares().rememberManagedFiles("torrent:a", snapshot);
    f.shares().rememberManagedFiles("torrent:b", snapshot);
    REQUIRE(f.shares().managedVerifiedBytes <= ShareManager::managedVerifiedByteLimit);
    REQUIRE(f.shares().managedVerifiedCount <= ShareManager::managedVerifiedFileLimit);
    REQUIRE(f.shares().managedVerifiedFiles.count("torrent:a") == 0);
    REQUIRE(f.shares().toReal("/Cached/shared.bin") == path);
}

TEST_CASE("Explicit hash invalidation fences a replacement holding old cached records", "[managedshare][managedhash-reuse]") {
    ManagedShareFixture f;
    const auto path = f.file("payload/shared.bin");
    REQUIRE(f.shares().replaceManagedFiles("torrent:reuse", "Cached", {path}));
    f.shares().removeManagedFiles("torrent:reuse");
    std::atomic<int> checks{0};
    std::atomic<bool> captured{false}, release{false};
    auto worker = std::async(std::launch::async, [&] {
        return f.shares().replaceManagedFiles("torrent:reuse", "Cached", {path}, [&] {
            // First whitelist check occurs after copying the cache, outside cs.
            if(++checks == 3) {
                captured = true;
                while(!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        });
    });
    struct Release { std::atomic<bool>& flag; ~Release() { flag = true; } } guard{release};
    for(int i = 0; i < 2000 && !captured; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(captured);
    f.shares().forgetManagedFileHashes("torrent:reuse");
    release = true;
    REQUIRE_FALSE(worker.get());
    REQUIRE(f.shares().managedVerifiedFiles.empty());
    REQUIRE(f.shares().getManagedFileHashes("torrent:reuse").empty());
}

TEST_CASE("Managed ownership overlaps survive removal without changing manual shares", "[managedshare]") {
    ManagedShareFixture f;
    const auto first = f.file("data/first.txt", "first");
    const auto second = f.file("data/second.txt", "second");
    const auto manual = f.file("manual/user.txt", "user");
    TigerTree tree(HashManager::MIN_BLOCK_SIZE);
    tree.update("user", 4); tree.finalize();
    File file(manual, File::READ, File::OPEN);
    f.context.getHashManager()->addTree(manual, file.getLastModified(), tree);
    f.shares().addDirectory((f.temp / "manual").string() + PATH_SEPARATOR, "Manual");
    const auto dirs = f.shares().getDirectories();
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {first}));
    REQUIRE(f.shares().replaceManagedFiles("b", "Completed", {first, second}));
    f.shares().removeManagedFiles("a");
    REQUIRE(f.shares().toReal("/Completed/first.txt") == first);
    REQUIRE(f.shares().replaceManagedFiles("b", "Completed", {second}));
    REQUIRE_THROWS_AS(f.shares().toReal("/Completed/first.txt"), ShareException);
    REQUIRE(f.shares().toReal("/Manual/user.txt") == manual);
    REQUIRE_FALSE(f.shares().replaceManagedFiles("c", "Manual", {second}));
    REQUIRE_THROWS_AS(f.shares().addDirectory((f.temp / "data").string() + PATH_SEPARATOR, "Completed"), ShareException);
    f.shares().refresh(true, false, true);
    while (f.shares().isRefreshing()) Thread::sleep(1);
    REQUIRE(f.shares().toReal("/Completed/second.txt") == second);
    f.shares().removeManagedFiles("b");
    f.shares().removeManagedFiles("missing");
    REQUIRE(f.shares().getDirectories() == dirs);
    REQUIRE(f.shares().toReal("/Manual/user.txt") == manual);
}

TEST_CASE("Managed snapshots reject unsafe candidates atomically", "[managedshare]") {
    ManagedShareFixture f;
    const auto valid = f.file("data/valid.txt");
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {valid}));
    StringList unsafe = {"relative.txt", (f.temp / "data").string(),
        (f.temp / "missing.txt").string(), (f.temp / "data/../data/valid.txt").string(),
        (f.temp / "data/./valid.txt").string(), valid + string("\0extra", 6)};
    for (const auto& name : {"job.torrent", "job.resume", "job.fastresume", "pending.part", "pending.partial", "pending.dctmp", ".job.parts", "pending.crdownload"})
        unsafe.push_back(f.file(string("data/") + name));
#ifndef _WIN32
    for (const auto& name : {"bad:name.txt", "bad\\name.txt", "bad\nname.txt"})
        unsafe.push_back(f.file(string("data/") + name));
    fs::create_symlink(valid, f.temp / "link.txt");
    fs::create_directory_symlink(f.temp / "data", f.temp / "linked");
    unsafe.push_back((f.temp / "link.txt").string());
    unsafe.push_back((f.temp / "linked/valid.txt").string());
    REQUIRE(::mkfifo((f.temp / "fifo").c_str(), 0600) == 0);
    unsafe.push_back((f.temp / "fifo").string());
#endif
    for (const auto& candidate : unsafe) {
        INFO(candidate);
        REQUIRE_FALSE(f.shares().replaceManagedFiles("a", "Completed", {valid, candidate}));
        REQUIRE(f.shares().toReal("/Completed/valid.txt") == valid);
        REQUIRE(f.shares().getSharedFiles() == 1);
    }
    REQUIRE_FALSE(f.shares().replaceManagedFiles("", "Completed", {valid}));
    for (const auto& name : {"", "..", "a/b", "a\\b", "a:b", "bad\nroot", "trailing."})
        REQUIRE_FALSE(f.shares().replaceManagedFiles("a", name, {valid}));
    const auto collision = f.file("other/valid.txt", "different");
    REQUIRE_FALSE(f.shares().replaceManagedFiles("b", "Completed", {collision}));
    REQUIRE_FALSE(f.shares().replaceManagedFiles("b", "Other", {valid, collision}));
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {}));
    REQUIRE(f.shares().getSharedFiles() == 0);
}

TEST_CASE("Managed cache cannot resurrect a whitelist after restart or settings reload", "[managedshare]") {
    ManagedShareFixture f;
    const auto path = f.file("data/file.txt");
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {path}));
    f.fullList();
    const string cachePath = f.shares().getOwnListFile();
    const string cache = File(cachePath, File::READ, File::OPEN).read();
    f.context.shareManager_.reset();
    File(cachePath, File::WRITE, File::CREATE | File::TRUNCATE).write(cache);
    f.context.shareManager_ = std::make_unique<ShareManager>(f.context);
    fs::create_directories(f.temp / "manual");
    f.shares().addDirectory((f.temp / "manual").string() + PATH_SEPARATOR, "Completed");
    f.shares().refresh(false, false, true);
    while (f.shares().isRefreshing()) Thread::sleep(1);
    REQUIRE(f.list().find("file.txt") == string::npos);
    REQUIRE_THROWS_AS(f.shares().getTTH("/Completed/file.txt"), ShareException);
    f.shares().removeDirectory((f.temp / "manual").string() + PATH_SEPARATOR);
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {path}));
    SimpleXML xml;
    xml.addTag("Share"); xml.stepIn();
    xml.addTag("Directory", (f.temp / "manual").string() + PATH_SEPARATOR);
    xml.addChildAttrib("Virtual", string("Completed")); xml.stepOut();
    f.shares().load(xml);
    // A settings load is a restart boundary, not permission to merge an owned root.
    REQUIRE(f.list().find("file.txt") == string::npos);
}

TEST_CASE("Managed duplicate content never removes manual ownership", "[managedshare]") {
    ManagedShareFixture f;
    f.context.getSettingsManager()->set(SettingsManager::LIST_DUPES, false);
    const auto path = f.file("data/file.txt", "same");
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {path}));
    TigerTree tree(HashManager::MIN_BLOCK_SIZE);
    tree.update("same", 4); tree.finalize();
    File file(path, File::READ, File::OPEN);
    f.context.getHashManager()->addTree(path, file.getLastModified(), tree);
    const string directory = (f.temp / "data").string() + PATH_SEPARATOR;
    f.shares().addDirectory(directory, "Manual");
    REQUIRE(f.shares().toReal("/Manual/file.txt") == path);
    f.shares().removeManagedFiles("a");
    REQUIRE(f.shares().toReal("/Manual/file.txt") == path);
    REQUIRE(f.shares().toReal("TTH/" + tree.getRoot().toBase32()) == path);
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {path}));
    REQUIRE(f.list().find("Completed") != string::npos);
    f.shares().removeDirectory(directory);
    REQUIRE(f.shares().toReal("/Completed/file.txt") == path);
    REQUIRE(f.shares().toReal("TTH/" + tree.getRoot().toBase32()) == path);
}

TEST_CASE("Managed hashing stays invisible and removal cancels a late publication", "[managedshare]") {
    ManagedShareFixture f;
    const auto path = f.file("data/file.txt", "hash me");
    std::future<bool> result;
    bool pending = false;
    {
        // Block only HashManager storage, not ShareManager. This pins the worker
        // before publication without sleeps or a production-only testing hook.
        Lock hashLock(f.context.getHashManager()->cs);
        result = std::async(std::launch::async, [&] {
            return f.shares().replaceManagedFiles("a", "Completed", {path});
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                Lock shareLock(f.shares().cs);
                pending = !f.shares().managedReplacements.empty();
            }
            if (pending) break;
            std::this_thread::yield();
        }
        CHECK(f.shares().getSharedFiles() == 0);
        CHECK(f.list().find("file.txt") == string::npos);
        f.shares().removeManagedFiles("a");
    }
    REQUIRE(pending);
    REQUIRE_FALSE(result.get());
    REQUIRE(f.shares().getSharedFiles() == 0);
}

TEST_CASE("Managed zero-byte and multi-block files have usable TTH trees", "[managedshare]") {
    ManagedShareFixture f;
    const auto empty = f.file("data/empty.txt", "");
    const string bytes(1024 * 1024 + 19, 'x');
    const auto large = f.file("data/large.bin", bytes);
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {empty, large}));
    TigerTree expected(65536);
    expected.update(bytes.data(), bytes.size()); expected.finalize();
    REQUIRE(f.shares().getTTH("/Completed/large.bin") == expected.getRoot());
    std::unique_ptr<MemoryInputStream> leaves(f.shares().getTree("TTH/" + expected.getRoot().toBase32()));
    REQUIRE(leaves);
    REQUIRE(leaves->getSize() > TTHValue::BYTES);
    REQUIRE(f.shares().getSharedFiles() == 2);
    REQUIRE(f.shares().getShareSize() == static_cast<int64_t>(bytes.size()));
    REQUIRE(f.shares().toReal("/Completed/empty.txt") == empty);
}

TEST_CASE("Managed publication ignores hash callbacks outside the whitelist", "[managedshare]") {
    ManagedShareFixture f;
    const auto selected = f.file("data/selected.txt", "selected");
    const auto unrelated = f.file("data/unrelated.txt", "unrelated");
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {selected}));
    TigerTree tree(HashManager::MIN_BLOCK_SIZE);
    tree.update("unrelated", 9); tree.finalize();
    File file(unrelated, File::READ, File::OPEN);
    f.context.getHashManager()->addTree(unrelated, file.getLastModified(), tree);
    REQUIRE_FALSE(f.shares().isTTHShared(tree.getRoot()));
    REQUIRE_THROWS_AS(f.shares().toReal("TTH/" + tree.getRoot().toBase32()), ShareException);
    REQUIRE(f.list().find("unrelated") == string::npos);
    REQUIRE(f.shares().getSharedFiles() == 1);
}

TEST_CASE("Managed publication fails atomically when TTH leaves cannot be stored", "[managedshare]") {
    ManagedShareFixture f;
    const auto first = f.file("data/first.txt", "first");
    const auto second = f.file("data/second.txt", string(200000, 's'));
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {first}));
    REQUIRE(fs::remove(f.temp / "config/HashData.dat"));
    REQUIRE_FALSE(f.shares().replaceManagedFiles("a", "Completed", {second}));
    REQUIRE(f.shares().toReal("/Completed/first.txt") == first);
    REQUIRE(f.list().find("second.txt") == string::npos);
}

TEST_CASE("Managed files fail closed after replacement or directory symlink substitution", "[managedshare]") {
    ManagedShareFixture f;
    const auto path = f.file("data/file.txt", "old bytes");
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {path}));
    const auto oldTth = f.shares().getTTH("/Completed/file.txt");
    const auto timestamp = fs::last_write_time(path);
    f.file("data/replacement.txt", "new bytes");
    fs::remove(path);
    fs::rename(f.temp / "data/replacement.txt", path);
    fs::last_write_time(path, timestamp);
    REQUIRE_THROWS_AS(f.shares().toReal("TTH/" + oldTth.toBase32()), ShareException);
    REQUIRE_THROWS_AS(f.shares().getTTH("/Completed/file.txt"), ShareException);
    REQUIRE_FALSE(f.shares().isTTHShared(oldTth));
    SearchResultList results;
    f.shares().search(results, StringList{"ANfile"}, 10);
    REQUIRE(results.empty());
    f.shares().search(results, "file", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, nullptr, 10);
    REQUIRE(results.empty());
    REQUIRE(f.list().find("file.txt") == string::npos);
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {path}));
    REQUIRE(f.shares().getTTH("/Completed/file.txt") != oldTth);
#ifndef _WIN32
    fs::rename(f.temp / "data", f.temp / "moved");
    fs::create_directory_symlink(f.temp / "moved", f.temp / "data");
    REQUIRE_THROWS_AS(f.shares().toReal("/Completed/file.txt"), ShareException);
    REQUIRE(f.list().find("file.txt") == string::npos);
#endif
}

TEST_CASE("Stale manual refresh cannot retain a revoked managed file", "[managedshare][managedshare-regression]") {
    ManagedShareFixture f;
    const auto oldManual = f.file("manual/old.txt", "old manual");
    const auto selected = f.file("payload/selected.txt", "selected");
    TigerTree manualTree(HashManager::MIN_BLOCK_SIZE);
    manualTree.update("old manual", 10); manualTree.finalize();
    File manualFile(oldManual, File::READ, File::OPEN);
    f.context.getHashManager()->addTree(oldManual, manualFile.getLastModified(), manualTree);
    const string manualDir = (f.temp / "manual").string() + PATH_SEPARATOR;
    f.shares().addDirectory(manualDir, "Completed");

    bool published = false;
    DuringRefresh interleave(*f.context.getLogManager(), [&] {
        f.shares().removeDirectory(manualDir);
        published = f.shares().replaceManagedFiles("a", "Completed", {selected});
    });
    f.shares().refresh(true, false, true);
    if(interleave.error) std::rethrow_exception(interleave.error);
    REQUIRE(published);
    CHECK(f.list().find("old.txt") == string::npos);
    REQUIRE(f.shares().toReal("/Completed/selected.txt") == selected);
    const auto tth = f.shares().getTTH("/Completed/selected.txt");

    f.shares().removeManagedFiles("a");
    CHECK_THROWS_AS(f.shares().toReal("/Completed/selected.txt"), ShareException);
    CHECK_THROWS_AS(f.shares().toReal("TTH/" + tth.toBase32()), ShareException);
    CHECK_FALSE(f.shares().isTTHShared(tth));
    CHECK(f.list().find("selected.txt") == string::npos);
    CHECK(f.fullList().find("selected.txt") == string::npos);
    SearchResultList results;
    f.shares().search(results, StringList{"ANselected"}, 10);
    CHECK(results.empty());
    CHECK(f.shares().getDirectories().empty());
}

TEST_CASE("Managed TTH leaves survive hash database rebuild", "[managedshare][managedshare-regression]") {
    ManagedShareFixture f;
    const string bytes(1024 * 1024 + 19, 'r');
    const auto file = f.file("payload/large.bin", bytes);
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {file}));
    TigerTree expected(max(TigerTree::calcBlockSize(bytes.size(), 10), HashManager::MIN_BLOCK_SIZE));
    expected.update(bytes.data(), bytes.size()); expected.finalize();
    const auto expectedLeaves = expected.getLeafData();
    REQUIRE(expectedLeaves.size() > TTHValue::BYTES);
    f.context.getHashManager()->doRebuild();

    for(const string path : {string("/Completed/large.bin"), "TTH/" + expected.getRoot().toBase32()}) {
        CAPTURE(path);
        std::unique_ptr<MemoryInputStream> stream(f.shares().getTree(path));
        REQUIRE(stream);
        ByteVector leaves(stream->getSize());
        size_t size = leaves.size();
        stream->read(leaves.data(), size);
        CHECK(leaves == expectedLeaves);
        CHECK(f.shares().getTTH(path) == expected.getRoot());
        CHECK(f.shares().toReal(path) == file);
    }
    f.shares().removeManagedFiles("a");
    std::unique_ptr<MemoryInputStream> removed(f.shares().getTree("TTH/" + expected.getRoot().toBase32()));
    CHECK_FALSE(removed);
}

TEST_CASE("Externally pre-cancelled managed publication never registers or retracts", "[managedshare][managedshare-cancellation]") {
    ManagedShareFixture f;
    const auto previous = f.file("payload/previous.txt", "previous");
    const auto next = f.file("payload/next.txt", "next");
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {previous}));
    for(const auto& files : {StringList{next}, StringList{}}) {
        bool checked = false, registered = false;
        CHECK_FALSE(f.shares().replaceManagedFiles("a", "Completed", files, [&] {
            checked = true;
            registered = registered || !f.shares().managedReplacements.empty();
            return true;
        }));
        CHECK(checked);
        CHECK_FALSE(registered);
        CHECK(f.shares().managedReplacements.empty());
        CHECK(f.shares().toReal("/Completed/previous.txt") == previous);
        CHECK_THROWS_AS(f.shares().toReal("/Completed/next.txt"), ShareException);
    }
}

TEST_CASE("External invalidation during managed hashing preserves the old snapshot", "[managedshare][managedshare-cancellation]") {
    ManagedShareFixture f;
    const auto previous = f.file("payload/previous.txt", "previous");
    const string bytes(8 * 1024 * 1024, 'c');
    const auto next = f.file("payload/next.bin", bytes);
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {previous}));
    std::atomic<unsigned> generation{0};
    unsigned polls = 0;
    // Invalidate after several bounded-read polls, well before this payload's EOF.
    CHECK_FALSE(f.shares().replaceManagedFiles("a", "Completed", {next}, [&] {
        if(++polls == 8) ++generation;
        return generation.load() != 0;
    }));
    CHECK(generation.load() == 1);
    CHECK(f.shares().managedReplacements.empty());
    CHECK(f.shares().toReal("/Completed/previous.txt") == previous);
    CHECK_THROWS_AS(f.shares().toReal("/Completed/next.bin"), ShareException);
    TigerTree expected(HashManager::MIN_BLOCK_SIZE);
    expected.update(bytes.data(), bytes.size()); expected.finalize();
    TigerTree stored;
    CHECK_FALSE(f.context.getHashManager()->getTree(expected.getRoot(), stored));
}

TEST_CASE("External invalidation after hashing prevents managed commit", "[managedshare][managedshare-cancellation]") {
    ManagedShareFixture f;
    const auto previous = f.file("payload/previous.txt", "previous");
    const auto next = f.file("payload/next.txt", "next");
    REQUIRE(f.shares().replaceManagedFiles("a", "Completed", {previous}));
    TigerTree expected(HashManager::MIN_BLOCK_SIZE);
    expected.update("next", 4); expected.finalize();
    bool invalidated = false;
    CHECK_FALSE(f.shares().replaceManagedFiles("a", "Completed", {next}, [&] {
        // This fixture has no running hasher. The new root appears only after
        // the synchronous publisher stores its completed tree, before commit.
        const auto& trees = f.context.getHashManager()->store.treeIndex;
        invalidated = trees.find(expected.getRoot()) != trees.end();
        return invalidated;
    }));
    CHECK(invalidated);
    CHECK(f.shares().managedReplacements.empty());
    CHECK(f.shares().toReal("/Completed/previous.txt") == previous);
    CHECK_THROWS_AS(f.shares().toReal("/Completed/next.txt"), ShareException);
    CHECK_FALSE(f.shares().isTTHShared(expected.getRoot()));
}
