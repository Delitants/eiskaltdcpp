#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "dcpp/GostProtocol.h"

using namespace dcpp::gost;
using dcpp::ByteVector;

namespace {
ByteVector domainFrame(const std::string& host, const ByteVector& payload, bool tunnel, uint16_t port = 51413) {
    const size_t length = tunnel ? payload.size() : 0;
    ByteVector bytes{uint8_t(length >> 8), uint8_t(length), uint8_t(tunnel ? 0xff : 0), 3, uint8_t(host.size())};
    bytes.insert(bytes.end(), host.begin(), host.end());
    bytes.push_back(uint8_t(port >> 8));
    bytes.push_back(uint8_t(port));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}
}

TEST_CASE("GOST accepts libtorrent domain-form numeric IPv6 without resolving names", "[gost][wire][ipv6-domain]") {
    for(const auto& host : {"::1", "2001:db8::123", "0:0:0:0:0:0:0:1", "::ffff:192.0.2.1"}) {
        DYNAMIC_SECTION(host) {
            const ByteVector payload{0, 0xff, 7};
            Datagram output;
            REQUIRE(decodeSocks(domainFrame(host, payload, false), output) == DecodeResult::Complete);
            REQUIRE(output.host == host);
            REQUIRE(output.port == 51413);
            REQUIRE(output.payload == payload);
            // Re-encoding the parsed address must use native IPv6, not DNS.
            REQUIRE(encodeSocks(output).at(3) == 4);
            REQUIRE(encodeTunnel(output).at(3) == 4);
            const auto tunnel = domainFrame(host, payload, true);
            size_t consumed = 123;
            REQUIRE(decodeTunnel(tunnel, output, consumed) == DecodeResult::Complete);
            REQUIRE(consumed == tunnel.size());
            REQUIRE(output.host == host);
            REQUIRE(output.payload == payload);
        }
    }
    Datagram name;
    REQUIRE(decodeSocks(domainFrame("unresolvable.invalid", {1}, false), name) == DecodeResult::Complete);
    REQUIRE(name.host == "unresolvable.invalid");
    REQUIRE(encodeTunnel(name).at(3) == 3);
}

TEST_CASE("GOST domain-form IPv6 exception preserves strict address rejection", "[gost][wire][ipv6-domain]") {
    const std::vector<std::string> invalid{
        "", "[::1]", "fe80::1%en0", "fe80::1%1", "::1%", ":::1", "2001:db8::xyz", "host:443",
        "host[part]", "host%part", "::ffff:999.0.0.1", "::1:65536", " ::1", "::1 ", "::1\n", "::1\t",
        std::string("::1\0suffix", 10), std::string("\0::1", 4), std::string("::1\x7f", 4), std::string(255, ':')
    };
    for(const auto& host : invalid) {
        INFO("address bytes=" << host.size());
        Datagram output{"unchanged.invalid", 9, {42}};
        REQUIRE(decodeSocks(domainFrame(host, {1}, false), output) == DecodeResult::Invalid);
        REQUIRE(output.host == "unchanged.invalid");
        REQUIRE(output.port == 9);
        REQUIRE(output.payload == ByteVector{42});
        size_t consumed = 123;
        REQUIRE(decodeTunnel(domainFrame(host, {1}, true), output, consumed) == DecodeResult::Invalid);
        REQUIRE(consumed == 0);
        REQUIRE(output.host == "unchanged.invalid");
    }
}

TEST_CASE("GOST domain-form IPv6 retains length port fragmentation and payload bounds", "[gost][wire][ipv6-domain]") {
    auto tunnel = domainFrame("::1", {1, 2, 3}, true);
    Datagram output{"unchanged.invalid", 9, {42}};
    size_t consumed = 123;
    for(size_t n = 0; n < tunnel.size(); ++n) {
        REQUIRE(decodeTunnel(std::span(tunnel).first(n), output, consumed) == DecodeResult::NeedMore);
        REQUIRE(consumed == 0);
        REQUIRE(output.host == "unchanged.invalid");
    }
    for(bool framed : {false, true}) {
        auto decode = [&](const ByteVector& bytes) {
            return framed ? decodeTunnel(bytes, output, consumed) : decodeSocks(bytes, output);
        };
        REQUIRE(decode(domainFrame("::1", {1}, framed, 0)) == DecodeResult::Invalid);
        auto badLength = domainFrame("::1", {1}, framed);
        badLength[4] = 255;
        REQUIRE(decode(badLength) == (framed ? DecodeResult::NeedMore : DecodeResult::Invalid));
        badLength[4] = 0;
        REQUIRE(decode(badLength) == DecodeResult::Invalid);
        const auto maximum = domainFrame("::1", ByteVector(MaxPayload, 0x71), framed);
        REQUIRE(decode(maximum) == DecodeResult::Complete);
        REQUIRE(output.payload.size() == MaxPayload);
        REQUIRE(decode(domainFrame("::1", ByteVector(MaxPayload + 1, 0x71), framed)) == DecodeResult::Invalid);
    }
}

TEST_CASE("GOST framing matches pinned RSV length and FF fragment", "[gost][wire]") {
    Datagram packet{"127.0.0.1", 53, {0, 0xff, 0x80}};
    REQUIRE(encodeTunnel(packet) == ByteVector{0, 3, 0xff, 1, 127, 0, 0, 1, 0, 53, 0, 0xff, 0x80});
    REQUIRE(encodeSocks(packet) == ByteVector{0, 0, 0, 1, 127, 0, 0, 1, 0, 53, 0, 0xff, 0x80});
}

TEST_CASE("GOST bounded codec handles fragmentation concatenation and remote domains", "[gost][wire]") {
    for(const auto& host : {"192.0.2.1", "2001:db8::123", "unresolvable.invalid"}) {
        DYNAMIC_SECTION(host) {
            Datagram packet{host, 65535, {0, 0xff, 1, 2}};
            auto encoded = encodeTunnel(packet);
            Datagram out{"unchanged", 1, {7}};
            size_t consumed = 123;
            for(size_t n = 0; n < encoded.size(); ++n) {
                REQUIRE(decodeTunnel(std::span(encoded).first(n), out, consumed) == DecodeResult::NeedMore);
                REQUIRE(consumed == 0);
                REQUIRE(out.host == "unchanged");
            }
            const auto copy = encoded;
            encoded.insert(encoded.end(), copy.begin(), copy.end());
            REQUIRE(decodeTunnel(encoded, out, consumed) == DecodeResult::Complete);
            REQUIRE(consumed == encoded.size() / 2);
            REQUIRE(out.host == host);
            REQUIRE(out.port == packet.port);
            REQUIRE(out.payload == packet.payload);
            size_t second = 0;
            REQUIRE(decodeTunnel(std::span(encoded).subspan(consumed), out, second) == DecodeResult::Complete);
            REQUIRE(second == consumed);
            REQUIRE(decodeSocks(encodeSocks(packet), out) == DecodeResult::Complete);
            REQUIRE(out.host == host);
            REQUIRE(out.payload == packet.payload);
        }
    }
}

TEST_CASE("GOST rejects empty tunneled payload but allows empty standard datagram", "[gost][wire]") {
    Datagram packet{"example.invalid", 1, {}};
    REQUIRE_THROWS(encodeTunnel(packet));
    auto standard = encodeSocks(packet);
    Datagram out;
    REQUIRE(decodeSocks(standard, out) == DecodeResult::Complete);
    REQUIRE(out.payload.empty());
    standard[2] = 0xff;
    size_t consumed = 42;
    REQUIRE(decodeTunnel(standard, out, consumed) == DecodeResult::Invalid);
    REQUIRE(consumed == 0);
}

TEST_CASE("GOST bounds maximum payload and validates address port and header", "[gost][wire]") {
    Datagram packet{"::1", 1, ByteVector(65507, 0xab)};
    auto encoded = encodeTunnel(packet);
    Datagram out;
    size_t consumed = 0;
    REQUIRE(decodeTunnel(encoded, out, consumed) == DecodeResult::Complete);
    REQUIRE(out.payload.size() == 65507);
    REQUIRE(consumed == encoded.size());
    REQUIRE(decodeSocks(encodeSocks(packet), out) == DecodeResult::Complete);
    REQUIRE(out.payload.size() == 65507);
    packet.payload.push_back(0);
    REQUIRE_THROWS(encodeTunnel(packet));
    REQUIRE_THROWS(encodeSocks(packet));
    encoded[0] = 0xff; encoded[1] = 0xff;
    REQUIRE(decodeTunnel(encoded, out, consumed) == DecodeResult::Invalid);
    packet.payload = {1};
    for(const auto& host : {std::string(), std::string(256, 'x'), std::string("a\0b", 3), std::string("[::1]"), std::string("fe80::1%en0")}) {
        INFO("host=" << host);
        packet.host = host;
        REQUIRE_THROWS(encodeTunnel(packet));
    }
    packet.host = "example.invalid";
    packet.port = 0;
    REQUIRE_THROWS(encodeTunnel(packet));
    packet.port = 53;
    encoded = encodeTunnel(packet);
    auto bad = encoded; bad[3] = 2;
    REQUIRE(decodeTunnel(bad, out, consumed) == DecodeResult::Invalid);
    bad = encoded; bad[2] = 0;
    REQUIRE(decodeTunnel(bad, out, consumed) == DecodeResult::Invalid);
    bad = encoded; bad[4] = 0;
    REQUIRE(decodeTunnel(bad, out, consumed) == DecodeResult::Invalid);
    bad = encoded; bad[5] = 0;
    REQUIRE(decodeTunnel(bad, out, consumed) == DecodeResult::Invalid);
    bad = encoded; bad[bad.size()-3] = 0; bad[bad.size()-2] = 0;
    REQUIRE(decodeTunnel(bad, out, consumed) == DecodeResult::Invalid);
    auto standard = encodeSocks(packet);
    standard[2] = 1;
    REQUIRE(decodeSocks(standard, out) == DecodeResult::Invalid);
    standard[2] = 0; standard[1] = 1;
    REQUIRE(decodeSocks(standard, out) == DecodeResult::Invalid);
    REQUIRE(decodeSocks(std::span(standard).first(3), out) == DecodeResult::Invalid);
    standard = encodeSocks(packet);
    REQUIRE(decodeSocks(std::span(standard).first(standard.size() - 3), out) == DecodeResult::Invalid);
    packet.host.assign(255, 'x');
    REQUIRE(decodeTunnel(encodeTunnel(packet), out, consumed) == DecodeResult::Complete);
    REQUIRE(out.host == packet.host);
}
