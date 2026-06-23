#include <catch2/catch_test_macros.hpp>

#if __has_include("dcpp/Shadowsocks2022.h")
#include "dcpp/Shadowsocks2022.h"
#define EISKALTDCPP_HAS_SHADOWSOCKS2022 1
#endif

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#if EISKALTDCPP_HAS_SHADOWSOCKS2022

using namespace dcpp;

namespace {

ByteVector sequence(uint8_t first, size_t size)
{
    ByteVector result(size);
    for(size_t i = 0; i < size; ++i) {
        result[i] = static_cast<uint8_t>(first + i);
    }
    return result;
}

ByteVector fromHex(const std::string& hex)
{
    ByteVector result;
    result.reserve(hex.size() / 2);
    for(size_t i = 0; i < hex.size(); i += 2) {
        result.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return result;
}

} // namespace

TEST_CASE("Shadowsocks 2022 parses the three supported methods", "[shadowsocks2022][kdf]")
{
    REQUIRE(Shadowsocks2022::parseMethod("2022-blake3-aes-128-gcm") ==
        Shadowsocks2022::Method::Blake3Aes128Gcm);
    REQUIRE(Shadowsocks2022::parseMethod("2022-blake3-aes-256-gcm") ==
        Shadowsocks2022::Method::Blake3Aes256Gcm);
    REQUIRE(Shadowsocks2022::parseMethod("2022-blake3-chacha20-poly1305") ==
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305);
    REQUIRE(Shadowsocks2022::keySize(Shadowsocks2022::Method::Blake3Aes128Gcm) == 16);
    REQUIRE(Shadowsocks2022::keySize(Shadowsocks2022::Method::Blake3Aes256Gcm) == 32);
    REQUIRE(Shadowsocks2022::keySize(Shadowsocks2022::Method::Blake3ChaCha20Poly1305) == 32);
    REQUIRE_THROWS_AS(Shadowsocks2022::parseMethod("aes-128-gcm"), std::invalid_argument);
    REQUIRE_THROWS_AS(Shadowsocks2022::parseMethod("2022-BLAKE3-aes-128-gcm"), std::invalid_argument);
}

TEST_CASE("Shadowsocks 2022 parses identity PSKs before the user PSK", "[shadowsocks2022][kdf]")
{
    const auto aes128 = Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3Aes128Gcm,
        "EBESExQVFhcYGRobHB0eHw==:AAECAwQFBgcICQoLDA0ODw==");
    REQUIRE(aes128.identityPsks == std::vector<ByteVector>{sequence(0x10, 16)});
    REQUIRE(aes128.userPsk == sequence(0x00, 16));

    const auto aes256 = Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3Aes256Gcm,
        "ICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj8=:"
        "QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl8=:"
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");
    REQUIRE(aes256.identityPsks ==
        std::vector<ByteVector>{sequence(0x20, 32), sequence(0x40, 32)});
    REQUIRE(aes256.userPsk == sequence(0x00, 32));

    const auto chacha20 = Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305,
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");
    REQUIRE(chacha20.identityPsks.empty());
    REQUIRE(chacha20.userPsk == sequence(0x00, 32));
}

TEST_CASE("Shadowsocks 2022 rejects empty and malformed PSK segments", "[shadowsocks2022][kdf]")
{
    const auto method = Shadowsocks2022::Method::Blake3Aes128Gcm;
    const std::string valid = "AAECAwQFBgcICQoLDA0ODw==";
    const std::vector<std::string> invalid = {
        "",
        ":" + valid,
        valid + ":",
        valid + "::" + valid,
        "not-base64!",
        "AAECAwQFBgcICQoLDA0ODw",
        "AAECAwQFBgcICQoLDA0ODw==\n",
        "AAAAAAAAAAAAAAAAAAAAAB=="
    };

    for(const auto& encoded : invalid) {
        CAPTURE(encoded);
        REQUIRE_THROWS_AS(Shadowsocks2022::parsePskChain(method, encoded), std::invalid_argument);
    }
}

TEST_CASE("Shadowsocks 2022 rejects wrong-sized user and identity PSKs", "[shadowsocks2022][kdf]")
{
    const std::string oneByte = "AA==";
    const std::string valid16 = "AAECAwQFBgcICQoLDA0ODw==";
    const std::string valid32 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";

    REQUIRE_THROWS_AS(Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3Aes128Gcm, oneByte), std::invalid_argument);
    REQUIRE_THROWS_AS(Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3Aes256Gcm, valid16), std::invalid_argument);
    REQUIRE_THROWS_AS(Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305, valid16), std::invalid_argument);
    REQUIRE_THROWS_AS(Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3Aes128Gcm, oneByte + ":" + valid16),
        std::invalid_argument);
    REQUIRE_THROWS_AS(Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3Aes256Gcm, oneByte + ":" + valid32),
        std::invalid_argument);
    REQUIRE_THROWS_AS(Shadowsocks2022::parsePskChain(
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305, oneByte + ":" + valid32),
        std::invalid_argument);
}

TEST_CASE("Shadowsocks 2022 derives deterministic session subkeys", "[shadowsocks2022][kdf]")
{
    // Generated independently with a Python implementation validated against
    // every derive_key case in upstream BLAKE3 1.8.5 test_vectors.json.
    REQUIRE(Shadowsocks2022::deriveSessionSubkey(
        Shadowsocks2022::Method::Blake3Aes128Gcm,
        sequence(0x00, 16), sequence(0xA0, 16)) ==
        fromHex("4d0d7016c8028969edf2d6ca7fc30b93"));

    REQUIRE(Shadowsocks2022::deriveSessionSubkey(
        Shadowsocks2022::Method::Blake3Aes256Gcm,
        sequence(0x00, 32), sequence(0x80, 32)) ==
        fromHex("11289b9d205255930f83932405c2b0a38ec32be703fe33f290ff25ffeff402f9"));

    REQUIRE(Shadowsocks2022::deriveSessionSubkey(
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305,
        sequence(0x20, 32), sequence(0x40, 32)) ==
        fromHex("f890938fdf83eaa57635296d037dbfced63cf082e5857c2104a175e8284a1b7e"));
}

#else

TEST_CASE("Shadowsocks 2022 key derivation is available", "[shadowsocks2022][kdf]")
{
    FAIL("dcpp/Shadowsocks2022.h has not been implemented");
}

#endif
