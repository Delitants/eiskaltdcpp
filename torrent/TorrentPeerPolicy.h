#pragma once

#include "TorrentEngine.h"
#include <QHash>
#include <QSet>
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_info.hpp>
#include <atomic>
#include <mutex>

namespace eiskalt::torrent {

// One immutable policy per session. The local resolver is shared by the network
// thread and snapshot worker; neither ever invokes UI code or online GeoIP.
class PeerPolicy {
public:
    PeerPolicy(const Settings &settings, TorrentEngine::CountryLookup lookup)
        : blocked(settings.blockedCountries.begin(), settings.blockedCountries.end()),
          blockUnknown(settings.blockUnknownClients), lookup(std::move(lookup)) {}

    QString country(const QString &ip) {
        std::lock_guard lock(mutex);
        const auto found = countries.constFind(ip);
        if (found != countries.cend()) return found.value();
        QString code;
        if (lookup) code = lookup(ip).trimmed().toUpper();
        if (countries.size() >= 4096) countries.clear();
        countries.insert(ip, code);
        return code;
    }
    bool countryBlocked(const QString &ip) {
        if (blocked.isEmpty()) return false;
        const bool rejected = blocked.contains(country(ip));
        if (rejected) ++countryRejects;
        return rejected;
    }
    bool clientBlocked(const std::string &client) {
        if (!blockUnknown) return false;
        const auto name = QString::fromUtf8(client.data(), qsizetype(client.size())).trimmed();
        const bool rejected = name.isEmpty() || name.startsWith(QStringLiteral("Unknown"), Qt::CaseInsensitive);
        if (rejected) ++clientRejects;
        return rejected;
    }
    std::atomic<int> countryRejects{0}, clientRejects{0};
private:
    const QSet<QString> blocked;
    const bool blockUnknown;
    const TorrentEngine::CountryLookup lookup;
    std::mutex mutex;
    QHash<QString, QString> countries;
};

class SecurityPeerPlugin final : public lt::peer_plugin {
public:
    SecurityPeerPlugin(lt::peer_connection_handle connection, std::shared_ptr<PeerPolicy> policy)
        : connection(std::move(connection)), policy(std::move(policy)) {}
    bool on_handshake(lt::span<char const>) override {
        // new_connection may run while libtorrent is constructing the peer.
        // Reject at the completed handshake, still before any payload exchange.
        if (policy->countryBlocked(QString::fromStdString(connection.remote().address().to_string()))) {
            connection.disconnect(lt::errors::banned_by_ip_filter, lt::operation_t::bittorrent);
            return true;
        }
        lt::peer_info info;
        connection.get_peer_info(info);
        if (policy->clientBlocked(info.client))
            connection.disconnect(lt::errors::banned_by_ip_filter, lt::operation_t::bittorrent);
        return true;
    }
private:
    lt::peer_connection_handle connection;
    std::shared_ptr<PeerPolicy> policy;
};

class SecurityTorrentPlugin final : public lt::torrent_plugin {
public:
    explicit SecurityTorrentPlugin(std::shared_ptr<PeerPolicy> policy) : policy(std::move(policy)) {}
    std::shared_ptr<lt::peer_plugin> new_connection(const lt::peer_connection_handle &connection) override {
        return std::make_shared<SecurityPeerPlugin>(connection, policy);
    }
private:
    std::shared_ptr<PeerPolicy> policy;
};

}
