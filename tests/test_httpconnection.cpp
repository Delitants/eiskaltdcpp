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
#include "HttpConnection.h"

using namespace dcpp;

TEST_CASE("HttpConnection: detects response body overflow before appending", "[http]") {
    REQUIRE(HttpConnection::responseBodyWouldExceedSize(100, 95, 6));
    REQUIRE_FALSE(HttpConnection::responseBodyWouldExceedSize(100, 95, 5));
}

TEST_CASE("HttpConnection: treats already-overrun bodies as too large", "[http]") {
    REQUIRE(HttpConnection::responseBodyWouldExceedSize(100, 101, 1));
}

TEST_CASE("HttpConnection: unknown response size has no declared body limit", "[http]") {
    REQUIRE_FALSE(HttpConnection::responseBodyWouldExceedSize(-1, 1024, 1024));
}
