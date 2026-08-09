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
#include "BufferedSocket.h"

using namespace dcpp;

TEST_CASE("BufferedSocket: data mode stops when the current data block is exhausted", "[socket]") {
    REQUIRE_FALSE(BufferedSocket::dataModeCanConsumeMore(0, 7));
    REQUIRE(BufferedSocket::dataModeCanConsumeMore(1, 7));
    REQUIRE_FALSE(BufferedSocket::dataModeCanConsumeMore(1, 0));
}
