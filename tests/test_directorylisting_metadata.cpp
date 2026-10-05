#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "DCContext.h"
#include "DirectoryListing.h"
#include "Streams.h"
#include "Util.h"
#include "ADLSearch.h"
#include "ClientManager.h"

#include <chrono>
#include <filesystem>
#include <limits>

using namespace dcpp;

namespace {

string fileTag(const string& name, const string& attributes = {}, const string& tth = string(39, 'A'))
{
    return "<File Name=\"" + name + "\" Size=\"42\" TTH=\"" + tth +
        "\" " + attributes + "/>";
}

struct MetadataFixture {
    Util::PathsMap previousPaths;
    std::filesystem::path temporaryPath;
    DCContext context;
    DirectoryListing listing{context, HintedUser()};

    explicit MetadataFixture(bool full = false)
    {
        temporaryPath = std::filesystem::temp_directory_path() /
            ("eiskalt_metadata_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temporaryPath);
        Util::PathsMap isolatedPaths;
        for (int i = 0; i < Util::PATH_LAST; ++i) {
            const auto path = static_cast<Util::Paths>(i);
            previousPaths[path] = Util::getPath(path);
            isolatedPaths[path] = temporaryPath.string() + PATH_SEPARATOR;
        }
        // Explicitly reset Util so prior tests cannot suppress our path overrides.
        Util::uninitialize();
        Util::initialize(isolatedPaths);
        if(full) context.startup(); else context.startupMinimal();
    }

    ~MetadataFixture()
    {
        context.shutdown();
        if (!previousPaths[Util::PATH_USER_CONFIG].empty())
            Util::initialize(previousPaths);
        std::error_code error;
        std::filesystem::remove_all(temporaryPath, error);
    }

    void load(const string& files)
    {
        const string xml = "<FileListing Base=\"/\">" + files + "</FileListing>";
        MemoryInputStream input(xml);
        listing.loadXML(input, false);
    }

    void update(const string& attributes, const string& tth = string(39, 'A'))
    {
        listing.updateXML("<FileListing Base=\"/\">" + fileTag("clip.mp4", attributes, tth) +
            "</FileListing>");
    }

    DirectoryListing::File& file(const string& name = "clip.mp4")
    {
        for (auto* file : listing.getRoot()->files) {
            if (file->getName() == name)
                return *file;
        }
        FAIL("Expected fixture file was not parsed");
        throw std::runtime_error("Missing fixture file");
    }
};

} // namespace

namespace {
template<class Listing> void loadCancellable(Listing& listing, InputStream& input, const std::function<bool()>& cancel) {
    if constexpr(requires { listing.loadXML(input, false, cancel); }) listing.loadXML(input, false, cancel);
    else listing.loadXML(input, false);
}
template<class Manager> void matchCancellable(Manager& manager, DirectoryListing& listing, const std::function<bool()>& cancel) {
    if constexpr(requires { manager.matchListing(listing, cancel); }) manager.matchListing(listing, cancel);
    else manager.matchListing(listing);
}
}

TEST_CASE("File-list cancellation stops reads and individual buffered XML rows", "[gui-worker-cancel]") {
    MetadataFixture f;
    struct CountedInput : MemoryInputStream {
        int reads = 0;
        explicit CountedInput(const string& xml) : MemoryInputStream(xml) {}
        size_t read(void* bytes, size_t& length) override { ++reads; return MemoryInputStream::read(bytes, length); }
    } input("<FileListing Base=\"/\">" + fileTag("one") + fileTag("two") + "</FileListing>");
    int checks = 0; bool cancelled = false;
    bool beforeRead = false;
    SECTION("before the first read") { beforeRead = true; }
    SECTION("between rows in one input buffer") {}
    try { loadCancellable(f.listing, input, [&] { return beforeRead || ++checks >= 5; }); }
    catch(const DirectoryListingCancelled&) { cancelled = true; }
    CHECK(cancelled);
    if(beforeRead) CHECK(input.reads == 0);
    else { CHECK(input.reads == 1); CHECK(f.listing.getRoot()->files.size() < 2); }
}

TEST_CASE("ADL cancellation discards provisional result directories", "[gui-worker-cancel]") {
    MetadataFixture f(true);
    DirectoryListing listing(f.context, HintedUser(f.context.getClientManager()->getMe(), ""));
    MemoryInputStream input("<FileListing Base=\"/\">" + fileTag("clip1") + fileTag("clip2") + "</FileListing>");
    listing.loadXML(input, false);
    auto* manager = f.context.getADLSearchManager(); REQUIRE(manager);
    ADLSearch rule; rule.searchString = "clip"; manager->collection.push_back(rule);
    int checks = 0; bool cancelled = false;
    try { matchCancellable(*manager, listing, [&] { return ++checks >= 7; }); }
    catch(const DirectoryListingCancelled&) { cancelled = true; }
    CHECK(cancelled); CHECK(listing.getRoot()->directories.empty());
    CHECK(listing.getRoot()->files.size() == 2);
    manager->matchListing(listing);
    REQUIRE(listing.getRoot()->directories.size() == 1);
    CHECK((*listing.getRoot()->directories.begin())->files.size() == 2);
    manager->collection.clear();
}

TEST_CASE("Cancelled file-list loading does not open an absent source", "[gui-worker-cancel]") {
    MetadataFixture f;
    REQUIRE_THROWS_AS(f.listing.loadFile((f.temporaryPath / "absent.xml").string(), [] { return true; }),
                      DirectoryListingCancelled);
}

TEST_CASE("Directory metadata does not require a timestamp", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "BR=\"1200\" WH=\"1920x1080\" MV=\"H.264\" MA=\"AAC\" HIT=\"7\""));

    const auto& file = fixture.file();
    CHECK(file.mediaInfo.bitrate == 1200);
    CHECK(file.mediaInfo.resolution == "1920x1080");
    CHECK(file.mediaInfo.video_info == "H.264");
    CHECK(file.mediaInfo.audio_info == "AAC");
    CHECK(file.getHit() == 7);
    CHECK(file.getTS() == 0);
}

TEST_CASE("Directory metadata is independent of the first file", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    const auto clip = fileTag("clip.mp4", "TS=\"1700000000\" BR=\"320\" MA=\"AAC\" HIT=\"4\"");

    SECTION("Plain file comes first") {
        fixture.load(fileTag("plain.txt") + clip);
    }
    SECTION("Metadata file comes first") {
        fixture.load(clip + fileTag("plain.txt"));
    }

    CHECK(fixture.file().getTS() == 1700000000);
    CHECK(fixture.file().getHit() == 4);
    CHECK(fixture.file().mediaInfo.bitrate == 320);
    CHECK(fixture.file().mediaInfo.audio_info == "AAC");
    CHECK(fixture.file("plain.txt").mediaInfo.bitrate == 0);
    CHECK(fixture.file("plain.txt").mediaInfo.audio_info.empty());
}

TEST_CASE("A timestamped file does not gate later independent fields", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    fixture.load(fileTag("first.txt", "TS=\"1700000000\"") +
        fileTag("clip.mp4", "WH=\"1280x720\" MV=\"AV1\" MA=\"Opus\" BR=\"900\" HIT=\"3\""));

    CHECK(fixture.file().mediaInfo.resolution == "1280x720");
    CHECK(fixture.file().mediaInfo.video_info == "AV1");
    CHECK(fixture.file().mediaInfo.audio_info == "Opus");
    CHECK(fixture.file().mediaInfo.bitrate == 900);
    CHECK(fixture.file().getHit() == 3);
}

TEST_CASE("Partial updates merge metadata without erasing omitted fields", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "TS=\"1700000000\" BR=\"320\" WH=\"1920x1080\" MV=\"H.264\" MA=\"AAC\" HIT=\"8\""));
    auto* original = &fixture.file();

    fixture.update("BR=\"640\" MA=\"Opus\" HIT=\"0\"");

    REQUIRE(fixture.listing.getRoot()->files.size() == 1);
    CHECK(&fixture.file() == original);
    CHECK(fixture.file().mediaInfo.bitrate == 640);
    CHECK(fixture.file().mediaInfo.audio_info == "Opus");
    CHECK(fixture.file().getHit() == 0);
    CHECK(fixture.file().getTS() == 1700000000);
    CHECK(fixture.file().mediaInfo.resolution == "1920x1080");
    CHECK(fixture.file().mediaInfo.video_info == "H.264");

    fixture.update("TS=\"1700000060\" WH=\"1280x720\" MV=\"AV1\"");
    CHECK(fixture.file().getTS() == 1700000060);
    CHECK(fixture.file().mediaInfo.resolution == "1280x720");
    CHECK(fixture.file().mediaInfo.video_info == "AV1");
    CHECK(fixture.file().mediaInfo.bitrate == 640);
    CHECK(fixture.file().mediaInfo.audio_info == "Opus");
}

TEST_CASE("Directory file copies retain media metadata", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "TS=\"1700000000\" BR=\"320\" WH=\"1920x1080\" MV=\"H.264\" MA=\"AAC\" HIT=\"8\""));

    for (const bool adl : {false, true}) {
        DirectoryListing::File copy(fixture.file(), adl);
        CHECK(copy.getAdls() == adl);
        CHECK(copy.getTS() == 1700000000);
        CHECK(copy.getHit() == 8);
        CHECK(copy.mediaInfo.bitrate == 320);
        CHECK(copy.mediaInfo.resolution == "1920x1080");
        CHECK(copy.mediaInfo.video_info == "H.264");
        CHECK(copy.mediaInfo.audio_info == "AAC");
    }
}

TEST_CASE("A changed TTH clears stale metadata before merging supplied fields", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "TS=\"1700000000\" BR=\"320\" WH=\"1920x1080\" MV=\"H.264\" MA=\"AAC\" HIT=\"0\""));
    REQUIRE(fixture.file().hasHit());
    const string replacementTTH = string(38, 'B') + "A";

    fixture.update("MA=\"Opus\"", replacementTTH);

    REQUIRE(fixture.listing.getRoot()->files.size() == 1);
    CHECK(fixture.file().getTTH() == TTHValue(replacementTTH));
    CHECK(fixture.file().getTS() == 0);
    CHECK(fixture.file().mediaInfo.bitrate == 0);
    CHECK(fixture.file().mediaInfo.resolution.empty());
    CHECK(fixture.file().mediaInfo.video_info.empty());
    CHECK(fixture.file().mediaInfo.audio_info == "Opus");
    CHECK_FALSE(fixture.file().hasHit());
    CHECK(fixture.file().getHit() == 0);
}

TEST_CASE("Malformed numeric metadata never becomes a partial or wrapped value", "[directorylisting-metadata]")
{
    for (const string value : {"-1", "+1", "12tail", " 12", "12 ", "1.5", "", "bad", "18446744073709551616"}) {
        INFO(value);
        MetadataFixture fixture;
        fixture.load(fileTag("clip.mp4", "TS=\"" + value + "\" BR=\"" + value + "\" HIT=\"" + value +
            "\" MV=\"AV1\""));
        CHECK(fixture.file().getTS() == 0);
        CHECK(fixture.file().getHit() == 0);
        CHECK(fixture.file().mediaInfo.bitrate == 0);
        CHECK(fixture.file().mediaInfo.video_info == "AV1");
    }
}

TEST_CASE("Numeric metadata accepts representable limits without narrowing", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "TS=\"1700000000\" BR=\"65535\" HIT=\"18446744073709551615\""));
    CHECK(fixture.file().mediaInfo.bitrate == 65535);
    CHECK(fixture.file().getHit() == std::numeric_limits<uint64_t>::max());

    MetadataFixture overflow;
    overflow.load(fileTag("clip.mp4", "TS=\"1700000000\" BR=\"65537\""));
    CHECK(overflow.file().mediaInfo.bitrate == 0);
}

TEST_CASE("Invalid update attributes do not replace previously valid metadata", "[directorylisting-metadata]")
{
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "TS=\"1700000000\" BR=\"320\" MA=\"AAC\" HIT=\"8\""));
    fixture.update("TS=\"bad\" BR=\"65537\" HIT=\"-1\" MA=\"Opus\"");

    CHECK(fixture.file().getTS() == 1700000000);
    CHECK(fixture.file().getHit() == 8);
    CHECK(fixture.file().mediaInfo.bitrate == 320);
    CHECK(fixture.file().mediaInfo.audio_info == "Opus");
}

namespace {
template<class Entry>
uint64_t modifiedDate(const Entry& entry) {
    if constexpr (requires { entry.getRemoteDate(); })
        return entry.getRemoteDate();
    else
        return 0;
}
}

TEST_CASE("Remote modification dates stay independent of shared timestamps", "[directorylisting-metadata][remote-date]") {
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "Date=\"1690000000\" TS=\"1700000000\""));
    CHECK(modifiedDate(fixture.file()) == 1690000000);
    CHECK(fixture.file().getTS() == 1700000000);
    DirectoryListing::File copy(fixture.file(), true);
    CHECK(modifiedDate(copy) == 1690000000);
    fixture.update("BR=\"640\"");
    CHECK(modifiedDate(fixture.file()) == 1690000000);
    fixture.update("Date=\"1690000060\"");
    CHECK(modifiedDate(fixture.file()) == 1690000060);
    CHECK(fixture.file().getTS() == 1700000000);
    fixture.update("", string(38, 'B') + "A");
    CHECK(modifiedDate(fixture.file()) == 0);
    CHECK(fixture.file().getTS() == 0);
}

TEST_CASE("Remote date omission and malformed replacements preserve known dates", "[directorylisting-metadata][remote-date]") {
    MetadataFixture fixture;
    fixture.load(fileTag("clip.mp4", "Date=\"1690000000\""));
    for (const string value : {"", "0", "-1", "+1", " 123", "123tail", "9223372036854775808"}) {
        INFO(value);
        fixture.update("Date=\"" + value + "\"");
        CHECK(modifiedDate(fixture.file()) == 1690000000);
    }
    CHECK(fixture.file().getTS() == 0);
}

TEST_CASE("BaseDate belongs only to the selected base directory", "[directorylisting-metadata][remote-date]") {
    MetadataFixture fixture;
    fixture.listing.updateXML("<FileListing Base=\"/Media/Nested/\" BaseDate=\"1690000000\">"
        "<Directory Name=\"Child\" Date=\"1680000000\" Incomplete=\"1\"/>" +
        fileTag("clip.mp4") + "</FileListing>");
    auto* media = *fixture.listing.getRoot()->directories.begin();
    auto* nested = *media->directories.begin();
    auto* child = *nested->directories.begin();
    CHECK(modifiedDate(*fixture.listing.getRoot()) == 0);
    CHECK(modifiedDate(*media) == 0);
    CHECK(modifiedDate(*nested) == 1690000000);
    CHECK(modifiedDate(*child) == 1680000000);
    CHECK(modifiedDate(**nested->files.begin()) == 0);
    fixture.listing.updateXML("<FileListing Base=\"/Media/Nested/\">"
        "<Directory Name=\"Child\"/></FileListing>");
    CHECK(modifiedDate(*nested) == 1690000000);
    CHECK(modifiedDate(*child) == 1680000000);
    fixture.listing.updateXML("<FileListing Base=\"/Media/Nested/Child/\" BaseDate=\"1700000000\"/>");
    CHECK(modifiedDate(*child) == 1700000000);
    CHECK(modifiedDate(*nested) == 1690000000);
}
