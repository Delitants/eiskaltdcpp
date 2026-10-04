#include "dcpp/stdinc.h"
#include "dcpp/HttpRequestPolicy.h"
#include "dcpp/SettingsManager.h"
#include "UpdateDns.h"
#include <QHostAddress>
#include <chrono>
#include <map>
#include <set>
#include <stdexcept>

namespace updates::dns_detail {
namespace {
void require(bool value) { if(!value) throw std::runtime_error("Updater DNS requirement failed"); }
bool active(const std::shared_ptr<const dcpp::ProxyRouteSnapshot>& route) {
    if(!route || !route->valid || !route->notifier || !route->revoked || route->revoked->load()) return false;
    const auto type = route->proxy.type;
    return (route->mode == dcpp::SettingsManager::OUTGOING_SOCKS5 && type == dcpp::Socket::StreamProxyConfig::Socks5) ||
        (route->mode == dcpp::SettingsManager::OUTGOING_GOST && type == dcpp::Socket::StreamProxyConfig::Gost) ||
        (route->mode == dcpp::SettingsManager::OUTGOING_SHADOWSOCKS && type == dcpp::Socket::StreamProxyConfig::Shadowsocks);
}
bool origin(const std::string& host) {
    return host == "raw.githubusercontent.com" || host == "github.com" ||
        host == "release-assets.githubusercontent.com" || host == "objects.githubusercontent.com";
}
void word(QByteArray& value, unsigned number) { value.append(char(number >> 8)); value.append(char(number)); }
struct Reader {
    const QByteArray& body;
    size_t position = 0;
    unsigned byte() { require(position < size_t(body.size())); return static_cast<uint8_t>(body[position++]); }
    unsigned word() { const auto high = byte(); return (high << 8) | byte(); }
    std::string name() {
        std::string result;
        size_t at = position, end = 0;
        for(unsigned hops = 0; hops < 128; ++hops) {
            require(at < size_t(body.size()));
            const auto length = static_cast<uint8_t>(body[at++]);
            if((length & 0xc0) == 0xc0) {
                require(at < size_t(body.size()));
                const size_t next = ((length & 0x3f) << 8) | static_cast<uint8_t>(body[at++]);
                // Backward pointers prevent cycles and cannot point into the header.
                require(next >= 12 && next < at - 2);
                if(!end) end = at;
                at = next;
                continue;
            }
            require(length <= 63 && at + length <= size_t(body.size()));
            if(!length) { position = end ? end : at; return result; }
            if(!result.empty()) result += '.';
            for(unsigned i = 0; i < length; ++i) {
                unsigned char c = body[at++];
                if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
                require((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
                result += c;
            }
            require(result.size() <= 253);
        }
        require(false);
        return {};
    }
};
}

QByteArray query(const std::string& host, uint16_t type) {
    require(origin(host) && (type == 1 || type == 28));
    auto body = QByteArray::fromHex("000001000001000000000000");
    for(const auto& label : QString::fromStdString(host).split('.')) {
        const auto bytes = label.toLatin1();
        require(!bytes.isEmpty() && bytes.size() <= 63);
        body.append(char(bytes.size())); body.append(bytes);
    }
    body.append(char(0)); word(body, type); word(body, 1);
    return body;
}

Answer parse(const QByteArray& body, const std::string& host, uint16_t type) {
    require(origin(host) && (type == 1 || type == 28) && body.size() >= 12 && body.size() <= 65535);
    Reader reader{body};
    require(reader.word() == 0);
    const auto flags = reader.word();
    require((flags & 0x8000) && !(flags & 0x7a50) && (flags & 0x0100) && (flags & 0x0080));
    require(reader.word() == 1);
    const auto answers = reader.word(), authorities = reader.word(), additional = reader.word();
    require(answers + authorities + additional <= 256);
    require(reader.name() == host && reader.word() == type && reader.word() == 1);
    struct Record { std::string owner, address; };
    std::vector<Record> records;
    std::map<std::string, std::string> aliases;
    for(unsigned index = 0; index < answers + authorities + additional; ++index) {
        const auto owner = reader.name();
        const auto recordType = reader.word(), recordClass = reader.word();
        reader.word(); reader.word(); // TTL; no cross-request DNS cache.
        const auto length = reader.word();
        const auto start = reader.position, end = start + length;
        require(end <= size_t(body.size()));
        if(recordType == 1 || recordType == 28) {
            require(recordClass == 1 && length == (recordType == 1 ? 4U : 16U));
            QHostAddress address;
            if(recordType == 1) {
                quint32 value = 0;
                for(unsigned i = 0; i < 4; ++i) value = (value << 8) | reader.byte();
                address = QHostAddress(value);
            } else {
                Q_IPV6ADDR value{};
                for(unsigned i = 0; i < 16; ++i) value[i] = reader.byte();
                address = QHostAddress(value);
            }
            require(publicAddress(address.toString()));
            if(index < answers) {
                require(recordType == type);
                records.push_back({owner, address.toString().toStdString()});
            }
        } else if(index < answers) {
            require(recordType == 5 && recordClass == 1);
            const auto alias = reader.name();
            require(!alias.empty() && reader.position == end && aliases.emplace(owner, alias).second);
        }
        reader.position = end;
    }
    require(reader.position == size_t(body.size()));
    const auto status = flags & 15;
    if(status != 0) {
        require((status == 2 || status == 5) && answers == 0);
        return {true, {}};
    }
    std::set<std::string> visited;
    auto terminal = host;
    while(aliases.count(terminal)) {
        require(visited.insert(terminal).second && visited.size() <= 32);
        terminal = aliases.at(terminal);
    }
    require(visited.size() == aliases.size());
    Answer result;
    for(const auto& record : records) {
        require(record.owner == terminal);
        if(std::find(result.addresses.begin(), result.addresses.end(), record.address) == result.addresses.end())
            result.addresses.push_back(record.address);
    }
    return result;
}

dcpp::HttpRequestPolicy policy(Provider provider, const QByteArray& wire,
    std::shared_ptr<const dcpp::ProxyRouteSnapshot> route, uint32_t timeout,
    const std::function<bool()>& cancelled, std::shared_ptr<dcpp::WakeNotifier> notifier) {
    require(active(route) && timeout && timeout <= 15000 && (!cancelled || !cancelled()));
    require(provider == Provider::Cloudflare || provider == Provider::Google);
    require(!wire.isEmpty() && wire.size() <= 512);
    const std::string host = provider == Provider::Cloudflare ? "cloudflare-dns.com" : "dns.google";
    const std::string address = provider == Provider::Cloudflare ? "1.1.1.1" : "8.8.8.8";
    const auto path = "/dns-query?dns=" + wire.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString();
    const auto url = "https://" + host + path;
    dcpp::HttpRequestPolicy result;
    result.route = route;
    result.cancelled = cancelled;
    result.cancellationNotifier = std::move(notifier);
    result.redirects = 0;
    result.dnsMessage = true;
    result.limits.bodyBytes = 65535;
    result.connectMs = result.idleMs = result.totalMs = timeout;
    result.prepare = [route, url, host, path, address](const auto& next, const auto& previous,
        const auto& selected, auto, const auto& stop) {
        require(active(route) && &selected == route.get() && (!stop || !stop()) && previous.empty() && next == url);
        return dcpp::HttpRequestPolicy::Target{url, host, "443", path, address};
    };
    return result;
}

transport_detail::Resolver resolver(std::shared_ptr<const dcpp::ProxyRouteSnapshot> route,
    transport_detail::Backend backend, std::shared_ptr<dcpp::WakeNotifier> notifier) {
    return [route, backend, notifier](const std::string& host, uint32_t timeout, const std::function<bool()>& cancelled) {
        require(origin(host) && active(route) && bool(backend) && timeout && timeout <= 15000);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        const auto stop = [&] { return !active(route) || (cancelled && cancelled()) || std::chrono::steady_clock::now() >= deadline; };
        for(const auto provider : {Provider::Cloudflare, Provider::Google}) {
            std::vector<std::string> addresses;
            bool retry = false;
            for(const uint16_t type : {1, 28}) {
                require(!stop());
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                require(remaining > 0);
                const auto wire = query(host, type);
                auto request = policy(provider, wire, route, uint32_t(remaining), stop, notifier);
                const auto name = provider == Provider::Cloudflare ? "cloudflare-dns.com" : "dns.google";
                const auto url = std::string("https://") + name + "/dns-query?dns=" +
                    wire.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString();
                QByteArray body;
                backend(request, url, [&](const uint8_t* bytes, size_t count) {
                    require(!stop() && count <= 65535 - size_t(body.size()));
                    body.append(reinterpret_cast<const char*>(bytes), qsizetype(count));
                });
                require(!stop());
                const auto answer = parse(body, host, type);
                if(answer.retryable) { retry = true; break; }
                addresses.insert(addresses.end(), answer.addresses.begin(), answer.addresses.end());
            }
            if(!retry) { require(!addresses.empty() && !stop()); return addresses; }
        }
        require(false);
        return std::vector<std::string>{};
    };
}
}
