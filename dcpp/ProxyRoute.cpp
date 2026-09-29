#include "stdinc.h"
#include "ProxyRoute.h"
#include "ProxyTrust.h"
#include "SettingsManager.h"

namespace dcpp {
namespace {
using SM = SettingsManager;
using Config = Socket::StreamProxyConfig;
void setCancellation(ProxyRouteSnapshot& value) {
    value.proxy.cancelled = [token = value.revoked] { return token->load(); };
    value.proxy.cancellationNotifier = value.notifier;
    if(value.revoked->load()) value.notifier->notify();
}
bool same(const ProxyRouteSnapshot& a, const ProxyRouteSnapshot& b) {
    const auto& x = a.proxy;
    const auto& y = b.proxy;
    return a.mode == b.mode && a.valid == b.valid && a.error == b.error && a.caFile == b.caFile &&
        a.udp == b.udp && x.type == y.type && x.host == y.host && x.port == y.port &&
        x.user == y.user && x.password == y.password && x.cipher == y.cipher &&
        x.tls == y.tls && x.verifyTls == y.verifyTls && x.remoteDns == y.remoteDns && x.caPem == y.caPem;
}
ProxyRouteSnapshot read(const SM& sm) {
    ProxyRouteSnapshot result;
    result.mode = sm.get(SM::OUTGOING_CONNECTIONS, true);
    auto& config = result.proxy;
    switch(result.mode) {
    case SM::OUTGOING_DIRECT: break;
    case SM::OUTGOING_SOCKS5:
        config.type = Config::Socks5;
        config.host = sm.get(SM::SOCKS_SERVER);
        config.port = sm.get(SM::SOCKS_PORT);
        config.user = sm.get(SM::SOCKS_USER);
        config.password = sm.get(SM::SOCKS_PASSWORD);
        config.remoteDns = sm.getBool(SM::SOCKS_RESOLVE);
        config.tls = sm.getBool(SM::SOCKS_TLS);
        config.verifyTls = false;
        result.udp = !config.tls;
        break;
    case SM::OUTGOING_SHADOWSOCKS:
        config.type = Config::Shadowsocks;
        config.host = sm.get(SM::SHADOWSOCKS_SERVER);
        config.port = sm.get(SM::SHADOWSOCKS_PORT);
        config.password = sm.get(SM::SHADOWSOCKS_PASSWORD);
        config.cipher = sm.get(SM::SHADOWSOCKS_METHOD);
        config.remoteDns = sm.getBool(SM::SOCKS_RESOLVE);
        result.udp = sm.get(SM::SHADOWSOCKS_TRANSPORT) == SM::SHADOWSOCKS_TRANSPORT_TCP_AND_UDP;
        break;
    case SM::OUTGOING_GOST:
        config.type = Config::Gost;
        config.host = sm.get(SM::GOST_SERVER);
        config.port = sm.get(SM::GOST_PORT);
        config.user = sm.get(SM::GOST_USER);
        config.password = sm.get(SM::GOST_PASSWORD);
        config.tls = config.verifyTls = config.remoteDns = true;
        result.udp = true;
        result.caFile = sm.get(SM::GOST_CA_FILE);
        if(config.host.empty() || config.host.size() > 255 || config.host.find('\0') != std::string::npos ||
           config.port <= 0 || config.port > 65535 || config.user.empty() || config.user.size() > 255 ||
           config.password.empty() || config.password.size() > 255) {
            result.valid = false;
            result.error = "GOST requires a valid server, port, and 1-255 byte username and password";
        } else {
            try { config.caPem = loadProxyCaPem(result.caFile); }
            catch(const ProxyTrustError& error) { result.valid = false; result.error = error.what(); }
        }
        break;
    default:
        result.valid = false;
        result.error = "Unknown global proxy mode";
    }
    result.revoked->store(!result.valid);
    setCancellation(result);
    return result;
}
}

ProxyRoute::ProxyRoute() {
    auto initial = std::make_shared<ProxyRouteSnapshot>();
    setCancellation(*initial);
    current = initial;
}
bool ProxyRoute::reload(const SettingsManager& settings) {
    std::shared_ptr<WakeNotifier> revoked;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto next = std::make_shared<ProxyRouteSnapshot>(read(settings));
        if(same(*current, *next) && !(current->valid && current->revoked->load())) return false;
        current->revoked->store(true);
        revoked = current->notifier;
        next->generation = current->generation + 1;
        current = std::move(next);
    }
    revoked->notify();
    return true;
}
std::shared_ptr<const ProxyRouteSnapshot> ProxyRoute::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return current;
}
void ProxyRoute::stop() {
    std::shared_ptr<WakeNotifier> revoked;
    {
        std::lock_guard<std::mutex> lock(mutex);
        current->revoked->store(true);
        revoked = current->notifier;
        try {
            auto next = makeStoppedSnapshot();
            next->valid = false;
            next->error = "Proxy route stopped";
            next->revoked->store(true);
            setCancellation(*next);
            next->mode = current->mode;
            next->generation = current->generation + 1;
            current = std::move(next);
        } catch(...) {
            // Revocation must survive allocation pressure, including shutdown.
            // Keeping the revoked snapshot is safe; publishing a label is optional.
        }
    }
    revoked->notify();
}
bool ProxyRoute::requiresProxiedPeers() const { return snapshot()->mode == SM::OUTGOING_GOST; }
bool ProxyRoute::supportsUdp() const { auto value = snapshot(); return value->valid && !value->revoked->load() && value->udp; }
std::shared_ptr<void> ProxyRoute::acquireUdpSlot() {
    unsigned count = udpSlots->load();
    do { if(count >= 8) return {}; }
    while(!udpSlots->compare_exchange_weak(count, count + 1));
    return std::shared_ptr<void>(udpSlots.get(), [slots = udpSlots](void*) { --*slots; });
}
}
