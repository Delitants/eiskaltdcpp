#include "updates/UpdateArchiveVerifier.h"
#include <catch2/catch_test_macros.hpp>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <openssl/evp.h>
#include <memory>
#include <stdexcept>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace {
struct Fixture {
    QTemporaryDir root;
    QByteArray bytes = QByteArray("PK\003\004signed archive fixture\0with binary", 37);
    QByteArray publicKey;
    updates::ArchiveExpectation expected;
    QString path = root.filePath("update.zip");
    Fixture() {
        REQUIRE(root.isValid());
        QByteArray seed(32, '\x42');
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
            EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                reinterpret_cast<const unsigned char*>(seed.constData()), seed.size()), EVP_PKEY_free);
        REQUIRE(key);
        QByteArray rawKey(32, '\0'); size_t keySize = 32;
        REQUIRE(EVP_PKEY_get_raw_public_key(key.get(), reinterpret_cast<unsigned char*>(rawKey.data()), &keySize) == 1);
        publicKey = rawKey.toBase64();
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        REQUIRE(ctx);
        REQUIRE(EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1);
        QByteArray signature(64, '\0'); size_t size = 64;
        REQUIRE(EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &size,
            reinterpret_cast<const unsigned char*>(bytes.constData()), bytes.size()) == 1);
        expected = {bytes.size(), QCryptographicHash::hash(bytes, QCryptographicHash::Sha256), signature.toBase64()};
        write(bytes);
    }
    void write(const QByteArray& data) {
        QFile file(path); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        REQUIRE(file.write(data) == data.size());
    }
    bool verify(const std::function<bool()>& cancel = {}) const {
        return updates::verifyUpdateArchive(path, expected, publicKey, cancel);
    }
};
}

#ifndef _WIN32
TEST_CASE("Staged updater archive requires both Ed25519 and exact size digest", "[updates][update-archive]") {
    Fixture f;
    REQUIRE(f.verify());
    SECTION("changed bytes even with matching attacker supplied hash") {
        f.bytes[5] ^= 1; f.write(f.bytes);
        f.expected.sha256 = QCryptographicHash::hash(f.bytes, QCryptographicHash::Sha256);
    }
    SECTION("truncated") { f.write(f.bytes.left(f.bytes.size() - 1)); }
    SECTION("trailing bytes") { f.write(f.bytes + "x"); }
    SECTION("wrong length") { ++f.expected.size; }
    SECTION("wrong digest") { f.expected.sha256[0] ^= 1; }
    SECTION("wrong signature") {
        auto signature = QByteArray::fromBase64(f.expected.signatureBase64);
        signature[0] ^= 1; f.expected.signatureBase64 = signature.toBase64();
    }
    SECTION("wrong pinned key") { f.publicKey = QByteArray(32, '\0').toBase64(); }
    SECTION("missing file") { REQUIRE(QFile::remove(f.path)); }
    CHECK_FALSE(f.verify());
}

TEST_CASE("Updater rejects malformed signature metadata before reading an archive", "[updates][update-archive]") {
    Fixture f;
    REQUIRE(f.verify());
    SECTION("zero bytes") { f.expected.size = 0; }
    SECTION("negative bytes") { f.expected.size = -1; }
    SECTION("over maximum") { f.expected.size = 512LL * 1024 * 1024 + 1; }
    SECTION("missing digest") { f.expected.sha256.clear(); }
    SECTION("hex rather than raw digest") { f.expected.sha256 = f.expected.sha256.toHex(); }
    SECTION("missing signature") { f.expected.signatureBase64.clear(); }
    SECTION("short signature") { f.expected.signatureBase64.chop(4); }
    SECTION("signature whitespace") { f.expected.signatureBase64.prepend(' '); }
    SECTION("signature invalid character") { f.expected.signatureBase64[0] = '!'; }
    SECTION("noncanonical padding bits") { f.expected.signatureBase64[85] = 'B'; }
    SECTION("missing key") { f.publicKey.clear(); }
    SECTION("wrong length key") { f.publicKey = QByteArray(31, 'a').toBase64(); }
    SECTION("key whitespace") { f.publicKey.append('\n'); }
    CHECK_FALSE(f.verify());
}

TEST_CASE("Updater archive verification respects cancellation and preserves staged bytes", "[updates][update-archive]") {
    Fixture f;
    REQUIRE(f.verify());
    SECTION("already cancelled") { CHECK_FALSE(f.verify([] { return true; })); }
    SECTION("cancel during verification") {
        int calls = 0;
        CHECK_FALSE(f.verify([&] { return ++calls >= 3; }));
        CHECK(calls >= 3);
    }
    SECTION("throwing cancellation callback fails closed") {
        CHECK_FALSE(f.verify([]() -> bool { throw std::runtime_error("private diagnostic"); }));
    }
    QFile file(f.path); REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == f.bytes);
}

TEST_CASE("Updater verifies only private regular staging files", "[updates][update-archive]") {
    Fixture f;
    REQUIRE(f.verify());
    SECTION("symlink") {
        const auto link = f.root.filePath("link.zip");
        REQUIRE(QFile::link(f.path, link)); f.path = link;
    }
    SECTION("directory") { f.path = f.root.path(); }
    SECTION("shared file") { REQUIRE(QFile::setPermissions(f.path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther)); }
    SECTION("shared parent") { REQUIRE(QFile::setPermissions(f.root.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther)); }
#ifndef _WIN32
    SECTION("hardlinked file") {
        REQUIRE(::link(QFile::encodeName(f.path).constData(), QFile::encodeName(f.root.filePath("alias.zip")).constData()) == 0);
    }
    SECTION("FIFO without blocking") {
        REQUIRE(QFile::remove(f.path));
        REQUIRE(::mkfifo(QFile::encodeName(f.path).constData(), 0600) == 0);
    }
#endif
    CHECK_FALSE(f.verify());
}

TEST_CASE("Staging pathname replacement invalidates archive verification", "[updates][update-archive]") {
    Fixture f;
    QTemporaryDir displaced;
    REQUIRE(displaced.isValid());
    int calls = 0;
    bool replaced = false;
    const auto replace = [&] {
        if(++calls == 4) {
            SECTION("file replaced with identical bytes") {
                replaced = QFile::rename(f.path, displaced.filePath("old.zip"));
                f.write(f.bytes);
            }
            SECTION("parent directory replaced") {
                replaced = QDir().rename(f.root.path(), displaced.filePath("old"));
                REQUIRE(QDir().mkdir(f.root.path()));
            }
        }
        return false;
    };
    CHECK_FALSE(f.verify(replace));
    CHECK(replaced);
}

TEST_CASE("OpenSSL updater verification accepts the pinned Sparkle signer archive", "[.][updates][update-archive-sparkle]") {
    const auto directory = qEnvironmentVariable("EISKALT_UPDATER_SIGNATURE_FIXTURE");
    if(directory.isEmpty()) SKIP("Opt-in: set EISKALT_UPDATER_SIGNATURE_FIXTURE to the pinned signed Sparkle fixture");
    QDir source(directory);
    QFile manifest(source.filePath("fixture.json")); REQUIRE(manifest.open(QIODevice::ReadOnly));
    const auto object = QJsonDocument::fromJson(manifest.readAll()).object();
    QTemporaryDir staging; REQUIRE(staging.isValid());
    const auto path = staging.filePath("update.zip");
    REQUIRE(QFile::copy(source.filePath("update.zip"), path));
    REQUIRE(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
    updates::ArchiveExpectation expected{
        QFileInfo(path).size(),
        QByteArray::fromHex(object["files"].toObject()["update.zip"].toString().toLatin1()),
        object["signature"].toString().toLatin1()};
    const auto key = object["public_key"].toString().toLatin1();
    REQUIRE(updates::verifyUpdateArchive(path, expected, key));
    REQUIRE(QFile::remove(path));
    REQUIRE(QFile::copy(source.filePath("bad-archive.zip"), path));
    REQUIRE(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
    expected.sha256 = QByteArray::fromHex(object["files"].toObject()["bad-archive.zip"].toString().toLatin1());
    CHECK_FALSE(updates::verifyUpdateArchive(path, expected, key));
}

TEST_CASE("Updater accepts a signed exact 512 MiB archive but rejects limit plus one", "[.][updates][update-archive-limits]") {
    Fixture f;
    constexpr qint64 limit = 512LL * 1024 * 1024;
    QFile file(f.path); REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.resize(limit));
    auto* bytes = file.map(0, limit); REQUIRE(bytes);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(qint64 offset = 0; offset < limit; offset += 65536)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(bytes + offset), 65536));
    QByteArray seed(32, '\x42');
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
            reinterpret_cast<const unsigned char*>(seed.constData()), seed.size()), EVP_PKEY_free);
    REQUIRE(key);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    REQUIRE(ctx);
    REQUIRE(EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1);
    QByteArray signature(64, '\0'); size_t signatureSize = 64;
    REQUIRE(EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &signatureSize,
        bytes, limit) == 1);
    f.expected = {limit, hash.result(), signature.toBase64()};
    REQUIRE(file.unmap(bytes)); file.close();
    CHECK(f.verify());
    REQUIRE(file.open(QIODevice::ReadWrite)); REQUIRE(file.resize(limit + 1)); file.close();
    f.expected.size = limit + 1;
    int callbacks = 0;
    CHECK_FALSE(f.verify([&] { ++callbacks; return false; }));
    CHECK(callbacks == 1);
}
#else
TEST_CASE("Windows archive verification fails closed until private staging ACL support", "[updates][update-archive]") {
    Fixture f;
    CHECK_FALSE(f.verify());
}
#endif
