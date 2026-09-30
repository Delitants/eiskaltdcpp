#include "dcpp/stdinc.h"
#include "dcpp/HttpsTrust.h"
#include "proxy/DisposableGostServer.h"
#include <catch2/catch_test_macros.hpp>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <memory>
#include <set>
#ifdef __APPLE__
#include <Security/Security.h>
#endif

namespace {
using Context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;
using dcpp::https_trust_detail::Anchors;
Context context() { return {SSL_CTX_new(TLS_client_method()), SSL_CTX_free}; }
Certificate certificate(const std::string& pem) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(pem.data(), int(pem.size())), BIO_free);
    return {PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free};
}
std::vector<uint8_t> der(X509* cert) {
    const auto size = i2d_X509(cert, nullptr); REQUIRE(size > 0);
    std::vector<uint8_t> bytes(size); auto* cursor = bytes.data();
    REQUIRE(i2d_X509(cert, &cursor) == size); return bytes;
}
int count(SSL_CTX* ctx) { return sk_X509_OBJECT_num(X509_STORE_get0_objects(SSL_CTX_get_cert_store(ctx))); }
std::set<std::vector<uint8_t>> contents(SSL_CTX* ctx) {
    std::set<std::vector<uint8_t>> result;
    const auto objects = X509_STORE_get0_objects(SSL_CTX_get_cert_store(ctx));
    for(int i = 0; i < sk_X509_OBJECT_num(objects); ++i) {
        const auto cert = X509_OBJECT_get0_X509(sk_X509_OBJECT_value(objects, i));
        REQUIRE(cert); result.insert(der(cert));
    }
    return result;
}
}

TEST_CASE("HTTPS root import replaces the store atomically without extra trust", "[https-roots][update-transport]") {
    auto ctx = context(); REQUIRE(ctx);
    auto first = proxy_test::Identity::make(), second = proxy_test::Identity::make();
    auto cert = certificate(first.pem), other = certificate(second.pem); REQUIRE(cert); REQUIRE(other);
    REQUIRE(X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx.get()), other.get()) == 1);
    const auto bytes = der(cert.get());
    REQUIRE(dcpp::https_trust_detail::installAnchors(ctx.get(), {bytes}));
    CHECK(count(ctx.get()) == 1);
    CHECK(contents(ctx.get()) == std::set<std::vector<uint8_t>>{bytes});
    CHECK(SSL_CTX_get0_certificate(ctx.get()) == nullptr);
    REQUIRE(dcpp::https_trust_detail::installAnchors(ctx.get(), {bytes, bytes}));
    CHECK(count(ctx.get()) == 1);
}

TEST_CASE("Malformed or over-limit HTTPS anchors preserve the previous store", "[https-roots][update-transport]") {
    auto ctx = context(); REQUIRE(ctx);
    auto ca = proxy_test::Identity::make(); auto cert = certificate(ca.pem); REQUIRE(cert);
    const auto bytes = der(cert.get());
    REQUIRE(dcpp::https_trust_detail::installAnchors(ctx.get(), {bytes}));
    Anchors bad;
    SECTION("empty root set") {}
    SECTION("empty certificate") { bad = {{}}; }
    SECTION("truncated DER") { auto value = bytes; value.pop_back(); bad = {value}; }
    SECTION("trailing DER bytes") { auto value = bytes; value.push_back(0); bad = {value}; }
    SECTION("garbage after valid certificate") { bad = {bytes, {1, 2, 3}}; }
    SECTION("server leaf is not an anchor") { auto leaf = proxy_test::Identity::make(&ca); bad = {der(leaf.cert.get())}; }
    SECTION("too many anchors") { bad = Anchors(4097, bytes); }
    SECTION("oversize certificate") { bad = {std::vector<uint8_t>(65537, 0)}; }
    SECTION("too much aggregate DER") { bad = Anchors(257, std::vector<uint8_t>(65536, 0)); }
    CHECK_FALSE(dcpp::https_trust_detail::installAnchors(ctx.get(), bad));
    CHECK(contents(ctx.get()) == std::set<std::vector<uint8_t>>{bytes});
    CHECK_FALSE(dcpp::https_trust_detail::installAnchors(nullptr, {bytes}));
    CHECK_FALSE(dcpp::loadDefaultHttpsRoots(nullptr));
}

TEST_CASE("HTTPS root import accepts the exact anchor count bound", "[https-roots][update-transport]") {
    auto ctx = context(); REQUIRE(ctx);
    auto ca = proxy_test::Identity::make();
    const auto bytes = der(ca.cert.get());
    REQUIRE(dcpp::https_trust_detail::installAnchors(ctx.get(), Anchors(4096, bytes)));
    CHECK(count(ctx.get()) == 1);
}

#ifdef __APPLE__
TEST_CASE("macOS HTTPS roots match the OS default anchors without Homebrew files", "[https-roots][update-transport]") {
    auto ctx = context(); REQUIRE(ctx);
    REQUIRE(dcpp::loadDefaultHttpsRoots(ctx.get()));
    REQUIRE(count(ctx.get()) > 0);
    CFArrayRef array = nullptr;
    REQUIRE(SecTrustCopyAnchorCertificates(&array) == errSecSuccess);
    std::unique_ptr<const void, decltype(&CFRelease)> hold(array, CFRelease);
    REQUIRE(array);
    std::set<std::vector<uint8_t>> expected;
    for(CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
        const auto cert = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(array, i)));
        auto data = SecCertificateCopyData(cert); REQUIRE(data);
        expected.emplace(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
        CFRelease(data);
    }
    CHECK(contents(ctx.get()) == expected);
    CHECK(SSL_CTX_get0_certificate(ctx.get()) == nullptr);
}
#endif
