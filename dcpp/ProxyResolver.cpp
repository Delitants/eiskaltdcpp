#include "stdinc.h"
#include "ProxyResolver.h"
#include "Socket.h"
#include <ares.h>
#include <chrono>
#include <map>
#include <thread>
#ifndef _WIN32
#include <poll.h>
#endif

namespace dcpp {
namespace {
struct Library {
    int status = ares_library_init(ARES_LIB_INIT_ALL);
    ~Library() { if(status == ARES_SUCCESS) ares_library_cleanup(); }
};
struct Query {
    bool done = false, failed = false;
    int status = ARES_ECANCELLED;
    std::map<ares_socket_t, short> sockets;
    std::vector<std::string> addresses;
};
void socketState(void* arg, ares_socket_t fd, int readable, int writable) noexcept {
    auto& state = *static_cast<Query*>(arg);
    try {
        if(!readable && !writable) state.sockets.erase(fd);
        else state.sockets[fd] = (readable ? POLLIN : 0) | (writable ? POLLOUT : 0);
    } catch(...) { state.failed = true; }
}
void completed(void* arg, int status, int, ares_addrinfo* result) noexcept {
    auto& state = *static_cast<Query*>(arg);
    state.status = status;
    state.done = true;
    try {
        if(status == ARES_SUCCESS && result) {
            for(auto* node = result->nodes; node && state.addresses.size() < 32; node = node->ai_next) {
                char text[INET6_ADDRSTRLEN]{};
                const void* address = nullptr;
                if(node->ai_family == AF_INET && node->ai_addrlen >= sizeof(sockaddr_in))
                    address = &reinterpret_cast<sockaddr_in*>(node->ai_addr)->sin_addr;
                else if(node->ai_family == AF_INET6 && node->ai_addrlen >= sizeof(sockaddr_in6))
                    address = &reinterpret_cast<sockaddr_in6*>(node->ai_addr)->sin6_addr;
                if(address && inet_ntop(node->ai_family, address, text, sizeof(text)) &&
                   std::find(state.addresses.begin(), state.addresses.end(), text) == state.addresses.end())
                    state.addresses.emplace_back(text);
            }
        }
    } catch(...) { state.failed = true; }
    if(result) ares_freeaddrinfo(result);
}
}

std::vector<std::string> proxy_resolver_detail::resolve(const std::string& host, uint32_t timeoutMs,
    const std::function<bool()>& cancelled, const std::string& servers) {
    const auto start = std::chrono::steady_clock::now();
    auto check = [&] {
        if(cancelled && cancelled()) throw SocketException("Proxy hostname lookup cancelled");
        if(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(timeoutMs))
            throw SocketException("Proxy hostname lookup timed out");
    };
    check();
    if(host.empty() || host.size() > 255 || host.find('\0') != std::string::npos)
        throw SocketException("Invalid proxy hostname");
    in_addr v4{};
    in6_addr v6{};
    if(inet_pton(AF_INET, host.c_str(), &v4) == 1 || inet_pton(AF_INET6, host.c_str(), &v6) == 1)
        return {host};
    static Library library;
    if(library.status != ARES_SUCCESS) throw SocketException("Cannot initialize proxy resolver");
    Query state;
    ares_options options{};
    options.sock_state_cb = socketState;
    options.sock_state_cb_data = &state;
    options.flags = ARES_FLAG_NOSEARCH;
    char lookups[] = "fb";
    options.lookups = lookups;
    ares_channel raw = nullptr;
    if(ares_init_options(&raw, &options, ARES_OPT_SOCK_STATE_CB | ARES_OPT_FLAGS | ARES_OPT_LOOKUPS) != ARES_SUCCESS)
        throw SocketException("Cannot initialize proxy lookup");
    // Destruction cancels outstanding operations before their callback state dies.
    std::unique_ptr<ares_channeldata, decltype(&ares_destroy)> channel(raw, ares_destroy);
    if(!servers.empty() && ares_set_servers_ports_csv(raw, servers.c_str()) != ARES_SUCCESS)
        throw SocketException("Invalid proxy resolver configuration");
    ares_addrinfo_hints hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    // Sorting otherwise probes the returned endpoints with direct connections.
    hints.ai_flags = ARES_AI_NOSORT;
    ares_getaddrinfo(raw, host.c_str(), nullptr, &hints, completed, &state);
    while(!state.done && !state.failed) {
        check();
#ifdef _WIN32
        std::vector<WSAPOLLFD> fds;
#else
        std::vector<pollfd> fds;
#endif
        for(const auto& [fd, events] : state.sockets) fds.push_back({fd, events, 0});
        timeval maximum{0, 25000}, value{};
        const auto* timeout = ares_timeout(raw, &maximum, &value);
        const auto remaining = timeoutMs - std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        const int waitMs = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(remaining,
            timeout->tv_sec * 1000 + (timeout->tv_usec + 999) / 1000)));
        int ready = 0;
        if(fds.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        else {
#ifdef _WIN32
            ready = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), waitMs);
#else
            ready = ::poll(fds.data(), fds.size(), waitMs);
#endif
        }
        check();
        if(ready > 0) {
            for(const auto& fd : fds) {
                if(!fd.revents || !state.sockets.count(fd.fd)) continue;
                ares_process_fd(raw, fd.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL) ? fd.fd : ARES_SOCKET_BAD,
                    fd.revents & POLLOUT ? fd.fd : ARES_SOCKET_BAD);
            }
        } else {
            ares_process_fd(raw, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
        }
    }
    check();
    if(state.failed || state.status != ARES_SUCCESS || state.addresses.empty())
        throw SocketException("Proxy hostname lookup failed");
    return state.addresses;
}

std::vector<std::string> resolveProxyEndpoint(const std::string& host, uint32_t timeoutMs,
    const std::function<bool()>& cancelled) {
    return proxy_resolver_detail::resolve(host, timeoutMs, cancelled, {});
}
}
