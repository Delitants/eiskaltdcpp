// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cerrno>
#include <ctime>
#include <string>

namespace dcpp::detail {

// Keep the DC++ retry ceiling, including when strftime returns an empty result
// without setting errno. The formatter parameter makes that case testable.
template<class Formatter>
std::string formatTimeBuffer(const std::string& format, const std::tm& time,
    Formatter formatter)
{
    constexpr size_t initialExtra = 256;
    constexpr size_t growth = 64;
    constexpr size_t retries = 1000;
    std::string buffer;
    if(format.empty() || format.size() > buffer.max_size() - initialExtra - retries * growth)
        return {};

    size_t capacity = format.size() + initialExtra;
    for(size_t attempt = 0; attempt <= retries; ++attempt, capacity += growth) {
        buffer.resize(capacity);
        errno = 0;
        const auto size = formatter(buffer.data(), capacity - 1, format.c_str(), &time);
        if(size) {
            buffer.resize(size);
            return buffer;
        }
        if(errno == EINVAL)
            return {};
    }
    return {};
}

} // namespace dcpp::detail
