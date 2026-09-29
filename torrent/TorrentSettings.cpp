#include "TorrentSettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocale>
#include <QSet>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>
#include <limits>

namespace eiskalt::torrent {
namespace {
class TorrentSettingsText {
    Q_DECLARE_TR_FUNCTIONS(TorrentSettings)
};

// Only torrent-owned profiles are serialized. Never copy application credentials.
#define BOOL_FIELDS(X) X(enabled) X(dht) X(pex) X(localDiscovery) X(tcp) X(utp) X(portMapping) X(shareCompleted) X(randomizePort) X(blockUnknownClients)
#define INT_FIELDS(X) X(downloadLimitKiB) X(uploadLimitKiB) X(activeDownloads) X(connectionLimit) X(perTorrentConnections) X(seedMinutes) X(listenPort)
#define STRING_FIELDS(X) X(downloadPath) X(completedPath) X(bindAddress) X(bindAddress6) X(bootstrapNodes) X(shareName)

Settings invalidDocument() {
    Settings s;
    s.enabled = false;
    s.proxyMode = ProxyMode::RequireProxy;
    return s;
}

QJsonObject proxyJson(const ProxyConfig &p) {
    QJsonObject result{{"type", static_cast<int>(p.type)}, {"host", p.host}, {"port", p.port},
        {"user", p.user}, {"password", p.password}, {"cipher", p.cipher}, {"udp", p.udp}};
    if (p.type == ProxyType::Gost) result.insert("caFile", p.caFile);
    return result;
}

bool readProxy(const QJsonValue &value, ProxyConfig &p) {
    if (!value.isObject()) return false;
    const auto obj = value.toObject();
    for (const auto *key : {"type", "port"})
        if (!obj[key].isDouble() || obj[key].toDouble() != obj[key].toInt()) return false;
    for (const auto *key : {"host", "user", "password", "cipher"})
        if (!obj[key].isString()) return false;
    if (!obj["udp"].isBool()) return false;
    if (obj.contains("caFile") && !obj["caFile"].isString()) return false;
    p.type = static_cast<ProxyType>(obj["type"].toInt());
    p.port = obj["port"].toInt();
    p.host = obj["host"].toString();
    p.user = obj["user"].toString();
    p.password = obj["password"].toString();
    p.cipher = obj["cipher"].toString();
    p.udp = obj["udp"].toBool();
    p.remoteDns = true;
    p.caFile = obj["caFile"].toString();
    p.caPem.clear();
    return true;
}
}

ProxyConfig selectedProxy(const Settings &s, const ProxyConfig &applicationProxy) {
    if (s.proxyMode == ProxyMode::Direct) return {};
    if (s.proxyMode != ProxyMode::Custom) return applicationProxy;
    auto proxy = s.customProxyType == ProxyType::Gost ? s.gostProxy :
        (s.customProxyType == ProxyType::Shadowsocks ? s.shadowsocksProxy : s.socks5Proxy);
    // GOST tunnels UDP; the other encrypted adapters remain TCP-only.
    if (proxy.type != ProxyType::Socks5 && proxy.type != ProxyType::Gost) proxy.udp = false;
    proxy.remoteDns = true;
    return proxy;
}

QString validateProxy(const ProxyConfig &p) {
    if (p.type != ProxyType::Socks5 && p.type != ProxyType::Socks5Tls &&
        p.type != ProxyType::Shadowsocks && p.type != ProxyType::Gost)
        return TorrentSettingsText::tr("Select a supported Torrent proxy type.");
    const QRegularExpression host(QStringLiteral(R"(^(?:[a-zA-Z0-9_.-]+|[0-9a-fA-F:%.]+)$)"));
    if (p.host.isEmpty() || p.host.size() > 253 || !host.match(p.host).hasMatch() ||
        p.port < 1 || p.port > 65535)
        return TorrentSettingsText::tr("Enter a Torrent proxy hostname or IP address and a port between 1 and 65535.");
    if (!p.remoteDns)
        return TorrentSettingsText::tr("Torrent proxy routing requires proxy-side hostname resolution.");
    if (p.type == ProxyType::Gost && (p.user.isEmpty() || p.password.isEmpty()))
        return TorrentSettingsText::tr("GOST authentication requires a username and password.");
    if (p.caFile.contains(QChar(0)))
        return TorrentSettingsText::tr("Invalid Torrent proxy CA certificate path.");
    if (p.user.contains(QChar(0)) || p.password.contains(QChar(0)) ||
        p.user.toUtf8().size() > 255 || p.password.toUtf8().size() > 4096)
        return TorrentSettingsText::tr("Invalid Torrent proxy credentials.");
    if (p.type != ProxyType::Shadowsocks) {
        if (p.password.toUtf8().size() > 255 || (p.user.isEmpty() && !p.password.isEmpty()))
            return TorrentSettingsText::tr("SOCKS5 authentication requires a username and credentials of at most 255 bytes each.");
    } else {
        const QStringList ciphers{"aes-256-gcm", "aes-128-gcm", "chacha20-ietf-poly1305",
            "2022-blake3-aes-128-gcm", "2022-blake3-aes-256-gcm", "2022-blake3-chacha20-poly1305"};
        if (!ciphers.contains(p.cipher) || p.password.isEmpty())
            return TorrentSettingsText::tr("Select a supported Shadowsocks cipher and enter its password or key.");
        if (p.cipher.startsWith("2022-")) {
            const int bytes = p.cipher == "2022-blake3-aes-128-gcm" ? 16 : 32;
            for (const auto &part : p.password.split(':')) {
                const auto encoded = part.toLatin1();
                const auto decoded = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
                if (decoded.size() != bytes || decoded.toBase64() != encoded)
                    return TorrentSettingsText::tr("Each Shadowsocks 2022 key must be canonical Base64 encoding of %1 bytes.").arg(bytes);
            }
        }
    }
    return {};
}

QString validateSettings(const Settings &s) {
    QSet<QString> countries;
    for (const auto &code : s.blockedCountries) {
        if (code.size() != 2 || code != code.toUpper() ||
            QLocale::codeToTerritory(code) == QLocale::AnyTerritory || countries.contains(code))
            return TorrentSettingsText::tr("Blocked countries must be unique two-letter country codes.");
        countries.insert(code);
    }
    if (s.listenPort < 1 || s.listenPort > 65535)
        return TorrentSettingsText::tr("Torrent listen port must be between 1 and 65535.");
    constexpr int maxKiB = std::numeric_limits<int>::max() / 1024;
    if (s.downloadLimitKiB < 0 || s.uploadLimitKiB < 0 ||
        s.downloadLimitKiB > maxKiB || s.uploadLimitKiB > maxKiB)
        return TorrentSettingsText::tr("Torrent bandwidth limits are outside the supported range.");
    if (s.activeDownloads < 1 || s.activeDownloads > 10000 ||
        s.connectionLimit < 1 || s.connectionLimit > 100000 ||
        s.perTorrentConnections < 1 || s.perTorrentConnections > 100000)
        return TorrentSettingsText::tr("Torrent connection and queue limits must be positive and bounded.");
    if (!std::isfinite(s.seedRatio) || s.seedRatio < 0 || s.seedRatio > 100000 ||
        s.seedMinutes < 0 || s.seedMinutes > 10000000)
        return TorrentSettingsText::tr("Torrent seeding limits are outside the supported range.");
    if (!s.tcp && !s.utp)
        return TorrentSettingsText::tr("Enable TCP or uTP for Torrent transfers.");
    if (s.bindAddress.isEmpty() && s.bindAddress6.isEmpty())
        return TorrentSettingsText::tr("At least one Torrent bind address is required.");
    if ((!s.bindAddress.isEmpty() && QHostAddress(s.bindAddress).protocol() != QAbstractSocket::IPv4Protocol) ||
        (!s.bindAddress6.isEmpty() && QHostAddress(s.bindAddress6).protocol() != QAbstractSocket::IPv6Protocol))
        return TorrentSettingsText::tr("Torrent bind addresses must be numeric IPv4 or IPv6 addresses.");
    if (s.proxyMode != ProxyMode::FollowApplication && s.proxyMode != ProxyMode::RequireProxy &&
        s.proxyMode != ProxyMode::Direct && s.proxyMode != ProxyMode::Custom)
        return TorrentSettingsText::tr("Invalid Torrent proxy mode.");
    if ((s.customProxyType != ProxyType::Socks5 && s.customProxyType != ProxyType::Shadowsocks &&
         s.customProxyType != ProxyType::Gost) ||
        (s.socks5Proxy.type != ProxyType::Socks5 && s.socks5Proxy.type != ProxyType::Socks5Tls) ||
        s.shadowsocksProxy.type != ProxyType::Shadowsocks || s.gostProxy.type != ProxyType::Gost)
        return TorrentSettingsText::tr("Select a supported Torrent proxy type.");
    if (s.proxyMode == ProxyMode::Custom) {
        const auto invalid = validateProxy(selectedProxy(s, {}));
        if (!invalid.isEmpty()) return invalid;
    }
    if (s.encryptionMode != EncryptionMode::Disabled && s.encryptionMode != EncryptionMode::Optional &&
        s.encryptionMode != EncryptionMode::Required)
        return TorrentSettingsText::tr("Invalid Torrent peer encryption mode.");
    for (const auto &path : {s.downloadPath, s.completedPath}) {
        if (path.contains(QChar(0)) || (!path.isEmpty() && !QDir::isAbsolutePath(path)))
            return TorrentSettingsText::tr("Torrent storage directories must be absolute paths.");
    }
    if (s.shareName.trimmed().isEmpty() || s.shareName.contains('/') || s.shareName.contains('\\') ||
        s.shareName.contains(QChar(0)) || s.shareName == "." || s.shareName == "..")
        return TorrentSettingsText::tr("Invalid Torrent share name.");
    if (s.bootstrapNodes.size() > 16384)
        return TorrentSettingsText::tr("Too many Torrent bootstrap nodes.");
    const QRegularExpression endpoint(R"(^(?:\[[0-9a-fA-F:%.]+\]|[a-zA-Z0-9_.-]+):([0-9]{1,5})$)");
    for (const auto &node : s.bootstrapNodes.split(',', Qt::SkipEmptyParts)) {
        const auto m = endpoint.match(node.trimmed());
        if (!m.hasMatch() || m.captured(1).toInt() < 1 || m.captured(1).toInt() > 65535)
            return TorrentSettingsText::tr("Torrent bootstrap nodes must be comma-separated host:port endpoints.");
    }
    return {};
}

bool saveSettings(const QString &path, const Settings &s, QString *error) {
    if (error) error->clear();
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    const auto invalid = validateSettings(s);
    if (!invalid.isEmpty()) return fail(invalid);
    QJsonObject obj{{"version", 2}};
#define WRITE_FIELD(name) obj.insert(#name, s.name);
    BOOL_FIELDS(WRITE_FIELD)
    INT_FIELDS(WRITE_FIELD)
    STRING_FIELDS(WRITE_FIELD)
#undef WRITE_FIELD
    obj.insert("seedRatio", s.seedRatio);
    obj.insert("proxyMode", static_cast<int>(s.proxyMode));
    obj.insert("encryptionMode", static_cast<int>(s.encryptionMode));
    obj.insert("blockedCountries", QJsonArray::fromStringList(s.blockedCountries));
    obj.insert("torrentProxy", QJsonObject{{"type", static_cast<int>(s.customProxyType)},
        {"socks5", proxyJson(s.socks5Proxy)}, {"shadowsocks", proxyJson(s.shadowsocksProxy)},
        {"gost", proxyJson(s.gostProxy)}});
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(TorrentSettingsText::tr("Cannot create Torrent settings directory."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(TorrentSettingsText::tr("Cannot open Torrent settings for atomic writing."));
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return fail(TorrentSettingsText::tr("Cannot restrict Torrent settings permissions to the current user."));
    const auto bytes = QJsonDocument(obj).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(TorrentSettingsText::tr("Cannot commit Torrent settings."));
    return true;
}

Settings loadSettings(const QString &path) {
    QFile f(path);
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly) || f.size() > 65536) return invalidDocument();
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return invalidDocument();
    const auto obj = doc.object();
    const auto version = obj.value("version");
    if (!version.isDouble() || (version.toDouble() != 1 && version.toDouble() != 2)) return invalidDocument();
    Settings s;
    if (obj.contains("blockedCountries")) {
        if (!obj["blockedCountries"].isArray()) return invalidDocument();
        for (const auto &value : obj["blockedCountries"].toArray()) {
            if (!value.isString()) return invalidDocument();
            s.blockedCountries.append(value.toString());
        }
    }
#define READ_BOOL(name) if (obj.contains(#name)) { if (!obj[#name].isBool()) return invalidDocument(); s.name = obj[#name].toBool(); }
#define READ_INT(name) if (obj.contains(#name)) { const auto v = obj[#name]; if (!v.isDouble() || v.toDouble() != v.toInt()) return invalidDocument(); s.name = v.toInt(); }
#define READ_STRING(name) if (obj.contains(#name)) { if (!obj[#name].isString()) return invalidDocument(); s.name = obj[#name].toString(); }
    BOOL_FIELDS(READ_BOOL)
    INT_FIELDS(READ_INT)
    STRING_FIELDS(READ_STRING)
#undef READ_BOOL
#undef READ_INT
#undef READ_STRING
    if (obj.contains("seedRatio")) {
        if (!obj["seedRatio"].isDouble()) return invalidDocument();
        s.seedRatio = obj["seedRatio"].toDouble();
    }
    if (obj.contains("proxyMode")) {
        const auto v = obj["proxyMode"];
        if (!v.isDouble() || v.toDouble() != v.toInt()) return invalidDocument();
        s.proxyMode = static_cast<ProxyMode>(v.toInt());
    }
    if (obj.contains("encryptionMode")) {
        const auto v = obj["encryptionMode"];
        if (!v.isDouble() || v.toDouble() != v.toInt()) return invalidDocument();
        s.encryptionMode = static_cast<EncryptionMode>(v.toInt());
    }
    if (version.toInt() == 2) {
        if (!obj["torrentProxy"].isObject()) return invalidDocument();
        const auto profiles = obj["torrentProxy"].toObject();
        const auto type = profiles["type"];
        if (!type.isDouble() || type.toDouble() != type.toInt() ||
            !readProxy(profiles["socks5"], s.socks5Proxy) ||
            !readProxy(profiles["shadowsocks"], s.shadowsocksProxy)) return invalidDocument();
        if (profiles.contains("gost") && !readProxy(profiles["gost"], s.gostProxy)) return invalidDocument();
        s.customProxyType = static_cast<ProxyType>(type.toInt());
    }
    if (s.bootstrapNodes == "31.3.251.150:6252") s.bootstrapNodes = "dhtb.hublist.eu:6252";
    return validateSettings(s).isEmpty() ? s : invalidDocument();
}
#undef BOOL_FIELDS
#undef INT_FIELDS
#undef STRING_FIELDS
}
