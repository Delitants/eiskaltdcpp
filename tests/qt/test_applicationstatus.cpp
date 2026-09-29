#include <catch2/catch_test_macros.hpp>
#include "tests/TestContext.h"
#include "ApplicationStatus.h"
#include <QLabel>

TEST_CASE("Application status records plain text without recursive system events", "[application-status][livelog]")
{
    dcpp::test::TestContext context;
    auto *logs = context.ownedCtx->getLogManager();
    logs->saveSetting(dcpp::LogManager::STATUS, dcpp::LogManager::FORMAT, "%[message]");
    logs->saveSetting(dcpp::LogManager::SYSTEM, dcpp::LogManager::FORMAT, "%[message]");
    logs->clearLiveEntries();
    application_status::record(*logs, "Ready; <plain>");
    application_status::diagnostic(*logs, "Torrent: relay warning; retrying");
    const auto entries = logs->getLiveEntries();
    REQUIRE(entries.size() == 2);
    CHECK(entries.front().area == dcpp::LogManager::STATUS);
    CHECK(entries.front().message == "Ready; <plain>");
    CHECK(entries.back().area == dcpp::LogManager::SYSTEM);
    CHECK(logs->getLastLogs().empty());
    logs->message("Torrent: actual configuration failure");
    CHECK(logs->getLastLogs().size() == 1);
    CHECK(logs->getLiveEntries().back().area == dcpp::LogManager::SYSTEM);
    QLabel label;
    label.resize(1000, 30);
    QStringList history;
    application_status::display(label, "Ready; <plain>", history, 3);
    CHECK(label.textFormat() == Qt::PlainText);
    CHECK(label.text() == "Ready; <plain>");
    CHECK_FALSE(label.toolTip().contains("&#59;"));
    CHECK(label.toolTip().contains("&lt;plain&gt;"));
    application_status::display(label, "Second", history, 1);
    REQUIRE(history.size() == 1);
    CHECK(history.front() == "Second");
}

TEST_CASE("Warning colors remain red in light and dark themes and reset for normal status", "[application-status][livelog]")
{
    for (const bool dark : {false, true}) {
        QWidget parent;
        QPalette palette = parent.palette();
        palette.setColor(QPalette::Window, QColor(dark ? "#25282c" : "#f2f3f5"));
        palette.setColor(QPalette::WindowText, QColor(dark ? "#f0f3f5" : "#16191d"));
        parent.setPalette(palette);
        QLabel label(&parent);
        label.resize(900, 25);
        const auto original = label.palette().color(QPalette::WindowText);
        QStringList history;
        application_status::display(label, "Passive peer <name>", history, 5, true);
        const auto warning = label.palette().color(QPalette::WindowText);
        CHECK(warning.red() > warning.green() * 1.5);
        CHECK(warning.red() > warning.blue() * 1.5);
        CHECK(label.text() == "Passive peer <name>");
        CHECK(label.toolTip().contains("&lt;name&gt;"));
        application_status::display(label, "Normal", history, 5);
        CHECK(label.palette().color(QPalette::WindowText) == original);
    }
}
