#pragma once

#include "TorrentTypes.h"

namespace eiskalt::torrent {

Settings loadSettings(const QString &path);
bool saveSettings(const QString &path, const Settings &, QString *error = nullptr);
QString validateSettings(const Settings &);
// Select the torrent route without copying application credentials into settings.
ProxyConfig selectedProxy(const Settings &, const ProxyConfig &applicationProxy);
QString validateProxy(const ProxyConfig &);

} // namespace eiskalt::torrent
