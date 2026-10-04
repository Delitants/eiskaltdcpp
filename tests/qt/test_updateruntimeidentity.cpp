#include <catch2/catch_test_macros.hpp>
#include "updates/UpdateRuntimeIdentity.h"
#include "updates/UpdateUrlPolicy.h"
#include <QVariantMap>
namespace {
QVariantMap metadata(const QString& cpu = "arm64") {
    return {{"CFBundleIdentifier", "com.github.eiskaltdcpp"}, {"CFBundleExecutable", "EiskaltDC++"},
        {"CFBundlePackageType", "APPL"}, {"CFBundleVersion", "30000001"},
        {"CFBundleShortVersionString", "3.0.0-pre.1"},
        {"LSMinimumSystemVersion", cpu == "arm64" ? "26.0" : "14.0"},
        {"SUPublicEDKey", QString::fromLatin1(QByteArray(32, '\x11').toBase64())},
        {"SUFeedURL", updates::feedUrlForCpu(cpu).toString()},
        {"SURequireSignedFeed", true}, {"SUVerifyUpdateBeforeExtraction", true},
        {"SUEnableDownloaderService", false}, {"SUEnableAutomaticChecks", false},
        {"SUAutomaticallyUpdate", false}, {"SUEnableSystemProfiling", false},
        {"SUEnableInstallerLauncherService", true},
        {"SUSignedFeedFailureExpirationInterval", qlonglong(0)},
        {"NSAppTransportSecurity", QVariantMap{{"NSAllowsLocalNetworking", true}}}};
}
}
TEST_CASE("Runtime identity binds numeric build to an approved release flavor", "[updates][update-runtime-identity]") {
    for(const auto& cpu : {QString("arm64"), QString("x86_64")}) {
        CAPTURE(cpu);
        auto info = metadata(cpu);
        auto config = updates::validateMacUpdateRuntime(info, cpu, QVersionNumber(26, 1));
        REQUIRE(config);
        CHECK(config->identity.build == 30000001);
        CHECK(config->identity.cpu == cpu);
        CHECK(config->identity.bundleId == "com.github.eiskaltdcpp");
        CHECK(config->identity.os == QVersionNumber(26, 1));
        CHECK(config->identity.defaultChannel == updates::Channel::Preview);
        CHECK(config->feedUrl == updates::feedUrlForCpu(cpu));
        CHECK(config->publicKeyBase64 == QByteArray(32, '\x11').toBase64());
        info["CFBundleShortVersionString"] = "3.0.0";
        REQUIRE(updates::validateMacUpdateRuntime(info, cpu, QVersionNumber(26, 1)));
        CHECK(updates::validateMacUpdateRuntime(info, cpu, QVersionNumber(26, 1))->identity.defaultChannel == updates::Channel::Stable);
    }
}
TEST_CASE("Runtime identity refuses missing coerced or foreign metadata", "[updates][update-runtime-identity]") {
    auto info = metadata();
    for(const auto& key : info.keys()) {
        CAPTURE(key);
        auto incomplete = info; incomplete.remove(key);
        CHECK_FALSE(updates::validateMacUpdateRuntime(incomplete, "arm64", QVersionNumber(26, 0)));
    }
    for(const auto& build : {QString("0"), QString("01"), QString("-1"), QString("3.0.0-pre.1"),
            QString(" 1"), QString("1\n"), QString("9223372036854775808")}) {
        CAPTURE(build); info = metadata(); info["CFBundleVersion"] = build;
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    }
    info = metadata(); info["CFBundleVersion"] = qlonglong(30000001);
    CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    for(const auto& key : {"CFBundleIdentifier", "CFBundleExecutable", "CFBundlePackageType"}) {
        info = metadata(); info[key] = "foreign";
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    }
    for(const auto& key : {"SURequireSignedFeed", "SUVerifyUpdateBeforeExtraction", "SUEnableDownloaderService",
            "SUEnableAutomaticChecks", "SUAutomaticallyUpdate", "SUEnableSystemProfiling", "SUEnableInstallerLauncherService"}) {
        CAPTURE(key);
        for(const auto& value : {QVariant(1), QVariant("false"), QVariant(!metadata()[key].toBool())}) {
            info = metadata(); info[key] = value;
            CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
        }
    }
    for(const auto& value : {QVariant(false), QVariant(0.0), QVariant("0"), QVariant(1)}) {
        info = metadata(); info["SUSignedFeedFailureExpirationInterval"] = value;
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    }
    info = metadata(); info["NSAppTransportSecurity"] = QVariantMap{{"NSAllowsLocalNetworking", 1}};
    CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    info["NSAppTransportSecurity"] = QVariantMap{{"NSAllowsLocalNetworking", true}, {"NSAllowsArbitraryLoads", true}};
    CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
}
TEST_CASE("Runtime identity refuses unsafe feeds keys versions and incompatible OS", "[updates][update-runtime-identity]") {
    auto info = metadata();
    for(const auto& cpu : {QString(), QString("aarch64"), QString("AMD64"), QString("universal")}) {
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, cpu, QVersionNumber(26, 0)));
    }
    for(const auto& os : {QVersionNumber(), QVersionNumber(25, 0), QVersionNumber(26, -1), QVersionNumber(26, 0, -1)}) {
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", os));
    }
    for(const auto& floor : {QString("14.0"), QString("26.1"), QString("026.0"), QString("26.0junk"), QString("26.0\n")}) {
        info = metadata(); info["LSMinimumSystemVersion"] = floor;
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 1)));
    }
    for(const auto& display : {QString(), QString(" 3.0.0"), QString("3.0.0\n"), QString("03.0.0"),
            QString("3.0"), QString("3.0.0-01"), QString("3.0.0-"), QString("3.0.0+"), QString(129, 'a')}) {
        info = metadata(); info["CFBundleShortVersionString"] = display;
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    }
    info = metadata(); info["CFBundleShortVersionString"] = "3.0.0+build.1";
    REQUIRE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    CHECK(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0))->identity.defaultChannel == updates::Channel::Stable);
    info["CFBundleShortVersionString"] = "3.0.0-rc.1+build.1";
    REQUIRE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    CHECK(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0))->identity.defaultChannel == updates::Channel::Preview);
    const QString feed = updates::feedUrlForCpu("arm64").toString();
    for(const auto& url : {QString("http://example.com/"), updates::feedUrlForCpu("x86_64").toString(),
            feed + "#fragment", feed + "?revision=01", feed + "?revision=0", feed + "?revision=1&x=2",
            feed + "\n", feed + "?revision=" + QString(2048, '1')}) {
        info = metadata(); info["SUFeedURL"] = url;
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    }
    info = metadata(); info["SUFeedURL"] = feed + "?revision=10";
    REQUIRE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    CHECK(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0))->feedUrl.query() == "revision=10");
    const auto key = QByteArray(32, '\x11').toBase64();
    for(const auto& invalid : {QByteArray(32, '\0').toBase64(), key + '\n', key.left(43), QByteArray(44, 'a'),
            key.left(42) + "R=", QByteArray(31, 'a').toBase64(), QByteArray(33, 'a').toBase64()}) {
        info = metadata(); info["SUPublicEDKey"] = QString::fromLatin1(invalid);
        CHECK_FALSE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    }
    info = metadata(); info["CFBundleVersion"] = "9223372036854775807";
    REQUIRE(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0)));
    CHECK(updates::validateMacUpdateRuntime(info, "arm64", QVersionNumber(26, 0))->identity.build == 9223372036854775807ULL);
    info = metadata("x86_64");
    CHECK_FALSE(updates::validateMacUpdateRuntime(info, "x86_64", QVersionNumber(13, 6)));
    CHECK(updates::validateMacUpdateRuntime(info, "x86_64", QVersionNumber(14, 0)));
}
