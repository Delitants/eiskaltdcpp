/*
 * Copyright (C) 2026 EiskaltDC++ developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "typedefs.h"

namespace dcpp {

class XChaCha20Poly1305
{
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 24;
    static constexpr size_t TAG_SIZE = 16;

    static ByteVector hChaCha20(const ByteVector& key, const ByteVector& nonce);

    static bool encrypt(const ByteVector& key, const ByteVector& nonce,
        const ByteVector& plaintext, const ByteVector& aad,
        ByteVector& ciphertext, ByteVector& tag);

    static bool decrypt(const ByteVector& key, const ByteVector& nonce,
        const ByteVector& ciphertext, const ByteVector& aad,
        const ByteVector& tag, ByteVector& plaintext);
};

} // namespace dcpp
