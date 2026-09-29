#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
// Like the managed-share fixture, assemble only the managers needed here.
#define private public
#include "DCContext.h"
#undef private
#include "ClientManager.h"
#include "CryptoManager.h"
#include "File.h"
#include "TimerManager.h"
#include "Util.h"

#include <chrono>
#include <filesystem>

using namespace dcpp;

namespace {

void freeBio(BIO* bio) { BIO_free(bio); }
using ScopedBio = ssl::scoped_handle<BIO, freeBio>;

bool matchesLifetimePolicy(int days, int seconds, time_t generationStart, time_t generationEnd)
{
    return days == 365 && seconds >= 0 &&
        seconds <= std::difftime(generationEnd, generationStart);
}

struct CertificateFixture {
    DCContext context;
    Util::PathsMap previousPaths;
    std::filesystem::path temp;
    string certificatePath;
    string keyPath;

    CertificateFixture()
    {
        temp = std::filesystem::temp_directory_path() /
            ("eiskalt_certificate_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp);
        Util::PathsMap paths;
        for(int i = 0; i < Util::PATH_LAST; ++i) {
            const auto path = static_cast<Util::Paths>(i);
            previousPaths[path] = Util::getPath(path);
            paths[path] = temp.string() + PATH_SEPARATOR;
        }
        Util::uninitialize();
        Util::initialize(paths);
        context.startupMinimal();
        context.getSettingsManager()->set(SettingsManager::PRIVATE_ID, CID::generate().toBase32());
        context.timerManager_ = std::make_unique<TimerManager>(context);
        context.clientManager_ = std::make_unique<ClientManager>(context);
        context.cryptoManager_ = std::make_unique<CryptoManager>(context);
        certificatePath = (temp / "identity.crt").string();
        keyPath = (temp / "identity.key").string();
        context.getSettingsManager()->set(SettingsManager::TLS_CERTIFICATE_FILE, certificatePath);
        context.getSettingsManager()->set(SettingsManager::TLS_PRIVATE_KEY_FILE, keyPath);
        context.getSettingsManager()->set(SettingsManager::TLS_TRUSTED_CERTIFICATES_PATH,
            (temp / "trusted").string() + PATH_SEPARATOR);
    }

    ~CertificateFixture()
    {
        context.shutdown();
        if(!previousPaths[Util::PATH_USER_CONFIG].empty())
            Util::initialize(previousPaths);
        std::error_code error;
        std::filesystem::remove_all(temp, error);
    }

    CryptoManager& crypto() { return *context.getCryptoManager(); }
    string read(const string& path) { return File(path, File::READ, File::OPEN).read(); }

    ssl::X509 certificate()
    {
        const auto pem = read(certificatePath);
        ScopedBio bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
        return ssl::X509(PEM_read_bio_X509(bio, nullptr, nullptr, nullptr));
    }

    void remainingDays(int days, bool malformed = false, bool zeroSerial = false)
    {
        auto cert = certificate();
        REQUIRE(cert);
        if(malformed) {
            REQUIRE(ASN1_STRING_set(X509_get_notAfter(cert), "invalid", 7) == 1);
        } else {
            REQUIRE(X509_gmtime_adj(X509_get_notAfter(cert), static_cast<long>(days) * 86400));
        }
        if(zeroSerial) REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(cert), 0));
        const auto pem = read(keyPath);
        ScopedBio keyBio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
        ssl::EVP_PKEY key(PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr));
        REQUIRE(key);
        REQUIRE(X509_sign(cert, key, EVP_sha256()) > 0);
        ScopedBio output(BIO_new(BIO_s_mem()));
        REQUIRE(PEM_write_bio_X509(output, cert) == 1);
        BUF_MEM* memory = nullptr;
        BIO_get_mem_ptr(output, &memory);
        File(certificatePath, File::WRITE, File::OPEN | File::TRUNCATE)
            .write(string(memory->data, memory->length));
    }
};

} // namespace

TEST_CASE("Fresh DC profiles allow TLS and initialize identity without GOST CA", "[certificate-lifecycle]")
{
    CertificateFixture fixture;
    auto& settings = *fixture.context.getSettingsManager();
    CHECK(settings.getBool(SettingsManager::USE_TLS));
    CHECK_FALSE(settings.getBool(SettingsManager::REQUIRE_TLS));
    CHECK(settings.get(SettingsManager::GOST_CA_FILE).empty());
    CHECK_FALSE(std::filesystem::exists(fixture.certificatePath));
    fixture.crypto().loadCertificates();
    REQUIRE(fixture.crypto().TLSOk());
    CHECK(std::filesystem::exists(fixture.keyPath));
    CHECK(settings.get(SettingsManager::GOST_CA_FILE).empty());
    settings.set(SettingsManager::REQUIRE_TLS, true);
    settings.save((fixture.temp / "saved.xml").string());
    settings.set(SettingsManager::REQUIRE_TLS, false);
    settings.load((fixture.temp / "saved.xml").string());
    CHECK(settings.getBool(SettingsManager::REQUIRE_TLS));
}

TEST_CASE("Detached certificate generation preserves active identity", "[certificate-lifecycle]")
{
    CertificateFixture fixture;
    fixture.crypto().loadCertificates();
    const auto oldCert = fixture.read(fixture.certificatePath);
    const auto oldKey = fixture.read(fixture.keyPath);
    const auto cid = fixture.context.getClientManager()->getMyCID().toBase32();
    const auto pem = CryptoManager::createIdentityPem(cid);
    CHECK(CryptoManager::identityPemMatches(pem.first, pem.second, cid));
    CHECK_FALSE(CryptoManager::identityPemMatches(pem.first, oldKey, cid));
    CHECK_FALSE(CryptoManager::identityPemMatches(pem.first, pem.second, "different identity"));
    CHECK_FALSE(CryptoManager::identityPemMatches("invalid", pem.second, cid));
    CHECK(fixture.read(fixture.certificatePath) == oldCert);
    CHECK(fixture.read(fixture.keyPath) == oldKey);
}

TEST_CASE("Unwritable identity locations fail without escaping startup", "[certificate-lifecycle]")
{
    CertificateFixture fixture;
    const auto blocker = (fixture.temp / "not-a-folder").string();
    File(blocker, File::WRITE, File::CREATE | File::TRUNCATE).write("keep");
    fixture.context.getSettingsManager()->set(SettingsManager::TLS_PRIVATE_KEY_FILE, blocker + "/key");
    REQUIRE_NOTHROW(fixture.crypto().loadCertificates());
    CHECK_FALSE(fixture.crypto().TLSOk());
    CHECK(fixture.read(blocker) == "keep");
}

TEST_CASE("Imports satisfy startup certificate acceptance", "[certificate-lifecycle][certificate-review]")
{
    CertificateFixture fixture;
    fixture.crypto().generateCertificate();
    const auto cid = fixture.context.getClientManager()->getMyCID().toBase32();
    SECTION("Near expiry cannot be imported and silently replaced on restart") {
        fixture.remainingDays(30);
        CHECK_FALSE(fixture.crypto().checkCertificate());
        CHECK_FALSE(CryptoManager::identityPemMatches(fixture.read(fixture.certificatePath), fixture.read(fixture.keyPath), cid));
    }
    SECTION("Zero serial cannot be imported") {
        // Re-sign to refresh OpenSSL's cached DER after changing the serial.
        fixture.remainingDays(200, false, true);
        REQUIRE(ASN1_INTEGER_get(X509_get_serialNumber(fixture.certificate())) == 0);
        CHECK_FALSE(fixture.crypto().checkCertificate());
        CHECK_FALSE(CryptoManager::identityPemMatches(fixture.read(fixture.certificatePath), fixture.read(fixture.keyPath), cid));
    }
}

TEST_CASE("Certificate lifetime assertion allows only observed clock boundaries", "[certificate-lifecycle]")
{
    CHECK(matchesLifetimePolicy(365, 0, 100, 100));
    CHECK(matchesLifetimePolicy(365, 1, 100, 101));
    CHECK_FALSE(matchesLifetimePolicy(365, 1, 100, 100));
    CHECK_FALSE(matchesLifetimePolicy(365, 2, 100, 101));
    CHECK_FALSE(matchesLifetimePolicy(364, 0, 100, 101));
    CHECK_FALSE(matchesLifetimePolicy(366, 0, 100, 101));
    CHECK_FALSE(matchesLifetimePolicy(365, -1, 100, 101));
}

TEST_CASE("DC identity certificates last one year", "[certificate-lifecycle]")
{
    CertificateFixture fixture;
    const auto generationStart = std::time(nullptr);
    fixture.crypto().generateCertificate();
    const auto generationEnd = std::time(nullptr);
    auto cert = fixture.certificate();
    REQUIRE(cert);
    int days = 0, seconds = 0;
    REQUIRE(ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(cert), X509_get0_notAfter(cert)) == 1);
    CHECK(matchesLifetimePolicy(days, seconds, generationStart, generationEnd));
    CHECK(fixture.crypto().checkCertificate());
}

TEST_CASE("DC identity certificates renew before expiry", "[certificate-lifecycle]")
{
    CertificateFixture fixture;
    fixture.crypto().generateCertificate();
    fixture.remainingDays(91);
    CHECK(fixture.crypto().checkCertificate());
    fixture.crypto().loadCertificates();
    REQUIRE(fixture.crypto().TLSOk());
    const auto previousKeyprint = fixture.crypto().getKeyprint();
    fixture.remainingDays(89);
    CHECK_FALSE(fixture.crypto().checkCertificate());
    fixture.crypto().loadCertificates();
    CHECK(fixture.crypto().TLSOk());
    CHECK(fixture.crypto().checkCertificate());
    CHECK(fixture.crypto().getKeyprint() != previousKeyprint);
    auto cert = fixture.certificate();
    REQUIRE(cert);
    int days = 0, seconds = 0;
    REQUIRE(ASN1_TIME_diff(&days, &seconds, nullptr, X509_get0_notAfter(cert)) == 1);
    CHECK(days >= 364);
}

TEST_CASE("Healthy DC identity and keyprint survive repeated loads", "[certificate-lifecycle]")
{
    CertificateFixture fixture;
    fixture.crypto().generateCertificate();
    fixture.remainingDays(200);
    fixture.crypto().loadCertificates();
    REQUIRE(fixture.crypto().TLSOk());
    const auto certificate = fixture.read(fixture.certificatePath);
    const auto key = fixture.read(fixture.keyPath);
    const auto keyprint = fixture.crypto().getKeyprint();
    fixture.crypto().loadCertificates();
    CHECK(fixture.read(fixture.certificatePath) == certificate);
    CHECK(fixture.read(fixture.keyPath) == key);
    CHECK(fixture.crypto().getKeyprint() == keyprint);
}

TEST_CASE("Expired or malformed DC certificate validity is rejected", "[certificate-lifecycle]")
{
    CertificateFixture fixture;
    fixture.crypto().generateCertificate();
    SECTION("expired") { fixture.remainingDays(-1); }
    SECTION("invalid time encoding") { fixture.remainingDays(0, true); }
    CHECK_FALSE(fixture.crypto().checkCertificate());
}
