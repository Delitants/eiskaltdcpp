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
};

} // namespace dcpp
