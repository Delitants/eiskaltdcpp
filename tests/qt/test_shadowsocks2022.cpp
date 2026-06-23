#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#if __has_include("dcpp/Shadowsocks2022.h")
#include "dcpp/Shadowsocks2022.h"
#define EISKALTDCPP_HAS_SHADOWSOCKS2022 1
#endif

#include <openssl/evp.h>

#include <cstdint>
#include <memory>
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

void appendU16(ByteVector& output, uint16_t value)
{
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

void appendU64(ByteVector& output, uint64_t value)
{
    for(int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<uint8_t>(value >> shift));
    }
}

ByteVector referenceSeal(Shadowsocks2022::Method method, const ByteVector& key,
    const ByteVector& nonce, const ByteVector& plaintext)
{
    const EVP_CIPHER* cipher = method == Shadowsocks2022::Method::Blake3Aes128Gcm ?
        EVP_aes_128_gcm() : method == Shadowsocks2022::Method::Blake3Aes256Gcm ?
        EVP_aes_256_gcm() : EVP_chacha20_poly1305();
    using Context = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    Context context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    REQUIRE(context);

    ByteVector output(plaintext.size() + 16);
    int outputLength = 0;
    int finalLength = 0;
    REQUIRE(EVP_EncryptInit_ex(context.get(), cipher, nullptr, nullptr, nullptr) == 1);
    REQUIRE(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1);
    REQUIRE(EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) == 1);
    REQUIRE(EVP_EncryptUpdate(context.get(), output.data(), &outputLength,
        plaintext.data(), static_cast<int>(plaintext.size())) == 1);
    REQUIRE(EVP_EncryptFinal_ex(context.get(), output.data() + outputLength, &finalLength) == 1);
    REQUIRE(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_GET_TAG, 16,
        output.data() + outputLength + finalLength) == 1);
    output.resize(static_cast<size_t>(outputLength + finalLength + 16));
    return output;
}

Shadowsocks2022TcpClient makeTcpClient(const ByteVector& userPsk,
    Shadowsocks2022::Method method = Shadowsocks2022::Method::Blake3Aes128Gcm)
{
    Shadowsocks2022::PskChain chain;
    chain.userPsk = userPsk;
    return { method, std::move(chain) };
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

    REQUIRE(Shadowsocks2022::deriveSessionSubkey(
        Shadowsocks2022::Method::Blake3Aes128Gcm,
        sequence(0x00, 16), sequence(0xA0, 8)).size() == 16);
}

TEST_CASE("Shadowsocks 2022 increments u96 little-endian nonces safely", "[shadowsocks2022][tcp]")
{
    ByteVector nonce(12, 0);
    REQUIRE(Shadowsocks2022::incrementNonce(nonce));
    REQUIRE(nonce.front() == 1);

    nonce.assign(12, 0);
    nonce[0] = 0xFF;
    REQUIRE(Shadowsocks2022::incrementNonce(nonce));
    REQUIRE(nonce[0] == 0);
    REQUIRE(nonce[1] == 1);

    nonce.assign(12, 0xFF);
    const ByteVector maximum = nonce;
    REQUIRE_FALSE(Shadowsocks2022::incrementNonce(nonce));
    REQUIRE(nonce == maximum);
    ByteVector invalidNonce(11, 0);
    REQUIRE_THROWS_AS(Shadowsocks2022::incrementNonce(invalidNonce), std::invalid_argument);
}

TEST_CASE("Shadowsocks 2022 encodes request headers and chunks", "[shadowsocks2022][tcp]")
{
    const ByteVector userPsk = sequence(0x00, 16);
    auto client = makeTcpClient(userPsk);
    const ByteVector salt = sequence(0xA0, 16);
    const ByteVector target = fromHex("030b6578616d706c652e636f6d01bb");
    const ByteVector padding = fromHex("dead");
    const ByteVector initialPayload = fromHex("beef");

    const ByteVector header = client.encodeRequestHeader(
        salt, 1700000000, target, padding, initialPayload);
    REQUIRE(std::equal(salt.begin(), salt.end(), header.begin()));
    REQUIRE(header.size() == salt.size() + 11 + 16 +
        target.size() + 2 + padding.size() + initialPayload.size() + 16);
    REQUIRE(client.requestSalt() == salt);

    const ByteVector payload = sequence(0x30, 65535);
    const ByteVector chunk = client.encodeRequestChunk(payload);
    REQUIRE(chunk.size() == 2 + 16 + payload.size() + 16);
    REQUIRE_THROWS_AS(client.encodeRequestChunk(ByteVector(65536, 0)), std::invalid_argument);

    auto invalid = makeTcpClient(userPsk);
    REQUIRE_THROWS_AS(invalid.encodeRequestHeader(salt, 1700000000,
        target, {}, {}), std::invalid_argument);
    REQUIRE_THROWS_AS(invalid.encodeRequestHeader(salt, 1700000000,
        target, ByteVector(901, 0), {}), std::invalid_argument);
}

TEST_CASE("Shadowsocks 2022 inserts one TCP identity header per identity PSK", "[shadowsocks2022][tcp]")
{
    Shadowsocks2022::PskChain chain;
    chain.identityPsks = { sequence(0x20, 16), sequence(0x40, 16) };
    chain.userPsk = sequence(0x60, 16);
    Shadowsocks2022TcpClient client(Shadowsocks2022::Method::Blake3Aes128Gcm,
        std::move(chain));
    const ByteVector salt = sequence(0x80, 16);
    const ByteVector target = fromHex("017f00000101bb");

    const ByteVector header = client.encodeRequestHeader(
        salt, 1700000000, target, { 0x01 }, {});
    REQUIRE(header.size() == salt.size() + 2 * 16 + 11 + 16 +
        target.size() + 2 + 1 + 16);
}

TEST_CASE("Shadowsocks 2022 TCP framing supports every method", "[shadowsocks2022][tcp]")
{
    const std::vector<Shadowsocks2022::Method> methods = {
        Shadowsocks2022::Method::Blake3Aes128Gcm,
        Shadowsocks2022::Method::Blake3Aes256Gcm,
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305
    };

    for(const auto method : methods) {
        CAPTURE(static_cast<int>(method));
        const size_t keySize = Shadowsocks2022::keySize(method);
        const ByteVector userPsk = sequence(0x00, keySize);
        const ByteVector requestSalt = sequence(0x40, keySize);
        const ByteVector responseSalt = sequence(0x80, keySize);
        auto client = makeTcpClient(userPsk, method);
        const ByteVector request = client.encodeRequestHeader(requestSalt, 1700000000,
            fromHex("017f00000101bb"), { 0x01 }, {});
        REQUIRE(request.size() == requestSalt.size() + 11 + 16 + 7 + 2 + 1 + 16);

        client.beginResponse(responseSalt);
        const ByteVector responseKey = Shadowsocks2022::deriveSessionSubkey(
            method, userPsk, responseSalt);
        ByteVector fixedHeader = { 0x01 };
        appendU64(fixedHeader, 1700000000);
        fixedHeader.insert(fixedHeader.end(), requestSalt.begin(), requestSalt.end());
        appendU16(fixedHeader, 1);
        REQUIRE(client.decodeResponseHeader(referenceSeal(
            method, responseKey, ByteVector(12, 0), fixedHeader), 1700000000) == 1);
    }
}

TEST_CASE("Shadowsocks 2022 decodes validated response chunks", "[shadowsocks2022][tcp]")
{
    const ByteVector userPsk = sequence(0x00, 16);
    const ByteVector requestSalt = sequence(0xA0, 16);
    const ByteVector responseSalt = sequence(0xC0, 16);
    auto client = makeTcpClient(userPsk);
    client.encodeRequestHeader(requestSalt, 1700000000,
        fromHex("017f00000101bb"), { 0x01 }, {});
    client.beginResponse(responseSalt);

    const ByteVector responseKey = Shadowsocks2022::deriveSessionSubkey(
        Shadowsocks2022::Method::Blake3Aes128Gcm, userPsk, responseSalt);
    ByteVector nonce(12, 0);
    ByteVector fixedHeader = { 0x01 };
    appendU64(fixedHeader, 1700000000);
    fixedHeader.insert(fixedHeader.end(), requestSalt.begin(), requestSalt.end());
    appendU16(fixedHeader, 3);

    REQUIRE(client.responseHeaderCiphertextSize() == fixedHeader.size() + 16);
    REQUIRE(client.decodeResponseHeader(referenceSeal(
        Shadowsocks2022::Method::Blake3Aes128Gcm, responseKey, nonce, fixedHeader),
        1700000000) == 3);

    REQUIRE(Shadowsocks2022::incrementNonce(nonce));
    REQUIRE(client.decodeResponsePayload(referenceSeal(
        Shadowsocks2022::Method::Blake3Aes128Gcm, responseKey, nonce,
        fromHex("010203")), 3) == fromHex("010203"));

    REQUIRE(Shadowsocks2022::incrementNonce(nonce));
    ByteVector length;
    appendU16(length, 2);
    REQUIRE(client.decodeResponseLength(referenceSeal(
        Shadowsocks2022::Method::Blake3Aes128Gcm, responseKey, nonce, length)) == 2);

    REQUIRE(Shadowsocks2022::incrementNonce(nonce));
    REQUIRE(client.decodeResponsePayload(referenceSeal(
        Shadowsocks2022::Method::Blake3Aes128Gcm, responseKey, nonce,
        fromHex("aabb")), 2) == fromHex("aabb"));
}

TEST_CASE("Shadowsocks 2022 rejects invalid response headers", "[shadowsocks2022][tcp]")
{
    const ByteVector userPsk = sequence(0x00, 16);
    const ByteVector requestSalt = sequence(0xA0, 16);
    const ByteVector responseSalt = sequence(0xC0, 16);
    const ByteVector responseKey = Shadowsocks2022::deriveSessionSubkey(
        Shadowsocks2022::Method::Blake3Aes128Gcm, userPsk, responseSalt);
    const ByteVector nonce(12, 0);

    auto makeHeader = [&](uint8_t type, uint64_t timestamp, const ByteVector& salt) {
        ByteVector header = { type };
        appendU64(header, timestamp);
        header.insert(header.end(), salt.begin(), salt.end());
        appendU16(header, 1);
        return referenceSeal(Shadowsocks2022::Method::Blake3Aes128Gcm,
            responseKey, nonce, header);
    };
    auto initialize = [&] {
        auto client = makeTcpClient(userPsk);
        client.encodeRequestHeader(requestSalt, 1700000000,
            fromHex("017f00000101bb"), { 0x01 }, {});
        client.beginResponse(responseSalt);
        return client;
    };

    REQUIRE_THROWS_WITH(initialize().decodeResponseHeader(
        makeHeader(0, 1700000000, requestSalt), 1700000000),
        "Unexpected Shadowsocks 2022 response type");
    REQUIRE_THROWS_WITH(initialize().decodeResponseHeader(
        makeHeader(1, 1699999969, requestSalt), 1700000000),
        "Stale Shadowsocks 2022 response timestamp");
    REQUIRE_THROWS_WITH(initialize().decodeResponseHeader(
        makeHeader(1, 1700000000, sequence(0xB0, 16)), 1700000000),
        "Shadowsocks 2022 response salt mismatch");

    ByteVector tampered = makeHeader(1, 1700000000, requestSalt);
    tampered.back() ^= 0x01;
    REQUIRE_THROWS_WITH(initialize().decodeResponseHeader(tampered, 1700000000),
        "Shadowsocks 2022 authentication failed");

    const ByteVector complete = makeHeader(1, 1700000000, requestSalt);
    REQUIRE_THROWS_WITH(initialize().decodeResponseHeader(
        ByteVector(complete.begin(), complete.end() - 1), 1700000000),
        "Truncated Shadowsocks 2022 response header");
    ByteVector oversized = complete;
    oversized.push_back(0);
    REQUIRE_THROWS_WITH(initialize().decodeResponseHeader(oversized, 1700000000),
        "Oversized Shadowsocks 2022 response header");
}

TEST_CASE("Shadowsocks 2022 replay windows accept bounded reordering", "[shadowsocks2022][udp]")
{
    Shadowsocks2022ReplayWindow window(8);
    REQUIRE(window.accept(100));
    REQUIRE(window.accept(98));
    REQUIRE(window.accept(99));
    REQUIRE_FALSE(window.accept(98));
    REQUIRE(window.accept(107));
    REQUIRE_FALSE(window.accept(99));
    REQUIRE(window.accept(106));
}

TEST_CASE("Shadowsocks 2022 UDP round trips every method", "[shadowsocks2022][udp]")
{
    const std::vector<Shadowsocks2022::Method> methods = {
        Shadowsocks2022::Method::Blake3Aes128Gcm,
        Shadowsocks2022::Method::Blake3Aes256Gcm,
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305
    };
    const ByteVector clientSessionId = sequence(0x10, 8);
    const ByteVector serverSessionId = sequence(0x30, 8);
    const ByteVector target = fromHex("030b6578616d706c652e636f6d0035");

    for(const auto method : methods) {
        CAPTURE(static_cast<int>(method));
        Shadowsocks2022::PskChain chain;
        chain.userPsk = sequence(0x40, Shadowsocks2022::keySize(method));
        Shadowsocks2022UdpSession session(method, std::move(chain), clientSessionId);
        const ByteVector nonce = method == Shadowsocks2022::Method::Blake3ChaCha20Poly1305 ?
            sequence(0x80, 24) : ByteVector{};

        const ByteVector request = session.encodeRequest(7, 1700000000,
            fromHex("aabb"), target, fromHex("010203"), nonce);
        const auto decodedRequest = session.decodeRequest(request, 1700000000);
        REQUIRE(decodedRequest.sessionId == clientSessionId);
        REQUIRE(decodedRequest.packetId == 7);
        REQUIRE(decodedRequest.socksAddress == target);
        REQUIRE(decodedRequest.payload == fromHex("010203"));

        ByteVector responseNonce = nonce;
        if(!responseNonce.empty()) {
            responseNonce.back() ^= 0x01;
        }
        const ByteVector response = session.encodeResponse(serverSessionId, 9,
            1700000000, {}, target, fromHex("a0a1"), responseNonce);
        const auto decodedResponse = session.decodeResponse(response, 1700000000);
        REQUIRE(decodedResponse.sessionId == serverSessionId);
        REQUIRE(decodedResponse.packetId == 9);
        REQUIRE(decodedResponse.socksAddress == target);
        REQUIRE(decodedResponse.payload == fromHex("a0a1"));
    }
}

TEST_CASE("Shadowsocks 2022 UDP verifies EIH chains", "[shadowsocks2022][udp]")
{
    const std::vector<Shadowsocks2022::Method> methods = {
        Shadowsocks2022::Method::Blake3Aes128Gcm,
        Shadowsocks2022::Method::Blake3Aes256Gcm,
        Shadowsocks2022::Method::Blake3ChaCha20Poly1305
    };

    for(const auto method : methods) {
        CAPTURE(static_cast<int>(method));
        const size_t keySize = Shadowsocks2022::keySize(method);
        Shadowsocks2022::PskChain chain;
        chain.identityPsks = { sequence(0x20, keySize), sequence(0x40, keySize) };
        chain.userPsk = sequence(0x60, keySize);
        Shadowsocks2022UdpSession session(method, std::move(chain), sequence(0x10, 8));
        const ByteVector nonce = method == Shadowsocks2022::Method::Blake3ChaCha20Poly1305 ?
            sequence(0x90, 24) : ByteVector{};
        const ByteVector packet = session.encodeRequest(1, 1700000000, {},
            fromHex("017f00000101bb"), fromHex("55"), nonce);
        const auto decoded = session.decodeRequest(packet, 1700000000);
        REQUIRE(decoded.payload == fromHex("55"));
    }
}

TEST_CASE("Shadowsocks 2022 UDP updates replay state only after validation", "[shadowsocks2022][udp]")
{
    Shadowsocks2022::PskChain chain;
    chain.userPsk = sequence(0x00, 16);
    Shadowsocks2022UdpSession session(Shadowsocks2022::Method::Blake3Aes128Gcm,
        std::move(chain), sequence(0x10, 8));
    const ByteVector packet = session.encodeRequest(22, 1700000000, {},
        fromHex("017f00000101bb"), fromHex("0102"));

    ByteVector tampered = packet;
    tampered.back() ^= 0x01;
    REQUIRE_THROWS_WITH(session.decodeRequest(tampered, 1700000000),
        "Shadowsocks 2022 authentication failed");
    REQUIRE(session.decodeRequest(packet, 1700000000).packetId == 22);
    REQUIRE_THROWS_WITH(session.decodeRequest(packet, 1700000000),
        "Replayed Shadowsocks 2022 UDP packet");

    const ByteVector stale = session.encodeRequest(23, 1699999969, {},
        fromHex("017f00000101bb"), fromHex("03"));
    REQUIRE_THROWS_WITH(session.decodeRequest(stale, 1700000000),
        "Stale Shadowsocks 2022 UDP timestamp");
}

TEST_CASE("Shadowsocks 2022 UDP responses require the client session ID", "[shadowsocks2022][udp]")
{
    Shadowsocks2022::PskChain encoderChain;
    encoderChain.userPsk = sequence(0x00, 16);
    Shadowsocks2022UdpSession encoder(Shadowsocks2022::Method::Blake3Aes128Gcm,
        encoderChain, sequence(0x10, 8));
    Shadowsocks2022UdpSession decoder(Shadowsocks2022::Method::Blake3Aes128Gcm,
        std::move(encoderChain), sequence(0x20, 8));
    const ByteVector response = encoder.encodeResponse(sequence(0x30, 8), 1,
        1700000000, {}, fromHex("017f00000101bb"), fromHex("01"));

    REQUIRE_THROWS_WITH(decoder.decodeResponse(response, 1700000000),
        "Shadowsocks 2022 UDP client session mismatch");
}

TEST_CASE("Shadowsocks 2022 UDP rejects malformed address boundaries", "[shadowsocks2022][udp]")
{
    Shadowsocks2022::PskChain chain;
    chain.userPsk = sequence(0x00, 16);
    Shadowsocks2022UdpSession session(Shadowsocks2022::Method::Blake3Aes128Gcm,
        std::move(chain), sequence(0x10, 8));
    ByteVector addressWithTrailingData = fromHex("017f00000101bbff");

    REQUIRE_THROWS_WITH(session.encodeRequest(1, 1700000000, {},
        addressWithTrailingData, {}), "Invalid Shadowsocks 2022 UDP address");
}

#else

TEST_CASE("Shadowsocks 2022 key derivation is available", "[shadowsocks2022][kdf]")
{
    FAIL("dcpp/Shadowsocks2022.h has not been implemented");
}

#endif
