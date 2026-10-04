#include "dcpp/stdinc.h"
#include "updates/UpdateFeedVerifier.h"
#include "updates/UpdateLoopbackBridge.h"
#include "dcpp/ProxyRoute.h"
#include "dcpp/ScopedFunctor.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QThread>
#include <openssl/evp.h>
#include <memory>
#include <future>
#include <mutex>
#ifdef EISKALT_TEST_MAC_UPDATE_DISCOVERY
#include "updates/MacUpdateDiscovery.h"
#include "updates/MacUpdateBackend.h"
#include "updates/UpdateRuntimeIdentity.h"
#include "updates/UpdateUrlPolicy.h"
#include <QOperatingSystemVersion>
#include "updates/UpdateController.h"
#include "WulforSettings.h"
#include "tests/TestContext.h"
#include "dcpp/HttpRequestPolicy.h"
#include <QSysInfo>
#include <QGuiApplication>
#include <QTimer>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace updates {
struct MacUpdateDiscoveryTestAccess {
    static std::unique_ptr<MacUpdateDiscovery> make(const QString& bundle) {
        return std::unique_ptr<MacUpdateDiscovery>(new MacUpdateDiscovery(bundle, nullptr));
    }
};
struct MacUpdateBackendTestAccess {
    static std::unique_ptr<MacUpdateBackend> make(dcpp::DCContext& context, const QString& bundle, transport_detail::Backend transport,
        const QString& cache = {}, std::optional<UpdateRuntimeConfig> config = {}) {
        if(config) return std::unique_ptr<MacUpdateBackend>(new MacUpdateBackend(context, bundle, std::move(transport), cache, std::move(*config), nullptr));
        return std::unique_ptr<MacUpdateBackend>(new MacUpdateBackend(context, bundle, std::move(transport), cache, nullptr));
    }
};
}
#endif

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

#ifdef EISKALT_TEST_MAC_UPDATE_DISCOVERY
namespace {
bool discoveryPump(const std::function<bool()>& done, int timeout = 10000) {
    QElapsedTimer timer; timer.start();
    while(!done() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2);
    }
    return done();
}
struct DiscoveryFixture {
    FeedFixture signer;
    QTemporaryDir root;
    QString app = root.filePath("UpdaterProbe.app");
    QByteArray bundle;
    std::shared_ptr<dcpp::ProxyRouteSnapshot> route = std::make_shared<dcpp::ProxyRouteSnapshot>();
    std::unique_ptr<MacUpdateDiscovery> adapter;
    int found = 0, absent = 0, errors = 0;
    quint64 operation = 0;
    SignedUpdate result;
    QByteArray run(const QString& command, const QStringList& args) {
        QProcess p; p.start(command, args); REQUIRE(p.waitForFinished(30000));
        REQUIRE(p.exitStatus() == QProcess::NormalExit); REQUIRE(p.exitCode() == 0);
        return p.readAllStandardOutput();
    }
    void setting(const QString& command) { run("/usr/libexec/PlistBuddy", {"-c", command, app + "/Contents/Info.plist"}); }
    DiscoveryFixture() {
        REQUIRE(root.isValid());
        const auto fixture = qEnvironmentVariable("EISKALT_UPDATER_SIGNATURE_FIXTURE");
        if(fixture.isEmpty()) SKIP("Opt-in: set EISKALT_UPDATER_SIGNATURE_FIXTURE and use the documented native discovery sandbox");
        REQUIRE_FALSE(fixture.isEmpty());
        // Run this opt-in group with a disposable home and network-denied process.
        REQUIRE(qEnvironmentVariable("CFFIXED_USER_HOME") == qEnvironmentVariable("HOME"));
        REQUIRE(qEnvironmentVariable("HOME").startsWith("/private/tmp/"));
        REQUIRE(QGuiApplication::platformName() == "cocoa");
        const int fd = socket(AF_INET, SOCK_STREAM, 0); REQUIRE(fd >= 0);
        fcntl(fd, F_SETFL, O_NONBLOCK);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(443);
        inet_pton(AF_INET, "203.0.113.1", &address.sin_addr);
        const int connected = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        const int error = errno; close(fd);
        REQUIRE(connected == -1); REQUIRE(error == EPERM);
        run("/usr/bin/ditto", {fixture + "/old/UpdaterProbe.app", app});
        setting("Set :SUPublicEDKey " + QString::fromLatin1(signer.publicKey));
        bundle = run("/usr/libexec/PlistBuddy", {"-c", "Print :CFBundleIdentifier", app + "/Contents/Info.plist"}).trimmed();
    }
    QByteArray feed() {
        const auto cpu = QSysInfo::buildCpuArchitecture().toLatin1();
        const auto foreign = cpu == "arm64" ? QByteArray("x86_64") : QByteArray("arm64");
        auto items = signer.item(2, cpu, "stable") + signer.item(3, cpu) + signer.item(4, foreign);
        items.replace("org.eiskaltdcpp.EiskaltDCpp", bundle);
        items.replace(">26.0<", ">14.0<");
        return signer.sign(signer.xml(items));
    }
    void create() {
        adapter = MacUpdateDiscoveryTestAccess::make(app);
        QObject::connect(adapter.get(), &MacUpdateDiscovery::available, [&](quint64 op, SignedUpdate r) { ++found; operation = op; result = r; });
        QObject::connect(adapter.get(), &MacUpdateDiscovery::noUpdate, [&](quint64 op) { ++absent; operation = op; });
        QObject::connect(adapter.get(), &MacUpdateDiscovery::failed, [&](quint64 op) { ++errors; operation = op; });
    }
    bool done() const { return found + absent + errors != 0; }
};
}

TEST_CASE("Native discovery reports only Sparkle-verified matching CPU and channel offers", "[.][update-discovery-native]") {
    DiscoveryFixture f;
    Channel channel = Channel::Stable;
    SECTION("stable") {}
    SECTION("preview") { channel = Channel::Preview; }
    f.create();
    REQUIRE(f.adapter->check(71, f.feed(), channel, f.route));
    CHECK(f.adapter->active()); CHECK_FALSE(f.done());
    CHECK_FALSE(f.adapter->check(72, f.feed(), channel, f.route));
    REQUIRE(discoveryPump([&] { return f.done(); }));
    CHECK(f.found == 1); CHECK(f.errors == 0); CHECK(f.absent == 0); CHECK(f.operation == 71);
    CHECK(f.result.offer.build == (channel == Channel::Stable ? 2 : 3));
    CHECK(f.result.offer.cpu == QSysInfo::buildCpuArchitecture());
    CHECK_FALSE(f.adapter->active());
}

TEST_CASE("Native discovery rejects unsigned metadata and unsafe bundle policies", "[.][update-discovery-native]") {
    DiscoveryFixture f;
    auto bytes = f.feed();
    SECTION("tampered signed feed") { bytes.replace("Plain", "Wrong"); }
    SECTION("wrong pinned key") { f.setting("Set :SUPublicEDKey " + QString::fromLatin1(QByteArray(32, 'z').toBase64())); }
    SECTION("unsigned feeds allowed") { f.setting("Set :SURequireSignedFeed false"); }
    SECTION("expiration fallback") { f.setting("Set :SUSignedFeedFailureExpirationInterval 1"); }
    SECTION("external downloader") { f.setting("Set :SUEnableDownloaderService true"); }
    SECTION("automatic checks enabled") { f.setting("Set :SUEnableAutomaticChecks true"); }
    SECTION("automatic download enabled") { f.setting("Set :SUAutomaticallyUpdate true"); }
    SECTION("profiling enabled") { f.setting("Set :SUEnableSystemProfiling true"); }
    SECTION("verification after extraction") { f.setting("Set :SUVerifyUpdateBeforeExtraction false"); }
    SECTION("missing signature policy") { f.setting("Delete :SURequireSignedFeed"); }
    SECTION("invalid installed build") { f.setting("Set :CFBundleVersion 3.0.0-pre.1"); }
    SECTION("revoked route") { f.route->revoked->store(true); }
    SECTION("invalid route") { f.route->valid = false; }
    SECTION("absent route") { f.route.reset(); }
    SECTION("oversized input") { bytes = QByteArray(1024 * 1024 + 1, 'a'); }
    f.create();
    REQUIRE(f.adapter->check(81, bytes, Channel::Preview, f.route));
    REQUIRE(discoveryPump([&] { return f.done(); }));
    CHECK(f.found == 0); CHECK(f.absent == 0); CHECK(f.errors == 1); CHECK(f.operation == 81);
}

TEST_CASE("Native discovery no-update requires a completed signed information cycle", "[.][update-discovery-native]") {
    DiscoveryFixture f; f.setting("Set :CFBundleVersion 10"); f.create();
    REQUIRE(f.adapter->check(91, f.feed(), Channel::Preview, f.route));
    REQUIRE(discoveryPump([&] { return f.done(); }));
    CHECK(f.absent == 1); CHECK(f.errors == 0); CHECK(f.found == 0);
}

TEST_CASE("Native discovery cancellation invalidates callbacks and permits a fresh operation", "[.][update-discovery-native]") {
    DiscoveryFixture f; f.create();
    REQUIRE(f.adapter->check(101, f.feed(), Channel::Preview, f.route));
    f.adapter->cancel(); CHECK_FALSE(f.adapter->active());
    REQUIRE(f.adapter->check(102, f.feed(), Channel::Stable, f.route));
    REQUIRE(discoveryPump([&] { return f.done(); }));
    CHECK(f.found == 1); CHECK(f.operation == 102); CHECK(f.result.offer.build == 2); CHECK(f.errors == 0);
    f.adapter.reset();
    QElapsedTimer timer; timer.start(); REQUIRE(discoveryPump([&] { return timer.elapsed() > 100; }));
    CHECK(f.found == 1);
}

TEST_CASE("Native discovery cancels after Sparkle starts without accepting stale callbacks", "[.][update-discovery-native]") {
    DiscoveryFixture f; f.create();
    REQUIRE(f.adapter->check(111, f.feed(), Channel::Preview, f.route));
    bool cancelled = false;
    // start() is queued before this callback; the feed request cannot finish in it.
    QTimer::singleShot(0, f.adapter.get(), [&] {
        CHECK_FALSE(f.done()); f.adapter->cancel(); cancelled = true;
        CHECK_FALSE(f.adapter->active());
        // Reusing an external ID must still reject callbacks from its old generation.
        REQUIRE(f.adapter->check(111, f.feed(), Channel::Stable, f.route));
    });
    REQUIRE(discoveryPump([&] { return f.done(); }));
    CHECK(cancelled); CHECK(f.found == 1); CHECK(f.operation == 111);
    CHECK(f.result.offer.build == 2); CHECK(f.errors == 0); CHECK(f.absent == 0);
}

TEST_CASE("Native discovery revocation and owner destruction suppress offers", "[.][update-discovery-native]") {
    DiscoveryFixture f; f.create();
    bool destroy = false;
    SECTION("route changes during native check") {}
    SECTION("owner destroyed during native check") { destroy = true; }
    REQUIRE(f.adapter->check(121, f.feed(), Channel::Preview, f.route));
    bool interrupted = false;
    QTimer::singleShot(0, QCoreApplication::instance(), [&] {
        CHECK_FALSE(f.done());
        if(destroy) f.adapter.reset();
        else f.route->revoked->store(true);
        interrupted = true;
    });
    REQUIRE(discoveryPump([&] { return interrupted; }));
    if(!destroy) {
        REQUIRE(discoveryPump([&] { return f.done(); }));
        CHECK(f.errors == 1); CHECK(f.operation == 121); CHECK_FALSE(f.adapter->active());
    }
    QElapsedTimer timer; timer.start(); REQUIRE(discoveryPump([&] { return timer.elapsed() >= 150; }));
    CHECK(f.found == 0); CHECK(f.absent == 0); CHECK(f.errors == (destroy ? 0 : 1));
}

namespace {
struct BackendFixture {
    DiscoveryFixture fixture;
    dcpp::test::TestContext context;
    QByteArray archive = QByteArray(8192, 'A');
    QByteArray feed;
    std::atomic_int requests{0};
    std::mutex requestMutex;
    std::string requestedFeed;
    std::atomic_bool holdArchive{false}, archiveEntered{false}, workerCancelled{false};
    std::atomic_bool holdFeed{false}, feedEntered{false}, workerExited{false};
    bool corrupt = false;
    std::unique_ptr<MacUpdateBackend> backend;
    int offers = 0, ready = 0, failures = 0, absent = 0;
    quint64 operation = 0;
    UpdateOffer offer;
    FailureCategory category{};
    BackendFixture(const QString& cache = {}) {
        auto item = fixture.signer.item(2, QSysInfo::buildCpuArchitecture().toLatin1(), "stable");
        const auto signedArchive = fixture.signer.sign(archive);
        const auto signature = signedArchive.mid(archive.size()).split('\n')[1].mid(13);
        item.replace("org.eiskaltdcpp.EiskaltDCpp", fixture.bundle);
        item.replace(">26.0<", ">14.0<");
        item.replace("length=\"123\"", "length=\"" + QByteArray::number(archive.size()) + "\"");
        item.replace(QByteArray(64, 's').toBase64(), signature);
        item.replace(QByteArray(64, 'a'), QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex());
        feed = fixture.signer.sign(fixture.signer.xml(item));
        createBackend(cache);
    }
    void createBackend(const QString& cache, std::optional<UpdateRuntimeConfig> config = {}) {
        backend.reset();
        offers = ready = failures = absent = 0; requests = 0;
        backend = MacUpdateBackendTestAccess::make(*context.ownedCtx, fixture.app,
            [this](const auto& policy, const std::string& url, const auto& sink) {
                ++requests;
                const bool isFeed = url.find("raw.githubusercontent.com") != std::string::npos;
                if(isFeed) {
                    std::lock_guard<std::mutex> lock(requestMutex);
                    requestedFeed = url;
                }
                auto markExit = dcpp::makeScopedFunctor([&] { workerExited = true; });
                if(isFeed) {
                    feedEntered = true;
                    for(int i = 0; holdFeed && i < 3000 && !policy.cancelled(); ++i) QThread::msleep(1);
                }
                if(url.find("raw.githubusercontent.com") == std::string::npos) {
                    archiveEntered = true;
                    for(int i = 0; holdArchive && i < 3000 && !policy.cancelled(); ++i) QThread::msleep(1);
                }
                if(policy.cancelled()) workerCancelled = true;
                if(policy.cancelled()) throw std::runtime_error("cancelled");
                const auto bytes = url.find("raw.githubusercontent.com") != std::string::npos ? feed :
                    (corrupt ? QByteArray(archive.size(), 'B') : archive);
                sink(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size());
            }, cache, std::move(config));
        QObject::connect(backend.get(), &UpdateBackend::available, [&](quint64 op, UpdateOffer value) { ++offers; operation = op; offer = value; });
        QObject::connect(backend.get(), &UpdateBackend::downloadReady, [&](quint64 op, quint64 build, QByteArray digest) {
            ++ready; operation = op; CHECK(build == offer.build); CHECK(digest == offer.sha256);
        });
        QObject::connect(backend.get(), &UpdateBackend::failed, [&](quint64 op, QString, FailureCategory value) { ++failures; operation = op; category = value; });
        QObject::connect(backend.get(), &UpdateBackend::noUpdate, [&](quint64 op) { ++absent; operation = op; });
    }
    void discover() {
        backend->check(201, Channel::Stable, CheckKind::Manual);
        REQUIRE(discoveryPump([&] { return offers || failures || absent; }));
        REQUIRE(offers == 1); REQUIRE(failures == 0); REQUIRE(operation == 201);
        REQUIRE(requests.load() == 1);
    }
    void populateCache(const QString& cache) {
        discover(); backend->download(301, offer);
        REQUIRE(discoveryPump([&] { return ready || failures; }));
        REQUIRE(ready == 1); REQUIRE(failures == 0);
        REQUIRE(QFile::exists(cache + "/update.zip"));
        REQUIRE(QFile::exists(cache + "/appcast.xml"));
        createBackend(cache);
    }
};
template<class T> bool backendFence(T& backend) {
    if constexpr(requires { backend.stopWorkersForQuit(); }) return backend.stopWorkersForQuit();
    else return false;
}
QStringList stagedArchives() {
    QStringList result;
    QDirIterator paths(QDir::tempPath(), {"update.zip"}, QDir::Files, QDirIterator::Subdirectories);
    while(paths.hasNext()) result.push_back(paths.next());
    return result;
}
}
TEST_CASE("Mac backend requests the published feed for its CPU flavor", "[.][update-feed-path-native]") {
    BackendFixture f; f.discover();
    const auto cpu = QSysInfo::buildCpuArchitecture();
    REQUIRE((cpu == "arm64" || cpu == "x86_64"));
    const std::string expected = cpu == "arm64"
        ? "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/apple-silicon.xml"
        : "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/intel.xml";
    std::lock_guard<std::mutex> lock(f.requestMutex);
    CHECK(f.requestedFeed == expected);
    CHECK(f.ready == 0);
}
namespace {
UpdateRuntimeConfig boundRuntime(const BackendFixture& f) {
    const auto os = QOperatingSystemVersion::current();
    const auto cpu = QSysInfo::buildCpuArchitecture();
    return {{1, Channel::Stable, QString::fromLatin1(f.fixture.bundle), cpu,
        QVersionNumber(os.majorVersion(), os.minorVersion(), qMax(0, os.microVersion()))},
        QUrl(feedUrlForCpu(cpu).toString() + "?revision=17"), f.fixture.signer.publicKey};
}
}
TEST_CASE("Native backend consumes an immutable runtime feed revision and signing key", "[.][update-backend-native][update-factory-native]") {
    BackendFixture f;
    auto config = boundRuntime(f);
    f.createBackend({}, config);
    config.feedUrl = QUrl("https://example.invalid/changed");
    config.publicKeyBase64 = QByteArray(32, 'x').toBase64();
    config.identity.build = 999;
    f.discover();
    const std::string expected = QSysInfo::buildCpuArchitecture() == "arm64"
        ? "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/apple-silicon.xml?revision=17"
        : "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/intel.xml?revision=17";
    { std::lock_guard<std::mutex> lock(f.requestMutex); CHECK(f.requestedFeed == expected); }
    CHECK_FALSE(f.backend->supportsInstallation());
    f.backend->download(202, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    CHECK(f.ready == 1); CHECK(f.failures == 0);
}
TEST_CASE("Native discovery rejects a runtime snapshot that differs from its host bundle", "[.][update-backend-native][update-factory-native]") {
    BackendFixture f;
    auto config = boundRuntime(f);
    SECTION("signing key") { config.publicKeyBase64 = QByteArray(32, 'x').toBase64(); }
    SECTION("installed build") { config.identity.build = 2; }
    SECTION("product identity") { config.identity.bundleId = "org.test.foreign"; }
    f.createBackend({}, config);
    f.backend->check(205, Channel::Stable, CheckKind::Manual);
    REQUIRE(discoveryPump([&] { return f.offers || f.failures || f.absent; }));
    CHECK(f.failures == 1); CHECK(f.offers == 0); CHECK(f.absent == 0); CHECK(f.ready == 0);
    CHECK(f.category == FailureCategory::Verification);
}
TEST_CASE("Native factory refuses a non-bundled process without selecting a network route", "[.][update-factory-native]") {
    dcpp::test::TestContext context;
    REQUIRE_FALSE(currentMacUpdateRuntime());
    std::unique_ptr<UpdateBackend> backend;
    SECTION("factory") { backend = MacUpdateBackend::create(*context.ownedCtx); }
    SECTION("correlated selection") {
        auto selection = MacUpdateBackend::createSelection(*context.ownedCtx);
        CHECK(selection.identity.build == 0); CHECK(selection.identity.bundleId.isEmpty());
        CHECK(selection.identity.cpu.isEmpty()); CHECK(selection.identity.os.isNull());
        backend = std::move(selection.backend);
    }
    SECTION("direct native construction") { backend = std::make_unique<MacUpdateBackend>(*context.ownedCtx); }
    REQUIRE(backend); CHECK_FALSE(backend->supportsInstallation());
    CHECK(backend->parent() == nullptr);
    int failures = 0; FailureCategory category{};
    QObject::connect(backend.get(), &UpdateBackend::failed, [&](quint64 op, QString, FailureCategory value) {
        CHECK(op == 209); ++failures; category = value;
    });
    backend->check(209, Channel::Stable, CheckKind::Manual);
    CHECK(failures == 0);
    REQUIRE(discoveryPump([&] { return failures != 0; }));
    CHECK(failures == 1); CHECK(category == FailureCategory::Unsupported);
    CHECK(backend->stopWorkersForQuit());
}
TEST_CASE("Native factory selects a structurally eligible current bundle without starting workers", "[.][update-factory-native]") {
    DiscoveryFixture f;
    REQUIRE(QFile::copy(QCoreApplication::applicationFilePath(), f.app + "/Contents/MacOS/EiskaltDC++"));
    const auto replace = [&](const QString& key, const QString& type, const QString& value) {
        QProcess remove; remove.start("/usr/libexec/PlistBuddy", {"-c", "Delete :" + key, f.app + "/Contents/Info.plist"});
        REQUIRE(remove.waitForFinished(5000));
        f.setting("Add :" + key + " " + type + " " + value);
    };
    const auto cpu = QSysInfo::buildCpuArchitecture();
    replace("CFBundleIdentifier", "string", "com.github.eiskaltdcpp");
    replace("CFBundleExecutable", "string", "EiskaltDC++");
    replace("CFBundleVersion", "string", "30000001");
    replace("CFBundleShortVersionString", "string", "3.0.0-pre.1");
    replace("LSMinimumSystemVersion", "string", cpu == "arm64" ? "26.0" : "14.0");
    replace("SUFeedURL", "string", feedUrlForCpu(cpu).toString() + "?revision=17");
    for(const auto& key : {"SURequireSignedFeed", "SUVerifyUpdateBeforeExtraction", "SUEnableInstallerLauncherService"})
        replace(key, "bool", "true");
    for(const auto& key : {"SUEnableDownloaderService", "SUEnableAutomaticChecks", "SUAutomaticallyUpdate", "SUEnableSystemProfiling"})
        replace(key, "bool", "false");
    replace("SUSignedFeedFailureExpirationInterval", "integer", "0");
    replace("NSAppTransportSecurity", "dict", "");
    f.setting("Add :NSAppTransportSecurity:NSAllowsLocalNetworking bool true");
    f.run("/usr/bin/codesign", {"--force", "--sign", "-", f.app});
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("EISKALT_FACTORY_CHILD", "1");
    QProcess child; child.setProcessEnvironment(environment);
    child.start(f.app + "/Contents/MacOS/EiskaltDC++", {"[factory-runtime-child]"});
    REQUIRE(child.waitForStarted(5000));
    REQUIRE(child.waitForFinished(20000));
    INFO(child.readAllStandardOutput().constData()); INFO(child.readAllStandardError().constData());
    CHECK(child.exitStatus() == QProcess::NormalExit); CHECK(child.exitCode() == 0);
}
TEST_CASE("Current bundle native factory fixture child", "[.][factory-runtime-child]") {
    if(qEnvironmentVariable("EISKALT_FACTORY_CHILD") != "1") SKIP("Internal fixture child; use the public native factory test");
    REQUIRE(qEnvironmentVariable("HOME").startsWith("/private/tmp/"));
    const auto runtime = currentMacUpdateRuntime(); REQUIRE(runtime);
    CHECK(runtime->feedUrl.query() == "revision=17"); CHECK(runtime->identity.build == 30000001);
    dcpp::test::TestContext context;
    auto selection = MacUpdateBackend::createSelection(*context.ownedCtx);
    CHECK(selection.identity.build == runtime->identity.build);
    CHECK(selection.identity.defaultChannel == runtime->identity.defaultChannel);
    CHECK(selection.identity.bundleId == runtime->identity.bundleId);
    CHECK(selection.identity.cpu == runtime->identity.cpu);
    CHECK(selection.identity.os == runtime->identity.os);
    auto backend = std::move(selection.backend); REQUIRE(backend);
    CHECK(qobject_cast<MacUpdateBackend*>(backend.get()) != nullptr);
    CHECK(backend->parent() == nullptr); CHECK_FALSE(backend->supportsInstallation());
    CHECK(backend->stopWorkersForQuit());
}
TEST_CASE("Mac backend restores verified cache only after fresh routed discovery", "[.][update-backend-native][backend-cache-native]") {
    QTemporaryDir cache; REQUIRE(cache.isValid());
    BackendFixture f(cache.path()); f.populateCache(cache.path());
    QCoreApplication::processEvents(); CHECK(f.ready == 0); CHECK(f.offers == 0); CHECK(f.requests.load() == 0);
    f.discover(); f.backend->download(302, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    CHECK(f.ready == 1); CHECK(f.failures == 0); CHECK(f.requests.load() == 1);
    CHECK(f.operation == 302);
    const auto paths = stagedArchives(); REQUIRE(paths.size() == 2);
    for(const auto& path : paths) {
        QFile file(path); REQUIRE(file.open(QIODevice::ReadOnly)); CHECK(file.readAll() == f.archive);
    }
}
TEST_CASE("Mac backend cache misses retain ordinary routed archive verification", "[.][update-backend-native][backend-cache-native]") {
    QTemporaryDir cache; REQUIRE(cache.isValid());
    BackendFixture f(cache.path()); f.populateCache(cache.path());
    SECTION("missing archive") { REQUIRE(QFile::remove(cache.filePath("update.zip"))); }
    SECTION("changed archive") {
        QFile file(cache.filePath("update.zip")); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(QByteArray(f.archive.size(), 'B')) == f.archive.size());
    }
    SECTION("changed cached signed feed") {
        QFile file(cache.filePath("appcast.xml")); REQUIRE(file.open(QIODevice::WriteOnly)); REQUIRE(file.write("bad") == 3);
    }
    SECTION("changed fresh authenticated notes") {
        auto xml = f.feed.left(f.feed.indexOf("</rss>") + 6);
        xml.replace("Plain &amp; safe notes", "New notes"); f.feed = f.fixture.signer.sign(xml);
    }
    SECTION("cache valid but fresh archive corrupt") {
        REQUIRE(QFile::remove(cache.filePath("update.zip"))); f.corrupt = true;
    }
    f.discover(); f.backend->download(303, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    CHECK(f.requests.load() == 2); CHECK(f.ready == (f.corrupt ? 0 : 1)); CHECK(f.failures == (f.corrupt ? 1 : 0));
    if(f.corrupt) CHECK(f.category == FailureCategory::Verification);
}
TEST_CASE("Mac backend creates only a private cache leaf and treats storage failure as optional", "[.][update-backend-native][backend-cache-native]") {
    QTemporaryDir parent; REQUIRE(parent.isValid());
    QString cache = parent.filePath("private-cache"); bool permitted = false;
    SECTION("fresh private leaf") { permitted = true; }
    SECTION("missing parent") { cache = parent.filePath("missing/cache"); }
    SECTION("shared parent") { REQUIRE(::chmod(QFile::encodeName(parent.path()).constData(), 0777) == 0); }
    SECTION("shared existing leaf") {
        REQUIRE(QDir().mkdir(cache)); REQUIRE(::chmod(QFile::encodeName(cache).constData(), 0755) == 0);
    }
    SECTION("symlink leaf") { REQUIRE(QFile::link(parent.path(), cache)); }
    SECTION("file leaf") { QFile file(cache); REQUIRE(file.open(QIODevice::WriteOnly)); REQUIRE(file.write("sentinel") == 8); }
    BackendFixture f(cache); f.discover(); f.backend->download(304, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    CHECK(f.ready == 1); CHECK(f.failures == 0); CHECK(f.requests.load() == 2);
    if(permitted) {
        struct stat node{}; REQUIRE(::lstat(QFile::encodeName(cache).constData(), &node) == 0);
        CHECK((node.st_mode & 0777) == 0700); CHECK(QFile::exists(cache + "/update.zip"));
    } else CHECK_FALSE(QFile::exists(cache + "/update.zip"));
}
TEST_CASE("Mac backend cached bytes cannot bypass current feed or installed key", "[.][update-backend-native][backend-cache-native]") {
    QTemporaryDir cache; REQUIRE(cache.isValid());
    BackendFixture old(cache.path()); old.populateCache(cache.path()); old.backend.reset();
    // New installed bundle path: NSBundle/Sparkle retain loaded plist values in-process.
    BackendFixture f(cache.path());
    bool current = false;
    SECTION("fresh feed rejected") { f.feed.replace("Plain", "Wrong"); }
    SECTION("installed version current") { current = true; f.fixture.setting("Set :CFBundleVersion 10"); }
    SECTION("current installed key changed") { f.fixture.setting("Set :SUPublicEDKey " + QByteArray(32, '\x55').toBase64()); }
    f.backend->check(305, Channel::Stable, CheckKind::Manual);
    REQUIRE(discoveryPump([&] { return f.absent || f.failures || f.offers; }));
    CHECK(f.offers == 0); CHECK(f.ready == 0); CHECK(f.failures == (current ? 0 : 1));
    CHECK(f.absent == (current ? 1 : 0)); CHECK(f.requests.load() == 1);
}
TEST_CASE("Mac backend restore completion cannot outlive cancellation route or quit fence", "[.][update-backend-native][backend-cache-native]") {
    QTemporaryDir cache; REQUIRE(cache.isValid());
    BackendFixture f(cache.path()); f.populateCache(cache.path()); f.discover();
    bool revoked = false;
    bool miss = GENERATE(false, true);
    if(miss) REQUIRE(QFile::remove(cache.filePath("update.zip")));
    f.backend->download(306, f.offer);
    // Hold the owner event loop so a completed worker cannot publish Ready or fetch on a miss.
    QThread::msleep(40);
    SECTION("cancel") { f.backend->cancel(); }
    SECTION("revoke route") { f.context.ownedCtx->getProxyRoute()->stop(); revoked = true; }
    SECTION("committed worker fence") { REQUIRE(backendFence(*f.backend)); }
    SECTION("owner destruction") { f.backend.reset(); }
    QElapsedTimer drain; drain.start(); REQUIRE(discoveryPump([&] { return drain.elapsed() > 100; }));
    CHECK(f.ready == 0); CHECK(f.failures == (revoked ? 1 : 0)); CHECK(f.requests.load() == 1);
    CHECK(stagedArchives().size() == (miss ? 0 : 1));
}
TEST_CASE("Internal Mac backend persisted cache child", "[.][backend-cache-child]") {
    const auto cache = qEnvironmentVariable("EISKALT_BACKEND_CACHE_CHILD_ROOT");
    const auto mode = qEnvironmentVariable("EISKALT_BACKEND_CACHE_CHILD_MODE");
    if(cache.isEmpty() || mode.isEmpty()) SKIP("Internal fixture child; use the public backend cache restart test");
    REQUIRE(cache.startsWith(QDir::tempPath() + "/"));
    REQUIRE((mode == "populate" || mode == "crash" || mode == "restore"));
    BackendFixture f(cache);
    CHECK(f.ready == 0); CHECK(f.offers == 0); CHECK(f.requests.load() == 0);
    f.discover(); f.backend->download(307, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    REQUIRE(f.ready == 1); REQUIRE(f.failures == 0);
    REQUIRE(f.requests.load() == (mode == "restore" ? 1 : 2));
    QFile archive(cache + "/update.zip"); REQUIRE(archive.open(QIODevice::ReadOnly));
    CHECK(archive.readAll() == f.archive); archive.close();
    if(mode == "crash") {
        QFile receipt(cache + "/child-ready"); REQUIRE(receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        REQUIRE(receipt.write("verified-cache-ready") == 20); REQUIRE(receipt.flush()); receipt.close();
        QElapsedTimer wait; wait.start();
        while(wait.elapsed() < 30000) QThread::msleep(10);
        FAIL("Parent must kill only its owned child after the verified cache receipt");
    }
    REQUIRE(backendFence(*f.backend));
}
TEST_CASE("Mac backend reauthenticates persisted staging after process exit or crash", "[.][update-backend-native][backend-cache-native]") {
    QTemporaryDir root; REQUIRE(root.isValid());
    const QDir cache(root.filePath("cache")); REQUIRE(QDir().mkdir(cache.path()));
    REQUIRE(::chmod(QFile::encodeName(cache.path()).constData(), 0700) == 0);
    bool crash = false;
    SECTION("normal process exit") {}
    SECTION("owned process SIGKILL") { crash = true; }
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("EISKALT_BACKEND_CACHE_CHILD_ROOT", cache.path());
    // Parent owns the complete child temp namespace, including SIGKILL leftovers.
    env.insert("TMPDIR", root.path() + "/");
    env.insert("EISKALT_BACKEND_CACHE_CHILD_MODE", crash ? "crash" : "populate");
    QProcess child; child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), {"[backend-cache-child]"});
    REQUIRE(child.waitForStarted(5000));
    if(crash) {
        const bool ready = discoveryPump([&] { return QFile::exists(cache.filePath("child-ready")) || child.state() == QProcess::NotRunning; }, 20000);
        if(!ready || !QFile::exists(cache.filePath("child-ready"))) {
            child.kill(); child.waitForFinished(5000);
            INFO(child.readAllStandardOutput().constData()); INFO(child.readAllStandardError().constData());
            FAIL("Owned child did not finish saving a verified private cache");
        }
        child.kill(); REQUIRE(child.waitForFinished(5000)); CHECK(child.exitStatus() == QProcess::CrashExit);
    } else {
        const bool finished = child.waitForFinished(20000);
        if(!finished) { child.kill(); child.waitForFinished(5000); }
        INFO(child.readAllStandardOutput().constData()); INFO(child.readAllStandardError().constData());
        REQUIRE(finished); REQUIRE(child.exitStatus() == QProcess::NormalExit); REQUIRE(child.exitCode() == 0);
    }
    REQUIRE(QFile::exists(cache.filePath("appcast.xml"))); REQUIRE(QFile::exists(cache.filePath("update.zip")));
    env.insert("EISKALT_BACKEND_CACHE_CHILD_MODE", "restore"); child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), {"[backend-cache-child]"});
    REQUIRE(child.waitForStarted(5000));
    const bool finished = child.waitForFinished(20000);
    if(!finished) { child.kill(); child.waitForFinished(5000); }
    INFO(child.readAllStandardOutput().constData()); INFO(child.readAllStandardError().constData());
    REQUIRE(finished); CHECK(child.exitStatus() == QProcess::NormalExit); CHECK(child.exitCode() == 0);
}
TEST_CASE("Mac backend retained fence preserves Ready staging through core route retirement", "[.][updater-worker-fence-native]") {
    BackendFixture f; f.discover();
    f.backend->download(211, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    REQUIRE(f.ready == 1); REQUIRE(f.failures == 0);
    const auto paths = stagedArchives(); REQUIRE(paths.size() == 1);
    CHECK_FALSE(std::async(std::launch::async, [&] { return backendFence(*f.backend); }).get());
    REQUIRE(backendFence(*f.backend));
    f.context.ownedCtx->getProxyRoute()->stop();
    f.backend->check(212, Channel::Stable, CheckKind::Manual);
    f.backend->download(213, f.offer);
    REQUIRE(backendFence(*f.backend));
    QElapsedTimer drain; drain.start(); REQUIRE(discoveryPump([&] { return drain.elapsed() > 120; }));
    CHECK(f.offers == 1); CHECK(f.ready == 1); CHECK(f.failures == 0); CHECK(f.requests.load() == 2);
    QFile archive(paths.front()); REQUIRE(archive.open(QIODevice::ReadOnly));
    CHECK(archive.readAll() == f.archive); archive.close();
    f.backend.reset(); CHECK_FALSE(QFile::exists(paths.front()));
}
TEST_CASE("Mac backend retained fence rejects staging revoked before preparation", "[.][updater-worker-fence-native]") {
    BackendFixture f; f.discover(); f.backend->download(214, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; })); REQUIRE(f.ready == 1);
    const auto paths = stagedArchives(); REQUIRE(paths.size() == 1);
    f.context.ownedCtx->getProxyRoute()->stop();
    REQUIRE(backendFence(*f.backend));
    CHECK_FALSE(QFile::exists(paths.front()));
    QCoreApplication::processEvents(); CHECK(f.failures == 0); CHECK(f.ready == 1);
}
TEST_CASE("Mac backend retained fence joins active transport without queued completion", "[.][updater-worker-fence-native]") {
    BackendFixture f;
    bool feed = false;
    SECTION("feed") { feed = true; f.holdFeed = true; f.backend->check(215, Channel::Stable, CheckKind::Manual); }
    SECTION("archive") { f.discover(); f.workerExited = false; f.holdArchive = true; f.backend->download(216, f.offer); }
    QElapsedTimer start; start.start();
    while(!(feed ? f.feedEntered.load() : f.archiveEntered.load()) && start.elapsed() < 1000) QThread::msleep(1);
    REQUIRE((feed ? f.feedEntered.load() : f.archiveEntered.load()));
    REQUIRE(backendFence(*f.backend));
    CHECK(f.workerCancelled.load()); CHECK(f.workerExited.load());
    f.backend->check(217, Channel::Stable, CheckKind::Manual);
    f.backend->download(218, f.offer);
    QElapsedTimer drain; drain.start(); REQUIRE(discoveryPump([&] { return drain.elapsed() > 120; }));
    CHECK(f.ready == 0); CHECK(f.failures == 0); CHECK(f.offers == (feed ? 0 : 1));
    CHECK(f.requests.load() == (feed ? 1 : 2)); CHECK(stagedArchives().isEmpty());
    CHECK(backendFence(*f.backend));
}
TEST_CASE("Mac backend hands retained staging to the real installer after route retirement", "[.][update-backend-native][backend-handoff-native]") {
    bool destroyOnReceipt = false;
    SECTION("retained observer") {}
    SECTION("observer destroys refused backend") { destroyOnReceipt = true; }
    BackendFixture f; f.discover(); f.backend->download(401, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; })); REQUIRE(f.ready == 1);
    const auto paths = stagedArchives(); REQUIRE(paths.size() == 1);
    REQUIRE(backendFence(*f.backend));
    f.context.ownedCtx->getProxyRoute()->stop();
    int checks = 0, receipts = 0; bool prepared = true;
    QObject::connect(f.backend.get(), &UpdateBackend::installHandoffFinished, f.backend.get(),
        [&](quint64 op, bool result) {
            CHECK(op == 402); ++receipts; prepared = result;
            if(destroyOnReceipt) f.backend.reset();
        });
    CHECK_FALSE(f.backend->supportsInstallation());
    f.backend->beginInstallHandoff(402, f.offer, [&] { ++checks; return true; });
    CHECK(checks == 1); CHECK(receipts == 0); CHECK(QFile::exists(paths.front()));
    f.backend->beginInstallHandoff(402, f.offer, [&] { FAIL("Duplicate handoff evaluated consent"); return true; });
    REQUIRE(discoveryPump([&] { return receipts != 0; }));
    // This process is intentionally not the selected signed app. The real
    // installer must refuse its missing bundle policy, never prepare an update.
    CHECK_FALSE(prepared); CHECK(receipts == 1); CHECK(f.requests.load() == 2);
    CHECK_FALSE(QFile::exists(paths.front()));
    if(f.backend) f.backend->beginInstallHandoff(402, f.offer, [] { return true; });
    QCoreApplication::processEvents(); CHECK(receipts == 1);
}

TEST_CASE("Mac backend handoff admission refuses unsafe staging or absent consent", "[.][update-backend-native][backend-handoff-native]") {
    BackendFixture f; f.discover(); f.backend->download(411, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; })); REQUIRE(f.ready == 1);
    auto offer = f.offer;
    bool fenced = true, approved = true, missing = false, throwing = false;
    int expectedChecks = 0;
    SECTION("unfenced") { fenced = false; }
    SECTION("different offer") { ++offer.build; }
    SECTION("metadata changed") { offer.notes += " changed"; }
    SECTION("route revoked before fence") { f.context.ownedCtx->getProxyRoute()->stop(); }
    SECTION("consent refused") { approved = false; expectedChecks = 1; }
    SECTION("consent throws") { throwing = true; expectedChecks = 1; }
    SECTION("consent absent") { missing = true; }
    if(fenced) REQUIRE(backendFence(*f.backend));
    int receipts = 0, checks = 0; bool prepared = true;
    QObject::connect(f.backend.get(), &UpdateBackend::installHandoffFinished, f.backend.get(),
        [&](quint64 op, bool value) { CHECK(op == 412); ++receipts; prepared = value; });
    std::function<bool()> consent;
    if(!missing) consent = [&] { ++checks; if(throwing) throw std::runtime_error("refused"); return approved; };
    f.backend->beginInstallHandoff(412, offer, consent);
    CHECK(checks == expectedChecks); CHECK(receipts == 0);
    REQUIRE(discoveryPump([&] { return receipts != 0; }));
    CHECK(receipts == 1); CHECK_FALSE(prepared); CHECK(f.requests.load() == 2);
}

TEST_CASE("Mac backend cancellation retains installer files until terminal refusal", "[.][update-backend-native][backend-handoff-native]") {
    BackendFixture f; f.discover(); f.backend->download(421, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; })); REQUIRE(f.ready == 1);
    const auto paths = stagedArchives(); REQUIRE(paths.size() == 1);
    REQUIRE(backendFence(*f.backend));
    int receipts = 0, checks = 0;
    QObject::connect(f.backend.get(), &UpdateBackend::installHandoffFinished, f.backend.get(),
        [&](quint64 op, bool prepared) { CHECK(op == 422); CHECK_FALSE(prepared); ++receipts; });
    f.backend->beginInstallHandoff(422, f.offer, [&] { ++checks; return true; });
    CHECK(checks == 1); CHECK(receipts == 0);
    bool ordinaryCancel = false;
    SECTION("consent revocation") {}
    SECTION("ordinary cancellation") { ordinaryCancel = true; }
    if(ordinaryCancel) f.backend->cancel(); else f.backend->revokeInstallConsent();
    CHECK(receipts == 0); CHECK(QFile::exists(paths.front()));
    REQUIRE(discoveryPump([&] { return receipts != 0; }));
    CHECK(receipts == 1); CHECK_FALSE(QFile::exists(paths.front()));
}

TEST_CASE("Mac backend pending installer destruction joins before removing staging", "[.][update-backend-native][backend-handoff-native]") {
    BackendFixture f; f.discover(); f.backend->download(431, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; })); REQUIRE(f.ready == 1);
    const auto paths = stagedArchives(); REQUIRE(paths.size() == 1);
    REQUIRE(backendFence(*f.backend));
    int checks = 0, receipts = 0;
    QObject::connect(f.backend.get(), &UpdateBackend::installHandoffFinished, [&](quint64, bool) { ++receipts; });
    f.backend->beginInstallHandoff(432, f.offer, [&] { ++checks; return true; });
    CHECK(checks == 1); CHECK(receipts == 0);
    f.backend.reset(); CHECK_FALSE(QFile::exists(paths.front()));
    QCoreApplication::processEvents(); CHECK(receipts == 0);
}

TEST_CASE("Mac backend foreign and competing requests cannot steal a retained handoff", "[.][update-backend-native][backend-handoff-native]") {
    BackendFixture f; f.discover(); f.backend->download(441, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; })); REQUIRE(f.ready == 1);
    REQUIRE(backendFence(*f.backend));
    int checks = 0; QList<quint64> receipts;
    QObject::connect(f.backend.get(), &UpdateBackend::installHandoffFinished, f.backend.get(),
        [&](quint64 op, bool prepared) { CHECK_FALSE(prepared); receipts.append(op); });
    std::async(std::launch::async, [&] {
        f.backend->beginInstallHandoff(442, f.offer, [] { FAIL("Foreign consent executed"); return true; });
    }).get();
    f.backend->beginInstallHandoff(443, f.offer, [&] { ++checks; return true; });
    CHECK(checks == 1); CHECK(receipts.isEmpty());
    for(const auto op : {quint64(0), quint64(444), quint64(444)})
        f.backend->beginInstallHandoff(op, f.offer, [] { FAIL("Competing consent executed"); return true; });
    REQUIRE(discoveryPump([&] { return receipts.size() == 4; }));
    CHECK(receipts.count(442) == 1); CHECK(receipts.count(443) == 1);
    CHECK(receipts.count(444) == 1); CHECK(receipts.count(0) == 1);
    CHECK(checks == 1); CHECK(f.requests.load() == 2);
}

TEST_CASE("Mac backend installs only the retained signed main bundle on committed handoff", "[.][backend-install-native]") {
    const auto mode = qEnvironmentVariable("EISKALT_INSTALL_MODE");
    REQUIRE((mode == "normal" || mode == "cancel-verifier" || mode == "cancel-before-native" || mode == "cancel-prepared"));
    const auto contents = QFileInfo(QCoreApplication::applicationDirPath()).absolutePath();
    QFile plist(contents + "/Info.plist"); REQUIRE(plist.open(QIODevice::ReadOnly));
    const auto info = plist.readAll();
    REQUIRE(info.contains("org.eiskaltdcpp.updater-probe."));
    REQUIRE(qEnvironmentVariable("CFFIXED_USER_HOME") == qEnvironmentVariable("HOME"));
    REQUIRE(qEnvironmentVariable("TMPDIR").startsWith(qEnvironmentVariable("HOME") + "/"));
    REQUIRE(QGuiApplication::platformName() == "cocoa");
    const int fd = socket(AF_INET, SOCK_STREAM, 0); REQUIRE(fd >= 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(443);
    inet_pton(AF_INET, "203.0.113.1", &address.sin_addr);
    const int connected = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const int error = errno; close(fd);
    REQUIRE(connected == -1); REQUIRE(error == EPERM);
    QFile feedFile(qEnvironmentVariable("EISKALT_PROBE_LOCAL_FEED")); REQUIRE(feedFile.open(QIODevice::ReadOnly));
    QFile archiveFile(qEnvironmentVariable("EISKALT_PROBE_LOCAL_ARCHIVE")); REQUIRE(archiveFile.open(QIODevice::ReadOnly));
    const auto feed = feedFile.readAll(), archive = archiveFile.readAll();
    dcpp::test::TestContext context;
    std::atomic_int requests{0};
    int offers = 0, ready = 0, failures = 0, receipts = 0, checks = 0;
    UpdateOffer offer; bool prepared = false;
    auto backend = MacUpdateBackendTestAccess::make(*context.ownedCtx, {},
        [&](const auto& policy, const std::string& url, const auto& sink) {
            if(policy.cancelled()) throw std::runtime_error("cancelled");
            ++requests;
            const auto& bytes = url.find("raw.githubusercontent.com") != std::string::npos ? feed : archive;
            sink(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size());
        });
    QObject::connect(backend.get(), &UpdateBackend::available, [&](quint64 op, UpdateOffer value) {
        CHECK(op == 501); ++offers; offer = value;
    });
    QObject::connect(backend.get(), &UpdateBackend::downloadReady, [&](quint64 op, quint64, QByteArray) { CHECK(op == 502); ++ready; });
    QObject::connect(backend.get(), &UpdateBackend::failed, [&](quint64, QString message, FailureCategory) {
        INFO(message.toStdString()); ++failures;
    });
    QObject::connect(backend.get(), &UpdateBackend::installHandoffFinished, backend.get(), [&](quint64 op, bool result) {
        CHECK(op == 503); ++receipts; prepared = result;
    });
    backend->check(501, Channel::Stable, CheckKind::Manual);
    REQUIRE(discoveryPump([&] { return offers || failures; })); REQUIRE(offers == 1); REQUIRE(failures == 0);
    backend->download(502, offer);
    REQUIRE(discoveryPump([&] { return ready || failures; }, 30000)); REQUIRE(ready == 1); REQUIRE(failures == 0);
    const auto paths = stagedArchives(); REQUIRE(paths.size() == 1);
    REQUIRE(backend->stopWorkersForQuit()); context.ownedCtx->getProxyRoute()->stop();
    backend->beginInstallHandoff(503, offer, [&] {
        ++checks;
        return mode == "normal" || mode == "cancel-verifier" ||
            (mode == "cancel-before-native" ? checks == 1 : checks < 3);
    });
    CHECK(checks == 1); CHECK(receipts == 0);
    backend->beginInstallHandoff(503, offer, [] { FAIL("Duplicate admitted native operation"); return true; });
    if(mode == "cancel-verifier") {
        backend->revokeInstallConsent(); CHECK(receipts == 0); CHECK(QFile::exists(paths.front()));
    }
    REQUIRE(discoveryPump([&] { return receipts != 0; }, 30000));
    CHECK(receipts == 1); CHECK(prepared == (mode == "normal")); CHECK(requests == 2);
    if(mode == "normal") {
        CHECK(checks >= 3); CHECK(QFile::exists(paths.front()));
        // Keep the prepared native owner and staging through process exit.
        backend.release();
    } else {
        CHECK_FALSE(QFile::exists(paths.front()));
        CHECK(checks == (mode == "cancel-verifier" ? 1 : mode == "cancel-before-native" ? 2 : 3));
    }
}
TEST_CASE("Mac backend stages only the independently verified selected archive", "[.][update-backend-native]") {
    BackendFixture f; f.discover();
    bool invalid = false;
    SECTION("valid archive") {}
    SECTION("corrupt archive") { f.corrupt = true; invalid = true; }
    auto requested = f.offer;
    SECTION("changed offer digest") { requested.sha256[0] ^= 1; invalid = true; }
    SECTION("changed offer size") { ++requested.size; invalid = true; }
    f.backend->download(202, requested);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    CHECK(f.operation == 202); CHECK(f.ready == (invalid ? 0 : 1)); CHECK(f.failures == (invalid ? 1 : 0));
    CHECK(f.requests.load() == (requested.sha256 != f.offer.sha256 || requested.size != f.offer.size ? 1 : 2));
    if(invalid) CHECK(f.category == FailureCategory::Verification);
}
TEST_CASE("Mac backend cancellation and route change invalidate discovered offers", "[.][update-backend-native]") {
    BackendFixture f; f.discover();
    SECTION("explicit cancellation") { f.backend->cancel(); }
    SECTION("route revoked") { f.context.ownedCtx->getProxyRoute()->stop(); }
    f.backend->download(203, f.offer);
    REQUIRE(discoveryPump([&] { return f.ready || f.failures; }));
    CHECK(f.ready == 0); CHECK(f.failures == 1); CHECK(f.requests.load() == 1);
}
TEST_CASE("Mac backend cancels an in-flight archive without stale readiness", "[.][update-backend-native]") {
    BackendFixture f; f.discover(); f.holdArchive = true;
    f.backend->download(204, f.offer);
    REQUIRE(discoveryPump([&] { return f.archiveEntered.load(); }));
    bool revoked = false, destroyed = false;
    QElapsedTimer cancellation; cancellation.start();
    SECTION("cancel then reuse") { f.backend->cancel(); }
    SECTION("route revoked") { f.context.ownedCtx->getProxyRoute()->stop(); revoked = true; }
    SECTION("owner destroyed") { f.backend.reset(); destroyed = true; }
    CHECK(cancellation.elapsed() < 500);
    REQUIRE(discoveryPump([&] { return f.workerCancelled.load(); }));
    QElapsedTimer drain; drain.start(); REQUIRE(discoveryPump([&] { return drain.elapsed() > 150; }));
    CHECK(f.ready == 0); CHECK(f.failures == (revoked ? 1 : 0)); CHECK(f.requests.load() == 2);
    if(!revoked && !destroyed) {
        f.backend->check(205, Channel::Stable, CheckKind::Manual);
        REQUIRE(discoveryPump([&] { return f.offers == 2 || f.failures; }));
        CHECK(f.offers == 2); CHECK(f.failures == 0); CHECK(f.operation == 205);
    }
}
TEST_CASE("Mac backend rejects a bad feed and completes no-update without downloading", "[.][update-backend-native]") {
    BackendFixture f; bool invalid = false;
    SECTION("bad signature") { f.feed.replace("Plain", "Wrong"); invalid = true; }
    SECTION("already current") { f.fixture.setting("Set :CFBundleVersion 10"); }
    f.backend->check(206, Channel::Stable, CheckKind::Manual);
    REQUIRE(discoveryPump([&] { return f.absent || f.failures || f.offers; }));
    CHECK(f.offers == 0); CHECK(f.ready == 0); CHECK(f.failures == (invalid ? 1 : 0));
    CHECK(f.absent == (invalid ? 0 : 1)); CHECK(f.requests.load() == 1);
}
TEST_CASE("Real Mac backend advances controller to staged Ready and honors Later", "[.][update-backend-native]") {
    BackendFixture f;
    WulforSettings settings(*f.context.ownedCtx);
    UpdateIdentity installed{1, Channel::Stable, QString::fromLatin1(f.fixture.bundle), QSysInfo::buildCpuArchitecture(), QVersionNumber(26, 0)};
    UpdateController controller(settings, *f.backend, installed);
    controller.checkNow();
    REQUIRE(discoveryPump([&] { return controller.state() == UpdateController::State::Available || f.failures; }));
    REQUIRE(f.failures == 0);
    controller.download();
    REQUIRE(discoveryPump([&] { return controller.state() == UpdateController::State::Ready || f.failures; }));
    REQUIRE(f.failures == 0); CHECK(f.ready == 1); CHECK(f.requests.load() == 2);
    controller.later(); controller.normalQuitCommitted();
    CHECK(controller.state() == UpdateController::State::Stopped);
    CHECK(f.failures == 0); CHECK(f.requests.load() == 2);
}
#endif
