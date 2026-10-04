#include <catch2/catch_test_macros.hpp>
#include "HttpResponseParser.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

using dcpp::HttpResponseParser;

namespace {
void feed(HttpResponseParser& parser, const std::string& bytes, std::string& body) {
    parser.feed(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
        [&](const uint8_t* data, size_t size) { body.append(reinterpret_cast<const char*>(data), size); });
}
}

TEST_CASE("Strict HTTP streams a fixed body across every packet boundary", "[http][update-transport][http-response]") {
    const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    for(size_t split = 0; split <= response.size(); ++split) {
        CAPTURE(split);
        HttpResponseParser parser({5, 65536});
        std::string body;
        feed(parser, response.substr(0, split), body);
        feed(parser, response.substr(split), body);
        REQUIRE(parser.complete());
        REQUIRE(parser.status() == 200);
        REQUIRE(parser.received() == 5);
        REQUIRE(body == "hello");
        REQUIRE_NOTHROW(parser.eof());
    }
}

TEST_CASE("Strict HTTP unframes chunked signed bytes including trailers", "[http][update-transport][http-response]") {
    const std::string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n"
        "2;name=value\r\nhe\r\n3\r\nllo\r\n0\r\nX-Checksum: ignored\r\n\r\n";
    for(size_t split = 0; split <= response.size(); ++split) {
        HttpResponseParser parser({5, 65536});
        std::string body;
        feed(parser, response.substr(0, split), body);
        feed(parser, response.substr(split), body);
        REQUIRE(parser.complete());
        REQUIRE(parser.received() == 5);
        REQUIRE(body == "hello");
    }
}

TEST_CASE("Strict HTTP rejects ambiguous or malformed response framing", "[http][update-transport][http-response]") {
    for(const std::string response : {
        "HTTP/1.1 404 Not Found 200\r\n\r\n",
        "HTTP/1.1 2000 Not OK\r\n\r\n",
        "HTTP/2 200 OK\r\n\r\n",
        "HTTP/1.1 100 Continue\r\n\r\n",
        "HTTP/1.1 200 OK\n\n",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: +1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1x\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1, 1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\ncontent-length: 1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length : 1\r\n\r\n",
        "HTTP/1.1 200 OK\r\n Bad: folded\r\n\r\n",
        "HTTP/1.1 200 OK\r\nMissingColon\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n+1\r\na\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nxyz\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10000000000000000\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naXX",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\nx"
    }) {
        CAPTURE(response);
        HttpResponseParser parser({1024, 65536});
        std::string body;
        REQUIRE_THROWS_AS(feed(parser, response, body), std::runtime_error);
        REQUIRE_FALSE(parser.complete());
        REQUIRE_THROWS_AS(parser.eof(), std::runtime_error);
    }
}

TEST_CASE("Strict HTTP never completes a truncated framed body", "[http][update-transport][http-response]") {
    for(const std::string response : {
        "", "HTTP/1.1 200 OK\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n"
    }) {
        HttpResponseParser parser({1024, 65536});
        std::string body;
        feed(parser, response, body);
        REQUIRE_THROWS_AS(parser.eof(), std::runtime_error);
        REQUIRE_FALSE(parser.complete());
    }
}

TEST_CASE("Strict HTTP supports bounded close-delimited responses", "[http][update-transport][http-response]") {
    HttpResponseParser parser({5, 65536});
    std::string body;
    feed(parser, "HTTP/1.0 200 OK\r\n\r\nhello", body);
    REQUIRE_FALSE(parser.complete());
    REQUIRE_NOTHROW(parser.eof());
    REQUIRE(parser.complete());
    REQUIRE(body == "hello");
}

TEST_CASE("Strict HTTP enforces exact header and streaming body budgets", "[http][update-transport][http-response]") {
    const std::string header = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n";
    std::string body;
    HttpResponseParser exact({5, header.size()});
    REQUIRE_NOTHROW(feed(exact, header + "hello", body));
    REQUIRE(exact.complete());
    HttpResponseParser shortHeader({5, header.size() - 1});
    REQUIRE_THROWS_AS(feed(shortHeader, header, body), std::runtime_error);
    HttpResponseParser tooLarge({4, 65536});
    REQUIRE_THROWS_AS(feed(tooLarge, header, body), std::runtime_error);
    HttpResponseParser unknown({4, 65536});
    REQUIRE_THROWS_AS(feed(unknown, "HTTP/1.1 200 OK\r\n\r\nhello", body), std::runtime_error);
    HttpResponseParser chunked({4, 65536});
    REQUIRE_THROWS_AS(feed(chunked, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n", body), std::runtime_error);
}

TEST_CASE("Strict HTTP caps unbounded header and chunk metadata", "[http][update-transport][http-response]") {
    std::string body;
    HttpResponseParser headers({10, 65536});
    REQUIRE_THROWS_AS(feed(headers, std::string(65537, 'x'), body), std::runtime_error);
    HttpResponseParser chunk({10, 65536});
    feed(chunk, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n", body);
    REQUIRE_THROWS_AS(feed(chunk, "1;" + std::string(1024, 'x'), body), std::runtime_error);
    HttpResponseParser trailer({10, 64});
    feed(trailer, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n", body);
    REQUIRE_THROWS_AS(feed(trailer, "X-Trailer: " + std::string(64, 'x'), body), std::runtime_error);
}

TEST_CASE("Strict HTTP streams full archive limit without accumulating its body", "[http][update-transport][http-response]") {
    constexpr uint64_t limit = 512ULL * 1024 * 1024;
    HttpResponseParser parser({limit, 65536});
    std::string unused;
    feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 536870912\r\n\r\n", unused);
    std::array<uint8_t, 8192> bytes{};
    uint64_t count = 0;
    const auto sink = [&](const uint8_t* data, size_t size) {
        REQUIRE(data == bytes.data());
        REQUIRE(size <= bytes.size());
        count += size;
    };
    for(uint64_t i = 0; i < limit; i += bytes.size()) parser.feed(bytes.data(), bytes.size(), sink);
    REQUIRE(parser.complete());
    REQUIRE(count == limit);
    REQUIRE(parser.received() == limit);
    REQUIRE_THROWS_AS(parser.feed(bytes.data(), 1, sink), std::runtime_error);
}

TEST_CASE("Strict HTTP recognizes GET redirects without consuming their bodies", "[http][update-transport][http-response]") {
    for(unsigned code : {301U, 302U, 303U, 307U, 308U}) {
        HttpResponseParser parser({5, 65536});
        std::string body;
        feed(parser, "HTTP/1.1 " + std::to_string(code) + " Redirect\r\nLocation: /next\r\n\r\nignored", body);
        REQUIRE(parser.complete());
        REQUIRE(parser.redirect());
        REQUIRE(parser.location() == "/next");
        REQUIRE(body.empty());
    }
    for(const std::string headers : {"", "Location: \r\n", "Location: /a\r\nLocation: /b\r\n"}) {
        HttpResponseParser parser({5, 65536});
        std::string body;
        REQUIRE_THROWS_AS(feed(parser, "HTTP/1.1 302 Found\r\n" + headers + "\r\n", body), std::runtime_error);
    }
}

TEST_CASE("Strict HTTP makes a failed body sink terminal", "[http][update-transport][http-response]") {
    HttpResponseParser parser({5, 65536});
    std::string body;
    feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n", body);
    const uint8_t bytes[] = {'h', 'e', 'l', 'l', 'o'};
    REQUIRE_THROWS_AS(parser.feed(bytes, 5, [](const uint8_t*, size_t) { throw std::runtime_error("disk full"); }), std::runtime_error);
    REQUIRE_FALSE(parser.complete());
    REQUIRE_THROWS_AS(feed(parser, "hello", body), std::runtime_error);
}

TEST_CASE("Strict HTTP provider outages are opt-in and fully framed", "[http][http-response][http-outage]") {
    for(unsigned code : {500U, 502U, 503U, 504U}) {
        const auto bytes = "HTTP/1.1 " + std::to_string(code) + " Unavailable\r\nContent-Length: 4\r\n\r\noops";
        HttpResponseParser ordinary({4, 65536});
        std::string body;
        CHECK_THROWS(feed(ordinary, bytes, body));
        for(size_t split = 0; split <= bytes.size(); ++split) {
            HttpResponseParser parser({4, 65536}, true);
            body.clear();
            feed(parser, bytes.substr(0, split), body);
            if(split < bytes.size()) CHECK_FALSE(parser.complete());
            feed(parser, bytes.substr(split), body);
            CHECK(parser.complete()); CHECK(parser.unavailable());
            CHECK_FALSE(parser.redirect()); CHECK(body == "oops");
        }
    }
}
