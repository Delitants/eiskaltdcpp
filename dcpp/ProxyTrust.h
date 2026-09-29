#pragma once

#include <stdexcept>
#include <string>

namespace dcpp {
class ProxyTrustError : public std::runtime_error {
public:
    enum Reason { Unreadable, TooLarge, Invalid };
    explicit ProxyTrustError(Reason reason);
    Reason reason() const noexcept { return reason_; }
private:
    Reason reason_;
};

// An empty path selects system trust. Explicit files never fall back to it.
std::string loadProxyCaPem(const std::string& path);
}
