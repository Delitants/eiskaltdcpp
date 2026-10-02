#include "../../../eiskaltdcpp-qt/src/updates/UpdateArchiveVerifier.h"
#include "../../../eiskaltdcpp-qt/src/updates/UpdateFeedVerifier.h"
#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QByteArray readFile(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
}

// Opt-in acceptance of the Python producer using the actual runtime consumer.
// These public-only crypto fixtures are not installable release bundles.
TEST_CASE("Offline publication feeds match the runtime verifier", "[.][feed-publication]") {
    using namespace updates;
    const auto root = qEnvironmentVariable("EISKALT_PUBLICATION_QA");
    REQUIRE(!root.isEmpty());
    for (const auto& channel : {QStringLiteral("preview"), QStringLiteral("stable")}) {
        const QDir directory(root + "/" + channel);
        const auto publicKey = readFile(directory.filePath("public-key.txt"));
        const auto manifest = QJsonDocument::fromJson(readFile(directory.filePath("manifest.json"))).object();
        REQUIRE(!manifest.isEmpty());
        for (const auto& flavor : {QStringLiteral("intel"), QStringLiteral("apple-silicon")}) {
            INFO(channel.toStdString() + "/" + flavor.toStdString());
            const auto bytes = readFile(directory.filePath("feeds/" + flavor + ".xml"));
            const auto verified = verifyUpdateFeed(bytes, publicKey);
            REQUIRE(verified);
            REQUIRE(verified->releases.size() == 1);
            const auto& release = verified->releases.front();
            const auto& offer = release.offer;
            CHECK(offer.build == 30000001);
            CHECK(offer.version == "3.0.0-pre.1");
            CHECK(offer.bundleId == "com.github.eiskaltdcpp");
            CHECK(offer.notes == manifest.value("notes").toString());
            CHECK(offer.cpu == (flavor == "intel" ? "x86_64" : "arm64"));
            CHECK(offer.minimumOs == QVersionNumber(flavor == "intel" ? 14 : 26, 0));
            CHECK(offer.channel == (channel == "preview" ? Channel::Preview : Channel::Stable));
            UpdateIdentity installed;
            installed.build = 1;
            installed.bundleId = offer.bundleId;
            installed.cpu = offer.cpu;
            installed.os = offer.minimumOs;
            REQUIRE(selectUpdate(*verified, installed, Channel::Preview));
            CHECK(bool(selectUpdate(*verified, installed, Channel::Stable)) == (channel == "stable"));
            installed.build = offer.build;
            CHECK_FALSE(selectUpdate(*verified, installed, Channel::Preview));
            installed.build = 1;
            installed.cpu = "other";
            CHECK_FALSE(selectUpdate(*verified, installed, Channel::Preview));
            installed.cpu = offer.cpu;
            installed.os = QVersionNumber(13, 0);
            CHECK_FALSE(selectUpdate(*verified, installed, Channel::Preview));
            installed.os = offer.minimumOs;
            installed.bundleId = "com.example.other";
            CHECK_FALSE(selectUpdate(*verified, installed, Channel::Preview));
            const auto archivePath = directory.filePath(release.archiveUrl.fileName());
            REQUIRE(verifyUpdateArchive(archivePath,
                {offer.size, offer.sha256, release.archiveSignatureBase64}, publicKey));
            auto corrupted = bytes;
            corrupted[0] = 'X';
            CHECK_FALSE(verifyUpdateFeed(corrupted, publicKey));
        }
    }
}
