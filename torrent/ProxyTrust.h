#pragma once

#include "TorrentTypes.h"

namespace eiskalt::torrent {

// Call before comparing runtime configurations. A false result must stop routing;
// an empty snapshot selects system roots only when caFile is also empty.
bool loadProxyTrust(ProxyConfig &proxy, QString *error);

} // namespace eiskalt::torrent
