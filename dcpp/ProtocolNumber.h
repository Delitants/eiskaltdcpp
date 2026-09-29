#pragma once

#include <charconv>
#include <string_view>
#include <type_traits>

namespace dcpp {

// Protocol fields are decimal tokens, not permissive user-entered numbers.
template<class Integer>
bool parseProtocolNumber(std::string_view text, Integer minimum, Integer maximum, Integer& value) {
    static_assert(std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>);
    if (text.empty() || text.front() < '0' || text.front() > '9')
        return false;
    Integer parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed < minimum || parsed > maximum)
        return false;
    value = parsed;
    return true;
}

} // namespace dcpp
