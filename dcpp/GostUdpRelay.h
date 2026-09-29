#pragma once
#include "Socket.h"
#include <memory>

namespace dcpp {
// One loopback UDP association; all network I/O belongs to its worker thread.
class GostUdpRelay {
public:
    explicit GostUdpRelay(Socket::StreamProxyConfig config, uint32_t setupTimeoutMs = 10000,
        std::vector<std::string> candidates = {}, std::shared_ptr<void> admission = {});
    ~GostUdpRelay();
    uint16_t start(const std::string& bindIp, const std::string& expectedSender, uint16_t expectedPort);
    void requestStop();
    void join();
    bool isRunning() const;
    GostUdpRelay(const GostUdpRelay&) = delete;
    GostUdpRelay& operator=(const GostUdpRelay&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
