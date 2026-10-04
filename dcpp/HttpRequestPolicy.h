#pragma once

#include "HttpResponseParser.h"
#include "ProxyRoute.h"
#include <functional>
#include <memory>
#include <string>

namespace dcpp {
struct HttpRequestPolicy {
    struct Target {
        std::string url, host, port = "443", path, address;
    };
    HttpResponseParser::Limits limits;
    unsigned redirects = 5;
    uint32_t connectMs = 15000, idleMs = 30000, totalMs = 30 * 60 * 1000;
    std::shared_ptr<const ProxyRouteSnapshot> route;
    std::function<bool()> cancelled;
    std::shared_ptr<WakeNotifier> cancellationNotifier;
    // Called on the I/O worker for the initial URL and every Location. Must
    // validate the namespace and resolve on this route, returning a pinned IP.
    std::function<Target(const std::string&, const std::string&,
        const ProxyRouteSnapshot&, uint32_t, const std::function<bool()>&)> prepare;
    // Empty selects default HTTPS roots, never the DC or proxy trust settings.
    std::string rootsPem;
    // Fixed MIME selector, not an arbitrary origin-header injection API.
    bool dnsMessage = false;
};
}
