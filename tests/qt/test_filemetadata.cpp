#include <catch2/catch_test_macros.hpp>

#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/DirectoryListing.h"
#include "dcpp/Streams.h"
#include "dcpp/Util.h"
#include "FileMetadata.h"

#include <QDateTime>
#include <QMetaType>
#include <QTemporaryDir>

namespace {

struct MetadataRowFixture {
    dcpp::Util::PathsMap previousPaths;
    QTemporaryDir temporaryDirectory;
    dcpp::DCContext context;
    dcpp::DirectoryListing listing{context, dcpp::HintedUser()};

    explicit MetadataRowFixture(const std::string& attributes = {})
    {
        REQUIRE(temporaryDirectory.isValid());
        dcpp::Util::PathsMap isolatedPaths;
        for (int i = 0; i < dcpp::Util::PATH_LAST; ++i) {
            const auto path = static_cast<dcpp::Util::Paths>(i);
            previousPaths[path] = dcpp::Util::getPath(path);
            isolatedPaths[path] = temporaryDirectory.path().toStdString() + PATH_SEPARATOR;
        }
        // Keep settings defaults and all filesystem paths inside this fixture.
        dcpp::Util::uninitialize();
        dcpp::Util::initialize(isolatedPaths);
        context.startupMinimal();
        const auto xml = "<FileListing Base=\"/\"><File Name=\"clip.mp4\" Size=\"42\" TTH=\"" +
            std::string(39, 'A') + "\" " + attributes + "/></FileListing>";
        dcpp::MemoryInputStream input(xml);
        listing.loadXML(input, false);
        REQUIRE(listing.getRoot()->files.size() == 1);
    }

    ~MetadataRowFixture()
    {
        context.shutdown();
        if (!previousPaths[dcpp::Util::PATH_USER_CONFIG].empty())
            dcpp::Util::initialize(previousPaths);
    }

    dcpp::DirectoryListing::File& file()
    {
        return **listing.getRoot()->files.begin();
    }

    QList<QVariant> columns()
    {
        return fileMetadataColumns(file());
    }
};

} // namespace

TEST_CASE("Unknown file metadata produces blank cells, never zero or epoch", "[qt][filemetadata]")
{
    MetadataRowFixture fixture;
    const auto columns = fixture.columns();
    REQUIRE(columns.size() == 7);
    for (const auto& column : columns)
        CHECK(column.toString().isEmpty());
}

TEST_CASE("Explicit zero hits stay visible while zero bitrate and timestamp do not", "[qt][filemetadata]")
{
    MetadataRowFixture fixture("TS=\"0\" BR=\"0\" HIT=\"0\"");
    const auto columns = fixture.columns();
    REQUIRE(columns.size() == 7);
    CHECK(columns[0].toString().isEmpty());
    CHECK(columns[4].toString() == "0");
    CHECK(columns[4].metaType().id() == QMetaType::ULongLong);
    CHECK(columns[5].toString().isEmpty());
}

TEST_CASE("Malformed and overflowing numeric metadata remains blank", "[qt][filemetadata]")
{
    for (const std::string value : {"bad", "-1", "+1", "42tail", " 42", "", "18446744073709551616"}) {
        INFO(value);
        MetadataRowFixture fixture("TS=\"" + value + "\" BR=\"" + value + "\" HIT=\"" + value + "\"");
        const auto columns = fixture.columns();
        CHECK(columns[0].toString().isEmpty());
        CHECK(columns[4].toString().isEmpty());
        CHECK(columns[5].toString().isEmpty());
    }
    for (const std::string timestamp : {"9223372036854776", "9223372036854775807", "18446744073709551615"}) {
        INFO(timestamp);
        MetadataRowFixture overflow("TS=\"" + timestamp + "\" BR=\"65537\"");
        CHECK(overflow.columns()[0].toString().isEmpty());
        CHECK(overflow.columns()[5].toString().isEmpty());
    }
}

TEST_CASE("Available metadata keeps its column order and numeric sort conversions", "[qt][filemetadata]")
{
    const auto timestamp = QDateTime(QDate(2023, 11, 14), QTime(12, 34)).toSecsSinceEpoch();
    MetadataRowFixture fixture("TS=\"" + std::to_string(timestamp) +
        "\" BR=\"1200\" WH=\"1920x1080\" MV=\"H.264\" MA=\"AAC\" HIT=\"7\"");
    const auto columns = fixture.columns();
    REQUIRE(columns.size() == 7);
    CHECK(columns[0].toULongLong() == 1200);
    CHECK(columns[0].metaType().id() == QMetaType::Int);
    CHECK(columns[1].toString() == "1920x1080");
    CHECK(columns[2].toString() == "H.264");
    CHECK(columns[3].toString() == "AAC");
    CHECK(columns[4].toULongLong() == 7);
    CHECK(columns[4].metaType().id() == QMetaType::ULongLong);
    CHECK(columns[5].toString() == "2023-11-14 12:34");

    MetadataRowFixture unknown;
    const auto absent = unknown.columns();
    CHECK(absent[0].toString().isEmpty());
    CHECK(absent[4].toString().isEmpty());
    CHECK(absent[0].toULongLong() == 0);
    CHECK(absent[4].toULongLong() == 0);
}

TEST_CASE("Independent media strings and bitrate are visible without a timestamp", "[qt][filemetadata]")
{
    MetadataRowFixture fixture("BR=\"900\" WH=\"1280x720\" MV=\"AV1\" MA=\"Opus\" HIT=\"2\"");
    const auto columns = fixture.columns();
    CHECK(columns[0].toULongLong() == 900);
    CHECK(columns[1].toString() == "1280x720");
    CHECK(columns[2].toString() == "AV1");
    CHECK(columns[3].toString() == "Opus");
    CHECK(columns[4].toULongLong() == 2);
    CHECK(columns[5].toString().isEmpty());
}

TEST_CASE("Copies retain the difference between missing hits and explicit zero", "[qt][filemetadata]")
{
    MetadataRowFixture missing;
    MetadataRowFixture zero("HIT=\"0\"");
    dcpp::DirectoryListing::File missingCopy(missing.file(), true);
    dcpp::DirectoryListing::File zeroCopy(zero.file(), true);
    CHECK(fileMetadataColumns(missingCopy)[4].toString().isEmpty());
    CHECK(fileMetadataColumns(zeroCopy)[4].toString() == "0");
}

TEST_CASE("Modified and Shared file columns do not substitute for each other", "[qt][filemetadata][remote-date]") {
    MetadataRowFixture fixture("Date=\"1690000000\" TS=\"1700000000\"");
    const auto columns = fixture.columns();
    REQUIRE(columns.size() == 7);
    CHECK(columns[5].toString() == QDateTime::fromSecsSinceEpoch(1700000000).toString("yyyy-MM-dd hh:mm"));
    CHECK(columns[6].toString() == QDateTime::fromSecsSinceEpoch(1690000000).toString("yyyy-MM-dd hh:mm"));
    MetadataRowFixture modifiedOnly("Date=\"1690000000\"");
    CHECK(modifiedOnly.columns()[5].toString().isEmpty());
    CHECK_FALSE(modifiedOnly.columns()[6].toString().isEmpty());
}
