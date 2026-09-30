#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dcpp {
// Worker-only; resolves the proxy endpoint, never its destinations.
std::vector<std::string> resolveProxyEndpoint(const std::string& host, uint32_t timeoutMs,
    const std::function<bool()>& cancelled);

// Worker-only local DNS. Call only for an explicitly direct destination route;
// proxied destinations must never use this resolver as a fallback.
std::vector<std::string> resolveDirectEndpoint(const std::string& host, uint32_t timeoutMs,
    const std::function<bool()>& cancelled);

namespace proxy_resolver_detail {
// Explicit DNS transport for deterministic local tests, not a persisted setting.
std::vector<std::string> resolve(const std::string& host, uint32_t timeoutMs,
    const std::function<bool()>& cancelled, const std::string& servers);
}
}
