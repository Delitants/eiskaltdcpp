/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "SSLSocket.h"

#include <openssl/ssl.h>

using namespace dcpp;

TEST_CASE("SSLSocket: classifies TLS EOF as closed instead of retryable", "[ssl]") {
    REQUIRE(SSLSocket::tlsReadResultMeansClosed(0, SSL_ERROR_ZERO_RETURN, 0));
    REQUIRE(SSLSocket::tlsReadResultMeansClosed(-1, SSL_ERROR_SYSCALL, 0));
    REQUIRE_FALSE(SSLSocket::tlsReadResultMeansClosed(-1, SSL_ERROR_WANT_READ, 0));
    REQUIRE_FALSE(SSLSocket::tlsReadResultMeansClosed(-1, SSL_ERROR_WANT_WRITE, 0));
}
