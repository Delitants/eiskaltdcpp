#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "stdinc.h"
#include <ranges>
#define private public
#include "DCContext.h"
#include "QueueManager.h"
#include "ConnectionManager.h"
#undef private
#include "ClientManager.h"
#include "DirectoryListing.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "SearchManager.h"
#include "SearchResult.h"
#include "TimerManager.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <type_traits>

using namespace dcpp;

namespace {

TTHValue rootFor(size_t value)
{
    const auto text = std::to_string(value);
    TigerHash hash;
    hash.update(text.data(), text.size());
    return TTHValue(hash.finalize());
}

template<class Queue> bool indexBuilt(const Queue& queue)
{
    if constexpr(requires { queue.tthIndexValid; })
        return queue.tthIndexValid;
    return false;
}

template<class Manager> void clearManaged(Manager& manager)
{
    if constexpr(requires { manager.clear(); })
        manager.clear();
    else
        FAIL("Queue clearing must go through QueueManager");
}

template<class Item> constexpr bool hasRootSetter = requires(Item& item, const TTHValue& root) {
    item.setTTH(root);
};

struct QueueFixture {
    DCContext context;
    Util::PathsMap previousPaths;
    std::filesystem::path temp;

    QueueFixture()
    {
        temp = std::filesystem::temp_directory_path() /
            ("eiskalt_queueindex_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp);
        Util::PathsMap paths;
        for(int i = 0; i < Util::PATH_LAST; ++i) {
            const auto key = static_cast<Util::Paths>(i);
            previousPaths[key] = Util::getPath(key);
            paths[key] = temp.string() + PATH_SEPARATOR;
        }
        Util::uninitialize();
        Util::initialize(paths);
        context.startupMinimal();
        context.timerManager_ = std::make_unique<TimerManager>(context);
        context.clientManager_ = std::make_unique<ClientManager>(context);
        context.searchManager_ = std::make_unique<SearchManager>(context);
        context.queueManager_ = std::make_unique<QueueManager>(context);
        context.getSettingsManager()->set(SettingsManager::KEEP_LISTS, true);
        context.getSettingsManager()->set(SettingsManager::CHECK_TARGETS_PATHS_ON_START, false);
    }

    ~QueueFixture()
    {
        context.shutdown();
        if(!previousPaths[Util::PATH_USER_CONFIG].empty())
            Util::initialize(previousPaths);
        std::error_code error;
        std::filesystem::remove_all(temp, error);
    }

    QueueManager& manager() { return *context.getQueueManager(); }
    QueueManager::FileQueue& queue() { return manager().fileQueue; }
    string target(const string& name) { return (temp / name).string(); }
    QueueItem* add(size_t name, size_t root)
    {
        return queue().add(target(std::to_string(name) + ".bin"), 4096, 0,
            QueueItem::NORMAL, {}, 1700000000, rootFor(root));
    }
};

StringList linearTargets(const QueueItem::StringMap& queue, const TTHValue& root)
{
    StringList result;
    for(const auto& [target, item] : queue) {
        if(item->getTTH() == root)
            result.push_back(*target);
    }
    std::sort(result.begin(), result.end());
    return result;
}

StringList indexedTargets(QueueManager::FileQueue& queue, const TTHValue& root)
{
    StringList result;
    for(const auto* item : queue.find(root))
        result.push_back(item->getTarget());
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace

namespace {

struct PassiveQueueFixture : QueueFixture, QueueManagerListener, LogManagerListener {
    std::vector<std::unique_ptr<OnlineUser>> online;
    std::vector<Client*> hubs;
    StringList events;
    StringList warnings;
    int added = 0;
    int sourcesUpdated = 0;

    PassiveQueueFixture() {
        context.favoriteManager_ = std::make_unique<FavoriteManager>(context);
        context.connectionManager_ = std::make_unique<ConnectionManager>(context);
        settings().set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_FIREWALL_PASSIVE);
        settings().set(SettingsManager::ALLOW_NATT, false);
        manager().addListener(this);
        context.getLogManager()->addListener(this);
        manager().dirty = false;
    }

    ~PassiveQueueFixture() {
        manager().removeListener(this);
        context.getLogManager()->removeListener(this);
        for(auto& user : online)
            context.getClientManager()->putOffline(user.get());
        online.clear();
        for(auto* hub : hubs)
            context.getClientManager()->putClient(hub);
    }

    SettingsManager& settings() { return *context.getSettingsManager(); }

    HintedUser peer(const string& url = "adc://passive.invalid", UserPtr user = {}) {
        auto* hub = context.getClientManager()->getClient(url);
        hubs.push_back(hub);
        if(!user)
            user = context.getClientManager()->getUser(CID::generate());
        user->setFlag(User::PASSIVE);
        if(url.find("adc://") != 0)
            user->setFlag(User::NMDC);
        auto identity = std::make_unique<OnlineUser>(user, *hub, 1);
        identity->getIdentity().setNick("PassivePeer");
        context.getClientManager()->putOnline(identity.get());
        online.push_back(std::move(identity));
        return HintedUser(user, url);
    }

    void hubMode(const string& url, int mode) {
        FavoriteHubEntry entry(context);
        entry.setServer(url);
        entry.setMode(mode);
        context.getFavoriteManager()->addFavorite(entry);
    }

    void request(const HintedUser& user) {
        manager().add(target("requested.bin"), 4096, rootFor(777), user);
    }

    void on(QueueManagerListener::Added, QueueItem*) noexcept override { ++added; }
    void on(SourcesUpdated, QueueItem*) noexcept override { ++sourcesUpdated; }
    void on(Message, time_t, const string&) noexcept override { events.push_back("message"); }
    void on(Warning, time_t, const string& msg) noexcept override {
        events.push_back("warning");
        warnings.push_back(msg);
    }
};

} // namespace

TEST_CASE("Passive requests reject file list and directory before any queue mutation", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    SECTION("file") { CHECK_NOTHROW(fixture.request(user)); }
    SECTION("list") { CHECK_NOTHROW(fixture.manager().addList(user, QueueItem::FLAG_CLIENT_VIEW)); }
    SECTION("directory") { CHECK_NOTHROW(fixture.manager().addDirectory("/folder/", user, fixture.target("folder"))); }
    CHECK(fixture.queue().getSize() == 0);
    CHECK(fixture.manager().directories.empty());
    CHECK_FALSE(fixture.manager().dirty);
    CHECK(fixture.added == 0);
    CHECK(fixture.sourcesUpdated == 0);
    CHECK(fixture.events == StringList{"message", "warning"});
    REQUIRE(fixture.warnings.size() == 1);
    CHECK(fixture.warnings.front().find("PassivePeer") != string::npos);
}

TEST_CASE("Passive rejection leaves existing sources flags and bad sources unchanged", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    auto* item = fixture.queue().add(fixture.target("requested.bin"), 4096, 0,
        QueueItem::PAUSED, {}, 1700000000, rootFor(777));
    const HintedUser existing(fixture.context.getClientManager()->getUser(CID::generate()), "adc://other.invalid");
    fixture.manager().addSource(item, existing, 0);
    fixture.manager().dirty = false;
    fixture.sourcesUpdated = 0;
    SECTION("same target") {
        fixture.manager().add(fixture.target("requested.bin"), 4096, rootFor(777), user, QueueItem::FLAG_TEXT);
    }
    SECTION("same hash") {
        fixture.settings().set(SettingsManager::DONT_DL_ALREADY_QUEUED, true);
        fixture.manager().add(fixture.target("other.bin"), 4096, rootFor(777), user);
    }
    CHECK(fixture.queue().getSize() == 1);
    REQUIRE(item->getSources().size() == 1);
    CHECK(item->isSource(existing));
    CHECK_FALSE(item->isSource(user));
    CHECK(item->getBadSources().empty());
    CHECK_FALSE(item->isSet(QueueItem::FLAG_TEXT));
    CHECK_FALSE(fixture.manager().dirty);
    CHECK(fixture.sourcesUpdated == 0);
    CHECK(fixture.warnings.size() == 1);
}

TEST_CASE("Passive reachability uses selected hub and preserves usable routes", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    bool allowed = false;
    SECTION("offline stale passive flag is not evidence") {
        fixture.context.getClientManager()->putOffline(fixture.online.back().get());
        fixture.online.pop_back();
        allowed = true;
    }
    SECTION("online flag without a known hub is not evidence") {
        fixture.context.getClientManager()->putOffline(fixture.online.back().get());
        fixture.online.pop_back();
        user.user->setFlag(User::ONLINE);
        allowed = true;
    }
    SECTION("remote active identity overrides stale global passive flag") {
        fixture.online.back()->getIdentity().setIp("192.0.2.10");
        fixture.online.back()->getIdentity().set("SU", "TCP4");
        allowed = true;
    }
    SECTION("self active") {
        fixture.settings().set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_DIRECT);
        allowed = true;
    }
    SECTION("selected hub active overrides global passive") {
        fixture.hubMode(user.hint, 1);
        allowed = true;
    }
    SECTION("selected hub passive overrides global active") {
        fixture.settings().set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_DIRECT);
        fixture.hubMode(user.hint, 2);
    }
    SECTION("another active hub does not override selected passive hub") {
        auto second = fixture.peer("adc://active.invalid", user.user);
        fixture.hubMode(second.hint, 1);
    }
    SECTION("missing hint falls back to actual active hub") {
        fixture.hubMode(user.hint, 1);
        user.hint = "adc://missing.invalid";
        allowed = true;
    }
    SECTION("missing private hint must not select another hub") {
        FavoriteHubEntry entry(fixture.context);
        entry.setServer("adc://private.invalid");
        entry.setGroup("private");
        fixture.context.getFavoriteManager()->setFavHubGroups({{"private", {true, false}}});
        fixture.context.getFavoriteManager()->addFavorite(entry);
        // addFavorite copies the entry without its group; set the stored entry.
        fixture.context.getFavoriteManager()->getFavoriteHubEntry(entry.getServer())->setGroup("private");
        REQUIRE(fixture.context.getFavoriteManager()->isPrivate(entry.getServer()));
        user.hint = entry.getServer();
        allowed = true;
    }
    SECTION("missing hint falls back to passive hub not global active") {
        fixture.settings().set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_DIRECT);
        fixture.hubMode(user.hint, 2);
        user.hint = "adc://missing.invalid";
    }
    SECTION("active remote remains reachable through GOST") {
        fixture.settings().set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_GOST);
        fixture.online.back()->getIdentity().setIp("192.0.2.10");
        fixture.online.back()->getIdentity().set("SU", "TCP4");
        allowed = true;
    }
    SECTION("ADC NAT traversal") {
        fixture.settings().set(SettingsManager::ALLOW_NATT, true);
        fixture.online.back()->getIdentity().set("SU", "NAT0");
        allowed = true;
    }
    SECTION("ADC NAT requires local permission") {
        fixture.online.back()->getIdentity().set("SU", "NAT0");
    }
    SECTION("ADC NAT requires peer capability") {
        fixture.settings().set(SettingsManager::ALLOW_NATT, true);
    }
    SECTION("NMDC NAT traversal") {
        user = fixture.peer("dchub://nmdc.invalid");
        fixture.settings().set(SettingsManager::ALLOW_NATT, true);
        fixture.online.back()->getIdentity().setStatus(Util::toString(Identity::NAT));
        allowed = true;
    }
    SECTION("NMDC passive without NAT") {
        user = fixture.peer("dchub://nmdc.invalid");
    }
    SECTION("proxy forbids NAT and per hub active bypass") {
        fixture.settings().set(SettingsManager::ALLOW_NATT, true);
        fixture.online.back()->getIdentity().set("SU", "NAT0");
        fixture.hubMode(user.hint, 1);
        SECTION("GOST without optional P2P switch") {
            fixture.settings().set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_GOST);
            fixture.settings().set(SettingsManager::PROXY_P2P_CONNECTIONS, false);
        }
        SECTION("SOCKS P2P") {
            fixture.settings().set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SOCKS5);
            fixture.settings().set(SettingsManager::PROXY_P2P_CONNECTIONS, true);
        }
        SECTION("SOCKS stealth") {
            fixture.settings().set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SOCKS5);
            fixture.settings().set(SettingsManager::SOCKS_STEALTH, true);
        }
    }
    fixture.request(user);
    if(allowed) {
        REQUIRE(fixture.queue().getSize() == 1);
        auto* item = fixture.queue().find(fixture.target("requested.bin"));
        REQUIRE(item);
        CHECK(item->isSource(user));
        CHECK(item->getBadSources().empty());
        CHECK(fixture.warnings.empty());
    } else {
        CHECK(fixture.queue().getSize() == 0);
        CHECK(fixture.warnings.size() == 1);
    }
}

TEST_CASE("Background passive source matching is silent and does not add bad sources", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    auto* item = fixture.add(1, 42);
    fixture.manager().dirty = false;
    for(int i = 0; i < 3; ++i)
        CHECK_THROWS_AS(fixture.manager().addSource(item, user, 0), QueueException);
    const SearchResultPtr result(new SearchResult(user.user, SearchResult::TYPE_FILE, 1, 1,
        4096, "remote.bin", "fixture", user.hint, "", rootFor(42), ""));
    SECTION("automatic source matching") {
        fixture.settings().set(SettingsManager::AUTO_SEARCH_AUTO_MATCH, false);
    }
    SECTION("automatic list matching") {
        fixture.settings().set(SettingsManager::AUTO_SEARCH_AUTO_MATCH, true);
    }
    for(int i = 0; i < 3; ++i)
        fixture.manager().on(SearchManagerListener::SR(), result);
    CHECK(item->getSources().empty());
    CHECK(item->getBadSources().empty());
    CHECK(fixture.queue().getSize() == 1);
    CHECK_FALSE(fixture.manager().dirty);
    CHECK(fixture.events.empty());
}

TEST_CASE("Offline passive users can still queue lists and folders", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    const HintedUser user(fixture.context.getClientManager()->getUser(CID::generate()), "adc://offline.invalid");
    user.user->setFlag(User::PASSIVE);
    SECTION("list") {
        fixture.manager().addList(user, QueueItem::FLAG_CLIENT_VIEW);
        CHECK(fixture.manager().directories.empty());
    }
    SECTION("folder") {
        fixture.manager().addDirectory("/folder/", user, fixture.target("folder"));
        CHECK(fixture.manager().directories.size() == 1);
    }
    REQUIRE(fixture.queue().getSize() == 1);
    auto* item = fixture.queue().getQueue().begin()->second;
    CHECK(item->isSource(user));
    CHECK(item->getBadSources().empty());
    CHECK(fixture.events.empty());
}

TEST_CASE("Explicit passive source retry preserves its existing bad source", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    auto* item = fixture.add(1, 42);
    item->addSource(user);
    item->removeSource(user, QueueItem::Source::FLAG_PASSIVE);
    fixture.manager().dirty = false;
    fixture.manager().readd(item->getTarget(), user);
    CHECK(item->getSources().empty());
    REQUIRE(item->getBadSources().size() == 1);
    CHECK(item->isBadSource(user));
    CHECK_FALSE(fixture.manager().dirty);
    CHECK(fixture.events == StringList{"message", "warning"});
}

TEST_CASE("Cached filelist browsing bypasses passive queue rejection", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    const auto path = fixture.manager().getListPath(user) + ".xml";
    const string xml = "<FileListing Base=\"/\"><File Name=\"cached.bin\" Size=\"42\" TTH=\"" +
        rootFor(42).toBase32() + "\"/></FileListing>";
    {
        File file(path, File::WRITE, File::CREATE | File::TRUNCATE);
        file.write(xml);
    }
    DirectoryListing listing(fixture.context, user);
    CHECK_NOTHROW(listing.loadFile(path));
    REQUIRE(listing.getRoot()->files.size() == 1);
    CHECK((*listing.getRoot()->files.begin())->getName() == "cached.bin");
    CHECK(fixture.queue().getSize() == 0);
    CHECK_FALSE(fixture.manager().dirty);
    CHECK(fixture.events.empty());

    // A refresh still requests a transfer; it must not create a dead queue row
    // or destroy the cached list that remains available to the local browser.
    fixture.manager().addList(user, QueueItem::FLAG_CLIENT_VIEW);
    CHECK(fixture.queue().getSize() == 0);
    CHECK_FALSE(fixture.manager().dirty);
    CHECK(fixture.warnings.size() == 1);
    CHECK(File::getSize(path) == static_cast<int64_t>(xml.size()));
}

TEST_CASE("Queue reload preserves existing passive sources without warnings", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    auto* item = fixture.add(1, 42);
    item->setPriority(QueueItem::PAUSED);
    item->addSource(user);
    fixture.manager().userQueue.add(item, user);
    item->addSegment(Segment(0, 1024));
    fixture.manager().saveQueue(true);
    clearManaged(fixture.manager());
    fixture.manager().loadQueue();
    REQUIRE(fixture.queue().getSize() == 1);
    item = fixture.queue().find(fixture.target("1.bin"));
    REQUIRE(item);
    CHECK(item->isSource(user));
    CHECK(item->getBadSources().empty());
    CHECK(item->getDownloadedBytes() == 1024);
    CHECK(fixture.events.empty());
}

TEST_CASE("Typed warning preserves legacy message delivery once and before warning", "[passive-queue]")
{
    PassiveQueueFixture fixture;
    auto& log = *fixture.context.getLogManager();
    log.warning("warning fixture");
    CHECK(fixture.events == StringList{"message", "warning"});
    REQUIRE(log.getLastLogs().size() == 1);
    CHECK(log.getLastLogs().front().second == "warning fixture");
    REQUIRE(log.getLiveEntries().size() == 1);
    CHECK(log.getLiveEntries().front().area == LogManager::SYSTEM);
}

TEST_CASE("Queue index is lazy and owns no independent queue items", "[queue-index]")
{
    QueueFixture fixture;
    auto& queue = fixture.queue();
    CHECK_FALSE(indexBuilt(queue));
    fixture.add(1, 42);
    fixture.add(2, 42);
    CHECK_FALSE(indexBuilt(queue));
    CHECK(queue.find(fixture.target("1.bin")));
    CHECK_FALSE(indexBuilt(queue));
    CHECK(queue.find(rootFor(42)).size() == 2);
    CHECK(indexBuilt(queue));
    CHECK(queue.find(rootFor(99)).empty());
    CHECK(queue.find(TTHValue()).empty());
    CHECK(std::is_const_v<std::remove_reference_t<decltype(queue.getQueue())>>);
    CHECK(std::is_const_v<std::remove_reference_t<decltype(fixture.manager().lockQueue())>>);
    CHECK_FALSE(hasRootSetter<QueueItem>);
}

TEST_CASE("Queue index tracks duplicates moves and exact removal after map growth", "[queue-index]")
{
    QueueFixture fixture;
    auto& queue = fixture.queue();
    CHECK(queue.find(rootFor(42)).empty());
    auto* first = fixture.add(1, 42);
    auto* second = fixture.add(2, 42);
    for(size_t i = 3; i < 2000; ++i)
        fixture.add(i, i + 100);
    CHECK(queue.find(rootFor(42)).size() == 2);
    queue.move(first, fixture.target("renamed.bin"));
    CHECK(queue.find(fixture.target("1.bin")) == nullptr);
    CHECK(queue.find(fixture.target("renamed.bin")) == first);
    REQUIRE(queue.find(rootFor(42)).size() == 2);
    queue.remove(second);
    REQUIRE(queue.find(rootFor(42)).size() == 1);
    CHECK(queue.find(rootFor(42)).front() == first);
    queue.move(first, fixture.target("RENAMED.bin"));
    REQUIRE(queue.find(rootFor(42)).size() == 1);
    queue.remove(first);
    CHECK(queue.find(rootFor(42)).empty());
    CHECK(queue.getSize() == 1997);
}

TEST_CASE("Queue index agrees with a linear oracle through mixed mutations", "[queue-index]")
{
    QueueFixture fixture;
    std::mt19937 random(20260926);
    std::vector<QueueItem*> items;
    for(size_t step = 0; step < 500; ++step) {
        const auto choice = random() % 3;
        if(items.empty() || choice == 0) {
            items.push_back(fixture.add(step, random() % 16));
        } else {
            const auto position = random() % items.size();
            if(choice == 1) {
                fixture.queue().remove(items[position]);
                items.erase(items.begin() + position);
            } else {
                fixture.queue().move(items[position], fixture.target("moved" + std::to_string(step)));
            }
        }
        for(size_t root = 0; root <= 16; ++root) {
            CAPTURE(step, root);
            CHECK(indexedTargets(fixture.queue(), rootFor(root)) ==
                linearTargets(fixture.queue().getQueue(), rootFor(root)));
        }
    }
}

TEST_CASE("Managed clear removes ownership and notifications but preserves later additions", "[queue-index]")
{
    int destroyed = 0;
    struct TrackedItem : QueueItem {
        int& destroyed;
        TrackedItem(DCContext& context, string path, int& counter) :
            QueueItem(context, path, 4096, NORMAL, 0, 1700000000, rootFor(42)), destroyed(counter) { }
        ~TrackedItem() override { ++destroyed; }
    };
    struct Listener : QueueManagerListener {
        QueueFixture& fixture;
        int removed = 0;
        explicit Listener(QueueFixture& fixture) : fixture(fixture) { }
        void on(Removed, QueueItem*) noexcept override {
            if(++removed == 1)
                fixture.add(100, 100);
        }
    };
    QueueFixture fixture;
    for(int i = 0; i < 3; ++i)
        fixture.queue().add(new TrackedItem(fixture.context, fixture.target(std::to_string(i)), destroyed));
    REQUIRE(fixture.manager().getTargets(rootFor(42)).size() == 3);
    fixture.manager().dirty = false;
    Listener listener(fixture);
    fixture.manager().addListener(&listener);
    clearManaged(fixture.manager());
    fixture.manager().removeListener(&listener);
    CHECK(listener.removed == 3);
    CHECK(destroyed == 3);
    CHECK(fixture.manager().dirty);
    CHECK(fixture.manager().getTargets(rootFor(42)).empty());
    CHECK(fixture.manager().getTargets(rootFor(100)).size() == 1);
}

TEST_CASE("Queue save and reload restore index and partial chunks", "[queue-index]")
{
    QueueFixture fixture;
    auto* first = fixture.add(1, 42);
    fixture.add(2, 42);
    auto* partial = fixture.add(3, 43);
    partial->addSegment(Segment(0, 1024));
    first->setPriority(QueueItem::HIGH);
    REQUIRE(fixture.manager().getTargets(rootFor(42)).size() == 2);
    fixture.manager().saveQueue(true);
    clearManaged(fixture.manager());
    REQUIRE(fixture.manager().getTargets(rootFor(42)).empty());
    fixture.manager().loadQueue();
    REQUIRE(fixture.manager().getTargets(rootFor(42)).size() == 2);
    REQUIRE(fixture.queue().getSize() == 3);
    CHECK(fixture.queue().find(fixture.target("1.bin"))->getPriority() == QueueItem::HIGH);
    int64_t bytes = 512, size = 0;
    string temporary;
    CHECK(fixture.manager().isChunkDownloaded(rootFor(43), 0, bytes, temporary, size));
    CHECK(size == 4096);
    bytes = 512;
    CHECK_FALSE(fixture.manager().isChunkDownloaded(rootFor(43), 2048, bytes, temporary, size));
}

TEST_CASE("Queue index generated large-queue benchmark", "[.][queue-index][queue-index-benchmark]")
{
    using Clock = std::chrono::steady_clock;
    auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    QueueFixture fixture;
    const auto start = Clock::now();
    for(size_t i = 0; i < 50000; ++i)
        fixture.add(i, i);
    const auto loaded = Clock::now();
    CHECK_FALSE(indexBuilt(fixture.queue()));
    REQUIRE(fixture.queue().find(rootFor(42)).size() == 1);
    const auto built = Clock::now();
    size_t linearCount = 0, indexedCount = 0;
    std::vector<TTHValue> queries;
    for(size_t i = 0; i < 2000; ++i)
        queries.push_back(rootFor((i * 7919) % 50000));
    const auto linearStart = Clock::now();
    for(const auto& root : queries)
        linearCount += linearTargets(fixture.queue().getQueue(), root).size();
    const auto indexedStart = Clock::now();
    for(const auto& root : queries)
        indexedCount += fixture.queue().find(root).size();
    const auto end = Clock::now();
    CHECK(linearCount == 2000);
    CHECK(indexedCount == linearCount);
    std::cout << "queue-index benchmark: items=50000 queries=2000 load_ms=" << milliseconds(loaded - start)
        << " build_ms=" << milliseconds(built - loaded)
        << " linear_ms=" << milliseconds(indexedStart - linearStart)
        << " indexed_ms=" << milliseconds(end - indexedStart) << '\n';
}

TEST_CASE("Usable passive routes survive connection timer validation", "[passive-queue][passive-review]")
{
    const int route = GENERATE(0, 1, 2);
    const int kind = GENERATE(0, 1, 2);
    INFO("route=" << route << ", request kind=" << kind);
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    if(route == 0) {
        fixture.hubMode(user.hint, 1);
    } else if(route == 1) {
        fixture.settings().set(SettingsManager::ALLOW_NATT, true);
        fixture.online.back()->getIdentity().set("SU", "NAT0");
    } else {
        user = fixture.peer("dchub://nmdc.invalid");
        fixture.settings().set(SettingsManager::ALLOW_NATT, true);
        fixture.online.back()->getIdentity().setStatus(Util::toString(Identity::NAT));
    }
    if(kind == 0) fixture.request(user);
    else if(kind == 1) fixture.manager().addList(user, QueueItem::FLAG_CLIENT_VIEW);
    else fixture.manager().addDirectory("/folder/", user, fixture.target("folder"));
    REQUIRE(fixture.queue().getSize() == 1);
    auto &connections = *fixture.context.getConnectionManager();
    REQUIRE(connections.downloads.size() == 1);
    for(auto *pending : connections.downloads) {
        // Exercise the real timer's reachability validation but deliberately
        // suppress connection retries; this fixture has no live hub socket.
        pending->setErrors(-1);
        pending->setLastAttempt(1);
    }
    connections.on(TimerManagerListener::Second(), 1000);
    REQUIRE(fixture.queue().getSize() == 1);
    auto *item = fixture.queue().getQueue().begin()->second;
    CHECK(item->isSource(user));
    CHECK(item->getBadSources().empty());
    CHECK(connections.downloads.size() == 1);
}

TEST_CASE("Merging queued items retains existing passive sources and their hints", "[passive-queue][passive-review]")
{
    PassiveQueueFixture fixture;
    auto user = fixture.peer();
    fixture.settings().set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_DIRECT);
    fixture.hubMode(user.hint, 2);
    auto *source = fixture.add(1, 42);
    auto *destination = fixture.add(2, 42);
    source->setPriority(QueueItem::PAUSED);
    destination->setPriority(QueueItem::PAUSED);
    source->addSource(user);
    fixture.manager().userQueue.add(source, user);
    fixture.manager().move(fixture.target("1.bin"), fixture.target("2.bin"));
    REQUIRE(fixture.queue().getSize() == 1);
    REQUIRE(destination->isSource(user));
    REQUIRE(destination->getSources().size() == 1);
    CHECK(destination->getSources().front().getUser().hint == user.hint);
    CHECK(fixture.warnings.empty());
    CHECK(fixture.context.getConnectionManager()->downloads.empty());
    fixture.manager().saveQueue(true);
    clearManaged(fixture.manager());
    fixture.manager().loadQueue();
    auto *restored = fixture.queue().find(fixture.target("2.bin"));
    REQUIRE(restored);
    REQUIRE(restored->isSource(user));
    CHECK(restored->getSources().front().getUser().hint == user.hint);
}
