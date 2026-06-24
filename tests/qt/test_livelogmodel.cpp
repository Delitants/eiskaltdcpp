#include <catch2/catch_test_macros.hpp>

#include "LiveLogModel.h"

#include <QDateTime>

#include <array>

namespace {

dcpp::LogEntry entry(uint64_t sequence, time_t timestamp, dcpp::LogManager::Area area,
    const std::string& message)
{
    return { sequence, timestamp, area, message };
}

} // namespace

TEST_CASE("LiveLogModel exposes time category and message columns", "[livelog][model]")
{
    LiveLogModel model;
    const time_t timestamp = 1710000000;
    model.appendEntry(entry(1, timestamp, dcpp::LogManager::CHAT, "hello"));

    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.columnCount() == 3);
    CHECK(model.headerData(LiveLogModel::Time, Qt::Horizontal).toString() == "Time");
    CHECK(model.headerData(LiveLogModel::Category, Qt::Horizontal).toString() == "Category");
    CHECK(model.headerData(LiveLogModel::Message, Qt::Horizontal).toString() == "Message");
    CHECK(model.data(model.index(0, LiveLogModel::Time)).toString() ==
        QDateTime::fromSecsSinceEpoch(timestamp).toString("yyyy-MM-dd HH:mm:ss"));
    CHECK(model.data(model.index(0, LiveLogModel::Category)).toString() == "Chat");
    CHECK(model.data(model.index(0, LiveLogModel::Message)).toString() == "hello");
}

TEST_CASE("LiveLogModel category mask hides rows without discarding history", "[livelog][model]")
{
    LiveLogModel model;
    dcpp::LogManager::EntryList entries = {
        entry(1, 1, dcpp::LogManager::CHAT, "chat"),
        entry(2, 2, dcpp::LogManager::DOWNLOAD, "download"),
        entry(3, 3, dcpp::LogManager::SYSTEM, "system")
    };
    model.replaceEntries(entries);

    model.setCategoryMask(LiveLogModel::categoryBit(dcpp::LogManager::DOWNLOAD));
    REQUIRE(model.rowCount() == 1);
    CHECK(model.data(model.index(0, LiveLogModel::Message)).toString() == "download");

    model.setCategoryMask(LiveLogModel::allCategoryMask());
    REQUIRE(model.rowCount() == 3);
    CHECK(model.data(model.index(0, LiveLogModel::Message)).toString() == "chat");
    CHECK(model.data(model.index(2, LiveLogModel::Message)).toString() == "system");
}

TEST_CASE("LiveLogModel assigns stable category bits and translated names", "[livelog][model]")
{
    const std::array expectedNames = {
        "Chat", "Private messages", "Downloads", "Finished downloads", "Uploads",
        "System", "Status", "Search spy", "Command debug"
    };

    for(size_t i = 0; i < expectedNames.size(); ++i) {
        const auto area = static_cast<dcpp::LogManager::Area>(i);
        CHECK(LiveLogModel::categoryBit(area) == (quint32(1) << i));
        CHECK(LiveLogModel::categoryName(area) == expectedNames[i]);
    }
    CHECK(LiveLogModel::allCategoryMask() == 0x1ffu);
}

TEST_CASE("LiveLogModel keeps entries in sequence order and rejects duplicates", "[livelog][model]")
{
    LiveLogModel model;
    dcpp::LogManager::EntryList entries = {
        entry(4, 4, dcpp::LogManager::STATUS, "four"),
        entry(2, 2, dcpp::LogManager::STATUS, "two"),
        entry(3, 3, dcpp::LogManager::STATUS, "three"),
        entry(3, 3, dcpp::LogManager::STATUS, "duplicate")
    };
    model.replaceEntries(entries);

    REQUIRE(model.rowCount() == 3);
    CHECK(model.data(model.index(0, LiveLogModel::Message)).toString() == "two");
    CHECK(model.data(model.index(2, LiveLogModel::Message)).toString() == "four");
    CHECK(model.lastSequence() == 4);

    model.appendEntry(entry(4, 4, dcpp::LogManager::STATUS, "duplicate four"));
    model.appendEntry(entry(5, 5, dcpp::LogManager::STATUS, "five"));
    REQUIRE(model.rowCount() == 4);
    CHECK(model.lastSequence() == 5);
    CHECK(model.data(model.index(3, LiveLogModel::Message)).toString() == "five");
}

TEST_CASE("LiveLogModel clear removes visible and retained entries", "[livelog][model]")
{
    LiveLogModel model;
    model.appendEntry(entry(7, 7, dcpp::LogManager::PM, "private"));
    REQUIRE(model.rowCount() == 1);

    model.clear();

    CHECK(model.rowCount() == 0);
    CHECK(model.lastSequence() == 0);
    model.setCategoryMask(LiveLogModel::allCategoryMask());
    CHECK(model.rowCount() == 0);
}

TEST_CASE("LiveLogModel catches up only entries newer than its last sequence",
    "[livelog][model]")
{
    LiveLogModel model;
    model.replaceEntries({
        entry(10, 10, dcpp::LogManager::STATUS, "ten"),
        entry(11, 11, dcpp::LogManager::STATUS, "eleven")
    });

    model.appendNewEntries({
        entry(10, 10, dcpp::LogManager::STATUS, "old ten"),
        entry(11, 11, dcpp::LogManager::STATUS, "old eleven"),
        entry(12, 12, dcpp::LogManager::STATUS, "twelve"),
        entry(13, 13, dcpp::LogManager::STATUS, "thirteen")
    });

    REQUIRE(model.rowCount() == 4);
    CHECK(model.lastSequence() == 13);
    CHECK(model.data(model.index(2, LiveLogModel::Message)).toString() == "twelve");
    CHECK(model.data(model.index(3, LiveLogModel::Message)).toString() == "thirteen");
}

TEST_CASE("LiveLogModel clear watermark rejects stale queued entries",
    "[livelog][model]")
{
    LiveLogModel model;
    model.replaceEntries({
        entry(20, 20, dcpp::LogManager::SYSTEM, "twenty"),
        entry(21, 21, dcpp::LogManager::SYSTEM, "twenty-one")
    });

    model.clearThroughSequence(21);
    model.appendEntry(entry(20, 20, dcpp::LogManager::SYSTEM, "stale twenty"));
    model.appendEntry(entry(21, 21, dcpp::LogManager::SYSTEM, "stale twenty-one"));
    model.appendEntry(entry(22, 22, dcpp::LogManager::SYSTEM, "twenty-two"));

    REQUIRE(model.rowCount() == 1);
    CHECK(model.lastSequence() == 22);
    CHECK(model.data(model.index(0, LiveLogModel::Message)).toString() == "twenty-two");
}
