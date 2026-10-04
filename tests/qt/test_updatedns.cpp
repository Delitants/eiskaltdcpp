#include "dcpp/stdinc.h"
#include "dcpp/HttpRequestPolicy.h"
#include "dcpp/HttpConnection.h"
#include "dcpp/SettingsManager.h"
#include "updates/UpdateDns.h"
#include "tests/TestContext.h"
#include "tests/proxy/NestedTlsOrigin.h"
#include <catch2/catch_test_macros.hpp>
#include <QUrlQuery>

namespace {
using namespace updates;
const std::string host = "raw.githubusercontent.com";
const QByteArray question = QByteArray::fromHex(
    "000001000001000000000000037261771167697468756275736572636f6e74656e7403636f6d0000010001");
void word(QByteArray& body, unsigned value) { body.append(char(value >> 8)); body.append(char(value)); }
QByteArray response(unsigned type = 1, unsigned code = 0, QByteArray address = QByteArray::fromHex("8c527203")) {
    auto body = question;
    body[2] = char(0x81); body[3] = char(0x80 | code);
    body[body.size() - 3] = char(type);
    if(!address.isEmpty()) {
        body[7] = 1;
        body.append(QByteArray::fromHex("c00c")); word(body, type); word(body, 1);
        body.append(QByteArray::fromHex("0000003c")); word(body, address.size()); body.append(address);
    }
    return body;
}
auto route() {
    auto result = std::make_shared<dcpp::ProxyRouteSnapshot>();
    result->mode = dcpp::SettingsManager::OUTGOING_GOST;
    result->proxy.type = dcpp::Socket::StreamProxyConfig::Gost;
    return result;
}
}

TEST_CASE("Updater DoH emits exact bounded DNS wire queries", "[updates][update-dns]") {
    CHECK(dns_detail::query(host, 1) == question);
    auto aaaa = question; aaaa[aaaa.size() - 3] = 28;
    CHECK(dns_detail::query(host, 28) == aaaa);
    for(const auto& invalid : {"", "localhost", "bad..name", "raw.githubusercontent.com/path", "127.0.0.1"})
        CHECK_THROWS(dns_detail::query(invalid, 1));
    CHECK_THROWS(dns_detail::query(host, 15));
}

TEST_CASE("Updater DoH validates wire answers and refuses unsafe results", "[updates][update-dns]") {
    const auto result = dns_detail::parse(response(), host, 1);
    CHECK_FALSE(result.retryable);
    CHECK(result.addresses == std::vector<std::string>{"140.82.114.3"});
    const auto v6 = dns_detail::parse(response(28, 0, QByteArray::fromHex("26064700470000000000000000001111")), host, 28);
    CHECK(v6.addresses == std::vector<std::string>{"2606:4700:4700::1111"});
    CHECK(dns_detail::parse(response(1, 2, {}), host, 1).retryable);
    CHECK(dns_detail::parse(response(1, 5, {}), host, 1).retryable);
    CHECK(dns_detail::parse(response(28, 0, {}), host, 28).addresses.empty());
    CHECK_THROWS(dns_detail::parse(response(1, 3, {}), host, 1));
    CHECK_THROWS(dns_detail::parse(response(), "github.com", 1));
    CHECK_THROWS(dns_detail::parse(response(), host, 28));
    CHECK_THROWS(dns_detail::parse(response(1, 0, QByteArray::fromHex("7f000001")), host, 1));
    for(int end = 0; end < response().size(); ++end)
        CHECK_THROWS(dns_detail::parse(response().left(end), host, 1));
    auto malformed = response(); malformed[0] = 1;
    CHECK_THROWS(dns_detail::parse(malformed, host, 1));
    malformed = response(); malformed[2] = char(0x83);
    CHECK_THROWS(dns_detail::parse(malformed, host, 1));
    malformed = response(); malformed.append('x');
    CHECK_THROWS(dns_detail::parse(malformed, host, 1));
    malformed = response(); malformed[question.size() + 1] = char(question.size());
    CHECK_THROWS(dns_detail::parse(malformed, host, 1));
}

TEST_CASE("Updater DoH uses fixed pinned provider HTTPS on the same route", "[updates][update-dns]") {
    auto snapshot = route();
    auto wake = std::make_shared<dcpp::WakeNotifier>();
    for(const auto provider : {dns_detail::Provider::Cloudflare, dns_detail::Provider::Google}) {
        auto policy = dns_detail::policy(provider, question, snapshot, 2000, [] { return false; }, wake);
        REQUIRE(bool(policy.prepare));
        CHECK(policy.route == snapshot);
        CHECK(policy.cancellationNotifier == wake);
        CHECK(policy.redirects == 0);
        CHECK(policy.limits.bodyBytes == 65535);
        CHECK(policy.totalMs == 2000);
        const auto name = provider == dns_detail::Provider::Cloudflare ? "cloudflare-dns.com" : "dns.google";
        const auto url = std::string("https://") + name + "/dns-query?dns=" +
            question.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString();
        const auto target = policy.prepare(url, "", *snapshot, 1000, [] { return false; });
        CHECK(target.host == name);
        CHECK(target.address == (provider == dns_detail::Provider::Cloudflare ? "1.1.1.1" : "8.8.8.8"));
        CHECK(target.port == "443");
        CHECK_THROWS(policy.prepare(url, url, *snapshot, 1000, [] { return false; }));
        CHECK_THROWS(policy.prepare("https://evil.test/", "", *snapshot, 1000, [] { return false; }));
        CHECK_THROWS(policy.prepare(url, "", *snapshot, 1000, [] { return true; }));
        snapshot->revoked->store(true);
        CHECK_THROWS(policy.prepare(url, "", *snapshot, 1000, [] { return false; }));
        snapshot->revoked->store(false);
    }
}

TEST_CASE("Updater DoH validates CNAME chains and rejects mixed or unrelated address records", "[updates][update-dns]") {
    auto body = response();
    const auto alias = QByteArray::fromHex("05616c696173076578616d706c6503636f6d00");
    body.truncate(question.size()); body[7] = 2;
    body.append(QByteArray::fromHex("c00c")); word(body, 5); word(body, 1);
    body.append(QByteArray::fromHex("0000003c")); word(body, alias.size()); body.append(alias);
    body.append(alias); word(body, 1); word(body, 1);
    body.append(QByteArray::fromHex("0000003c0004")); body.append(QByteArray::fromHex("8c527203"));
    CHECK(dns_detail::parse(body, host, 1).addresses == std::vector<std::string>{"140.82.114.3"});
    auto unrelated = body; unrelated[question.size() + 3] = 15;
    CHECK_THROWS(dns_detail::parse(unrelated, host, 1));
    auto mixed = response(); mixed[7] = 2;
    mixed.append(response(1, 0, QByteArray::fromHex("0a000001")).mid(question.size()));
    CHECK_THROWS(dns_detail::parse(mixed, host, 1));
    auto hidden = response(); hidden[11] = 1;
    hidden.append(response(1, 0, QByteArray::fromHex("0a000001")).mid(question.size()));
    CHECK_THROWS(dns_detail::parse(hidden, host, 1));
}

TEST_CASE("Updater DoH fallback remains bounded and cannot hide unsafe responses", "[updates][update-dns]") {
    auto snapshot = route();
    std::vector<std::string> providers;
    unsigned primaryCode = 0;
    bool privateAnswer = false, malformed = false, transportFailure = false, cancel = false, revoke = false;
    SECTION("Primary") {}
    SECTION("SERVFAIL retries Google") { primaryCode = 2; }
    SECTION("REFUSED retries Google") { primaryCode = 5; }
    SECTION("NXDOMAIN is terminal") { primaryCode = 3; }
    SECTION("Private is terminal") { privateAnswer = true; }
    SECTION("Malformed is terminal") { malformed = true; }
    SECTION("TLS auth or transport failure is terminal") { transportFailure = true; }
    SECTION("Cancel is terminal") { cancel = true; }
    SECTION("Revocation is terminal") { revoke = true; }
    auto lookup = dns_detail::resolver(snapshot, [&](const auto& policy, const auto& url, const auto& sink) {
        CHECK(policy.route == snapshot);
        const QUrl request(QString::fromStdString(url));
        providers.push_back(request.host().toStdString());
        if(transportFailure) throw std::runtime_error("unclassified HTTP failure");
        const auto wire = QByteArray::fromBase64(QUrlQuery(request).queryItemValue("dns").toLatin1(), QByteArray::Base64UrlEncoding);
        const auto type = static_cast<unsigned char>(wire[wire.size() - 3]);
        const auto code = request.host() == "cloudflare-dns.com" ? primaryCode : 0;
        auto body = response(type, code, code ? QByteArray{} : type == 28 ? QByteArray{} :
            QByteArray::fromHex(privateAnswer ? "0a000001" : "8c527203"));
        if(malformed) body = "bad";
        if(revoke) snapshot->revoked->store(true);
        sink(reinterpret_cast<const uint8_t*>(body.constData()), body.size());
    });
    if(primaryCode == 3 || privateAnswer || malformed || transportFailure || cancel || revoke) {
        CHECK_THROWS(lookup(host, 2000, [&] { return cancel; }));
        CHECK(providers.size() == (cancel ? 0 : 1));
    } else {
        CHECK(lookup(host, 2000, [] { return false; }) == std::vector<std::string>{"140.82.114.3"});
        CHECK(providers == (primaryCode ? std::vector<std::string>{"cloudflare-dns.com", "dns.google", "dns.google"} :
            std::vector<std::string>{"cloudflare-dns.com", "cloudflare-dns.com"}));
    }
}

TEST_CASE("Updater DoH physically tunnels verified HTTPS through authenticated SOCKS5 and GOST", "[updates][update-dns][update-dns-proxy]") {
    dcpp::test::TestContext context;
    dcpp::Socket listener;
    listener.create(dcpp::Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1"); listener.listen();
    auto snapshot = route();
    const auto proxyCa = proxy_test::Identity::make(), originCa = proxy_test::Identity::make();
    auto& proxy = snapshot->proxy;
    proxy.host = proxy.connectHost = "127.0.0.1";
    proxy.port = std::stoi(listener.getLocalPort());
    proxy.user = "disposable-user"; proxy.password = "disposable-password";
    proxy.caPem = proxyCa.pem;
    bool failAuth = false, failProxyCa = false, failOriginCa = false, failHostname = false, fallback = false;
    bool plain = false, failMime = false, ipv6Pin = false, cancelOnReply = false, revokeOnReply = false;
    std::atomic_bool cancelled{false};
    auto wake = std::make_shared<dcpp::WakeNotifier>();
    SECTION("Valid nested HTTPS") {}
    SECTION("Valid Google fallback") { fallback = true; }
    SECTION("Wrong proxy authentication") { failAuth = true; }
    SECTION("Wrong proxy CA") { failProxyCa = true; proxy.caPem = originCa.pem; }
    SECTION("Wrong HTTPS CA") { failOriginCa = true; }
    SECTION("Wrong HTTPS hostname") { failHostname = true; }
    SECTION("Wrong DNS MIME is terminal") { failMime = true; }
    SECTION("Plain SOCKS5 verified HTTPS") { plain = true; }
    SECTION("Plain SOCKS5 Google fallback") { plain = true; fallback = true; }
    SECTION("Plain SOCKS5 wrong authentication") { plain = true; failAuth = true; }
    SECTION("Plain SOCKS5 numeric IPv6 pin") { plain = true; ipv6Pin = true; }
    SECTION("Cancellation during HTTPS is terminal") { cancelOnReply = true; }
    SECTION("Route revocation during HTTPS is terminal") { revokeOnReply = true; }
    if(plain) {
        snapshot->mode = dcpp::SettingsManager::OUTGOING_SOCKS5;
        proxy.type = dcpp::Socket::StreamProxyConfig::Socks5;
    }
    unsigned attempts = 0;
    auto lookup = dns_detail::resolver(snapshot, [&](const auto& original, const auto& url, const auto& sink) {
        ++attempts;
        const QUrl request(QString::fromStdString(url));
        const auto wire = QByteArray::fromBase64(QUrlQuery(request).queryItemValue("dns").toLatin1(), QByteArray::Base64UrlEncoding);
        const auto type = static_cast<unsigned char>(wire[wire.size() - 3]);
        const bool primaryFailed = fallback && request.host() == "cloudflare-dns.com";
        const auto body = response(type, primaryFailed ? 2 : 0, primaryFailed ? QByteArray{} : type == 1 ?
            QByteArray::fromHex("8c527203") : QByteArray::fromHex("26064700470000000000000000001111"));
        const std::string http = std::string("HTTP/1.1 200 OK\r\nContent-Type: ") +
            (failMime ? "text/plain" : "application/dns-message") + "\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body.toStdString();
        auto origin = std::make_shared<proxy_test::NestedTlsEcho>(http, false, &originCa,
            failHostname ? "DNS:wrong.test" : "DNS:cloudflare-dns.com,DNS:dns.google");
        proxy_test::GostOptions options;
        if(failAuth) options.authStatus = 1;
        options.transform = [&, origin](auto input) {
            auto result = origin->exchange(input);
            if(!origin->request.empty() && (cancelOnReply || revokeOnReply)) {
                if(cancelOnReply) { cancelled = true; wake->notify(); }
                if(revokeOnReply) { snapshot->revoked->store(true); snapshot->notifier->notify(); }
            }
            return result;
        };
        proxy_test::GostServer server(options, "IP:127.0.0.1,DNS:proxy.test", false, &proxyCa);
        auto policy = original;
        policy.rootsPem = failOriginCa ? proxyCa.pem : originCa.pem;
        if(ipv6Pin) {
            // Fixture-only bootstrap substitution exercises the core binary IPv6
            // envelope; production provider pins remain untouched.
            const auto prepare = policy.prepare;
            policy.prepare = [prepare](const auto& next, const auto& previous, const auto& selected, auto timeout, const auto& stop) {
                auto target = prepare(next, previous, selected, timeout, stop);
                target.address = "2606:4700:4700::1111";
                return target;
            };
        }
        struct Receiver : dcpp::HttpConnectionListener {
            const transport_detail::Sink& sink;
            bool complete = false;
            explicit Receiver(const transport_detail::Sink& value) : sink(value) {}
            void on(Data, dcpp::HttpConnection*, const uint8_t* data, size_t size) override { sink(data, size); }
            void on(Complete, dcpp::HttpConnection*, const std::string&) override { complete = true; }
        } receiver(sink);
        std::atomic_bool stopping{false};
        std::string socksUser, socksTarget;
        std::jthread plainRelay;
        if(!plain) server.start(&listener);
        else plainRelay = std::jthread([&] {
            try {
                if(listener.wait(1500, dcpp::Socket::WAIT_READ) != dcpp::Socket::WAIT_READ) return;
                dcpp::Socket peer; peer.accept(listener); peer.setBlocking(false);
#ifdef SO_NOSIGPIPE
                peer.setSocketOpt(SO_NOSIGPIPE, 1);
#endif
                auto read = [&](void* data, size_t size) {
                    if(peer.readAll(data, int(size), 1500) != int(size)) throw std::runtime_error("Fixture EOF");
                };
                uint8_t hello[3]{}; read(hello, 3);
                if(hello[0] != 5 || hello[1] != 1 || hello[2] != 2) return;
                const uint8_t selected[]{5, 2}; peer.writeAll(selected, 2, 1500);
                uint8_t auth[2]{}; read(auth, 2);
                if(auth[0] != 1) return;
                socksUser.resize(auth[1]); read(socksUser.data(), socksUser.size());
                uint8_t size = 0; read(&size, 1);
                std::string password(size, '\0'); read(password.data(), password.size());
                const uint8_t status[]{1, static_cast<uint8_t>(failAuth ? 1 : 0)};
                peer.writeAll(status, 2, 1500); if(failAuth) return;
                if(password != "disposable-password") return;
                uint8_t command[4]{}; read(command, 4);
                if(command[0] != 5 || command[1] != 1 || command[3] != (ipv6Pin ? 4 : 1)) return;
                uint8_t endpoint[18]{};
                const auto addressSize = ipv6Pin ? 16 : 4;
                read(endpoint, addressSize + 2);
                if(endpoint[addressSize] != 1 || endpoint[addressSize + 1] != 187) return;
                char address[INET6_ADDRSTRLEN]{}; inet_ntop(ipv6Pin ? AF_INET6 : AF_INET, endpoint, address, sizeof(address));
                socksTarget = address;
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
            } catch(...) { }
        });
        dcpp::HttpConnection connection(*context.ownedCtx, policy, "isolated-doh-test");
        connection.addListener(&receiver); connection.downloadFile(url);
        if(plain) { stopping = true; plainRelay.join(); }
        else { server.requestStop(); server.join(); }
        CHECK(policy.route == snapshot);
        if(!receiver.complete) throw std::runtime_error("Isolated HTTPS failure");
        CHECK((plain ? socksUser : server.user) == "disposable-user");
        CHECK((plain ? socksTarget : server.target) == (ipv6Pin ? "2606:4700:4700::1111" :
            request.host() == "cloudflare-dns.com" ? "1.1.1.1" : "8.8.8.8"));
        if(!plain) CHECK(server.targetPort == 443);
        CHECK(origin->request.find("Host: " + request.host().toStdString()) != std::string::npos);
        CHECK(origin->request.find("Accept: application/dns-message\r\n") != std::string::npos);
        CHECK(origin->request.find("disposable-password") == std::string::npos);
        CHECK(origin->request.find("Authorization") == std::string::npos);
    }, wake);
    if(failAuth || failProxyCa || failOriginCa || failHostname || failMime || cancelOnReply || revokeOnReply) {
        CHECK_THROWS(lookup(host, 5000, [&] { return cancelled.load(); }));
        CHECK(attempts == 1);
    } else {
        CHECK(lookup(host, 5000, [] { return false; }) == std::vector<std::string>{"140.82.114.3", "2606:4700:4700::1111"});
        CHECK(attempts == (fallback ? 3 : 2));
    }
}
