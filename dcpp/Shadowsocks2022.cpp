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

#include <limits>
#include <stdexcept>
#include <utility>

namespace dcpp {

namespace {

const char SESSION_SUBKEY_CONTEXT[] = "shadowsocks 2022 session subkey";

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

} // namespace dcpp
