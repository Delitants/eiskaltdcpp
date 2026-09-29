#pragma once

#include "typedefs.h"
#include <span>

namespace dcpp::gost {

inline constexpr size_t MaxPayload = 65507;
inline constexpr size_t MaxTunnelFrame = MaxPayload + 262;

struct Datagram {
    std::string host;
    uint16_t port = 0;
    ByteVector payload;
};

enum class DecodeResult { Complete, NeedMore, Invalid };

// No DNS resolution. Numeric IPs use their native ATYP; domains remain remote.
// Encoders throw std::invalid_argument for invalid addresses, ports or sizes.
ByteVector encodeTunnel(const Datagram& datagram);
ByteVector encodeSocks(const Datagram& datagram);
// On NeedMore/Invalid, consumed is zero and datagram is unchanged.
DecodeResult decodeTunnel(std::span<const uint8_t> input, Datagram& datagram, size_t& consumed);
// A standard datagram is one complete UDP packet; truncation is Invalid.
DecodeResult decodeSocks(std::span<const uint8_t> input, Datagram& datagram);

} // namespace dcpp::gost
