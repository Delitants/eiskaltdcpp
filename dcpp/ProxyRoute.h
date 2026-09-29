#pragma once

#include "Socket.h"
#include "SocketWake.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace dcpp {
class SettingsManager;

struct ProxyRouteSnapshot {
    int mode = 0;
    uint64_t generation = 0;
    Socket::StreamProxyConfig proxy;
    bool valid = true;
    bool udp = false;
    std::string error, caFile;
    std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<WakeNotifier> notifier = std::make_shared<WakeNotifier>();
};

class ProxyRoute {
public:
    ProxyRoute();
    ~ProxyRoute() { stop(); }
    bool reload(const SettingsManager& settings);
    std::shared_ptr<const ProxyRouteSnapshot> snapshot() const;
    void stop();
    bool requiresProxiedPeers() const;
    bool supportsUdp() const;
    std::shared_ptr<void> acquireUdpSlot();
private:
    std::shared_ptr<ProxyRouteSnapshot> (*makeStoppedSnapshot)() = [] {
        return std::make_shared<ProxyRouteSnapshot>();
    };
    mutable std::mutex mutex;
    std::shared_ptr<const ProxyRouteSnapshot> current;
    std::shared_ptr<std::atomic_uint> udpSlots = std::make_shared<std::atomic_uint>(0);
};
}
