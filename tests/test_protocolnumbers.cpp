#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <ranges>
#define private public
#define protected public
#include "DCContext.h"
#include "AdcHub.h"
#include "NmdcHub.h"
#include "UserConnection.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ChatMessage.h"
#include "ClientManager.h"
#include "FavoriteManager.h"
#include "TimerManager.h"
#include "UserCommand.h"
#include "ProtocolNumber.h"

#include <limits>

using namespace dcpp;

namespace {
struct CommandFixture : ClientListener {
    test::TestContext fixture;
    Client* client = nullptr;
    struct Command { int type, context; string name, body; };
    vector<Command> commands;
    vector<std::pair<string, time_t>> messages;
    struct Search { int mode; int64_t size; int type; string terms; };
    vector<Search> searches;
    int userUpdates = 0;
    int userRemovals = 0;
    vector<string> statuses;
    vector<string> redirects;

    explicit CommandFixture(const string& url) {
        auto& context = *fixture.ownedCtx;
        context.timerManager_ = std::make_unique<TimerManager>(context);
        context.clientManager_ = std::make_unique<ClientManager>(context);
        context.favoriteManager_ = std::make_unique<FavoriteManager>(context);
        client = context.getClientManager()->getClient(url);
        client->removeListeners();
        client->addListener(this);
    }
    ~CommandFixture() {
        fixture.ownedCtx->getClientManager()->putClient(client);
    }
    void on(HubUserCommand, Client*, int type, int context, const string& name, const string& body) override {
        commands.push_back({type, context, name, body});
    }
    void on(Message, Client*, const ChatMessage& message) override {
        messages.emplace_back(message.text, message.timestamp);
    }
    void on(NmdcSearch, Client*, const string&, int mode, int64_t size, int type, const string& terms) override {
        searches.push_back({mode, size, type, terms});
    }
    void on(UserUpdated, Client*, const OnlineUser&) override { ++userUpdates; }
    void on(UserRemoved, Client*, const OnlineUser&) override { ++userRemovals; }
    void on(StatusMessage, Client*, const string& message, int) override { statuses.push_back(message); }
    void on(Redirect, Client*, const string& url) override { redirects.push_back(url); }
};
}

TEST_CASE("Protocol decimal numbers preserve output when invalid or outside field bounds", "[protocol-number]") {
    int value = 73;
    for (const auto* input : {"", "-1", "+1", " 1", "1 ", "1tail", "2147483648"}) {
        CAPTURE(input);
        CHECK_FALSE(parseProtocolNumber(input, 0, std::numeric_limits<int>::max(), value));
        CHECK(value == 73);
    }
    CHECK_FALSE(parseProtocolNumber("9", 1, 8, value));
    CHECK_FALSE(parseProtocolNumber("0", 1, 8, value));
    CHECK(value == 73);
    REQUIRE(parseProtocolNumber("0001", 0, 8, value));
    CHECK(value == 1);
    REQUIRE(parseProtocolNumber("0", 0, 8, value));
    CHECK(value == 0);
    REQUIRE(parseProtocolNumber("2147483647", 0, std::numeric_limits<int>::max(), value));
    CHECK(value == 2147483647);
    uint64_t wide = 7;
    REQUIRE(parseProtocolNumber("18446744073709551615", uint64_t(0), UINT64_MAX, wide));
    CHECK(wide == UINT64_MAX);
    CHECK_FALSE(parseProtocolNumber("18446744073709551616", uint64_t(0), UINT64_MAX, wide));
    CHECK(wide == UINT64_MAX);
}

TEST_CASE("ADC user command contexts require a complete decimal token", "[protocol-number]") {
    CommandFixture fixture("adc://commands.invalid");
    auto& hub = *static_cast<AdcHub*>(fixture.client);
    for (const auto* context : {"1tail", "-1", "+1", "2147483648", "0", ""}) {
        CAPTURE(context);
        fixture.commands.clear();
        hub.emulateCommand(string("ICMD Test CT") + context + " TTbody");
        CHECK(fixture.commands.empty());
    }
    for (const auto& [context, expected] : vector<std::pair<string, int>>{
             {"1", 1}, {"0001", 1}, {"16", 16}, {"2147483647", 2147483647}}) {
        fixture.commands.clear();
        hub.emulateCommand(string("ICMD Test CT") + context + " TTbody");
        REQUIRE(fixture.commands.size() == 1);
        CHECK(fixture.commands[0].context == expected);
        CHECK(fixture.commands[0].name == "Test");
        CHECK(fixture.commands[0].body == "body");
    }
    fixture.commands.clear();
    hub.emulateCommand("ICMD Test RM1");
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands[0].type == UserCommand::TYPE_REMOVE);
}

TEST_CASE("ADC chat keeps messages but omits malformed optional timestamps", "[protocol-number]") {
    CommandFixture fixture("adc://commands.invalid");
    auto& hub = *static_cast<AdcHub*>(fixture.client);
    hub.emulateCommand("ISTA 000 ready");
    for (const auto& field : {string(), string(" TS"), string(" TS12tail"),
             string(" TS-1"), string(" TS+12"), string(" TS18446744073709551616")}) {
        CAPTURE(field);
        fixture.messages.clear();
        hub.emulateCommand("IMSG hello" + field);
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages[0].first == "hello");
        CHECK(fixture.messages[0].second == 0);
    }
    for (time_t timestamp : {time_t(0), time_t(1), time_t(1700000000), std::numeric_limits<time_t>::max()}) {
        fixture.messages.clear();
        hub.emulateCommand("IMSG hello TS" + std::to_string(timestamp));
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages[0].second == timestamp);
    }
}

TEST_CASE("ADC quit validates reconnect delays while retaining the minus-one sentinel", "[protocol-number]") {
    CommandFixture fixture("adc://commands.invalid");
    auto& hub = *static_cast<AdcHub*>(fixture.client);
    hub.sid = AdcCommand::toSID("TEST");
    for (bool enabled : {false, true}) {
        for (const auto& field : {string(), string(" TL"), string(" TL12tail"),
                 string(" TL-2"), string(" TL+12"), string(" TL4294967296")}) {
            CAPTURE(enabled, field);
            hub.setAutoReconnect(enabled);
            hub.setReconnDelay(73);
            hub.emulateCommand("IQUI TEST" + field);
            CHECK(hub.getAutoReconnect() == enabled);
            CHECK(hub.getReconnDelay() == 73);
        }
    }
    for (const auto& [value, expected] : vector<std::pair<string, uint32_t>>{
             {"0", 0}, {"0001", 1}, {"4294967295", UINT32_MAX}}) {
        hub.setAutoReconnect(false);
        hub.emulateCommand("IQUI TEST TL" + value);
        CHECK(hub.getAutoReconnect());
        CHECK(hub.getReconnDelay() == expected);
    }
    hub.setReconnDelay(73);
    hub.emulateCommand("IQUI TEST TL-1");
    CHECK_FALSE(hub.getAutoReconnect());
    CHECK(hub.getReconnDelay() == 73);
}

TEST_CASE("ADC status validates severity and digits before changing hub state", "[protocol-number]") {
    for (const auto* code : {"x23", "+23", "323", "023", "12x", "125tail", "23", "0"}) {
        CAPTURE(code);
        CommandFixture fixture("adc://commands.invalid");
        auto& hub = *static_cast<AdcHub*>(fixture.client);
        hub.setPassword("fixture-password");
        hub.emulateCommand(string("ISTA ") + code + " note FCBMSG");
        CHECK(hub.getPassword() == "fixture-password");
        CHECK(fixture.messages.empty());
        CHECK(hub.users.empty());
        CHECK(hub.forbiddenCommands.empty());
    }
    for (const auto* code : {"000", "100", "199", "200", "299"}) {
        CommandFixture fixture("adc://commands.invalid");
        auto& hub = *static_cast<AdcHub*>(fixture.client);
        hub.emulateCommand(string("ISTA ") + code + " note");
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages[0].first == "note");
    }
    CommandFixture fixture("adc://commands.invalid");
    auto& hub = *static_cast<AdcHub*>(fixture.client);
    hub.setPassword("fixture-password");
    hub.emulateCommand("ISTA 223 note");
    CHECK(hub.getPassword().empty());
    hub.emulateCommand("ISTA 125 note FCBMSG");
    CHECK(hub.forbiddenCommands.count(AdcCommand::toFourCC("BMSG")) == 1);
}

TEST_CASE("ADC quit retains removal and redirects when only the optional delay is invalid", "[protocol-number]") {
    CommandFixture fixture("adc://commands.invalid");
    auto& hub = *static_cast<AdcHub*>(fixture.client);
    hub.sid = AdcCommand::toSID("TEST");
    hub.getUser(hub.sid, CID()).getIdentity().setNick("fixture-user");
    hub.setAutoReconnect(false);
    hub.setReconnDelay(73);
    hub.emulateCommand("IQUI TEST TL12tail MSbye RDadc://redirect.invalid");
    CHECK(hub.findUser(hub.sid) == nullptr);
    CHECK(fixture.userRemovals == 1);
    REQUIRE(fixture.statuses.size() == 1);
    CHECK(fixture.statuses[0].find("bye") != string::npos);
    REQUIRE(fixture.redirects.size() == 1);
    CHECK(fixture.redirects[0] == "adc://redirect.invalid");
    CHECK_FALSE(hub.getAutoReconnect());
    CHECK(hub.getReconnDelay() == 73);
}

TEST_CASE("ADC quit ignores absent or incomplete required session identifiers", "[protocol-number]") {
    CommandFixture fixture("adc://commands.invalid");
    auto& hub = *static_cast<AdcHub*>(fixture.client);
    hub.sid = AdcCommand::toSID("TEST");
    hub.setAutoReconnect(false);
    hub.setReconnDelay(73);
    for (const auto* command : {"IQUI", "IQUI A TL1", "IQUI TESTX TL1"}) {
        hub.emulateCommand(command);
        CHECK_FALSE(hub.getAutoReconnect());
        CHECK(hub.getReconnDelay() == 73);
        CHECK(fixture.statuses.empty());
        CHECK(fixture.redirects.empty());
        CHECK(fixture.userRemovals == 0);
    }
}

TEST_CASE("NMDC searches require complete numeric fields and preserve valid search modes", "[protocol-number]") {
    for (const auto* fields : {"F?T?12tail?1?term", "T?F?-1?1?term", "T?T?+1?1?term",
             "T?T?9223372036854775808?1?term", "F?T?0?1tail?term", "F?T?0?0?term",
             "F?T?0?11?term", "F?T?0?-1?term", "F?T?0?2147483648?term",
             "X?T?0?1?term", "F?X?0?1?term", "F!T?0?1?term"}) {
        CAPTURE(fields);
        CommandFixture fixture("dchub://commands.invalid");
        fixture.fixture.ownedCtx->getSettingsManager()->set(SettingsManager::INCOMING_CONNECTIONS,
            SettingsManager::INCOMING_FIREWALL_PASSIVE);
        auto& hub = *static_cast<NmdcHub*>(fixture.client);
        hub.state = Client::STATE_NORMAL;
        hub.emulateCommand(string("$Search 192.0.2.1:412 ") + fields);
        CHECK(fixture.searches.empty());
    }
    struct Example { string fields; int mode; int64_t size; int type; string terms; };
    for (const auto& example : vector<Example>{
             {"F?F?0?1?two$words", 0, 0, 0, "two$words"},
             {"F?T?12?9?TTH:fixture", 0, 12, 8, "TTH:fixture"},
             {"F?T?0?10?disc", 0, 0, 9, "disc"},
             {"T?F?0001?01?term", 1, 1, 0, "term"},
             {"T?T?9223372036854775807?8?folder", 2, INT64_MAX, 7, "folder"}}) {
        CommandFixture fixture("dchub://commands.invalid");
        fixture.fixture.ownedCtx->getSettingsManager()->set(SettingsManager::INCOMING_CONNECTIONS,
            SettingsManager::INCOMING_FIREWALL_PASSIVE);
        auto& hub = *static_cast<NmdcHub*>(fixture.client);
        hub.state = Client::STATE_NORMAL;
        hub.emulateCommand("$Search 192.0.2.1:412 " + example.fields);
        REQUIRE(fixture.searches.size() == 1);
        CHECK(fixture.searches[0].mode == example.mode);
        CHECK(fixture.searches[0].size == example.size);
        CHECK(fixture.searches[0].type == example.type);
        CHECK(fixture.searches[0].terms == example.terms);
    }
}

TEST_CASE("NMDC upload limits omit invalid fields and scale valid values without narrowing", "[protocol-number]") {
    CommandFixture fixture("dchub://commands.invalid");
    auto& hub = *static_cast<NmdcHub*>(fixture.client);
    Identity identity(fixture.fixture.ownedCtx->getClientManager()->getUser(CID()), 0);
    for (const auto* value : {"", "1tail", "-1", "+1", "9007199254740992"}) {
        CAPTURE(value);
        identity.set("US", "1024");
        hub.updateFromTag(identity, string("Fixture V:1,S:2,L:") + value);
        CHECK(identity.get("US").empty());
        CHECK(identity.get("SL") == "2");
    }
    for (const auto& [value, expected] : vector<std::pair<string, string>>{
             {"0", "0"}, {"0001", "1024"}, {"2097152", "2147483648"},
             {"9007199254740991", "9223372036854774784"}}) {
        hub.updateFromTag(identity, "Fixture V:1,L:" + value);
        CHECK(identity.get("US") == expected);
    }
    hub.updateFromTag(identity, "Fixture V:1,S:3");
    CHECK(identity.get("US").empty());
}

TEST_CASE("NMDC invalid searches do not change an existing user to passive", "[protocol-number]") {
    CommandFixture fixture("dchub://commands.invalid");
    fixture.fixture.ownedCtx->getSettingsManager()->set(SettingsManager::INCOMING_CONNECTIONS,
        SettingsManager::INCOMING_FIREWALL_PASSIVE);
    auto& hub = *static_cast<NmdcHub*>(fixture.client);
    hub.state = Client::STATE_NORMAL;
    auto& user = hub.getUser("fixture-peer");
    user.getUser()->unsetFlag(User::PASSIVE);
    fixture.userUpdates = 0;
    for (const auto* fields : {"F?T?0tail?1?term", "F?T?0?1tail?term"}) {
        hub.emulateCommand(string("$Search Hub:fixture-peer ") + fields);
        CHECK_FALSE(user.getUser()->isSet(User::PASSIVE));
        CHECK(fixture.userUpdates == 0);
        CHECK(fixture.searches.empty());
    }
    hub.emulateCommand("$Search Hub:fixture-peer F?T?0?1?term");
    CHECK(user.getUser()->isSet(User::PASSIVE));
    CHECK(fixture.userUpdates == 1);
    CHECK(fixture.searches.size() == 1);
}

TEST_CASE("NMDC GET checks its one-based offset before dispatch", "[protocol-number]") {
    struct Listener : UserConnectionListener {
        vector<std::pair<string, int64_t>> gets;
        int errors = 0;
        void on(Get, UserConnection*, const string& path, int64_t offset) override {
            gets.emplace_back(path, offset);
        }
        void on(ProtocolError, UserConnection*, const string&) override { ++errors; }
    } listener;
    test::TestContext fixture;
    UserConnection connection(false, *fixture.ownedCtx);
    connection.setEncoding(Text::utf8);
    connection.addListener(&listener);
    for (const auto* value : {"", "0", "-1", "+1", "1tail", "9223372036854775808"}) {
        CAPTURE(value);
        listener.gets.clear();
        listener.errors = 0;
        connection.on(BufferedSocketListener::Line(), string("$Get folder/file.txt$") + value);
        CHECK(listener.gets.empty());
        CHECK(listener.errors == 1);
    }
    listener.errors = 0;
    for (const auto& [value, expected] : vector<std::pair<string, int64_t>>{
             {"1", 0}, {"0002", 1}, {"9223372036854775807", INT64_MAX - 1}}) {
        listener.gets.clear();
        connection.on(BufferedSocketListener::Line(), "$Get folder/file.txt$" + value);
        REQUIRE(listener.gets.size() == 1);
        CHECK(listener.gets[0].first == "folder/file.txt");
        CHECK(listener.gets[0].second == expected);
        CHECK(listener.errors == 0);
    }
}

TEST_CASE("NMDC user commands reject partial numbers but preserve names and bodies", "[protocol-number]") {
    CommandFixture fixture("dchub://commands.invalid");
    auto& hub = *static_cast<NmdcHub*>(fixture.client);
    for (const auto* command : {"$UserCommand 1tail 1 Test$body", "$UserCommand 0tail 1",
             "$UserCommand 255tail 1", "$UserCommand 1 1tail Test$body",
             "$UserCommand 0 1tail", "$UserCommand 255 -1",
             "$UserCommand 1 2147483648 Test$body"}) {
        CAPTURE(command);
        fixture.commands.clear();
        hub.emulateCommand(command);
        CHECK(fixture.commands.empty());
    }
    fixture.commands.clear();
    hub.emulateCommand("$UserCommand 1 0001 Test name$body with spaces");
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands[0].context == 1);
    CHECK(fixture.commands[0].name == "Test name");
    CHECK(fixture.commands[0].body == "body with spaces");
    fixture.commands.clear();
    hub.emulateCommand("$UserCommand 255 0");
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands[0].context == 0);
    CHECK(fixture.commands[0].type == UserCommand::TYPE_CLEAR);
}
