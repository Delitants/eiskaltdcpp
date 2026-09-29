#include <catch2/catch_test_macros.hpp>

#include "LiveLogModel.h"

#include <QDateTime>
#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>

#include <array>

namespace {

dcpp::LogEntry entry(uint64_t sequence, time_t timestamp, dcpp::LogManager::Area area,
    const std::string& message)
{
    return { sequence, timestamp, area, message };
}

} // namespace

TEST_CASE("LiveLogModel inserts a catch-up batch with one row notification",
    "[livelog][batch]")
{
    LiveLogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.appendNewEntries({
        entry(1, 1, dcpp::LogManager::SYSTEM, "one"),
        entry(2, 2, dcpp::LogManager::STATUS, "two"),
        entry(2, 2, dcpp::LogManager::STATUS, "duplicate"),
        entry(3, 3, dcpp::LogManager::SYSTEM, "three")});
    REQUIRE(model.rowCount() == 3);
    CHECK(inserted.count() == 1);
    CHECK(reset.isEmpty());
}

TEST_CASE("LiveLogModel retains persistent rows when the history rolls over",
    "[livelog][batch]")
{
    LiveLogModel model;
    dcpp::LogManager::EntryList history;
    for(uint64_t i = 1; i <= 5000; ++i)
        history.push_back(entry(i, i, dcpp::LogManager::SYSTEM, std::to_string(i)));
    model.replaceEntries(history);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    QPersistentModelIndex retained(model.index(20, LiveLogModel::Message));
    QPersistentModelIndex removed(model.index(0, LiveLogModel::Message));
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removals(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);

    model.appendNewEntries({
        entry(5001, 5001, dcpp::LogManager::SYSTEM, "new one"),
        entry(5002, 5002, dcpp::LogManager::STATUS, "new two")});

    CHECK(model.rowCount() == 5000);
    CHECK(resets.isEmpty());
    CHECK(removals.count() == 1);
    CHECK(inserts.count() == 1);
    CHECK_FALSE(removed.isValid());
    REQUIRE(retained.isValid());
    CHECK(retained.row() == 18);
    CHECK(retained.data().toString() == "21");
    CHECK(model.data(model.index(4999, LiveLogModel::Message)).toString() == "new two");
}

TEST_CASE("LiveLogModel batches filtered retention and oversized catch-up",
    "[livelog][batch]")
{
    LiveLogModel model;
    model.setCategoryMask(LiveLogModel::categoryBit(dcpp::LogManager::STATUS));
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    dcpp::LogManager::EntryList history;
    for(uint64_t i = 1; i <= 6000; ++i)
        history.push_back(entry(i, i, i % 2 ? dcpp::LogManager::SYSTEM :
            dcpp::LogManager::STATUS, std::to_string(i)));
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    model.appendNewEntries(history);
    REQUIRE(model.rowCount() == 2500);
    CHECK(model.lastSequence() == 6000);
    CHECK(model.data(model.index(0, LiveLogModel::Message)).toString() == "1002");
    CHECK(resets.isEmpty());
    CHECK(inserted.count() == 1);
    QPersistentModelIndex kept(model.index(0, LiveLogModel::Message));
    model.appendEntry(entry(6001, 6001, dcpp::LogManager::SYSTEM, "hidden"));
    REQUIRE(kept.isValid());
    CHECK(kept.row() == 0);
    CHECK(kept.data().toString() == "1002");
    model.appendEntry(entry(6002, 6002, dcpp::LogManager::STATUS, "visible"));
    CHECK_FALSE(kept.isValid());
    CHECK(model.rowCount() == 2500);
    CHECK(model.data(model.index(0, LiveLogModel::Message)).toString() == "1004");
    CHECK(resets.isEmpty());

    model.clearThroughSequence(6002);
    model.appendNewEntries(history);
    CHECK(model.rowCount() == 0);
    CHECK(model.lastSequence() == 6002);
    model.setCategoryMask(LiveLogModel::allCategoryMask());
    CHECK(model.rowCount() == 0);
}

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
