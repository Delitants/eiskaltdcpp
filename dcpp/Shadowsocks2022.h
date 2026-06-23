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
#include <map>
#include <set>
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

class Shadowsocks2022ReplayWindow
{
public:
    explicit Shadowsocks2022ReplayWindow(uint64_t windowSize = 1024);
    bool accept(uint64_t packetId);

private:
    uint64_t windowSize;
    uint64_t highest = 0;
    bool initialized = false;
    std::set<uint64_t> seen;
};

struct Shadowsocks2022UdpMessage {
    ByteVector sessionId;
    uint64_t packetId = 0;
    ByteVector socksAddress;
    ByteVector payload;
};

class Shadowsocks2022UdpSession
{
public:
    Shadowsocks2022UdpSession(Shadowsocks2022::Method method,
        Shadowsocks2022::PskChain pskChain, ByteVector clientSessionId);

    ByteVector encodeRequest(uint64_t packetId, uint64_t timestamp,
        const ByteVector& padding, const ByteVector& socksAddress,
        const ByteVector& payload, const ByteVector& nonce = {});
    Shadowsocks2022UdpMessage decodeRequest(const ByteVector& packet, uint64_t now);

    ByteVector encodeResponse(const ByteVector& serverSessionId, uint64_t packetId,
        uint64_t timestamp, const ByteVector& padding,
        const ByteVector& socksAddress, const ByteVector& payload,
        const ByteVector& nonce = {});
    Shadowsocks2022UdpMessage decodeResponse(const ByteVector& packet, uint64_t now);

private:
    Shadowsocks2022::Method method;
    Shadowsocks2022::PskChain pskChain;
    ByteVector clientSessionId;
    std::map<ByteVector, Shadowsocks2022ReplayWindow> requestReplayWindows;
    std::map<ByteVector, Shadowsocks2022ReplayWindow> responseReplayWindows;
};

} // namespace dcpp
