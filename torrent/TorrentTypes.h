#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

namespace eiskalt::torrent {

enum class ProxyMode { FollowApplication = 0, RequireProxy = 1, Direct = 2, Custom = 3 };
enum class ProxyType { Direct, Socks5, Socks5Tls, Shadowsocks, Gost = 4 };
enum class EncryptionMode { Disabled = 0, Optional = 1, Required = 2 };
struct ProxyConfig {
    ProxyType type = ProxyType::Direct;
    QString host, user, password, cipher;
    int port = 0;
    bool remoteDns = true, udp = false;
    QString caFile;
    QByteArray caPem; // Validated runtime snapshot, never persisted.
    std::shared_ptr<std::atomic_bool> revoked; // Global route lifetime, never persisted.
    bool operator==(const ProxyConfig &) const = default;
};
struct Settings {
    bool enabled = true;
    QString downloadPath, completedPath;
    int downloadLimitKiB = 0, uploadLimitKiB = 0;
    int activeDownloads = 3, connectionLimit = 200, perTorrentConnections = 50;
    double seedRatio = 1.0;
    int seedMinutes = 0, listenPort = 6881;
    bool randomizePort = true;
    QString bindAddress = "0.0.0.0", bindAddress6 = "::";
    QString bootstrapNodes = "dhtb.hublist.eu:6252";
    bool dht = true, pex = true, localDiscovery = true;
    bool tcp = true, utp = true, portMapping = true;
    ProxyMode proxyMode = ProxyMode::FollowApplication;
    ProxyType customProxyType = ProxyType::Socks5;
    ProxyConfig socks5Proxy{ProxyType::Socks5, {}, {}, {}, {}, 1080, true, true};
    ProxyConfig shadowsocksProxy{ProxyType::Shadowsocks, {}, {}, {}, "aes-256-gcm", 8388, true, false};
    EncryptionMode encryptionMode = EncryptionMode::Optional;
    QStringList blockedCountries;
    bool blockUnknownClients = false;
    bool shareCompleted = true;
    QString shareName = "Torrent Downloads";
    ProxyConfig gostProxy{ProxyType::Gost, {}, {}, {}, {}, 1080, true, true};
    bool operator==(const Settings &) const = default;
};
struct FileEntry { int index; QString path; qint64 size; bool wanted; int priority = 4; };
struct Peer {
    QString id, ip, client, transport, encryption, state, countryCode;
    quint16 port = 0;
    double progress = 0;
    qint64 downloadRate = 0, uploadRate = 0;
};
struct Job {
    quint64 revision = 0;
    QList<FileEntry> files;
    QString id, name, state, error, savePath;
    QString magnet; // Hashes and display name only, never tracker credentials.
    double progress = 0;
    qint64 size = 0, downloaded = 0, uploaded = 0;
    qint64 downloadRate = 0, uploadRate = 0;
    int peers = 0, seeds = 0;
    int downloadingPeers = 0, uploadingPeers = 0;
    bool paused = false, complete = false, privateTorrent = false;
    bool dcShareExcluded = false;
    bool proxied = false;
    bool stopped = false, v1 = false, v2 = false;
};

} // namespace eiskalt::torrent
