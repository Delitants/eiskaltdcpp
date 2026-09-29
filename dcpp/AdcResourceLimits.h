#pragma once

#include <cstddef>
#include <cmath>
#include <limits>
#include <string_view>
#include "ProtocolNumber.h"

namespace dcpp {

inline bool adcPasswordChallengeSize(std::string_view encoded, size_t maxEncoded, size_t& decoded) {
    if(encoded.empty() || encoded.size() > maxEncoded)
        return false;
    const size_t remainder = encoded.size() % 8;
    if(remainder == 1 || remainder == 3 || remainder == 6)
        return false;
    const size_t bytes = (encoded.size() / 8) * 5 + (remainder * 5) / 8;
    if(bytes < 24)
        return false;
    unsigned last = 0;
    for(char c : encoded) {
        if(c >= 'A' && c <= 'Z') last = static_cast<unsigned>(c - 'A');
        else if(c >= '2' && c <= '7') last = static_cast<unsigned>(c - '2' + 26);
        else return false;
    }
    const unsigned unused = static_cast<unsigned>((remainder * 5) % 8);
    if((last & ((1U << unused) - 1)) != 0)
        return false;
    decoded = bytes;
    return true;
}

struct BloomRequest {
    size_t bits = 0;
    size_t hashes = 0;
    size_t hashBits = 0;
};

enum class BloomRequestError { None, Hashes, HashBits, Size };

inline BloomRequestError parseBloomRequest(std::string_view byteText, std::string_view hashText,
        std::string_view hashBitsText, size_t maxBytes, BloomRequest& output) {
    BloomRequest request;
    if(!parseProtocolNumber(hashText, size_t(1), size_t(8), request.hashes))
        return BloomRequestError::Hashes;
    if(!parseProtocolNumber(hashBitsText, size_t(1), size_t(64), request.hashBits) ||
            request.hashes > 192 / request.hashBits)
        return BloomRequestError::HashBits;
    size_t bytes;
    if(!parseProtocolNumber(byteText, size_t(0), maxBytes, bytes) || bytes % 8 != 0 ||
            bytes > std::numeric_limits<size_t>::max() / 8)
        return BloomRequestError::Size;
    request.bits = bytes * 8;
    if(request.hashBits < std::numeric_limits<size_t>::digits &&
            request.bits > (size_t(1) << request.hashBits))
        return BloomRequestError::Size;
    output = request;
    return BloomRequestError::None;
}

inline bool bloomShareBudgetAllows(const BloomRequest& request, size_t sharedFiles) {
    // Preserve the existing five-times rounded ideal budget without narrowing it.
    const long double ideal = static_cast<long double>(sharedFiles) * request.hashes / std::log(2.0L);
    const long double budget = 5.0L * std::ceil(std::floor(ideal) / 64.0L) * 64.0L;
    return static_cast<long double>(request.bits) <= budget;
}

inline BloomRequestError validateBloomRequest(std::string_view byteText, std::string_view hashText,
        std::string_view hashBitsText, size_t sharedFiles, size_t maxBytes, BloomRequest& output) {
    BloomRequest request;
    const auto error = parseBloomRequest(byteText, hashText, hashBitsText, maxBytes, request);
    if(error != BloomRequestError::None)
        return error;
    if(!bloomShareBudgetAllows(request, sharedFiles))
        return BloomRequestError::Size;
    output = request;
    return BloomRequestError::None;
}

} // namespace dcpp
