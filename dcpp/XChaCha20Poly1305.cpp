/*
 * Copyright (C) 2026 EiskaltDC++ developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "XChaCha20Poly1305.h"

#include <openssl/evp.h>

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>

namespace dcpp {

namespace {

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

uint32_t load32(const uint8_t* input)
{
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

void store32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t rotateLeft(uint32_t value, unsigned int shift)
{
    return (value << shift) | (value >> (32 - shift));
}

void quarterRound(std::array<uint32_t, 16>& state, size_t a, size_t b, size_t c, size_t d)
{
    state[a] += state[b];
    state[d] = rotateLeft(state[d] ^ state[a], 16);
    state[c] += state[d];
    state[b] = rotateLeft(state[b] ^ state[c], 12);
    state[a] += state[b];
    state[d] = rotateLeft(state[d] ^ state[a], 8);
    state[c] += state[d];
    state[b] = rotateLeft(state[b] ^ state[c], 7);
}

bool validAeadInputs(const ByteVector& key, const ByteVector& nonce,
    const ByteVector& data, const ByteVector& aad)
{
    const size_t maxLength = static_cast<size_t>(std::numeric_limits<int>::max());
    return key.size() == XChaCha20Poly1305::KEY_SIZE &&
        nonce.size() == XChaCha20Poly1305::NONCE_SIZE &&
        data.size() <= maxLength && aad.size() <= maxLength;
}

std::array<uint8_t, 12> ietfNonce(const ByteVector& nonce)
{
    std::array<uint8_t, 12> result{};
    std::copy(nonce.begin() + 16, nonce.end(), result.begin() + 4);
    return result;
}

} // namespace

ByteVector XChaCha20Poly1305::hChaCha20(const ByteVector& key, const ByteVector& nonce)
{
    if(key.size() != KEY_SIZE || nonce.size() != 16) {
        throw std::invalid_argument("Invalid HChaCha20 key or nonce size");
    }

    std::array<uint32_t, 16> state = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        load32(key.data()), load32(key.data() + 4),
        load32(key.data() + 8), load32(key.data() + 12),
        load32(key.data() + 16), load32(key.data() + 20),
        load32(key.data() + 24), load32(key.data() + 28),
        load32(nonce.data()), load32(nonce.data() + 4),
        load32(nonce.data() + 8), load32(nonce.data() + 12)
    };

    for(int round = 0; round < 10; ++round) {
        quarterRound(state, 0, 4, 8, 12);
        quarterRound(state, 1, 5, 9, 13);
        quarterRound(state, 2, 6, 10, 14);
        quarterRound(state, 3, 7, 11, 15);
        quarterRound(state, 0, 5, 10, 15);
        quarterRound(state, 1, 6, 11, 12);
        quarterRound(state, 2, 7, 8, 13);
        quarterRound(state, 3, 4, 9, 14);
    }

    ByteVector result(KEY_SIZE);
    const std::array<size_t, 8> outputWords = { 0, 1, 2, 3, 12, 13, 14, 15 };
    for(size_t i = 0; i < outputWords.size(); ++i) {
        store32(result.data() + i * 4, state[outputWords[i]]);
    }
    return result;
}

bool XChaCha20Poly1305::encrypt(const ByteVector& key, const ByteVector& nonce,
    const ByteVector& plaintext, const ByteVector& aad,
    ByteVector& ciphertext, ByteVector& tag)
{
    ciphertext.clear();
    tag.clear();
    if(!validAeadInputs(key, nonce, plaintext, aad)) {
        return false;
    }

    const ByteVector noncePrefix(nonce.begin(), nonce.begin() + 16);
    const ByteVector subkey = hChaCha20(key, noncePrefix);
    const auto iv = ietfNonce(nonce);
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if(!context) {
        return false;
    }

    ByteVector encrypted(plaintext.size() + TAG_SIZE);
    int outputLength = 0;
    int finalLength = 0;
    int aadLength = 0;
    bool ok = EVP_EncryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_IVLEN, iv.size(), nullptr) == 1 &&
        EVP_EncryptInit_ex(context.get(), nullptr, nullptr, subkey.data(), iv.data()) == 1;
    if(ok && !aad.empty()) {
        ok = EVP_EncryptUpdate(context.get(), nullptr, &aadLength,
            aad.data(), static_cast<int>(aad.size())) == 1;
    }
    if(ok) {
        ok = EVP_EncryptUpdate(context.get(), encrypted.data(), &outputLength,
            plaintext.data(), static_cast<int>(plaintext.size())) == 1 &&
            EVP_EncryptFinal_ex(context.get(), encrypted.data() + outputLength, &finalLength) == 1;
    }

    ByteVector authenticationTag(TAG_SIZE);
    if(ok) {
        ok = EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_GET_TAG,
            authenticationTag.size(), authenticationTag.data()) == 1;
    }
    if(!ok) {
        return false;
    }

    encrypted.resize(static_cast<size_t>(outputLength + finalLength));
    ciphertext.swap(encrypted);
    tag.swap(authenticationTag);
    return true;
}

bool XChaCha20Poly1305::decrypt(const ByteVector& key, const ByteVector& nonce,
    const ByteVector& ciphertext, const ByteVector& aad,
    const ByteVector& tag, ByteVector& plaintext)
{
    plaintext.clear();
    if(!validAeadInputs(key, nonce, ciphertext, aad) || tag.size() != TAG_SIZE) {
        return false;
    }

    const ByteVector noncePrefix(nonce.begin(), nonce.begin() + 16);
    const ByteVector subkey = hChaCha20(key, noncePrefix);
    const auto iv = ietfNonce(nonce);
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if(!context) {
        return false;
    }

    ByteVector decrypted(ciphertext.size() + TAG_SIZE);
    int outputLength = 0;
    int finalLength = 0;
    int aadLength = 0;
    bool ok = EVP_DecryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_IVLEN, iv.size(), nullptr) == 1 &&
        EVP_DecryptInit_ex(context.get(), nullptr, nullptr, subkey.data(), iv.data()) == 1;
    if(ok && !aad.empty()) {
        ok = EVP_DecryptUpdate(context.get(), nullptr, &aadLength,
            aad.data(), static_cast<int>(aad.size())) == 1;
    }
    if(ok) {
        ok = EVP_DecryptUpdate(context.get(), decrypted.data(), &outputLength,
            ciphertext.data(), static_cast<int>(ciphertext.size())) == 1 &&
            EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_TAG,
                tag.size(), const_cast<uint8_t*>(tag.data())) == 1 &&
            EVP_DecryptFinal_ex(context.get(), decrypted.data() + outputLength, &finalLength) == 1;
    }
    if(!ok) {
        return false;
    }

    decrypted.resize(static_cast<size_t>(outputLength + finalLength));
    plaintext.swap(decrypted);
    return true;
}

} // namespace dcpp
