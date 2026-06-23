/*
 * Copyright (C) 2026 EiskaltDC++ developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "stdinc.h"
#include "Shadowsocks2022.h"

#include <blake3.h>
#include <openssl/evp.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace dcpp {

namespace {

const char SESSION_SUBKEY_CONTEXT[] = "shadowsocks 2022 session subkey";
const char IDENTITY_SUBKEY_CONTEXT[] = "shadowsocks 2022 identity subkey";
constexpr size_t AEAD_TAG_SIZE = 16;
constexpr size_t AEAD_NONCE_SIZE = 12;
constexpr size_t MAX_PADDING_SIZE = 900;

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

bool isBase64Character(char value)
{
    return (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') || value == '+' || value == '/';
}

ByteVector decodeCanonicalBase64(const std::string& encoded)
{
    if(encoded.empty() || encoded.size() % 4 != 0 ||
            encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Invalid Shadowsocks 2022 PSK base64");
    }

    std::size_t padding = 0;
    if(encoded.back() == '=') {
        ++padding;
        if(encoded.size() > 1 && encoded[encoded.size() - 2] == '=') {
            ++padding;
        }
    }

    const std::size_t dataLength = encoded.size() - padding;
    for(std::size_t i = 0; i < dataLength; ++i) {
        if(!isBase64Character(encoded[i])) {
            throw std::invalid_argument("Invalid Shadowsocks 2022 PSK base64");
        }
    }
    for(std::size_t i = dataLength; i < encoded.size(); ++i) {
        if(encoded[i] != '=') {
            throw std::invalid_argument("Invalid Shadowsocks 2022 PSK base64");
        }
    }

    ByteVector decoded(encoded.size() / 4 * 3);
    const int decodedLength = EVP_DecodeBlock(decoded.data(),
        reinterpret_cast<const unsigned char*>(encoded.data()), static_cast<int>(encoded.size()));
    if(decodedLength < 0 || static_cast<std::size_t>(decodedLength) < padding) {
        throw std::invalid_argument("Invalid Shadowsocks 2022 PSK base64");
    }
    decoded.resize(static_cast<std::size_t>(decodedLength) - padding);

    std::string canonical(4 * ((decoded.size() + 2) / 3), '\0');
    const int canonicalLength = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(&canonical[0]), decoded.data(), static_cast<int>(decoded.size()));
    canonical.resize(static_cast<std::size_t>(canonicalLength));
    if(canonical != encoded) {
        throw std::invalid_argument("Non-canonical Shadowsocks 2022 PSK base64");
    }

    return decoded;
}

const EVP_CIPHER* aeadCipher(Shadowsocks2022::Method method)
{
    switch(method) {
    case Shadowsocks2022::Method::Blake3Aes128Gcm:
        return EVP_aes_128_gcm();
    case Shadowsocks2022::Method::Blake3Aes256Gcm:
        return EVP_aes_256_gcm();
    case Shadowsocks2022::Method::Blake3ChaCha20Poly1305:
        return EVP_chacha20_poly1305();
    }
    return nullptr;
}

ByteVector seal(Shadowsocks2022::Method method, const ByteVector& key,
    const ByteVector& nonce, const ByteVector& plaintext)
{
    if(plaintext.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Oversized Shadowsocks 2022 plaintext");
    }

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    ByteVector output(plaintext.size() + AEAD_TAG_SIZE);
    int outputLength = 0;
    int finalLength = 0;
    const EVP_CIPHER* cipher = aeadCipher(method);
    const bool ok = context && cipher &&
        EVP_EncryptInit_ex(context.get(), cipher, nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_IVLEN,
            static_cast<int>(nonce.size()), nullptr) == 1 &&
        EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) == 1 &&
        EVP_EncryptUpdate(context.get(), output.data(), &outputLength,
            plaintext.data(), static_cast<int>(plaintext.size())) == 1 &&
        EVP_EncryptFinal_ex(context.get(), output.data() + outputLength, &finalLength) == 1 &&
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_GET_TAG, AEAD_TAG_SIZE,
            output.data() + outputLength + finalLength) == 1;
    if(!ok) {
        throw std::runtime_error("Shadowsocks 2022 encryption failed");
    }

    output.resize(static_cast<size_t>(outputLength + finalLength) + AEAD_TAG_SIZE);
    return output;
}

ByteVector open(Shadowsocks2022::Method method, const ByteVector& key,
    const ByteVector& nonce, const ByteVector& ciphertext)
{
    if(ciphertext.size() < AEAD_TAG_SIZE ||
            ciphertext.size() - AEAD_TAG_SIZE > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Shadowsocks 2022 authentication failed");
    }

    const size_t encryptedLength = ciphertext.size() - AEAD_TAG_SIZE;
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    ByteVector plaintext(encryptedLength + AEAD_TAG_SIZE);
    int outputLength = 0;
    int finalLength = 0;
    const EVP_CIPHER* cipher = aeadCipher(method);
    const bool initialized = context && cipher &&
        EVP_DecryptInit_ex(context.get(), cipher, nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_IVLEN,
            static_cast<int>(nonce.size()), nullptr) == 1 &&
        EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) == 1 &&
        EVP_DecryptUpdate(context.get(), plaintext.data(), &outputLength,
            ciphertext.data(), static_cast<int>(encryptedLength)) == 1 &&
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_TAG, AEAD_TAG_SIZE,
            const_cast<uint8_t*>(ciphertext.data() + encryptedLength)) == 1;
    if(!initialized || EVP_DecryptFinal_ex(
            context.get(), plaintext.data() + outputLength, &finalLength) != 1) {
        throw std::runtime_error("Shadowsocks 2022 authentication failed");
    }

    plaintext.resize(static_cast<size_t>(outputLength + finalLength));
    return plaintext;
}

ByteVector deriveIdentitySubkey(Shadowsocks2022::Method method,
    const ByteVector& psk, const ByteVector& salt)
{
    const size_t keySize = Shadowsocks2022::keySize(method);
    if(psk.size() != keySize || salt.size() != keySize) {
        throw std::invalid_argument("Wrong Shadowsocks 2022 identity key material size");
    }

    blake3_hasher hasher;
    blake3_hasher_init_derive_key(&hasher, IDENTITY_SUBKEY_CONTEXT);
    blake3_hasher_update(&hasher, psk.data(), psk.size());
    blake3_hasher_update(&hasher, salt.data(), salt.size());
    ByteVector subkey(keySize);
    blake3_hasher_finalize(&hasher, subkey.data(), subkey.size());
    return subkey;
}

ByteVector pskHash16(const ByteVector& psk)
{
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, psk.data(), psk.size());
    ByteVector hash(16);
    blake3_hasher_finalize(&hasher, hash.data(), hash.size());
    return hash;
}

ByteVector aesBlockEncrypt(const ByteVector& key, const ByteVector& plaintext)
{
    if((key.size() != 16 && key.size() != 32) || plaintext.size() != 16) {
        throw std::invalid_argument("Invalid Shadowsocks 2022 identity header input");
    }

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    ByteVector output(32);
    int outputLength = 0;
    int finalLength = 0;
    const EVP_CIPHER* cipher = key.size() == 16 ? EVP_aes_128_ecb() : EVP_aes_256_ecb();
    const bool ok = context &&
        EVP_EncryptInit_ex(context.get(), cipher, nullptr, key.data(), nullptr) == 1 &&
        EVP_CIPHER_CTX_set_padding(context.get(), 0) == 1 &&
        EVP_EncryptUpdate(context.get(), output.data(), &outputLength,
            plaintext.data(), static_cast<int>(plaintext.size())) == 1 &&
        EVP_EncryptFinal_ex(context.get(), output.data() + outputLength, &finalLength) == 1;
    if(!ok || outputLength + finalLength != 16) {
        throw std::runtime_error("Shadowsocks 2022 identity encryption failed");
    }
    output.resize(16);
    return output;
}

ByteVector tcpIdentityHeaders(Shadowsocks2022::Method method,
    const Shadowsocks2022::PskChain& chain, const ByteVector& salt)
{
    ByteVector result;
    result.reserve(chain.identityPsks.size() * 16);
    for(size_t i = 0; i < chain.identityPsks.size(); ++i) {
        const ByteVector& currentPsk = chain.identityPsks[i];
        const ByteVector& nextPsk = i + 1 < chain.identityPsks.size() ?
            chain.identityPsks[i + 1] : chain.userPsk;
        const ByteVector subkey = deriveIdentitySubkey(method, currentPsk, salt);
        const ByteVector header = aesBlockEncrypt(subkey, pskHash16(nextPsk));
        result.insert(result.end(), header.begin(), header.end());
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

uint16_t readU16(const uint8_t* input)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint64_t readU64(const uint8_t* input)
{
    uint64_t value = 0;
    for(size_t i = 0; i < 8; ++i) {
        value = (value << 8) | input[i];
    }
    return value;
}

void advanceNonce(ByteVector& nonce, bool& exhausted)
{
    if(!Shadowsocks2022::incrementNonce(nonce)) {
        exhausted = true;
    }
}

} // namespace

Shadowsocks2022::Method Shadowsocks2022::parseMethod(const std::string& method)
{
    if(method == "2022-blake3-aes-128-gcm") {
        return Method::Blake3Aes128Gcm;
    }
    if(method == "2022-blake3-aes-256-gcm") {
        return Method::Blake3Aes256Gcm;
    }
    if(method == "2022-blake3-chacha20-poly1305") {
        return Method::Blake3ChaCha20Poly1305;
    }
    throw std::invalid_argument("Unsupported Shadowsocks 2022 method");
}

std::size_t Shadowsocks2022::keySize(Method method)
{
    switch(method) {
    case Method::Blake3Aes128Gcm:
        return 16;
    case Method::Blake3Aes256Gcm:
    case Method::Blake3ChaCha20Poly1305:
        return 32;
    }
    throw std::invalid_argument("Unsupported Shadowsocks 2022 method");
}

Shadowsocks2022::PskChain Shadowsocks2022::parsePskChain(
    Method method, const std::string& encodedChain)
{
    const std::size_t expectedSize = keySize(method);
    std::vector<ByteVector> keys;
    std::size_t begin = 0;

    while(true) {
        const std::size_t end = encodedChain.find(':', begin);
        ByteVector key = decodeCanonicalBase64(encodedChain.substr(begin, end - begin));
        if(key.size() != expectedSize) {
            throw std::invalid_argument("Wrong Shadowsocks 2022 PSK size");
        }
        keys.push_back(std::move(key));

        if(end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }

    PskChain result;
    result.userPsk = std::move(keys.back());
    keys.pop_back();
    result.identityPsks = std::move(keys);
    return result;
}

ByteVector Shadowsocks2022::deriveSessionSubkey(
    Method method, const ByteVector& psk, const ByteVector& salt)
{
    const std::size_t expectedSize = keySize(method);
    if(psk.size() != expectedSize || salt.size() != expectedSize) {
        throw std::invalid_argument("Wrong Shadowsocks 2022 key material size");
    }

    ByteVector material;
    material.reserve(psk.size() + salt.size());
    material.insert(material.end(), psk.begin(), psk.end());
    material.insert(material.end(), salt.begin(), salt.end());

    blake3_hasher hasher;
    blake3_hasher_init_derive_key(&hasher, SESSION_SUBKEY_CONTEXT);
    blake3_hasher_update(&hasher, material.data(), material.size());

    ByteVector subkey(expectedSize);
    blake3_hasher_finalize(&hasher, subkey.data(), subkey.size());
    return subkey;
}

bool Shadowsocks2022::incrementNonce(ByteVector& nonce)
{
    if(nonce.size() != AEAD_NONCE_SIZE) {
        throw std::invalid_argument("Invalid Shadowsocks 2022 nonce size");
    }
    if(std::all_of(nonce.begin(), nonce.end(), [](uint8_t value) { return value == 0xFF; })) {
        return false;
    }
    for(uint8_t& value : nonce) {
        if(++value != 0) {
            break;
        }
    }
    return true;
}

Shadowsocks2022TcpClient::Shadowsocks2022TcpClient(
    Shadowsocks2022::Method method, Shadowsocks2022::PskChain pskChain) :
    method(method), pskChain(std::move(pskChain))
{
    const size_t expectedSize = Shadowsocks2022::keySize(method);
    if(this->pskChain.userPsk.size() != expectedSize ||
            std::any_of(this->pskChain.identityPsks.begin(), this->pskChain.identityPsks.end(),
                [expectedSize](const ByteVector& psk) { return psk.size() != expectedSize; })) {
        throw std::invalid_argument("Wrong Shadowsocks 2022 PSK size");
    }
}

ByteVector Shadowsocks2022TcpClient::encodeRequestHeader(const ByteVector& salt,
    uint64_t timestamp, const ByteVector& socksAddress, const ByteVector& padding,
    const ByteVector& initialPayload)
{
    const size_t expectedSaltSize = Shadowsocks2022::keySize(method);
    if(!requestSubkey.empty()) {
        throw std::logic_error("Shadowsocks 2022 request already started");
    }
    if(salt.size() != expectedSaltSize || socksAddress.empty()) {
        throw std::invalid_argument("Invalid Shadowsocks 2022 request header");
    }
    if(padding.size() > MAX_PADDING_SIZE || (padding.empty() && initialPayload.empty())) {
        throw std::invalid_argument("Invalid Shadowsocks 2022 request padding");
    }

    ByteVector variableHeader;
    variableHeader.reserve(socksAddress.size() + 2 + padding.size() + initialPayload.size());
    variableHeader.insert(variableHeader.end(), socksAddress.begin(), socksAddress.end());
    appendU16(variableHeader, static_cast<uint16_t>(padding.size()));
    variableHeader.insert(variableHeader.end(), padding.begin(), padding.end());
    variableHeader.insert(variableHeader.end(), initialPayload.begin(), initialPayload.end());
    if(variableHeader.size() > std::numeric_limits<uint16_t>::max()) {
        throw std::invalid_argument("Oversized Shadowsocks 2022 request header");
    }

    currentRequestSalt = salt;
    requestSubkey = Shadowsocks2022::deriveSessionSubkey(method, pskChain.userPsk, salt);
    requestNonce.assign(AEAD_NONCE_SIZE, 0);
    requestNonceExhausted = false;

    ByteVector fixedHeader = { 0x00 };
    appendU64(fixedHeader, timestamp);
    appendU16(fixedHeader, static_cast<uint16_t>(variableHeader.size()));

    ByteVector output = salt;
    const ByteVector identityHeaders = tcpIdentityHeaders(method, pskChain, salt);
    output.insert(output.end(), identityHeaders.begin(), identityHeaders.end());
    const ByteVector encryptedFixed = sealRequest(fixedHeader);
    output.insert(output.end(), encryptedFixed.begin(), encryptedFixed.end());
    const ByteVector encryptedVariable = sealRequest(variableHeader);
    output.insert(output.end(), encryptedVariable.begin(), encryptedVariable.end());
    return output;
}

ByteVector Shadowsocks2022TcpClient::encodeRequestChunk(const ByteVector& payload)
{
    if(requestSubkey.empty()) {
        throw std::logic_error("Shadowsocks 2022 request not started");
    }
    if(payload.size() > std::numeric_limits<uint16_t>::max()) {
        throw std::invalid_argument("Oversized Shadowsocks 2022 request payload");
    }

    ByteVector length;
    appendU16(length, static_cast<uint16_t>(payload.size()));
    ByteVector output = sealRequest(length);
    const ByteVector encryptedPayload = sealRequest(payload);
    output.insert(output.end(), encryptedPayload.begin(), encryptedPayload.end());
    return output;
}

void Shadowsocks2022TcpClient::beginResponse(const ByteVector& salt)
{
    if(currentRequestSalt.empty()) {
        throw std::logic_error("Shadowsocks 2022 request not started");
    }
    if(salt.size() != Shadowsocks2022::keySize(method)) {
        throw std::invalid_argument("Invalid Shadowsocks 2022 response salt");
    }
    responseSubkey = Shadowsocks2022::deriveSessionSubkey(method, pskChain.userPsk, salt);
    responseNonce.assign(AEAD_NONCE_SIZE, 0);
    responseNonceExhausted = false;
}

uint16_t Shadowsocks2022TcpClient::decodeResponseHeader(
    const ByteVector& encryptedHeader, uint64_t now)
{
    const size_t expectedSize = responseHeaderCiphertextSize();
    if(encryptedHeader.size() < expectedSize) {
        throw std::runtime_error("Truncated Shadowsocks 2022 response header");
    }
    if(encryptedHeader.size() > expectedSize) {
        throw std::runtime_error("Oversized Shadowsocks 2022 response header");
    }

    const ByteVector header = openResponse(encryptedHeader);
    if(header[0] != 0x01) {
        throw std::runtime_error("Unexpected Shadowsocks 2022 response type");
    }
    const uint64_t timestamp = readU64(header.data() + 1);
    const uint64_t difference = timestamp > now ? timestamp - now : now - timestamp;
    if(difference > 30) {
        throw std::runtime_error("Stale Shadowsocks 2022 response timestamp");
    }
    const auto saltBegin = header.begin() + 9;
    if(!std::equal(currentRequestSalt.begin(), currentRequestSalt.end(), saltBegin)) {
        throw std::runtime_error("Shadowsocks 2022 response salt mismatch");
    }
    return readU16(header.data() + 9 + currentRequestSalt.size());
}

uint16_t Shadowsocks2022TcpClient::decodeResponseLength(const ByteVector& encryptedLength)
{
    constexpr size_t expectedSize = 2 + AEAD_TAG_SIZE;
    if(encryptedLength.size() < expectedSize) {
        throw std::runtime_error("Truncated Shadowsocks 2022 response length");
    }
    if(encryptedLength.size() > expectedSize) {
        throw std::runtime_error("Oversized Shadowsocks 2022 response length");
    }
    const ByteVector length = openResponse(encryptedLength);
    return readU16(length.data());
}

ByteVector Shadowsocks2022TcpClient::decodeResponsePayload(
    const ByteVector& encryptedPayload, uint16_t expectedLength)
{
    const size_t expectedSize = static_cast<size_t>(expectedLength) + AEAD_TAG_SIZE;
    if(encryptedPayload.size() < expectedSize) {
        throw std::runtime_error("Truncated Shadowsocks 2022 response payload");
    }
    if(encryptedPayload.size() > expectedSize) {
        throw std::runtime_error("Oversized Shadowsocks 2022 response payload");
    }
    return openResponse(encryptedPayload);
}

size_t Shadowsocks2022TcpClient::responseHeaderCiphertextSize() const
{
    return 1 + 8 + Shadowsocks2022::keySize(method) + 2 + AEAD_TAG_SIZE;
}

const ByteVector& Shadowsocks2022TcpClient::requestSalt() const noexcept
{
    return currentRequestSalt;
}

ByteVector Shadowsocks2022TcpClient::sealRequest(const ByteVector& plaintext)
{
    if(requestNonceExhausted) {
        throw std::overflow_error("Shadowsocks 2022 request nonce exhausted");
    }
    ByteVector output = seal(method, requestSubkey, requestNonce, plaintext);
    advanceNonce(requestNonce, requestNonceExhausted);
    return output;
}

ByteVector Shadowsocks2022TcpClient::openResponse(const ByteVector& ciphertext)
{
    if(responseSubkey.empty()) {
        throw std::logic_error("Shadowsocks 2022 response not started");
    }
    if(responseNonceExhausted) {
        throw std::overflow_error("Shadowsocks 2022 response nonce exhausted");
    }
    ByteVector output = open(method, responseSubkey, responseNonce, ciphertext);
    advanceNonce(responseNonce, responseNonceExhausted);
    return output;
}

} // namespace dcpp
