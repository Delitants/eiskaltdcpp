#include <catch2/catch_test_macros.hpp>

#include "tests/TestContext.h"

#include "dcpp/LogManager.h"
#include "dcpp/SettingsManager.h"

#include <filesystem>
#include <vector>

namespace {

class LogCapture final : public dcpp::LogManagerListener
{
public:
    void on(EntryAdded, const dcpp::LogEntry& entry) noexcept override
    {
        entries.push_back(entry);
    }

    void on(Message, time_t, const std::string& message) noexcept override
    {
        messages.push_back(message);
    }

    std::vector<dcpp::LogEntry> entries;
    std::vector<std::string> messages;
};

void configureLog(dcpp::test::TestContext& testContext, dcpp::LogManager::Area area,
    const std::string& fileName)
{
    auto* settings = testContext.ownedCtx->getSettingsManager();
    const auto logDirectory = testContext.tmpDir / "logs";
    settings->set(dcpp::SettingsManager::LOG_DIRECTORY,
        logDirectory.string() + std::string(1, PATH_SEPARATOR));

    auto* logs = testContext.ownedCtx->getLogManager();
    logs->saveSetting(area, dcpp::LogManager::FILE, fileName);
    logs->saveSetting(area, dcpp::LogManager::FORMAT, "%[message]");
}

dcpp::ParamMap messageParams(const std::string& message)
{
    dcpp::ParamMap params;
    params["message"] = message;
    return params;
}

} // namespace

TEST_CASE("Live log retains structured entries when disk logging is disabled",
    "[livelog][core]")
{
    dcpp::test::TestContext testContext;
    configureLog(testContext, dcpp::LogManager::CHAT, "chat.log");

    auto* logs = testContext.ownedCtx->getLogManager();
    logs->clearLiveEntries();
    auto params = messageParams("not written");
    const auto path = logs->getPath(dcpp::LogManager::CHAT, params);

    logs->log(dcpp::LogManager::CHAT, params, false);

    const auto entries = logs->getLiveEntries();
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().area == dcpp::LogManager::CHAT);
    CHECK(entries.front().message == "not written");
    CHECK(entries.front().timestamp > 0);
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("Live log assigns monotonic sequences and evicts its oldest entry",
    "[livelog][core]")
{
    dcpp::test::TestContext testContext;
    configureLog(testContext, dcpp::LogManager::STATUS, "status.log");

    auto* logs = testContext.ownedCtx->getLogManager();
    logs->clearLiveEntries();
    auto params = messageParams("entry");
    logs->log(dcpp::LogManager::STATUS, params, false);
    const auto firstSequence = logs->getLiveEntries().front().sequence;

    for(size_t i = 0; i < 5000; ++i) {
        logs->log(dcpp::LogManager::STATUS, params, false);
    }

    const auto entries = logs->getLiveEntries();
    REQUIRE(entries.size() == 5000);
    CHECK(entries.front().sequence == firstSequence + 1);
    CHECK(entries.back().sequence == firstSequence + 5000);
}

TEST_CASE("Clearing live log history does not reset its sequence",
    "[livelog][core]")
{
    dcpp::test::TestContext testContext;
    configureLog(testContext, dcpp::LogManager::SPY, "spy.log");

    auto* logs = testContext.ownedCtx->getLogManager();
    logs->clearLiveEntries();
    auto params = messageParams("before clear");
    logs->log(dcpp::LogManager::SPY, params, false);
    const auto previousSequence = logs->getLiveEntries().back().sequence;

    const auto clearedThrough = logs->clearLiveEntries();
    CHECK(logs->getLiveEntries().empty());
    CHECK(clearedThrough == previousSequence);

    params["message"] = "after clear";
    logs->log(dcpp::LogManager::SPY, params, false);
    const auto entries = logs->getLiveEntries();
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().sequence > previousSequence);
}

TEST_CASE("LogManager message emits one compatibility event and one system entry",
    "[livelog][core]")
{
    dcpp::test::TestContext testContext;
    auto* settings = testContext.ownedCtx->getSettingsManager();
    settings->set(dcpp::SettingsManager::LOG_SYSTEM, false);

    auto* logs = testContext.ownedCtx->getLogManager();
    logs->clearLiveEntries();
    LogCapture capture;
    logs->addListener(&capture);

    logs->message("one system message");

    logs->removeListener(&capture);
    const auto entries = logs->getLiveEntries();
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().area == dcpp::LogManager::SYSTEM);
    CHECK(entries.front().message.find("one system message") != std::string::npos);
    REQUIRE(capture.entries.size() == 1);
    CHECK(capture.entries.front().sequence == entries.front().sequence);
    REQUIRE(capture.messages.size() == 1);
    CHECK(capture.messages.front() == "one system message");
}
