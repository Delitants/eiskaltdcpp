#pragma once
#include "tests/proxy/NestedTlsOrigin.h"
#include "dcpp/HttpRequestPolicy.h"
#include "updates/UpdateTransport.h"
#include <QUrl>
#include <QUrlQuery>
#include <QByteArray>
#include <arpa/inet.h>
#include <thread>

namespace update_test {
struct PlainRelay {
    std::atomic_bool stopping{false};
    std::jthread worker;
    std::string user, password, target, error;
    uint16_t port = 0;
    PlainRelay(dcpp::Socket& listener, std::shared_ptr<proxy_test::NestedTlsEcho> origin, bool reject) {
        worker = std::jthread([this, &listener, origin, reject] {
            try {
                if(listener.wait(1500, dcpp::Socket::WAIT_READ) != dcpp::Socket::WAIT_READ)
                    throw std::runtime_error("No fixture proxy connection");
                dcpp::Socket peer; peer.accept(listener); peer.setBlocking(false);
#ifdef SO_NOSIGPIPE
                peer.setSocketOpt(SO_NOSIGPIPE, 1);
#endif
                auto read = [&](void* data, size_t size) {
                    if(peer.readAll(data, int(size), 1500) != int(size)) throw std::runtime_error("Fixture EOF");
                };
                uint8_t hello[3]{}; read(hello, 3);
                if(hello[0] != 5 || hello[1] != 1 || hello[2] != 2) throw std::runtime_error("No proxy authentication");
                const uint8_t method[]{5, 2}; peer.writeAll(method, 2, 1500);
                uint8_t auth[2]{}; read(auth, 2);
                if(auth[0] != 1) throw std::runtime_error("Wrong authentication version");
                user.resize(auth[1]); read(user.data(), user.size());
                uint8_t size{}; read(&size, 1); password.resize(size); read(password.data(), password.size());
                const bool denied = reject || user != "disposable-user" || password != "disposable-password";
                const uint8_t status[]{1, static_cast<uint8_t>(denied ? 1 : 0)};
                peer.writeAll(status, 2, 1500); if(denied) return;
                uint8_t command[4]{}; read(command, 4);
                if(command[0] != 5 || command[1] != 1 || command[2] != 0 || command[3] != 1)
                    throw std::runtime_error("Expected pinned IPv4 CONNECT");
                uint8_t endpoint[6]{}; read(endpoint, 6);
                char address[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, endpoint, address, sizeof(address));
                target = address; port = (uint16_t(endpoint[4]) << 8) | endpoint[5];
                const uint8_t reply[]{5, 0, 0, 1, 127, 0, 0, 1, 0, 1}; peer.writeAll(reply, 10, 1500);
                std::array<uint8_t, 4096> bytes{};
                while(!stopping) {
                    if(!(peer.wait(50, dcpp::Socket::WAIT_READ) & dcpp::Socket::WAIT_READ)) continue;
                    const auto count = peer.read(bytes.data(), bytes.size());
                    if(!count) return;
                    if(count < 0) continue;
                    const auto result = origin->exchange(std::span(bytes.data(), size_t(count)));
                    if(!result.empty()) peer.writeAll(result.data(), int(result.size()), 1500);
                }
            } catch(const std::exception& e) { error = e.what(); }
        });
    }
    ~PlainRelay() { stop(); }
    void stop() { stopping = true; if(worker.joinable()) worker.join(); }
};

// Only fixture roots/responses are substituted: retain production prepare(),
// DNS resolver, public address pins, strict HTTP and snapshot cancellation.
struct UpdateRouteRelay {
    dcpp::Socket listener;
    proxy_test::Identity proxyCa = proxy_test::Identity::make(), originCa = proxy_test::Identity::make();
    std::shared_ptr<const dcpp::ProxyRouteSnapshot> route;
    QByteArray feed, archive;
    bool plain = false, rejectAuth = false, wrongOrigin = false, redirect = false,
        badRedirect = false, providerOutage = false, revokeArchive = false;
    unsigned queries = 0, origins = 0;
    struct Record { std::string host, address, request, user, password, target; uint16_t port; };
    std::vector<Record> records;
    UpdateRouteRelay() {
        listener.create(dcpp::Socket::TYPE_TCP, AF_INET);
        listener.bind("0", "127.0.0.1"); listener.listen();
    }
    void request(dcpp::DCContext& context, const dcpp::HttpRequestPolicy& original,
        const std::string& url, const updates::transport_detail::Sink& sink) {
        if(original.route != route) throw std::runtime_error("Updater changed its route snapshot");
        struct Hop {
            std::shared_ptr<proxy_test::NestedTlsEcho> origin;
            std::unique_ptr<proxy_test::GostServer> gost;
            std::unique_ptr<PlainRelay> socks;
            std::string host, address;
        };
        std::vector<Hop> hops;
        auto policy = original; policy.rootsPem = originCa.pem;
        const auto prepare = original.prepare;
        policy.prepare = [&](const auto& next, const auto& previous, const auto& snapshot, auto timeout, const auto& stop) {
            auto target = prepare(next, previous, snapshot, timeout, stop);
            const QUrl request(QString::fromStdString(next));
            QByteArray body;
            std::string http;
            if(original.dnsMessage) {
                ++queries;
                if(providerOutage && request.host() == "cloudflare-dns.com")
                    http = "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n";
                else {
                    body = QByteArray::fromBase64(QUrlQuery(request).queryItemValue("dns").toLatin1(), QByteArray::Base64UrlEncoding);
                    const auto type = static_cast<uint8_t>(body[body.size() - 3]);
                    body[2] = char(0x81); body[3] = char(0x80);
                    if(type == 1) {
                        body[7] = 1;
                        body.append(QByteArray::fromHex("c00c000100010000003c00048c527203"));
                    }
                }
            } else {
                ++origins;
                if(redirect && request.host() == "github.com")
                    http = std::string("HTTP/1.1 302 Redirect\r\nLocation: https://") +
                        (badRedirect ? "unapproved.test" : "release-assets.githubusercontent.com") +
                        "/fixture.zip\r\nContent-Length: 0\r\n\r\n";
                else body = request.host() == "raw.githubusercontent.com" ? feed : archive;
            }
            if(http.empty()) http = std::string("HTTP/1.1 200 OK\r\n") +
                (original.dnsMessage ? "Content-Type: application/dns-message\r\n" : "") +
                "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body.toStdString();
            auto origin = std::make_shared<proxy_test::NestedTlsEcho>(http, false,
                wrongOrigin && !original.dnsMessage ? &proxyCa : &originCa,
                "DNS:cloudflare-dns.com,DNS:dns.google,DNS:raw.githubusercontent.com,DNS:github.com,DNS:release-assets.githubusercontent.com");
            Hop hop{origin, {}, {}, request.host().toStdString(), target.address};
            if(plain) hop.socks = std::make_unique<PlainRelay>(listener, origin, rejectAuth);
            else {
                proxy_test::GostOptions options; options.authStatus = rejectAuth ? 1 : 0;
                options.transform = [origin](auto input) { return origin->exchange(input); };
                hop.gost = std::make_unique<proxy_test::GostServer>(options, "IP:127.0.0.1", false, &proxyCa);
                hop.gost->start(&listener);
            }
            hops.push_back(std::move(hop));
            return target;
        };
        std::exception_ptr failure;
        try { updates::transport_detail::request(context, policy, url, sink); }
        catch(...) { failure = std::current_exception(); }
        for(auto& hop : hops) {
            if(hop.socks) hop.socks->stop();
            else { hop.gost->requestStop(); hop.gost->join(); }
            records.push_back({hop.host, hop.address, hop.origin->request,
                plain ? hop.socks->user : hop.gost->user, plain ? hop.socks->password : hop.gost->password,
                plain ? hop.socks->target : hop.gost->target, plain ? hop.socks->port : hop.gost->targetPort});
            if(!failure && hop.socks && !hop.socks->error.empty())
                failure = std::make_exception_ptr(std::runtime_error(hop.socks->error));
        }
        if(revokeArchive && !original.dnsMessage && url.find("raw.githubusercontent.com") == std::string::npos) {
            route->revoked->store(true); route->notifier->notify();
        }
        if(failure) std::rethrow_exception(failure);
    }
};
}
