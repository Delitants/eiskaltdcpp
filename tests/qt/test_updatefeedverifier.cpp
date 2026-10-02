#include "dcpp/stdinc.h"
#include "updates/UpdateFeedVerifier.h"
#include "updates/UpdateLoopbackBridge.h"
#include "dcpp/ProxyRoute.h"
#include "dcpp/ScopedFunctor.h"
#include <catch2/catch_test_macros.hpp>
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
#ifdef EISKALT_TEST_MAC_UPDATE_DISCOVERY
#include "updates/MacUpdateDiscovery.h"
#include "updates/MacUpdateBackend.h"
#include "updates/UpdateController.h"
#include "WulforSettings.h"
#include "tests/TestContext.h"
#include "dcpp/HttpRequestPolicy.h"
#include <QSysInfo>
#include <QGuiApplication>
#include <QTimer>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
namespace updates {
struct MacUpdateDiscoveryTestAccess {
    static std::unique_ptr<MacUpdateDiscovery> make(const QString& bundle) {
        return std::unique_ptr<MacUpdateDiscovery>(new MacUpdateDiscovery(bundle, nullptr));
    }
};
struct MacUpdateBackendTestAccess {
    static std::unique_ptr<MacUpdateBackend> make(dcpp::DCContext& context, const QString& bundle, transport_detail::Backend transport) {
        return std::unique_ptr<MacUpdateBackend>(new MacUpdateBackend(context, bundle, std::move(transport), nullptr));
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
    std::atomic_bool holdArchive{false}, archiveEntered{false}, workerCancelled{false};
    std::atomic_bool holdFeed{false}, feedEntered{false}, workerExited{false};
    bool corrupt = false;
    std::unique_ptr<MacUpdateBackend> backend;
    int offers = 0, ready = 0, failures = 0, absent = 0;
    quint64 operation = 0;
    UpdateOffer offer;
    FailureCategory category{};
    BackendFixture() {
        auto item = fixture.signer.item(2, QSysInfo::buildCpuArchitecture().toLatin1(), "stable");
        const auto signedArchive = fixture.signer.sign(archive);
        const auto signature = signedArchive.mid(archive.size()).split('\n')[1].mid(13);
        item.replace("org.eiskaltdcpp.EiskaltDCpp", fixture.bundle);
        item.replace(">26.0<", ">14.0<");
        item.replace("length=\"123\"", "length=\"" + QByteArray::number(archive.size()) + "\"");
        item.replace(QByteArray(64, 's').toBase64(), signature);
        item.replace(QByteArray(64, 'a'), QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex());
        feed = fixture.signer.sign(fixture.signer.xml(item));
        backend = MacUpdateBackendTestAccess::make(*context.ownedCtx, fixture.app,
            [this](const auto& policy, const std::string& url, const auto& sink) {
                ++requests;
                const bool isFeed = url.find("raw.githubusercontent.com") != std::string::npos;
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
            });
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
