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

#pragma once

#include "typedefs.h"

#include <cstddef>
#include <string>
#include <vector>

namespace dcpp {

class Shadowsocks2022
{
public:
    enum class Method {
        Blake3Aes128Gcm,
        Blake3Aes256Gcm,
        Blake3ChaCha20Poly1305
    };

    struct PskChain {
        std::vector<ByteVector> identityPsks;
        ByteVector userPsk;
    };

    static Method parseMethod(const std::string& method);
    static std::size_t keySize(Method method);
    static PskChain parsePskChain(Method method, const std::string& encodedChain);
    static ByteVector deriveSessionSubkey(Method method, const ByteVector& psk,
        const ByteVector& salt);
    static bool incrementNonce(ByteVector& nonce);
};

class Shadowsocks2022TcpClient
{
public:
    Shadowsocks2022TcpClient(Shadowsocks2022::Method method,
        Shadowsocks2022::PskChain pskChain);

    ByteVector encodeRequestHeader(const ByteVector& salt, uint64_t timestamp,
        const ByteVector& socksAddress, const ByteVector& padding,
        const ByteVector& initialPayload);
    ByteVector encodeRequestChunk(const ByteVector& payload);

    void beginResponse(const ByteVector& salt);
    uint16_t decodeResponseHeader(const ByteVector& encryptedHeader, uint64_t now);
    uint16_t decodeResponseLength(const ByteVector& encryptedLength);
    ByteVector decodeResponsePayload(const ByteVector& encryptedPayload,
        uint16_t expectedLength);

    size_t responseHeaderCiphertextSize() const;
    const ByteVector& requestSalt() const noexcept;

private:
    ByteVector sealRequest(const ByteVector& plaintext);
    ByteVector openResponse(const ByteVector& ciphertext);

    Shadowsocks2022::Method method;
    Shadowsocks2022::PskChain pskChain;
    ByteVector currentRequestSalt;
    ByteVector requestSubkey;
    ByteVector requestNonce;
    ByteVector responseSubkey;
    ByteVector responseNonce;
    bool requestNonceExhausted = false;
    bool responseNonceExhausted = false;
};

} // namespace dcpp
