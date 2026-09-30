#include "dcpp/stdinc.h"
#include "updates/UpdateFeedVerifier.h"
#include "updates/UpdateLoopbackBridge.h"
#include "dcpp/ProxyRoute.h"
#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QThread>
#include <openssl/evp.h>
#include <memory>

using namespace updates;
namespace {
const QByteArray sparkle = "http://www.andymatuschak.org/xml-namespaces/sparkle";
const QByteArray eiskalt = "https://eiskaltdcpp.org/xml-namespaces/updates/1";
struct FeedFixture {
    QByteArray seed = QByteArray(32, '\x36'); // Disposable test identity, never a release key.
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(seed.constData()), seed.size()), EVP_PKEY_free};
    QByteArray publicKey;
    FeedFixture() {
        REQUIRE(key);
        QByteArray raw(32, '\0'); size_t size = 32;
        REQUIRE(EVP_PKEY_get_raw_public_key(key.get(), reinterpret_cast<unsigned char*>(raw.data()), &size) == 1);
        publicKey = raw.toBase64();
    }
    QByteArray sign(const QByteArray& bytes) const {
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        REQUIRE(ctx);
        REQUIRE(EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1);
        QByteArray signature(64, '\0'); size_t size = 64;
        REQUIRE(EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &size,
            reinterpret_cast<const unsigned char*>(bytes.constData()), bytes.size()) == 1);
        return bytes + "<!-- sparkle-signatures:\nedSignature: " + signature.toBase64() +
            "\nlength: " + QByteArray::number(bytes.size()) + "\n-->\n";
    }
    QByteArray item(int build = 2, QByteArray cpu = "arm64", QByteArray channel = "preview") const {
        return "<item><title>Release</title><sparkle:version>" + QByteArray::number(build) +
            "</sparkle:version><sparkle:shortVersionString>3.0.0-pre.2</sparkle:shortVersionString>"
            "<sparkle:minimumSystemVersion>26.0</sparkle:minimumSystemVersion>"
            + (channel == "stable" ? QByteArray{} : "<sparkle:channel>" + channel + "</sparkle:channel>") +
            "<eiskalt:bundleIdentifier>org.eiskaltdcpp.EiskaltDCpp</eiskalt:bundleIdentifier>"
            "<eiskalt:cpu>" + cpu + "</eiskalt:cpu><description>Plain &amp; safe notes</description>"
            "<enclosure url=\"https://github.com/Delitants/eiskaltdcpp/releases/download/v3/update-" + cpu + ".zip\""
            " length=\"123\" type=\"application/octet-stream\" sparkle:os=\"macos\" sparkle:edSignature=\"" +
            QByteArray(64, 's').toBase64() + "\" eiskalt:sha256=\"" + QByteArray(64, 'a') + "\"/></item>";
    }
    QByteArray xml(const QByteArray& items) const {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?><rss version=\"2.0\" xmlns:sparkle=\"" +
            sparkle + "\" xmlns:eiskalt=\"" + eiskalt + "\"><channel><title>Updates</title>" + items + "</channel></rss>";
    }
    std::optional<VerifiedUpdateFeed> verify(const QByteArray& items) const {
        return verifyUpdateFeed(sign(xml(items)), publicKey);
    }
};
UpdateIdentity identity() { return {1, Channel::Preview, "org.eiskaltdcpp.EiskaltDCpp", "arm64", QVersionNumber(26, 0)}; }
}

TEST_CASE("Signed update feed binds every offer to authenticated full archive metadata", "[updates][update-feed]") {
    FeedFixture f;
    const auto bytes = f.sign(f.xml(f.item()));
    const auto feed = verifyUpdateFeed(bytes, f.publicKey);
    REQUIRE(feed);
    CHECK(feed->signedBytes == bytes);
    REQUIRE(feed->releases.size() == 1);
    const auto& r = feed->releases.front();
    CHECK(r.offer.build == 2);
    CHECK(r.offer.version == "3.0.0-pre.2");
    CHECK(r.offer.notes == "Plain & safe notes");
    CHECK(r.offer.bundleId == identity().bundleId);
    CHECK(r.offer.cpu == "arm64");
    CHECK(r.offer.channel == Channel::Preview);
    CHECK(r.offer.minimumOs == QVersionNumber(26, 0));
    CHECK(r.offer.size == 123);
    CHECK(r.offer.sha256 == QByteArray::fromHex(QByteArray(64, 'a')));
    CHECK(r.archiveSignatureBase64 == QByteArray(64, 's').toBase64());
    CHECK(r.archiveUrl.path().endsWith("update-arm64.zip"));
    const auto empty = f.verify({});
    REQUIRE(empty);
    CHECK(empty->releases.isEmpty());
}

TEST_CASE("Feed signature failure never produces parsed metadata", "[updates][update-feed]") {
    FeedFixture f;
    auto bytes = f.sign(f.xml(f.item()));
    REQUIRE(verifyUpdateFeed(bytes, f.publicKey));
    SECTION("unsigned") { bytes = f.xml(f.item()); }
    SECTION("modified signed text") { bytes.replace("Plain", "Wrong"); }
    SECTION("wrong key") { f.publicKey = QByteArray(32, 'a').toBase64(); }
    SECTION("noncanonical key") { f.publicKey.append('\n'); }
    SECTION("signature length") { bytes.replace("edSignature: ", "edSignature: A"); }
    SECTION("wrong declared payload length") { bytes.replace("\nlength: ", "\nlength: 1"); }
    SECTION("leading zero length") { bytes.replace("\nlength: ", "\nlength: 0"); }
    SECTION("unsigned trailing XML") { bytes += "<item/>"; }
    SECTION("unsigned trailing comment") { bytes += "<!-- appended -->"; }
    SECTION("duplicate signature") { bytes += bytes.mid(bytes.indexOf("<!-- sparkle-signatures:")); }
    SECTION("unknown trailer field") { bytes.replace("\nlength:", "\nunknown: true\nlength:"); }
    SECTION("oversize feed") { bytes = QByteArray(1024 * 1024 + 1, 'a'); }
    CHECK_FALSE(verifyUpdateFeed(bytes, f.publicKey));
}

TEST_CASE("Signed but malformed feeds and ambiguous release identities fail closed", "[updates][update-feed]") {
    FeedFixture f;
    auto item = f.item();
    REQUIRE(f.verify(item));
    SECTION("duplicate build field") { item.replace("</item>", "<sparkle:version>3</sparkle:version></item>"); }
    SECTION("duplicate release identity") { item += item; }
    SECTION("conflicting release identity") { item += f.item().replace("length=\"123\"", "length=\"124\""); }
    SECTION("wrong version namespace") { item.replace("sparkle:version", "eiskalt:version"); }
    SECTION("empty build") { item.replace(">2</sparkle:version>", "></sparkle:version>"); }
    SECTION("display build instead of numeric") { item.replace(">2</sparkle:version>", ">3.0.0-pre.2</sparkle:version>"); }
    SECTION("noncanonical build") { item.replace(">2</sparkle:version>", ">02</sparkle:version>"); }
    SECTION("overflow build") { item.replace(">2</sparkle:version>", ">18446744073709551616</sparkle:version>"); }
    SECTION("zero build") { item.replace(">2</sparkle:version>", ">0</sparkle:version>"); }
    SECTION("partial minimum OS") { item.replace(">26.0<", ">26.0junk<"); }
    SECTION("missing CPU") { item.replace("<eiskalt:cpu>arm64</eiskalt:cpu>", ""); }
    SECTION("universal CPU") { item.replace(">arm64<", ">universal<"); }
    SECTION("invalid bundle id") { item.replace(">org.eiskaltdcpp.EiskaltDCpp<", ">../app<"); }
    SECTION("unknown channel") { item.replace(">preview<", ">nightly<"); }
    SECTION("oversize version") { item.replace("3.0.0-pre.2", QByteArray(129, 'v')); }
    SECTION("oversize notes") { item.replace("Plain &amp; safe notes", QByteArray(16385, 'a')); }
    SECTION("nested notes element") { item.replace("Plain &amp; safe notes", "<img src=\"https://example.invalid/\"/>"); }
    SECTION("external release notes") { item.replace("</item>", "<sparkle:releaseNotesLink>https://example.invalid/</sparkle:releaseNotesLink></item>"); }
    SECTION("too many items") { item.clear(); for(int i = 1; i <= 129; ++i) item += f.item(i); }
    CHECK_FALSE(f.verify(item));
}

TEST_CASE("Signed archive references cannot enable deltas packages or foreign URLs", "[updates][update-feed]") {
    FeedFixture f;
    auto item = f.item();
    REQUIRE(f.verify(item));
    SECTION("wrong origin") { item.replace("https://github.com/", "https://example.invalid/"); }
    SECTION("HTTP origin") { item.replace("https://github.com/", "http://github.com/"); }
    SECTION("asset CDN is redirect only") { item.replace("github.com", "objects.githubusercontent.com"); }
    SECTION("query") { item.replace("update-arm64.zip", "update-arm64.zip?q=1"); }
    SECTION("non ZIP") { item.replace("update-arm64.zip", "update-arm64.pkg"); }
    SECTION("delta") { item.replace("<enclosure ", "<enclosure sparkle:deltaFrom=\"1\" "); }
    SECTION("package") { item.replace("<enclosure ", "<enclosure sparkle:installationType=\"package\" "); }
    SECTION("wrong OS") { item.replace("sparkle:os=\"macos\"", "sparkle:os=\"windows\""); }
    SECTION("missing signature") { item.replace("sparkle:edSignature", "signature"); }
    SECTION("invalid signature") { item.replace(QByteArray(64, 's').toBase64(), "not-base64"); }
    SECTION("missing digest") { item.replace("eiskalt:sha256", "sha256"); }
    SECTION("invalid digest") { item.replace(QByteArray(64, 'a'), QByteArray(64, 'g')); }
    SECTION("length zero") { item.replace("length=\"123\"", "length=\"0\""); }
    SECTION("length overflow") { item.replace("length=\"123\"", "length=\"536870913\""); }
    SECTION("duplicate enclosure") { item.replace("</item>", item.mid(item.indexOf("<enclosure"))); }
    SECTION("duplicate attribute") { item.replace("length=\"123\"", "length=\"123\" length=\"124\""); }
    SECTION("duplicate namespace aliased signature") { item.replace("<enclosure ", "<enclosure xmlns:s=\"" + sparkle + "\" s:edSignature=\"other\" "); }
    SECTION("namespace attribute spoof") { item.replace("length=\"123\"", "eiskalt:length=\"123\""); }
    CHECK_FALSE(f.verify(item));
}

TEST_CASE("Feed parser rejects entities DTD and extra XML even when signed", "[updates][update-feed]") {
    FeedFixture f;
    auto xml = f.xml(f.item());
    REQUIRE(verifyUpdateFeed(f.sign(xml), f.publicKey));
    SECTION("internal entity") { xml.replace("<rss ", "<!DOCTYPE rss [<!ENTITY e 'abc'>]><rss "); }
    SECTION("external entity") { xml.replace("<rss ", "<!DOCTYPE rss SYSTEM 'file:///etc/passwd'><rss "); }
    SECTION("extra document") { xml += "<rss/>"; }
    SECTION("processing instruction") { xml.replace("<rss ", "<?xml-stylesheet href='https://example.invalid/'?><rss "); }
    SECTION("unknown channel child") { xml.replace("<channel>", "<channel><unexpected/>"); }
    SECTION("nested item") { xml.replace("<item>", "<item><item/>"); }
    SECTION("mismatched close") { xml.replace("</rss>", "</oops>"); }
    SECTION("unknown root attribute") { xml.replace("<rss ", "<rss xml:base=\"https://example.invalid/\" "); }
    CHECK_FALSE(verifyUpdateFeed(f.sign(xml), f.publicKey));
}

TEST_CASE("Feed selection uses numeric build CPU channel and exact OS compatibility", "[updates][update-feed]") {
    FeedFixture f;
    const auto feed = f.verify(f.item(2) + f.item(3, "arm64", "stable") + f.item(4) +
        f.item(5, "x86_64") + f.item(6).replace(">26.0<", ">27.0<"));
    REQUIRE(feed);
    auto selected = selectUpdate(*feed, identity(), Channel::Preview);
    REQUIRE(selected); CHECK(selected->offer.build == 4);
    selected = selectUpdate(*feed, identity(), Channel::Stable);
    REQUIRE(selected); CHECK(selected->offer.build == 3);
    auto installed = identity(); installed.build = 4;
    CHECK_FALSE(selectUpdate(*feed, installed, Channel::Preview));
    installed = identity(); installed.bundleId = "org.other";
    CHECK_FALSE(selectUpdate(*feed, installed, Channel::Preview));
    installed = identity(); installed.cpu = "x86_64";
    selected = selectUpdate(*feed, installed, Channel::Preview);
    REQUIRE(selected); CHECK(selected->offer.build == 5);
}

TEST_CASE("Feed verification is bounded and cancelable and preserves literal notes", "[updates][update-feed]") {
    FeedFixture f;
    auto xml = f.xml(f.item().replace("Plain &amp; safe notes", "<![CDATA[<img src='https://example.invalid/'>]]>"));
    auto feed = verifyUpdateFeed(f.sign(xml), f.publicKey);
    REQUIRE(feed);
    CHECK(feed->releases.front().offer.notes == "<img src='https://example.invalid/'>");
    CHECK_FALSE(verifyUpdateFeed(f.sign(xml), f.publicKey, [] { return true; }));
    int calls = 0;
    CHECK_FALSE(verifyUpdateFeed(f.sign(xml), f.publicKey, [&] { return ++calls > 2; }));
    CHECK(calls > 2);
}

TEST_CASE("Stable feed channel is absent as required by Sparkle", "[updates][update-feed]") {
    FeedFixture f;
    const auto feed = f.verify(f.item().replace("<sparkle:channel>preview</sparkle:channel>", ""));
    REQUIRE(feed);
    CHECK(feed->releases.front().offer.channel == Channel::Stable);
    CHECK(selectUpdate(*feed, identity(), Channel::Stable).has_value());
    CHECK_FALSE(f.verify(f.item().replace(">preview<", ">stable<")));
}

TEST_CASE("Authenticated feed accepts the exact byte bound and namespace aliases", "[updates][update-feed]") {
    FeedFixture f;
    auto xml = f.xml(f.item());
    xml.replace("xmlns:sparkle=", "xmlns:s=").replace("sparkle:", "s:");
    xml.replace("xmlns:eiskalt=", "xmlns:e=").replace("eiskalt:", "e:");
    REQUIRE(verifyUpdateFeed(f.sign(xml), f.publicKey));
    xml.replace("</rss>", "<!--padding--></rss>");
    auto bytes = f.sign(xml);
    // The length's digit count can grow after padding, so settle it exactly.
    int padding = 1024 * 1024 - bytes.size();
    for(int i = 0; i < 3; ++i) {
        auto padded = xml; padded.replace("padding", QByteArray(padding + 7, 'p'));
        bytes = f.sign(padded);
        padding += 1024 * 1024 - bytes.size();
    }
    REQUIRE(bytes.size() == 1024 * 1024);
    CHECK(verifyUpdateFeed(bytes, f.publicKey).has_value());
    bytes += '\n';
    CHECK_FALSE(verifyUpdateFeed(bytes, f.publicKey));
}

TEST_CASE("Feed accepts maximum item and archive bounds without selecting a foreign CPU", "[updates][update-feed]") {
    FeedFixture f;
    QByteArray items;
    for(int i = 1; i <= 128; ++i) items += f.item(i);
    items.replace("length=\"123\"", "length=\"536870912\"");
    const auto feed = f.verify(items); REQUIRE(feed);
    REQUIRE(feed->releases.size() == 128);
    const auto selected = selectUpdate(*feed, identity(), Channel::Preview); REQUIRE(selected);
    CHECK(selected->offer.build == 128); CHECK(selected->offer.size == 536870912);
    const auto pair = f.verify(f.item(2) + f.item(2, "x86_64")); REQUIRE(pair);
    REQUIRE(pair->releases.size() == 2);
    const auto arm = selectUpdate(*pair, identity(), Channel::Preview); REQUIRE(arm);
    CHECK(arm->offer.cpu == "arm64");
}

TEST_CASE("Feed notes byte limit also applies to multibyte UTF8 text", "[updates][update-feed]") {
    FeedFixture f;
    const auto atLimit = QString(8192, QChar(0x00e9)).toUtf8();
    REQUIRE(atLimit.size() == 16384);
    REQUIRE(f.verify(f.item().replace("Plain &amp; safe notes", atLimit)));
    CHECK_FALSE(f.verify(f.item().replace("Plain &amp; safe notes", atLimit + "x")));
}

#ifdef __APPLE__
TEST_CASE("Pinned Sparkle signer and Eiskalt feed verifier interoperate", "[.][updates][update-feed-native]") {
    FeedFixture f;
    const auto signer = qEnvironmentVariable("EISKALT_SPARKLE_SIGNER");
    REQUIRE_FALSE(signer.isEmpty());
    QTemporaryDir root; REQUIRE(root.isValid());
    auto write = [&](const QString& path, const QByteArray& bytes) {
        QFile file(path); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        REQUIRE(file.write(bytes) == bytes.size());
    };
    const auto path = root.filePath("appcast.xml");
    write(path, f.xml(f.item()));
    write(root.filePath("fixture-seed.b64"), f.seed.toBase64());
    QProcess process;
    process.start(signer, {"--ed-key-file", root.filePath("fixture-seed.b64"), path});
    REQUIRE(process.waitForFinished(30000));
    REQUIRE(process.exitStatus() == QProcess::NormalExit); REQUIRE(process.exitCode() == 0);
    QFile input(path); REQUIRE(input.open(QIODevice::ReadOnly));
    auto bytes = input.readAll();
    REQUIRE(verifyUpdateFeed(bytes, f.publicKey));
    bytes.replace("Plain", "Wrong");
    CHECK_FALSE(verifyUpdateFeed(bytes, f.publicKey));
}

TEST_CASE("Real Sparkle discovers the same authenticated Eiskalt release without preparing it", "[.][updates][update-feed-bridge-native]") {
    FeedFixture f;
    Channel channel = Channel::Stable;
    SECTION("stable channel does not select preview") {}
    SECTION("preview channel includes newer prereleases") { channel = Channel::Preview; }
    const auto fixture = qEnvironmentVariable("EISKALT_UPDATER_SIGNATURE_FIXTURE");
    const auto binary = qEnvironmentVariable("EISKALT_UPDATER_PROBE_BINARY");
    const auto signer = qEnvironmentVariable("EISKALT_SPARKLE_SIGNER");
    REQUIRE_FALSE(fixture.isEmpty()); REQUIRE_FALSE(binary.isEmpty()); REQUIRE_FALSE(signer.isEmpty());
    QTemporaryDir root; REQUIRE(root.isValid());
    const auto run = [](const QString& command, const QStringList& args) {
        QProcess process; process.start(command, args);
        REQUIRE(process.waitForFinished(30000));
        REQUIRE(process.exitStatus() == QProcess::NormalExit); REQUIRE(process.exitCode() == 0);
        return process.readAllStandardOutput();
    };
    const auto write = [](const QString& path, const QByteArray& bytes) {
        QFile file(path); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        REQUIRE(file.write(bytes) == bytes.size());
    };
    const auto digest = [](const QString& path) {
        QFile file(path); REQUIRE(file.open(QIODevice::ReadOnly));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        REQUIRE(hash.addData(&file)); return hash.result();
    };
    const auto app = root.filePath("UpdaterProbe.app");
    run("/usr/bin/ditto", {fixture + "/old/UpdaterProbe.app", app});
    const auto executable = app + "/Contents/MacOS/UpdaterProbe";
    const auto plist = app + "/Contents/Info.plist";
    REQUIRE(QFile::remove(executable)); REQUIRE(QFile::copy(binary, executable));
    run("/usr/libexec/PlistBuddy", {"-c", "Set :SUPublicEDKey " + QString::fromLatin1(f.publicKey), plist});
    const auto bundle = run("/usr/libexec/PlistBuddy", {"-c", "Print :CFBundleIdentifier", plist}).trimmed();
    run("/usr/bin/codesign", {"--force", "--sign", "-", app});
    const auto beforeBinary = digest(executable), beforePlist = digest(plist);
    auto items = f.item(2, "arm64", "stable") + f.item(3, "arm64", "stable") + f.item(4);
    items.replace("org.eiskaltdcpp.EiskaltDCpp", bundle);
    const auto feedPath = root.filePath("appcast.xml");
    write(feedPath, f.xml(items)); write(root.filePath("fixture-seed.b64"), f.seed.toBase64());
    run(signer, {"--ed-key-file", root.filePath("fixture-seed.b64"), feedPath});
    QFile input(feedPath); REQUIRE(input.open(QIODevice::ReadOnly));
    const auto feed = verifyUpdateFeed(input.readAll(), f.publicKey); REQUIRE(feed);
    auto installed = identity(); installed.bundleId = QString::fromLatin1(bundle);
    const auto selected = selectUpdate(*feed, installed, channel); REQUIRE(selected);
    REQUIRE(selected->offer.build == (channel == Channel::Stable ? 3 : 4));
    auto route = std::make_shared<dcpp::ProxyRouteSnapshot>();
    updates::UpdateLoopbackBridge bridge;
    const auto url = bridge.beginSession(feedPath, route); REQUIRE(url.isValid());
    auto environment = QProcessEnvironment::systemEnvironment();
    const auto home = root.filePath("home"); REQUIRE(QDir().mkpath(home));
    environment.insert("HOME", home); environment.insert("CFFIXED_USER_HOME", home);
    environment.insert("XDG_CONFIG_HOME", home + "/config");
    environment.insert("EISKALT_PROBE_FEED", url.toString());
    environment.insert("EISKALT_PROBE_MODE", "information-only");
    environment.insert("EISKALT_PROBE_PREVIEW", channel == Channel::Preview ? "1" : "0");
    environment.insert("EISKALT_PROBE_EXPECTED_BUILD", QString::number(selected->offer.build));
    environment.insert("EISKALT_PROBE_EXPECTED_ARCHIVE", selected->archiveUrl.toString());
    QProcess probe; probe.setProcessEnvironment(environment);
    const QString sandbox = "(version 1)(allow default)(deny network*)"
        "(allow network-outbound (remote ip \"localhost:*\"))"
        "(allow network-bind (local ip \"localhost:*\"))"
        "(allow network-inbound (local ip \"localhost:*\"))";
    probe.start("/usr/bin/sandbox-exec", {"-p", sandbox, executable, "[bridge-probe]"});
    REQUIRE(probe.waitForStarted(5000));
    QElapsedTimer timer; timer.start();
    while(probe.state() != QProcess::NotRunning && timer.elapsed() < 40000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2);
    }
    const auto output = probe.readAllStandardOutput() + probe.readAllStandardError();
    INFO(output.toStdString());
    REQUIRE(probe.state() == QProcess::NotRunning);
    REQUIRE(probe.exitStatus() == QProcess::NormalExit); REQUIRE(probe.exitCode() == 0);
    CHECK(output.contains("All tests passed"));
    CHECK(output.contains("downloaded=0 extracting=0 ready=0 rewrites=0"));
    CHECK(digest(executable) == beforeBinary); CHECK(digest(plist) == beforePlist);
}
#endif
