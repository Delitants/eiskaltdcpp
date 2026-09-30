#include "UpdateUrlPolicy.h"
#include <QHostAddress>
#include <QAbstractSocket>

namespace updates {
bool allowedUrl(const QUrl& url, ResourceKind kind, bool redirect) {
    if(!url.isValid() || url.isRelative() || url.scheme() != QStringLiteral("https") ||
        url.hasFragment() || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@')) ||
        url.port(443) != 443 || url.toEncoded().size() > 16384) return false;
    const auto path = url.path(QUrl::FullyEncoded);
    if(path.isEmpty() || path.size() > 2048 || path.contains(QLatin1Char('%')) ||
        path.contains(QLatin1Char('\\')) || path.contains(QStringLiteral("//"))) return false;
    for(const auto& component : path.split(QLatin1Char('/'))) {
        if(component == QStringLiteral(".") || component == QStringLiteral("..")) return false;
    }
    const auto host = url.host();
    QString prefix;
    if(kind == ResourceKind::Feed) {
        if(host != QStringLiteral("raw.githubusercontent.com")) return false;
        prefix = QStringLiteral("/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/");
    } else if(host == QStringLiteral("github.com")) {
        prefix = QStringLiteral("/Delitants/eiskaltdcpp/releases/download/");
    } else {
        return redirect && (host == QStringLiteral("release-assets.githubusercontent.com") ||
            host == QStringLiteral("objects.githubusercontent.com"));
    }
    return path.startsWith(prefix) && path.size() > prefix.size();
}

bool publicAddress(const QString& text) {
    if(text.isEmpty() || text != text.trimmed() || text.contains(QLatin1Char('%')) ||
        text.contains(QLatin1Char('[')) || text.contains(QLatin1Char(']'))) return false;
    const QHostAddress address(text);
    if(address.protocol() == QAbstractSocket::IPv4Protocol) {
        // Reject noncanonical IPv4 forms accepted by platform parsers.
        if(address.toString() != text) return false;
        const quint32 ip = address.toIPv4Address();
        const auto subnet = [ip](quint32 network, unsigned bits) {
            return (ip >> (32 - bits)) == (network >> (32 - bits));
        };
        return !(subnet(0x00000000U, 8) || subnet(0x0a000000U, 8) ||
            subnet(0x64400000U, 10) || subnet(0x7f000000U, 8) ||
            subnet(0xa9fe0000U, 16) || subnet(0xac100000U, 12) ||
            subnet(0xc0000000U, 24) || subnet(0xc0000200U, 24) ||
            subnet(0xc0586300U, 24) || subnet(0xc0a80000U, 16) ||
            subnet(0xc6120000U, 15) || subnet(0xc6336400U, 24) ||
            subnet(0xcb007100U, 24) || ip >= 0xe0000000U);
    }
    if(address.protocol() != QAbstractSocket::IPv6Protocol) return false;
    const auto bytes = address.toIPv6Address();
    // Only native global-unicast space; no mapped, translation, or 6to4 routes.
    if((bytes[0] & 0xe0) != 0x20) return false;
    if(bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] < 2) return false;
    if(bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0d && bytes[3] == 0xb8) return false;
    if(bytes[0] == 0x20 && bytes[1] == 0x02) return false;
    if(bytes[0] == 0x3f && bytes[1] == 0xff && (bytes[2] & 0xf0) == 0) return false;
    return true;
}
}
