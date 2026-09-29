#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include <ranges>
#define private public
#define protected public
#include "DCContext.h"
#include "AdcHub.h"
#undef protected
#undef private
#include "TestContext.h"
#include "ClientManager.h"
#include "FavoriteManager.h"
#include "TimerManager.h"
#include "ShareManager.h"
#include "HashManager.h"
#include "SearchManager.h"
#include "QueueManager.h"
#include "UploadManager.h"
#include "HashBloom.h"
#include "MerkleTree.h"
#include "AdcResourceLimits.h"
#include "Encoder.h"
#include <limits>

using namespace dcpp;

namespace {
struct RecordingHub : AdcHub {
    vector<AdcCommand> replies;
    explicit RecordingHub(DCContext& context) : AdcHub(context, "adc://resources.invalid", false) { }
    void send(const AdcCommand& command) override { replies.push_back(command); }
};

struct HubFixture : ClientListener {
    test::TestContext fixture;
    std::unique_ptr<RecordingHub> hub;
    int prompts = 0;
    HubFixture() {
        auto& context = *fixture.ownedCtx;
        context.timerManager_ = std::make_unique<TimerManager>(context);
        context.hashManager_ = std::make_unique<HashManager>(context);
        context.searchManager_ = std::make_unique<SearchManager>(context);
        context.clientManager_ = std::make_unique<ClientManager>(context);
        context.favoriteManager_ = std::make_unique<FavoriteManager>(context);
        context.queueManager_ = std::make_unique<QueueManager>(context);
        context.uploadManager_ = std::make_unique<UploadManager>(context);
        context.shareManager_ = std::make_unique<ShareManager>(context);
        hub = std::make_unique<RecordingHub>(context);
        hub->addListener(this);
    }
    void on(GetPassword, Client*) override { ++prompts; }
};
}

TEST_CASE("ADC challenge validation accepts bounded complete Base32 byte strings", "[adc-resources]") {
    for (size_t length : {24, 25, 26, 27, 28, 29, 30, 31, 256}) {
        vector<uint8_t> data(length);
        for (size_t i = 0; i < length; ++i) data[i] = static_cast<uint8_t>(i);
        const auto encoded = Encoder::toBase32(data.data(), data.size());
        size_t decoded = 73;
        REQUIRE(adcPasswordChallengeSize(encoded, encoded.size(), decoded));
        CHECK(decoded == length);
        decoded = 73;
        CHECK_FALSE(adcPasswordChallengeSize(encoded, encoded.size() - 1, decoded));
        CHECK(decoded == 73);
    }
}

TEST_CASE("ADC challenge guard rejects malformed encodings without changing its output", "[adc-resources]") {
    const auto minimum = string(39, 'A');
    for (const auto& value : vector<string>{"", "A", string(37, 'A'), string(41, 'A'),
             minimum + "=", string(39, 'a'), string(38, 'A') + "B",
             string(38, 'A') + "!", string(38, 'A') + string(1, '\0')}) {
        size_t decoded = 73;
        CHECK_FALSE(adcPasswordChallengeSize(value, 1024, decoded));
        CHECK(decoded == 73);
    }
    size_t decoded = 73;
    CHECK_FALSE(adcPasswordChallengeSize(minimum, 0, decoded));
    CHECK(decoded == 73);
}

TEST_CASE("Bloom request guard preserves supported dimensions and empty replies", "[adc-resources]") {
    struct Example { string bytes, hashes, hashBits; size_t files, bits, k, h; };
    for (const auto& example : vector<Example>{
             {"0", "1", "1", 0, 0, 1, 1},
             {"0008", "01", "06", 100, 64, 1, 6},
             {"8", "3", "64", 100, 64, 3, 64},
             {"400", "4", "24", 100, 3200, 4, 24},
             {"40", "1", "24", 1, 320, 1, 24}}) {
        BloomRequest request;
        REQUIRE(validateBloomRequest(example.bytes, example.hashes, example.hashBits,
            example.files, 1024, request) == BloomRequestError::None);
        CHECK(request.bits == example.bits);
        CHECK(request.hashes == example.k);
        CHECK(request.hashBits == example.h);
    }
}

TEST_CASE("Bloom request guard bounds numeric fields without allocating a filter", "[adc-resources]") {
    struct Example { string bytes, hashes, hashBits; size_t files, capacity; BloomRequestError error; };
    for (const auto& example : vector<Example>{
             {"8", "0", "24", 100, 1024, BloomRequestError::Hashes},
             {"8", "9", "24", 100, 1024, BloomRequestError::Hashes},
             {"8", "1tail", "24", 100, 1024, BloomRequestError::Hashes},
             {"8", "1", "0", 100, 1024, BloomRequestError::HashBits},
             {"8", "1", "65", 100, 1024, BloomRequestError::HashBits},
             {"8", "8", "32", 100, 1024, BloomRequestError::HashBits},
             {"8", "1", "24tail", 100, 1024, BloomRequestError::HashBits},
             {"8tail", "1", "24", 100, 1024, BloomRequestError::Size},
             {"-1", "1", "24", 100, 1024, BloomRequestError::Size},
             {"+8", "1", "24", 100, 1024, BloomRequestError::Size},
             {"7", "1", "24", 100, 1024, BloomRequestError::Size},
             {"16", "1", "6", 100, 1024, BloomRequestError::Size},
             {"8", "1", "24", 0, 1024, BloomRequestError::Size},
             {"408", "4", "24", 100, 1024, BloomRequestError::Size},
             {"8", "1", "24", 100, 7, BloomRequestError::Size},
             {"64", "1", "24", 100, 63, BloomRequestError::Size},
             {std::to_string(std::numeric_limits<size_t>::max() / 8 + 1), "1", "64",
                 std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max(), BloomRequestError::Size},
             {std::to_string(std::numeric_limits<size_t>::max()), "1", "64",
                 std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max(), BloomRequestError::Size}}) {
        BloomRequest request{73, 3, 24};
        CHECK(validateBloomRequest(example.bytes, example.hashes, example.hashBits,
            example.files, example.capacity, request) == example.error);
        CHECK(request.bits == 73);
        CHECK(request.hashes == 3);
        CHECK(request.hashBits == 24);
    }
    BloomRequest request;
    CHECK(validateBloomRequest("8", "3", "64", std::numeric_limits<size_t>::max(),
        1024, request) == BloomRequestError::None);
    CHECK(request.bits == 64);
}

TEST_CASE("Bloom geometry validation handles native-width boundaries without allocation", "[adc-resources]") {
    constexpr size_t width = std::numeric_limits<size_t>::digits;
    constexpr size_t halfRangeBits = size_t(1) << (width - 1);
    const auto capacity = std::numeric_limits<size_t>::max();
    BloomRequest request;
    REQUIRE(parseBloomRequest(std::to_string(halfRangeBits / 8), "1",
        std::to_string(width - 1), capacity, request) == BloomRequestError::None);
    CHECK(request.bits == halfRangeBits);
    CHECK(parseBloomRequest(std::to_string(halfRangeBits / 8 + 8), "1",
        std::to_string(width - 1), capacity, request) == BloomRequestError::Size);
    CHECK(request.bits == halfRangeBits);
    REQUIRE(parseBloomRequest(std::to_string(halfRangeBits / 8 + 8), "1",
        std::to_string(width), capacity, request) == BloomRequestError::None);
    CHECK(request.bits == halfRangeBits + 64);
}

TEST_CASE("ADC password replies retain modern and legacy hash input order", "[adc-resources]") {
    for (bool legacy : {false, true}) {
        HubFixture fixture;
        auto& hub = *fixture.hub;
        hub.oldPassword = legacy;
        const string password = "fixture-password";
        ByteVector challenge(24);
        for (size_t i = 0; i < challenge.size(); ++i) challenge[i] = static_cast<uint8_t>(i);
        const auto encoded = Encoder::toBase32(challenge.data(), challenge.size());
        hub.emulateCommand("IGPA " + encoded);
        REQUIRE(fixture.prompts == 1);
        CHECK(hub.state == Client::STATE_VERIFY);
        CHECK(hub.salt == encoded);
        hub.password(password);
        REQUIRE(hub.replies.size() == 1);
        CHECK(hub.replies[0].getCommand() == AdcCommand::CMD_PAS);
        TigerHash expected;
        if(legacy) {
            const auto cid = hub.getMyIdentity().getUser()->getCID();
            expected.update(cid.data(), CID::SIZE);
        }
        expected.update(password.data(), password.size());
        expected.update(challenge.data(), challenge.size());
        CHECK(hub.replies[0].getParam(0) == Encoder::toBase32(expected.finalize(), TigerHash::BYTES));
        CHECK(hub.salt.empty());
        hub.password(password);
        CHECK(hub.replies.size() == 1);
    }
}

TEST_CASE("ADC rejected challenges do not replace a valid prompt and obey the configured envelope", "[adc-resources]") {
    HubFixture fixture;
    auto& hub = *fixture.hub;
    const string valid(39, 'A');
    hub.state = Client::STATE_IDENTIFY;
    hub.emulateCommand("IGPA invalid");
    CHECK(fixture.prompts == 0);
    CHECK(hub.state == Client::STATE_IDENTIFY);
    CHECK(hub.salt.empty());
    hub.emulateCommand("IGPA " + valid);
    REQUIRE(fixture.prompts == 1);
    hub.emulateCommand("IGPA invalid");
    CHECK(fixture.prompts == 1);
    CHECK(hub.salt == valid);
    CHECK(hub.state == Client::STATE_VERIFY);
    auto& settings = *fixture.fixture.ownedCtx->getSettingsManager();
    settings.set(SettingsManager::MAX_COMMAND_LENGTH, 43);
    hub.emulateCommand("IGPA " + valid);
    CHECK(fixture.prompts == 1);
    hub.password("fixture-password");
    CHECK(hub.replies.empty());
    CHECK(hub.salt.empty());
    settings.set(SettingsManager::MAX_COMMAND_LENGTH, 44);
    hub.emulateCommand("IGPA " + valid);
    CHECK(fixture.prompts == 2);
    hub.password("fixture-password");
    CHECK(hub.replies.size() == 1);
}

TEST_CASE("ADC empty bloom reply retains positional fields and tolerates unknown flags", "[adc-resources]") {
    HubFixture fixture;
    auto& hub = *fixture.hub;
    hub.emulateCommand("IGET blom / 0 0 BK3 BH64 XXfuture");
    REQUIRE(hub.replies.size() == 1);
    CHECK(hub.replies[0].getCommand() == AdcCommand::CMD_SND);
    CHECK(hub.replies[0].getParam(0) == "blom");
    CHECK(hub.replies[0].getParam(1) == "/");
    CHECK(hub.replies[0].getParam(2) == "0");
    CHECK(hub.replies[0].getParam(3) == "0");
    CHECK(hub.replies[0].getParam(4) == "BK3");
    CHECK(hub.replies[0].getParameters().size() == 5);
}

TEST_CASE("ADC bloom rejects invalid fields before inspecting the share", "[adc-resources]") {
    HubFixture fixture;
    auto& hub = *fixture.hub;
    fixture.fixture.ownedCtx->shareManager_.reset();
    struct Example { const char* command; const char* reason; };
    for (const auto& example : {
             Example{"IGET blom / 0 8 BKinvalid BH24", "Unsupported k"},
             Example{"IGET blom / 0 8 BK1 BHinvalid", "Unsupported h"},
             Example{"IGET blom / 0 invalid BK1 BH24", "Unsupported m"},
             Example{"IGET blom other 0 8 BK1 BH24", "Unsupported bloom range"},
             Example{"IGET blom / 1 8 BK1 BH24", "Unsupported bloom range"},
             Example{"IGET blom / 0 8 BK1", "Unsupported h"},
             Example{"IGET blom / 0 8 BH24", "Unsupported k"}}) {
        hub.replies.clear();
        hub.emulateCommand(example.command);
        REQUIRE(hub.replies.size() == 1);
        CHECK(hub.replies[0].getCommand() == AdcCommand::CMD_STA);
        CHECK(hub.replies[0].getParam(0) == "250");
        CHECK(hub.replies[0].getParam(1) == example.reason);
    }
}

TEST_CASE("Small bloom filters preserve exact bytes with 64-bit sub-hashes", "[adc-resources]") {
    HashBloom bloom;
    bloom.reset(3, 192, 64);
    TTHValue hash;
    memset(hash.data, 0, sizeof(hash.data));
    hash.data[7] = 0x80;
    bloom.add(hash);
    CHECK(bloom.match(hash));
    ByteVector bytes;
    bloom.copy_to(bytes);
    REQUIRE(bytes.size() == 24);
    for (size_t i = 0; i < bytes.size(); ++i)
        CHECK(bytes[i] == (i == 0 || i == 16 ? 1 : 0));
}
