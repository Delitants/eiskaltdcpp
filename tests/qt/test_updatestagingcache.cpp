#include "updates/UpdateStagingCache.h"
#include "updates/UpdateArchiveVerifier.h"
#include <catch2/catch_test_macros.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <openssl/evp.h>
#include <memory>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace updates;
namespace {
struct CacheFixture {
    QTemporaryDir cache, stage, restored;
    QByteArray bytes = QByteArray(8192, 'K'), publicKey, feed;
    SignedUpdate selected;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{nullptr, EVP_PKEY_free};
    QString source = stage.filePath("update.zip"), destination = restored.filePath("update.zip");
    CacheFixture() {
        REQUIRE(cache.isValid()); REQUIRE(stage.isValid()); REQUIRE(restored.isValid());
        const QByteArray seed(32, '\x5e'); // Disposable test key, not a release identity.
        key.reset(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
            reinterpret_cast<const unsigned char*>(seed.constData()), seed.size()));
        REQUIRE(key);
        QByteArray raw(32, '\0'); size_t size = raw.size();
        REQUIRE(EVP_PKEY_get_raw_public_key(key.get(), reinterpret_cast<unsigned char*>(raw.data()), &size) == 1);
        publicKey = raw.toBase64();
        feed = makeFeed();
        const auto verified = verifyUpdateFeed(feed, publicKey);
        REQUIRE(verified); REQUIRE(verified->releases.size() == 1);
        selected = verified->releases.front();
        write(source, bytes);
    }
    QByteArray signature(const QByteArray& value) {
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        REQUIRE(context); REQUIRE(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1);
        QByteArray result(64, '\0'); size_t size = result.size();
        REQUIRE(EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(result.data()), &size,
            reinterpret_cast<const unsigned char*>(value.constData()), value.size()) == 1);
        return result.toBase64();
    }
    QByteArray makeFeed(int build = 2) {
        const auto xml = QByteArray("<?xml version=\"1.0\"?><rss version=\"2.0\" xmlns:sparkle=\"http://www.andymatuschak.org/xml-namespaces/sparkle\" "
            "xmlns:eiskalt=\"https://eiskaltdcpp.org/xml-namespaces/updates/1\"><channel><title>Cache QA</title><item>"
            "<title>Release</title><sparkle:version>") + QByteArray::number(build) + "</sparkle:version>"
            "<sparkle:shortVersionString>3.0.0-pre.2</sparkle:shortVersionString><sparkle:minimumSystemVersion>14.0</sparkle:minimumSystemVersion>"
            "<eiskalt:bundleIdentifier>org.eiskaltdcpp.updater-cache-test</eiskalt:bundleIdentifier><eiskalt:cpu>arm64</eiskalt:cpu>"
            "<description>Verified cache</description><enclosure url=\"https://github.com/Delitants/eiskaltdcpp/releases/download/v3/update-arm64.zip\" "
            "type=\"application/octet-stream\" sparkle:os=\"macos\" length=\"" + QByteArray::number(bytes.size()) +
            "\" sparkle:edSignature=\"" + signature(bytes) + "\" eiskalt:sha256=\"" +
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() + "\"/></item></channel></rss>";
        return xml + "<!-- sparkle-signatures:\nedSignature: " + signature(xml) +
            "\nlength: " + QByteArray::number(xml.size()) + "\n-->\n";
    }
    static void write(const QString& path, const QByteArray& value) {
        QFile file(path); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        REQUIRE(file.write(value) == value.size());
    }
    static QByteArray read(const QString& path) {
        QFile file(path); REQUIRE(file.open(QIODevice::ReadOnly)); return file.readAll();
    }
    bool save(const std::function<bool()>& cancel = {}) {
        return saveVerifiedStage(cache.path(), feed, selected, publicKey, source, cancel);
    }
    bool restore(const std::function<bool()>& cancel = {}) {
        return restoreVerifiedStage(cache.path(), feed, selected, publicKey, destination, cancel);
    }
};
}

#ifndef _WIN32
TEST_CASE("Verified update cache restores complete bytes only under a fresh matching signed feed", "[updates][update-stage-cache]") {
    CacheFixture f;
    REQUIRE(f.save());
    REQUIRE(QFile::remove(f.source)); // No live source object or archive survives the simulated restart.
    REQUIRE(f.restore());
    CHECK(CacheFixture::read(f.destination) == f.bytes);
    CHECK(verifyUpdateArchive(f.destination, {f.selected.offer.size, f.selected.offer.sha256,
        f.selected.archiveSignatureBase64}, f.publicKey));
    CHECK(QDir(f.cache.path()).entryList(QDir::Files | QDir::Hidden).size() == 2);
}

TEST_CASE("Update cache rejects unsigned partial mixed and mismatched restored entries", "[updates][update-stage-cache]") {
    CacheFixture f; REQUIRE(f.save());
    SECTION("changed archive") { CacheFixture::write(f.cache.filePath("update.zip"), QByteArray(f.bytes.size(), 'X')); }
    SECTION("truncated archive") { CacheFixture::write(f.cache.filePath("update.zip"), f.bytes.left(4)); }
    SECTION("missing archive") { REQUIRE(QFile::remove(f.cache.filePath("update.zip"))); }
    SECTION("missing feed") { REQUIRE(QFile::remove(f.cache.filePath("appcast.xml"))); }
    SECTION("changed cached feed") { CacheFixture::write(f.cache.filePath("appcast.xml"), f.feed + "tampered"); }
    SECTION("mixed newer feed") { CacheFixture::write(f.cache.filePath("appcast.xml"), f.makeFeed(3)); }
    SECTION("changed currently pinned key") { f.publicKey = QByteArray(32, 'Z').toBase64(); }
    SECTION("untrusted current feed") { f.feed += "tampered"; }
    SECTION("wrong current build") { ++f.selected.offer.build; }
    SECTION("wrong current version") { f.selected.offer.version = "3.0.0-pre.99"; }
    SECTION("wrong current notes") { f.selected.offer.notes += "changed"; }
    SECTION("wrong current channel") { f.selected.offer.channel = Channel::Preview; }
    SECTION("wrong current CPU") { f.selected.offer.cpu = "x86_64"; }
    SECTION("wrong current bundle") { f.selected.offer.bundleId = "other.application"; }
    SECTION("wrong current minimum OS") { f.selected.offer.minimumOs = QVersionNumber(99); }
    SECTION("wrong current archive URL") { f.selected.archiveUrl = QUrl("https://github.com/Delitants/eiskaltdcpp/releases/download/v3/other.zip"); }
    SECTION("wrong current signature") { f.selected.archiveSignatureBase64 = QByteArray(64, 'q').toBase64(); }
    SECTION("empty selected digest") { f.selected.offer.sha256.clear(); }
    SECTION("wrong current size") { ++f.selected.offer.size; }
    CHECK_FALSE(f.restore());
    CHECK_FALSE(QFile::exists(f.destination));
}

TEST_CASE("Cache publication refuses unverified or canceled source bytes without replacing a valid entry", "[updates][update-stage-cache]") {
    CacheFixture f; REQUIRE(f.save());
    const auto oldFeed = CacheFixture::read(f.cache.filePath("appcast.xml"));
    const auto oldArchive = CacheFixture::read(f.cache.filePath("update.zip"));
    bool cancellationCase = false;
    SECTION("bad source") { CacheFixture::write(f.source, QByteArray(f.bytes.size(), 'X')); }
    SECTION("bad feed") { f.feed += "tampered"; }
    SECTION("bad selection") { ++f.selected.offer.build; }
    SECTION("already canceled") { cancellationCase = true; CHECK_FALSE(f.save([] { return true; })); }
    SECTION("throwing cancellation") { cancellationCase = true; CHECK_FALSE(f.save([]() -> bool { throw 7; })); }
    if(!cancellationCase) CHECK_FALSE(f.save());
    CHECK(CacheFixture::read(f.cache.filePath("appcast.xml")) == oldFeed);
    CHECK(CacheFixture::read(f.cache.filePath("update.zip")) == oldArchive);
}

TEST_CASE("Update cache respects private roots regular files and a fresh destination", "[updates][update-stage-cache]") {
    CacheFixture f; REQUIRE(f.save());
    SECTION("existing destination") { CacheFixture::write(f.destination, "keep"); }
    SECTION("symlink cached archive") {
        REQUIRE(QFile::remove(f.cache.filePath("update.zip")));
        REQUIRE(QFile::link(f.source, f.cache.filePath("update.zip")));
    }
    SECTION("symlink cached feed") {
        const auto other = f.stage.filePath("other-feed"); CacheFixture::write(other, f.feed);
        REQUIRE(QFile::remove(f.cache.filePath("appcast.xml")));
        REQUIRE(QFile::link(other, f.cache.filePath("appcast.xml")));
    }
    SECTION("hardlinked cached archive") {
        REQUIRE(::link(QFile::encodeName(f.cache.filePath("update.zip")).constData(),
                       QFile::encodeName(f.stage.filePath("hardlink")).constData()) == 0);
    }
    SECTION("shared cached archive") { REQUIRE(QFile::setPermissions(f.cache.filePath("update.zip"), QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther)); }
    SECTION("shared cached feed") { REQUIRE(QFile::setPermissions(f.cache.filePath("appcast.xml"), QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup)); }
    SECTION("shared cache root") { REQUIRE(QFile::setPermissions(f.cache.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther)); }
    SECTION("shared destination root") { REQUIRE(QFile::setPermissions(f.restored.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther)); }
    SECTION("symlink destination") { REQUIRE(QFile::link(f.source, f.destination)); }
    SECTION("directory instead of archive") { REQUIRE(QFile::remove(f.cache.filePath("update.zip"))); REQUIRE(QDir().mkdir(f.cache.filePath("update.zip"))); }
    SECTION("FIFO instead of archive") {
        REQUIRE(QFile::remove(f.cache.filePath("update.zip")));
        REQUIRE(::mkfifo(QFile::encodeName(f.cache.filePath("update.zip")).constData(), 0600) == 0);
    }
    CHECK_FALSE(f.restore());
    if(QFile::exists(f.destination) && !QFileInfo(f.destination).isSymLink()) CHECK(CacheFixture::read(f.destination) == "keep");
}

TEST_CASE("Canceled cache restoration publishes no partial file", "[updates][update-stage-cache]") {
    CacheFixture f; REQUIRE(f.save());
    SECTION("early") { CHECK_FALSE(f.restore([] { return true; })); }
    SECTION("during copy or verification") {
        int calls = 0; CHECK_FALSE(f.restore([&] { return ++calls >= 5; })); CHECK(calls >= 5);
    }
    SECTION("throwing callback") { CHECK_FALSE(f.restore([]() -> bool { throw 7; })); }
    CHECK(QDir(f.restored.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Cache refuses unsafe publication roots and never modifies link targets", "[updates][update-stage-cache]") {
    CacheFixture f;
    QTemporaryDir foreign;
    const auto sentinel = foreign.filePath("preserve"); CacheFixture::write(sentinel, "untouched");
    SECTION("symlink cache root") {
        const auto link = f.stage.filePath("cache-link"); REQUIRE(QFile::link(f.cache.path(), link));
        CHECK_FALSE(saveVerifiedStage(link, f.feed, f.selected, f.publicKey, f.source));
    }
    SECTION("shared cache root") {
        REQUIRE(QFile::setPermissions(f.cache.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther));
        CHECK_FALSE(f.save());
    }
    SECTION("symlink archive entry") {
        REQUIRE(QFile::link(sentinel, f.cache.filePath("update.zip"))); CHECK_FALSE(f.save());
    }
    SECTION("symlink feed entry") {
        REQUIRE(QFile::link(sentinel, f.cache.filePath("appcast.xml"))); CHECK_FALSE(f.save());
    }
    SECTION("hardlink entry") {
        REQUIRE(::link(QFile::encodeName(sentinel).constData(), QFile::encodeName(f.cache.filePath("appcast.xml")).constData()) == 0);
        CHECK_FALSE(f.save());
    }
    SECTION("symlink source") {
        const auto link = f.stage.filePath("source-link"); REQUIRE(QFile::link(f.source, link));
        CHECK_FALSE(saveVerifiedStage(f.cache.path(), f.feed, f.selected, f.publicKey, link));
    }
    SECTION("relative cache root") { CHECK_FALSE(saveVerifiedStage("relative", f.feed, f.selected, f.publicKey, f.source)); }
    SECTION("missing cache root") { CHECK_FALSE(saveVerifiedStage(f.cache.filePath("missing"), f.feed, f.selected, f.publicKey, f.source)); }
    CHECK(CacheFixture::read(sentinel) == "untouched");
    CHECK_FALSE((QFile::exists(f.cache.filePath("appcast.xml")) && QFile::exists(f.cache.filePath("update.zip"))));
}

TEST_CASE("Update cache aborts if its output root is replaced while copying", "[updates][update-stage-cache]") {
    CacheFixture f; REQUIRE(f.save());
    QTemporaryDir foreign;
    const auto sentinel = foreign.filePath("keep"); CacheFixture::write(sentinel, "untouched");
    const auto original = f.restored.path(), moved = original + "-moved";
    bool replaced = false;
    const bool restored = f.restore([&] {
        if(!replaced && !QDir(original).entryList({".stage-*"}, QDir::Files | QDir::Hidden).isEmpty()) {
            REQUIRE(::rename(QFile::encodeName(original).constData(), QFile::encodeName(moved).constData()) == 0);
            REQUIRE(::symlink(QFile::encodeName(foreign.path()).constData(), QFile::encodeName(original).constData()) == 0);
            replaced = true;
        }
        return false;
    });
    CHECK(replaced); CHECK_FALSE(restored);
    CHECK(CacheFixture::read(sentinel) == "untouched");
    CHECK_FALSE(QFile::exists(foreign.filePath("update.zip")));
    if(replaced) {
        REQUIRE(::unlink(QFile::encodeName(original).constData()) == 0);
        REQUIRE(::rename(QFile::encodeName(moved).constData(), QFile::encodeName(original).constData()) == 0);
    }
    CHECK(QDir(original).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Interrupted cache publication leaves at most two reserved temporary slots", "[updates][update-stage-cache]") {
    CacheFixture f; REQUIRE(f.save());
    CacheFixture::write(f.cache.filePath(".stage-archive"), "interrupted archive");
    CacheFixture::write(f.cache.filePath(".stage-feed"), "interrupted feed");
    REQUIRE(f.save());
    CHECK(QDir(f.cache.path()).entryList(QDir::Files | QDir::Hidden).size() == 2);
    REQUIRE(f.restore());
    CHECK(CacheFixture::read(f.destination) == f.bytes);
}

TEST_CASE("An active cache writer excludes another reader or writer", "[updates][update-stage-cache]") {
    CacheFixture f; REQUIRE(f.save());
    const int fd = ::open(QFile::encodeName(f.cache.path()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    REQUIRE(::flock(fd, LOCK_EX | LOCK_NB) == 0);
    CHECK_FALSE(f.save()); CHECK_FALSE(f.restore());
    CHECK(::close(fd) == 0);
    REQUIRE(f.restore());
    CHECK(CacheFixture::read(f.destination) == f.bytes);
}
#endif
