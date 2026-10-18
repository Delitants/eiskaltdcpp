/*
 * Tests for FavoriteManager — hub CRUD, directory CRUD, user commands, save/load.
 * Requires TestContext. FavoriteManager is constructed directly since the
 * constructor now null-guards the ClientManager listener.
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "FavoriteManager.h"
#include "FavoriteManagerListener.h"
#include "SettingsManager.h"
#include "DCPlusPlus.h"
#include "Util.h"
#include "TestContext.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace dcpp;

// Shared test context
static std::unique_ptr<dcpp::test::TestContext> g_tc;
static std::unique_ptr<FavoriteManager> g_fm;

// atexit callback: destroy FavoriteManager while DCContext is still alive.
// Registered AFTER TestContext's atexit, so it runs FIRST (LIFO order).
static void cleanupFavoriteManager() {
    g_fm.reset();
}

static void ensureContext() {
    if (!g_tc) {
        g_tc = std::make_unique<dcpp::test::TestContext>();
        // Construct FavoriteManager with context reference (minimal context has no ClientManager,
        // but the constructor null-guards the listener registration).
        // Saves go to temp dir automatically via TestContext path overrides.
        g_fm = std::make_unique<FavoriteManager>(*getContext());
        // Register cleanup AFTER TestContext's atexit → runs FIRST (LIFO).
        std::atexit(cleanupFavoriteManager);
    }
}

TEST_CASE("Hublist priority: fresh defaults prepend hublist.eu without losing existing sources",
          "[favoritemanager][hublist-priority]") {
    test::TestContext context;
    FavoriteManager manager(*context.ownedCtx);
    const StringList expected = {
        "https://hublist.eu/hublist.xml.bz2",
        "https://www.te-home.net/?do=hublist&get=hublist.xml.bz2",
        "https://dchublist.org/hublist.xml.bz2",
        "https://dchublist.biz/hublist.xml.bz2",
        "https://dchublist.ru/hublist.xml.bz2",
        "https://dcnf.github.io/Hublist/hublist.xml.bz2",
        "https://dchublist.org/adchublist.xml.bz2"
    };
    const auto raw = context.ownedCtx->getSettingsManager()->get(SettingsManager::HUBLIST_SERVERS);
    CHECK(raw.find(expected.front() + ";") == 0);
    CHECK(manager.getHubLists() == expected);
    CHECK(context.ownedCtx->getSettingsManager()->get(SettingsManager::HUBLIST_SERVERS) == raw);
}

TEST_CASE("Hublist priority: an existing last-position preference moves only in the returned list",
          "[favoritemanager][hublist-priority]") {
    test::TestContext context;
    FavoriteManager manager(*context.ownedCtx);
    auto *settings = context.ownedCtx->getSettingsManager();
    const string raw = "https://first.invalid/custom.xml;https://second.invalid/list.xml;"
                       "https://hublist.eu/hublist.xml.bz2";
    settings->set(SettingsManager::HUBLIST_SERVERS, raw);
    const auto path = (context.tmpDir / "hublist-priority-settings.xml").string();
    REQUIRE(settings->saveChecked(path));
    const auto readBytes = [&] {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.good());
        return string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto preimage = readBytes();
    const StringList expected = {"https://hublist.eu/hublist.xml.bz2",
        "https://first.invalid/custom.xml", "https://second.invalid/list.xml"};
    CHECK(manager.getHubLists() == expected);
    CHECK(manager.getHubLists() == expected);
    CHECK(settings->get(SettingsManager::HUBLIST_SERVERS) == raw);
    CHECK(readBytes() == preimage);
}

TEST_CASE("Hublist priority: exact decoded hosts promote stably without suffix or URL-text matches",
          "[favoritemanager][hublist-priority]") {
    test::TestContext context;
    FavoriteManager manager(*context.ownedCtx);
    const StringList ordinary = {
        "https://hublist.eu.attacker.invalid/list.xml",
        "https://not-hublist.eu/list.xml",
        "https://mirror.hublist.eu/list.xml",
        "https://ordinary.invalid/hublist.eu/list.xml?next=https://hublist.eu/",
        "https://hublist.eu@attacker.invalid/list.xml",
        "https://hublist.eu:password@attacker.invalid/list.xml",
        "https://www.hublist.eu:password@attacker.invalid/list.xml"
    };
    const StringList preferred = {
        "https://www.hublist.eu/custom.xml?preserve=1#fragment",
        "http://HUBLIST.EU:8080/alternate.xml",
        "https://hublist.eu/list.xml",
        "https://hublist.eu/list.xml"
    };
    StringList original = {ordinary[0], preferred[0], ordinary[1], preferred[1],
        ordinary[2], preferred[2], ordinary[3], preferred[3], ordinary[4], ordinary[5], ordinary[6]};
    string raw;
    for (const auto &url : original) {
        if (!raw.empty()) raw += ';';
        raw += url;
    }
    context.ownedCtx->getSettingsManager()->set(SettingsManager::HUBLIST_SERVERS, raw);
    StringList expected = preferred;
    expected.insert(expected.end(), ordinary.begin(), ordinary.end());
    CHECK(manager.getHubLists() == expected);
    CHECK(context.ownedCtx->getSettingsManager()->get(SettingsManager::HUBLIST_SERVERS) == raw);
}

TEST_CASE("Hublist priority: custom lists without the exact host are not injected or reordered",
          "[favoritemanager][hublist-priority]") {
    test::TestContext context;
    FavoriteManager manager(*context.ownedCtx);
    auto *settings = context.ownedCtx->getSettingsManager();
    const StringList custom = {"https://custom.invalid/private.xml",
        "https://hublist.eu.attacker.invalid/keep.xml", "https://mirror.hublist.eu/keep.xml"};
    const string raw = custom[0] + ';' + custom[1] + ';' + custom[2];
    settings->set(SettingsManager::HUBLIST_SERVERS, raw);
    CHECK(manager.getHubLists() == custom);
    CHECK(settings->get(SettingsManager::HUBLIST_SERVERS) == raw);
}

// ─── Favorite Hubs ──────────────────────────────────────────────────────

TEST_CASE("FavoriteManager: legacy ADC challenge flag defaults off and survives save/load", "[favoritemanager][adc-legacy-challenge]") {
    test::TestContext context;
    FavoriteManager manager(*context.ownedCtx);
    FavoriteHubEntry enabled(*context.ownedCtx), disabled(*context.ownedCtx);
    enabled.setServer("adcs://legacy.invalid:411");
    enabled.setLegacyAdcPasswordChallenges(true);
    disabled.setServer("adcs://strict.invalid:411");
    CHECK_FALSE(disabled.getLegacyAdcPasswordChallenges());
    manager.addFavorite(enabled);
    manager.addFavorite(disabled);
    REQUIRE(manager.saveChecked());
    FavoriteManager restored(*context.ownedCtx);
    restored.load();
    REQUIRE(restored.getFavoriteHubEntry(enabled.getServer()));
    REQUIRE(restored.getFavoriteHubEntry(disabled.getServer()));
    CHECK(restored.getFavoriteHubEntry(enabled.getServer())->getLegacyAdcPasswordChallenges());
    CHECK_FALSE(restored.getFavoriteHubEntry(disabled.getServer())->getLegacyAdcPasswordChallenges());
}

TEST_CASE("FavoriteManager: older favourites do not implicitly enable legacy ADC challenges", "[favoritemanager][adc-legacy-challenge]") {
    test::TestContext context;
    const auto file = context.tmpDir / "Favorites.xml";
    {
        std::ofstream output(file);
        output << "<Favorites><Hubs><Hub Server=\"adcs://older.invalid:411\" Name=\"Older\"/></Hubs></Favorites>";
        REQUIRE(output.good());
    }
    FavoriteManager manager(*context.ownedCtx);
    manager.load();
    auto* entry = manager.getFavoriteHubEntry("adcs://older.invalid:411");
    REQUIRE(entry);
    CHECK_FALSE(entry->getLegacyAdcPasswordChallenges());
}

TEST_CASE("FavoriteManager: addFavorite hub", "[favoritemanager]") {
    ensureContext();

    FavoriteHubEntry entry(*getContext());
    entry.setServer("adc://test-hub.example.com");
    entry.setName("Test Hub");
    entry.setHubDescription("A test hub");

    g_fm->addFavorite(entry);
    REQUIRE(g_fm->isFavoriteHub("adc://test-hub.example.com") == true);
}

TEST_CASE("FavoriteManager: isFavoriteHub false for unknown", "[favoritemanager]") {
    ensureContext();

    REQUIRE(g_fm->isFavoriteHub("adc://nonexistent.example.com") == false);
}

TEST_CASE("FavoriteManager: addFavorite no duplicate", "[favoritemanager]") {
    ensureContext();

    FavoriteHubEntry entry(*getContext());
    entry.setServer("adc://dup-hub.example.com");
    entry.setName("Dup Hub");
    g_fm->addFavorite(entry);

    auto sizeBefore = g_fm->getFavoriteHubs().size();

    // Try adding again with same server
    g_fm->addFavorite(entry);
    REQUIRE(g_fm->getFavoriteHubs().size() == sizeBefore); // no duplicate
}

TEST_CASE("FavoriteManager: removeFavorite hub", "[favoritemanager]") {
    ensureContext();

    FavoriteHubEntry entry(*getContext());
    entry.setServer("adc://remove-me.example.com");
    entry.setName("Remove Me");
    g_fm->addFavorite(entry);

    REQUIRE(g_fm->isFavoriteHub("adc://remove-me.example.com") == true);

    // Find and remove
    auto* found = g_fm->getFavoriteHubEntry("adc://remove-me.example.com");
    REQUIRE(found != nullptr);
    g_fm->removeFavorite(found);

    REQUIRE(g_fm->isFavoriteHub("adc://remove-me.example.com") == false);
}

TEST_CASE("FavoriteManager: getFavoriteHubEntry", "[favoritemanager]") {
    ensureContext();

    FavoriteHubEntry entry(*getContext());
    entry.setServer("adc://entry-test.example.com");
    entry.setName("Entry Test");
    entry.setNick("myNick");
    g_fm->addFavorite(entry);

    auto* found = g_fm->getFavoriteHubEntry("adc://entry-test.example.com");
    REQUIRE(found != nullptr);
    REQUIRE(found->getName() == "Entry Test");
}

// ─── Favorite Hub Groups ────────────────────────────────────────────────

TEST_CASE("FavoriteManager: hub groups CRUD", "[favoritemanager]") {
    ensureContext();

    FavHubGroups groups;
    FavHubGroupProperties props;
    props.priv = true;
    props.connect = false;
    groups["TestGroup"] = props;
    g_fm->setFavHubGroups(groups);

    auto retrieved = g_fm->getFavHubGroups();
    REQUIRE(retrieved.count("TestGroup") == 1);
    REQUIRE(retrieved["TestGroup"].priv == true);
    REQUIRE(retrieved["TestGroup"].connect == false);
}

// Helper: build a directory path string with native separators + trailing separator
static std::string nativeDirPath(const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / name;
    auto s = p.make_preferred().string();
    if (!s.empty() && s.back() != PATH_SEPARATOR)
        s += PATH_SEPARATOR;
    return s;
}

// ─── Favorite Directories ───────────────────────────────────────────────

TEST_CASE("FavoriteManager: addFavoriteDir", "[favoritemanager]") {
    ensureContext();

    bool added = g_fm->addFavoriteDir(nativeDirPath("share1"), "Share One");
    REQUIRE(added == true);

    auto dirs = g_fm->getFavoriteDirs();
    bool found = false;
    for (auto& d : dirs) {
        if (d.second == "Share One") {
            found = true;
            break;
        }
    }
    REQUIRE(found == true);
}

TEST_CASE("FavoriteManager: addFavoriteDir no duplicate name", "[favoritemanager]") {
    ensureContext();

    g_fm->addFavoriteDir(nativeDirPath("unique_dir1"), "UniqueName");
    bool added = g_fm->addFavoriteDir(nativeDirPath("unique_dir2"), "UniqueName");
    REQUIRE(added == false); // same name
}

TEST_CASE("FavoriteManager: removeFavoriteDir", "[favoritemanager]") {
    ensureContext();

    std::string dirPath = nativeDirPath("removable");
    g_fm->addFavoriteDir(dirPath, "Removable");
    bool removed = g_fm->removeFavoriteDir(dirPath);
    REQUIRE(removed == true);

    bool removedAgain = g_fm->removeFavoriteDir(dirPath);
    REQUIRE(removedAgain == false); // already removed
}

TEST_CASE("FavoriteManager: renameFavoriteDir", "[favoritemanager]") {
    ensureContext();

    g_fm->addFavoriteDir(nativeDirPath("renamedir"), "OldName");
    bool renamed = g_fm->renameFavoriteDir("OldName", "NewName");
    REQUIRE(renamed == true);

    auto dirs = g_fm->getFavoriteDirs();
    bool foundNew = false;
    for (auto& d : dirs) {
        if (d.second == "NewName") {
            foundNew = true;
            break;
        }
    }
    REQUIRE(foundNew == true);
}

// ─── User Commands ──────────────────────────────────────────────────────

TEST_CASE("FavoriteManager: addUserCommand", "[favoritemanager]") {
    ensureContext();

    UserCommand uc = g_fm->addUserCommand(
        UserCommand::TYPE_RAW, UserCommand::CONTEXT_HUB, UserCommand::FLAG_NOSAVE,
        "TestCmd", "/raw command", "", "adc://hub.com"
    );

    REQUIRE(uc.getName() == "TestCmd");
    REQUIRE(uc.getCommand() == "/raw command");
}

TEST_CASE("FavoriteManager: getUserCommand", "[favoritemanager]") {
    ensureContext();

    UserCommand added = g_fm->addUserCommand(
        UserCommand::TYPE_RAW, UserCommand::CONTEXT_HUB, UserCommand::FLAG_NOSAVE,
        "FindMe", "/find", "", ""
    );

    UserCommand found;
    REQUIRE(g_fm->getUserCommand(added.getId(), found) == true);
    REQUIRE(found.getName() == "FindMe");
}

TEST_CASE("FavoriteManager: getUserCommand not found", "[favoritemanager]") {
    ensureContext();

    UserCommand uc;
    REQUIRE(g_fm->getUserCommand(99999, uc) == false);
}

TEST_CASE("FavoriteManager: findUserCommand", "[favoritemanager]") {
    ensureContext();

    g_fm->addUserCommand(
        UserCommand::TYPE_RAW, UserCommand::CONTEXT_HUB, UserCommand::FLAG_NOSAVE,
        "Findable", "/cmd", "", "adc://find.hub"
    );

    int id = g_fm->findUserCommand("Findable", "adc://find.hub");
    REQUIRE(id >= 0);
}

TEST_CASE("FavoriteManager: removeUserCommand", "[favoritemanager]") {
    ensureContext();

    UserCommand uc = g_fm->addUserCommand(
        UserCommand::TYPE_RAW, UserCommand::CONTEXT_HUB, UserCommand::FLAG_NOSAVE,
        "RemoveMe", "/rm", "", ""
    );

    g_fm->removeUserCommand(uc.getId());

    UserCommand found;
    REQUIRE(g_fm->getUserCommand(uc.getId(), found) == false);
}

TEST_CASE("FavoriteManager: updateUserCommand", "[favoritemanager]") {
    ensureContext();

    UserCommand uc = g_fm->addUserCommand(
        UserCommand::TYPE_RAW, UserCommand::CONTEXT_HUB, UserCommand::FLAG_NOSAVE,
        "UpdateMe", "/old", "", ""
    );

    uc.setCommand("/new");
    g_fm->updateUserCommand(uc);

    UserCommand found;
    REQUIRE(g_fm->getUserCommand(uc.getId(), found) == true);
    REQUIRE(found.getCommand() == "/new");
}

TEST_CASE("FavoriteManager: save writes file", "[favoritemanager]") {
    ensureContext();

    FavoriteHubEntry entry(*getContext());
    entry.setServer("adc://save-test.example.com");
    entry.setName("Save Test Hub");
    g_fm->addFavorite(entry);

    // addFavorite calls save() internally, which writes Favorites.xml to the
    // temp config dir. Verify the file exists.
    std::string configPath = Util::getPath(Util::PATH_USER_CONFIG) + "Favorites.xml";
    namespace fs = std::filesystem;
    REQUIRE(fs::exists(configPath));
    REQUIRE(fs::file_size(configPath) > 0);
}
