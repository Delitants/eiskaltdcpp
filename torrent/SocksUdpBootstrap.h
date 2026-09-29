#pragma once

#include "TorrentTypes.h"
#include <functional>

namespace eiskalt::torrent {

// Blocking worker-thread helper: bootstrap names use the selected SOCKS5 UDP
// relay, never local DNS. Numeric inputs need no connection.
QStringList proxyBootstrapNodes(const ProxyConfig &proxy, const QStringList &nodes,
                               const std::function<bool()> &cancelled = {}, int timeoutMs = 5000);

} // namespace eiskalt::torrent
