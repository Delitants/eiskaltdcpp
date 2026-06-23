#include <catch2/catch_test_macros.hpp>

#include "dcpp/XChaCha20Poly1305.h"

#include <cstdint>
#include <string>

using namespace dcpp;

namespace {

ByteVector fromHex(const std::string& hex)
{
    ByteVector result;
    result.reserve(hex.size() / 2);
    for(size_t i = 0; i < hex.size(); i += 2) {
        result.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return result;
}

ByteVector sequence(uint8_t first, size_t size)
{
    ByteVector result(size);
    for(size_t i = 0; i < size; ++i) {
        result[i] = static_cast<uint8_t>(first + i);
    }
    return result;
}

} // namespace

TEST_CASE("HChaCha20 matches the published draft vector", "[xchacha20]")
{
    const ByteVector nonce = fromHex("000000090000004a0000000031415927");

    REQUIRE(XChaCha20Poly1305::hChaCha20(sequence(0x00, 32), nonce) ==
        fromHex("82413b4227b27bfed30e42508a877d73a0f9e4d58a74a853c12ec41326d3ecdc"));
}

TEST_CASE("XChaCha20-Poly1305 matches the published AEAD vector", "[xchacha20]")
{
    const ByteVector key = sequence(0x80, 32);
    const ByteVector nonce = sequence(0x40, 24);
    const ByteVector aad = fromHex("50515253c0c1c2c3c4c5c6c7");
    const std::string message =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    const ByteVector plaintext(message.begin(), message.end());
    const ByteVector expectedCiphertext = fromHex(
        "bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb"
        "731c7f1b0b4aa6440bf3a82f4eda7e39ae64c6708c54c216cb96b72e1213b452"
        "2f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b2383565d3fff9"
        "21f9664c97637da9768812f615c68b13b52e");
    const ByteVector expectedTag = fromHex("c0875924c1c7987947deafd8780acf49");

    ByteVector ciphertext;
    ByteVector tag;
    REQUIRE(XChaCha20Poly1305::encrypt(key, nonce, plaintext, aad, ciphertext, tag));
    REQUIRE(ciphertext == expectedCiphertext);
    REQUIRE(tag == expectedTag);

    ByteVector decrypted;
    REQUIRE(XChaCha20Poly1305::decrypt(key, nonce, ciphertext, aad, tag, decrypted));
    REQUIRE(decrypted == plaintext);
}

TEST_CASE("XChaCha20-Poly1305 rejects tampering without releasing plaintext", "[xchacha20]")
{
    const ByteVector key = sequence(0x10, 32);
    const ByteVector nonce = sequence(0x30, 24);
    const ByteVector plaintext = fromHex("00112233445566778899aabbccddeeff");
    const ByteVector aad = fromHex("a0a1a2a3");

    ByteVector ciphertext;
    ByteVector tag;
    REQUIRE(XChaCha20Poly1305::encrypt(key, nonce, plaintext, aad, ciphertext, tag));

    ByteVector output = { 0xAA };
    ciphertext[0] ^= 0x01;
    REQUIRE_FALSE(XChaCha20Poly1305::decrypt(key, nonce, ciphertext, aad, tag, output));
    REQUIRE(output.empty());

    ciphertext[0] ^= 0x01;
    tag.back() ^= 0x80;
    output = { 0xBB };
    REQUIRE_FALSE(XChaCha20Poly1305::decrypt(key, nonce, ciphertext, aad, tag, output));
    REQUIRE(output.empty());

    tag.back() ^= 0x80;
    ByteVector differentNonce = nonce;
    differentNonce.back() ^= 0x01;
    output = { 0xCC };
    REQUIRE_FALSE(XChaCha20Poly1305::decrypt(key, differentNonce, ciphertext, aad, tag, output));
    REQUIRE(output.empty());
}
