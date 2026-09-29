#include "stdinc.h"
#include "GostProtocol.h"
#include "Socket.h"
#include <stdexcept>

namespace dcpp::gost {
namespace {
bool validDomain(const std::string& host) {
    return !host.empty() && host.size() <= 255 &&
        host.find_first_of(":[]%") == std::string::npos &&
        std::none_of(host.begin(), host.end(), [](unsigned char c) { return c <= 32 || c == 127; });
}

ByteVector encode(const Datagram& datagram, bool tunnel) {
    if(!datagram.port || datagram.payload.size() > MaxPayload || (tunnel && datagram.payload.empty()))
        throw std::invalid_argument("Invalid GOST datagram port or payload length");
    ByteVector result;
    result.reserve(262 + datagram.payload.size());
    const size_t length = tunnel ? datagram.payload.size() : 0;
    result.insert(result.end(), {uint8_t(length >> 8), uint8_t(length), uint8_t(tunnel ? 0xff : 0)});
    in_addr v4{};
    in6_addr v6{};
    const uint8_t* address = nullptr;
    size_t addressLength = 0;
    // Some platforms accept a scope suffix in inet_pton, but SOCKS cannot carry it.
    if(datagram.host.find('\0') != std::string::npos || datagram.host.find_first_of("[]%") != std::string::npos)
        throw std::invalid_argument("Invalid GOST datagram address");
    if(inet_pton(AF_INET, datagram.host.c_str(), &v4) == 1) {
        result.push_back(1);
        address = reinterpret_cast<const uint8_t*>(&v4);
        addressLength = 4;
    } else if(inet_pton(AF_INET6, datagram.host.c_str(), &v6) == 1) {
        result.push_back(4);
        address = reinterpret_cast<const uint8_t*>(&v6);
        addressLength = 16;
    } else {
        if(!validDomain(datagram.host))
            throw std::invalid_argument("Invalid GOST datagram address");
        result.push_back(3);
        result.push_back(uint8_t(datagram.host.size()));
        address = reinterpret_cast<const uint8_t*>(datagram.host.data());
        addressLength = datagram.host.size();
    }
    result.insert(result.end(), address, address + addressLength);
    result.push_back(uint8_t(datagram.port >> 8));
    result.push_back(uint8_t(datagram.port));
    result.insert(result.end(), datagram.payload.begin(), datagram.payload.end());
    return result;
}

DecodeResult decode(std::span<const uint8_t> input, Datagram& datagram, size_t& consumed, bool tunnel) {
    consumed = 0;
    const auto shortInput = tunnel ? DecodeResult::NeedMore : DecodeResult::Invalid;
    if(input.size() < 3) return shortInput;
    const size_t length = size_t(input[0]) << 8 | input[1];
    // gosocks5/v0.5.0 interprets RSV=0 as unframed data, even on a TCP tunnel.
    if(tunnel ? (!length || length > MaxPayload || input[2] != 0xff) : (length || input[2]))
        return DecodeResult::Invalid;
    if(input.size() < 4) return shortInput;
    const uint8_t atyp = input[3];
    size_t offset = 4, addressLength;
    if(atyp == 1) addressLength = 4;
    else if(atyp == 4) addressLength = 16;
    else if(atyp == 3) {
        if(input.size() < 5) return shortInput;
        addressLength = input[offset++];
        if(!addressLength) return DecodeResult::Invalid;
    } else return DecodeResult::Invalid;
    if(input.size() < offset + addressLength + 2) return shortInput;
    const size_t portOffset = offset + addressLength;
    const uint16_t port = uint16_t(input[portOffset] << 8 | input[portOffset + 1]);
    if(!port) return DecodeResult::Invalid;
    Datagram result;
    if(atyp == 3) {
        result.host.assign(reinterpret_cast<const char*>(input.data() + offset), addressLength);
        if(!validDomain(result.host)) {
            // libtorrent's proxy_hostnames path also uses DOMAIN for numeric IPv6.
            // Parse only a literal here; never resolve or accept scoped addresses.
            in6_addr v6{};
            if(result.host.find_first_of("[]%") != std::string::npos ||
               std::any_of(result.host.begin(), result.host.end(), [](unsigned char c) { return c <= 32 || c == 127; }) ||
               inet_pton(AF_INET6, result.host.c_str(), &v6) != 1)
                return DecodeResult::Invalid;
        }
    } else {
        char host[INET6_ADDRSTRLEN]{};
        if(!inet_ntop(atyp == 1 ? AF_INET : AF_INET6, input.data() + offset, host, sizeof(host)))
            return DecodeResult::Invalid;
        result.host = host;
    }
    offset = portOffset + 2;
    const size_t payloadLength = tunnel ? length : input.size() - offset;
    if(payloadLength > MaxPayload) return DecodeResult::Invalid;
    if(input.size() - offset < payloadLength) return shortInput;
    result.port = port;
    result.payload.assign(input.begin() + offset, input.begin() + offset + payloadLength);
    datagram = std::move(result);
    consumed = offset + payloadLength;
    return DecodeResult::Complete;
}
} // namespace

ByteVector encodeTunnel(const Datagram& datagram) { return encode(datagram, true); }
ByteVector encodeSocks(const Datagram& datagram) { return encode(datagram, false); }
DecodeResult decodeTunnel(std::span<const uint8_t> input, Datagram& datagram, size_t& consumed) {
    return decode(input, datagram, consumed, true);
}
DecodeResult decodeSocks(std::span<const uint8_t> input, Datagram& datagram) {
    size_t consumed = 0;
    return decode(input, datagram, consumed, false);
}
} // namespace dcpp::gost
