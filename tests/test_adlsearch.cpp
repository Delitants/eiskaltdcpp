/*
 * Tests for ADLSearch — source type/size type conversions, size base
 * calculation, and file/directory matching.
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "ADLSearch.h"
#include "TestContext.h"
#include "File.h"
#include "SimpleXML.h"
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace dcpp;

// ----- SourceType string conversions -----

TEST_CASE("ADLSearch: SourceType string round-trips", "[adlsearch]") {
    ADLSearch adl;
    REQUIRE(adl.SourceTypeToString(ADLSearch::OnlyFile) == "Filename");
    REQUIRE(adl.SourceTypeToString(ADLSearch::OnlyDirectory) == "Directory");
    REQUIRE(adl.SourceTypeToString(ADLSearch::FullPath) == "Full Path");

    REQUIRE(adl.StringToSourceType("Filename") == ADLSearch::OnlyFile);
    REQUIRE(adl.StringToSourceType("Directory") == ADLSearch::OnlyDirectory);
    REQUIRE(adl.StringToSourceType("Full Path") == ADLSearch::FullPath);
}

TEST_CASE("ADLSearch: SourceType unknown string defaults to OnlyFile", "[adlsearch]") {
    ADLSearch adl;
    REQUIRE(adl.StringToSourceType("bogus") == ADLSearch::OnlyFile);
    REQUIRE(adl.StringToSourceType("") == ADLSearch::OnlyFile);
}

TEST_CASE("ADLSearch: SourceType case insensitive", "[adlsearch]") {
    ADLSearch adl;
    REQUIRE(adl.StringToSourceType("filename") == ADLSearch::OnlyFile);
    REQUIRE(adl.StringToSourceType("DIRECTORY") == ADLSearch::OnlyDirectory);
    REQUIRE(adl.StringToSourceType("full path") == ADLSearch::FullPath);
}

// ----- SizeType string conversions -----

TEST_CASE("ADLSearch: SizeType string round-trips", "[adlsearch]") {
    ADLSearch adl;
    REQUIRE(adl.SizeTypeToString(ADLSearch::SizeBytes) == "B");
    REQUIRE(adl.SizeTypeToString(ADLSearch::SizeKibiBytes) == "KiB");
    REQUIRE(adl.SizeTypeToString(ADLSearch::SizeMebiBytes) == "MiB");
    REQUIRE(adl.SizeTypeToString(ADLSearch::SizeGibiBytes) == "GiB");

    REQUIRE(adl.StringToSizeType("B") == ADLSearch::SizeBytes);
    REQUIRE(adl.StringToSizeType("KiB") == ADLSearch::SizeKibiBytes);
    REQUIRE(adl.StringToSizeType("MiB") == ADLSearch::SizeMebiBytes);
    REQUIRE(adl.StringToSizeType("GiB") == ADLSearch::SizeGibiBytes);
}

TEST_CASE("ADLSearch: SizeType unknown defaults to SizeBytes", "[adlsearch]") {
    ADLSearch adl;
    REQUIRE(adl.StringToSizeType("bogus") == ADLSearch::SizeBytes);
    REQUIRE(adl.StringToSizeType("") == ADLSearch::SizeBytes);
}

// ----- GetSizeBase -----

TEST_CASE("ADLSearch: GetSizeBase returns correct multipliers", "[adlsearch]") {
    ADLSearch adl;

    adl.typeFileSize = ADLSearch::SizeBytes;
    REQUIRE(adl.GetSizeBase() == 1);

    adl.typeFileSize = ADLSearch::SizeKibiBytes;
    REQUIRE(adl.GetSizeBase() == 1024);

    adl.typeFileSize = ADLSearch::SizeMebiBytes;
    REQUIRE(adl.GetSizeBase() == 1024 * 1024);

    adl.typeFileSize = ADLSearch::SizeGibiBytes;
    REQUIRE(adl.GetSizeBase() == int64_t(1024) * 1024 * 1024);
}

// ----- Default construction -----

TEST_CASE("ADLSearch: default construction values", "[adlsearch]") {
    ADLSearch adl;
    REQUIRE(adl.isActive == true);
    REQUIRE(adl.isAutoQueue == false);
    REQUIRE(adl.sourceType == ADLSearch::OnlyFile);
    REQUIRE(adl.minFileSize == -1);
    REQUIRE(adl.maxFileSize == -1);
    REQUIRE(adl.typeFileSize == ADLSearch::SizeBytes);
    REQUIRE(adl.destDir == "ADLSearch");
    REQUIRE(adl.ddIndex == 0);
}

// NOTE: matchesFile, matchesDirectory, and prepare are private (friend of ADLSearchManager).
// Full matching tests require Phase 2 TestContext infrastructure.
// See TEST_COVERAGE_PLAN.md Phase 2 for ADLSearchManager integration tests.

namespace {
namespace fs = std::filesystem;

// Baseline adapters exercise the legacy writer until the checked APIs exist.
template<class Manager> bool saveAdlChecked(Manager& manager) {
    if constexpr(requires { manager.saveChecked(); }) return manager.saveChecked();
    else { manager.save(); return true; }
}
template<class Manager> bool saveAdlTerminal(Manager& manager) {
    if constexpr(requires { manager.saveForShutdownChecked(); }) return manager.saveForShutdownChecked();
    else { manager.save(); return true; }
}
void writeAdlFile(const string& path, const string& bytes) {
    File out(path, File::WRITE, File::CREATE | File::TRUNCATE);
    out.write(bytes); out.flush(); out.close();
}
string readAdlFile(const string& path) {
    return File(path, File::READ, File::OPEN).read();
}
struct AdlSaveFixture {
    test::TestContext owner;
    std::unique_ptr<ADLSearchManager> manager;
    AdlSaveFixture() {
        REQUIRE(fs::equivalent(fs::u8path(Util::getPath(Util::PATH_USER_CONFIG)), owner.tmpDir));
        manager = std::make_unique<ADLSearchManager>(*owner.ownedCtx);
    }
    string target() const { return (owner.tmpDir / "ADLSearch.xml").string(); }
    void rule(const string& text) {
        ADLSearch search;
        search.searchString = text;
        manager->collection.push_back(search);
    }
    string seed() {
        rule("Original");
        REQUIRE(saveAdlChecked(*manager));
        return readAdlFile(target());
    }
};
// Leave staging writable, but refuse native replacement of the saved file.
struct AdlDenyReplace {
#ifdef _WIN32
    File held;
    explicit AdlDenyReplace(const string& target) : held(target, File::READ, File::OPEN) { }
#else
    fs::path parent;
    fs::perms original;
    explicit AdlDenyReplace(const string& target) :
        parent(fs::path(target).parent_path()), original(fs::status(parent).permissions()) {
        fs::permissions(parent, fs::perms::owner_read | fs::perms::owner_exec);
    }
    ~AdlDenyReplace() {
        std::error_code error;
        fs::permissions(parent, original, error);
    }
#endif
};
}

TEST_CASE("Checked ADL snapshot reloads the existing schema and every rule field", "[adlsearch][adl-save]") {
    AdlSaveFixture f;
    f.rule("movie <&> clip");
    auto& first = f.manager->collection.front();
    first.sourceType = ADLSearch::FullPath;
    first.destDir = "Saved <&> matches";
    first.isActive = false;
    first.isAutoQueue = true;
    first.minFileSize = 23;
    first.maxFileSize = 456;
    first.typeFileSize = ADLSearch::SizeMebiBytes;
    f.rule("Directory rule");
    f.manager->collection.back().sourceType = ADLSearch::OnlyDirectory;
    f.manager->collection.back().typeFileSize = ADLSearch::SizeGibiBytes;
    f.rule("Filename rule");
    f.manager->collection.back().typeFileSize = ADLSearch::SizeKibiBytes;
    f.rule("");
    REQUIRE(saveAdlChecked(*f.manager));
    SimpleXML xml;
    REQUIRE_NOTHROW(xml.fromXML(readAdlFile(f.target())));
    REQUIRE(xml.findChild("ADLSearch")); xml.stepIn();
    REQUIRE(xml.findChild("SearchGroup"));
    f.manager->collection.clear(); f.manager->load();
    REQUIRE(f.manager->collection.size() == 3);
    const auto& loaded = f.manager->collection.front();
    CHECK(loaded.searchString == "movie <&> clip");
    CHECK(loaded.sourceType == ADLSearch::FullPath);
    CHECK(loaded.destDir == "Saved <&> matches");
    CHECK_FALSE(loaded.isActive);
    CHECK(loaded.isAutoQueue);
    CHECK(loaded.minFileSize == 23);
    CHECK(loaded.maxFileSize == 456);
    CHECK(loaded.typeFileSize == ADLSearch::SizeMebiBytes);
    CHECK(f.manager->collection[1].sourceType == ADLSearch::OnlyDirectory);
    CHECK(f.manager->collection[1].typeFileSize == ADLSearch::SizeGibiBytes);
    CHECK(f.manager->collection[2].sourceType == ADLSearch::OnlyFile);
    CHECK(f.manager->collection[2].typeFileSize == ADLSearch::SizeKibiBytes);
    CHECK_FALSE(fs::exists(f.target() + ".tmp"));
}

TEST_CASE("ADL legacy defaults and ordinary void saves remain reusable", "[adlsearch][adl-save]") {
    AdlSaveFixture f;
    writeAdlFile(f.target(), "<ADLSearch><SearchGroup><Search><SearchString>Legacy</SearchString>"
        "</Search></SearchGroup></ADLSearch>");
    f.manager->load();
    REQUIRE(f.manager->collection.size() == 1);
    const auto& loaded = f.manager->collection.front();
    CHECK(loaded.isActive); CHECK_FALSE(loaded.isAutoQueue);
    CHECK(loaded.sourceType == ADLSearch::OnlyFile);
    CHECK(loaded.destDir == "ADLSearch");
    CHECK(loaded.minFileSize == -1); CHECK(loaded.maxFileSize == -1);
    CHECK(loaded.typeFileSize == ADLSearch::SizeBytes);
    f.manager->collection.front().searchString = "Ordinary edit";
    f.manager->save();
    f.manager->collection.clear(); f.manager->load();
    REQUIRE(f.manager->collection.size() == 1);
    CHECK(f.manager->collection.front().searchString == "Ordinary edit");
    f.manager->collection.clear(); REQUIRE(saveAdlChecked(*f.manager));
    f.manager->load(); CHECK(f.manager->collection.empty());
}

TEST_CASE("Checked ADL stage refusal retains saved bytes and edits for retry", "[adlsearch][adl-save]") {
    AdlSaveFixture f; const auto previous = f.seed();
    f.manager->collection.front().searchString = "Updated";
    REQUIRE(fs::create_directory(f.target() + ".tmp"));
    CHECK_FALSE(saveAdlChecked(*f.manager));
    CHECK(readAdlFile(f.target()) == previous);
    CHECK(f.manager->collection.front().searchString == "Updated");
    REQUIRE(fs::remove(f.target() + ".tmp"));
    REQUIRE(saveAdlChecked(*f.manager));
    f.manager->collection.clear(); f.manager->load();
    REQUIRE(f.manager->collection.size() == 1);
    CHECK(f.manager->collection.front().searchString == "Updated");
}

TEST_CASE("Checked ADL native replacement refusal retains old bytes and retries", "[adlsearch][adl-save]") {
#ifndef _WIN32
    if(geteuid() == 0) SKIP("Native directory replacement refusal needs an unprivileged runner");
#endif
    AdlSaveFixture f; const auto previous = f.seed();
    f.manager->collection.front().searchString = "Updated";
    writeAdlFile(f.target() + ".tmp", "staging");
    {
        AdlDenyReplace deny(f.target());
        CHECK_FALSE(saveAdlChecked(*f.manager));
        CHECK(readAdlFile(f.target()) == previous);
        CHECK(readAdlFile(f.target() + ".tmp").find("Updated") != string::npos);
    }
    CHECK(f.manager->collection.front().searchString == "Updated");
    REQUIRE(saveAdlChecked(*f.manager));
    f.manager->collection.clear(); f.manager->load();
    REQUIRE(f.manager->collection.size() == 1);
    CHECK(f.manager->collection.front().searchString == "Updated");
}

#ifndef _WIN32
TEST_CASE("Checked ADL native partial write refusal preserves the old snapshot and retries", "[adlsearch][adl-save]") {
    AdlSaveFixture f; const auto previous = f.seed();
    f.manager->collection.front().searchString = "Updated";
    // Only the disposable child gets a native file-size limit; no worker exists.
    const auto child = fork(); REQUIRE(child >= 0);
    if(child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit limit{16, 16};
        if(setrlimit(RLIMIT_FSIZE, &limit)) std::_Exit(3);
        std::_Exit(saveAdlChecked(*f.manager) ? 1 : 0);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status)); CHECK(WEXITSTATUS(status) == 0);
    CHECK(readAdlFile(f.target()) == previous);
    CHECK(fs::exists(f.target() + ".tmp"));
    if(fs::exists(f.target() + ".tmp")) CHECK(fs::file_size(f.target() + ".tmp") == 16);
    REQUIRE(saveAdlChecked(*f.manager));
    f.manager->collection.clear(); f.manager->load();
    REQUIRE(f.manager->collection.size() == 1);
    CHECK(f.manager->collection.front().searchString == "Updated");
}
#endif

TEST_CASE("ADL terminal success never takes a fresh snapshot through any save or destructor", "[adlsearch][adl-save]") {
    AdlSaveFixture f; f.rule("Committed");
    REQUIRE(saveAdlTerminal(*f.manager));
    const auto committed = readAdlFile(f.target());
    // This is deliberately not an admission-fence test: collection stays public.
    f.manager->collection.front().searchString = "Late raw edit";
    writeAdlFile(f.target() + ".tmp", "untouched staging sentinel");
    CHECK(saveAdlTerminal(*f.manager));
    CHECK(saveAdlChecked(*f.manager));
    f.manager->save();
    CHECK(readAdlFile(f.target()) == committed);
    CHECK(readAdlFile(f.target() + ".tmp") == "untouched staging sentinel");
    CHECK(f.manager->collection.front().searchString == "Late raw edit");
    f.manager.reset();
    CHECK(readAdlFile(f.target()) == committed);
    CHECK(readAdlFile(f.target() + ".tmp") == "untouched staging sentinel");
}

TEST_CASE("ADL terminal success cannot recreate a deleted saved snapshot", "[adlsearch][adl-save]") {
    AdlSaveFixture f; f.rule("Committed"); REQUIRE(saveAdlTerminal(*f.manager));
    REQUIRE(fs::remove(f.target()));
    CHECK(saveAdlTerminal(*f.manager)); CHECK(saveAdlChecked(*f.manager));
    f.manager->save(); f.manager.reset();
    CHECK_FALSE(fs::exists(f.target()));
    CHECK_FALSE(fs::exists(f.target() + ".tmp"));
}

TEST_CASE("ADL terminal stage failure stays false after repair and suppresses destructor saving", "[adlsearch][adl-save]") {
    AdlSaveFixture f; const auto previous = f.seed();
    f.manager->collection.front().searchString = "Unsaved terminal edit";
    REQUIRE(fs::create_directory(f.target() + ".tmp"));
    CHECK_FALSE(saveAdlTerminal(*f.manager));
    CHECK(readAdlFile(f.target()) == previous);
    REQUIRE(fs::remove(f.target() + ".tmp"));
    CHECK_FALSE(saveAdlTerminal(*f.manager));
    CHECK_FALSE(saveAdlChecked(*f.manager));
    f.manager->save();
    CHECK(readAdlFile(f.target()) == previous);
    CHECK(f.manager->collection.front().searchString == "Unsaved terminal edit");
    CHECK_FALSE(fs::exists(f.target() + ".tmp"));
    f.manager.reset();
    CHECK(readAdlFile(f.target()) == previous);
    CHECK_FALSE(fs::exists(f.target() + ".tmp"));
}

TEST_CASE("ADL terminal replacement failure is stable after native refusal ends", "[adlsearch][adl-save]") {
#ifndef _WIN32
    if(geteuid() == 0) SKIP("Native directory replacement refusal needs an unprivileged runner");
#endif
    AdlSaveFixture f; const auto previous = f.seed();
    f.manager->collection.front().searchString = "Unsaved terminal edit";
    writeAdlFile(f.target() + ".tmp", "staging");
    {
        AdlDenyReplace deny(f.target());
        CHECK_FALSE(saveAdlTerminal(*f.manager));
        CHECK(readAdlFile(f.target()) == previous);
    }
    const auto staged = readAdlFile(f.target() + ".tmp");
    f.manager->collection.front().searchString = "Another late edit";
    CHECK_FALSE(saveAdlTerminal(*f.manager)); CHECK_FALSE(saveAdlChecked(*f.manager));
    f.manager->save(); f.manager.reset();
    CHECK(readAdlFile(f.target()) == previous);
    CHECK(readAdlFile(f.target() + ".tmp") == staged);
}

TEST_CASE("ADL destructor still persists ordinary nonterminal edits", "[adlsearch][adl-save]") {
    AdlSaveFixture f; f.rule("Ordinary destructor edit"); f.manager.reset();
    f.manager = std::make_unique<ADLSearchManager>(*f.owner.ownedCtx);
    REQUIRE(f.manager->collection.size() == 1);
    CHECK(f.manager->collection.front().searchString == "Ordinary destructor edit");
}
