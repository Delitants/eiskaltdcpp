/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2019 Boris Pek <tehnick-8@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"
#include "Socket.h"
#include "GostProtocol.h"
#include "ProxyRoute.h"
#include "SocketWake.h"
#ifndef _WIN32
#include <poll.h>
#endif
#include "ProxyResolver.h"
#include "GostUdpRelay.h"

#include "format.h"
#include "SettingsManager.h"
#include "TimerManager.h"
#include "XChaCha20Poly1305.h"
#include "ScopedFunctor.h"

#ifdef __MINGW32__
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL WSAEADDRNOTAVAIL
#endif
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef IP_TOS
#define        IP_TOS          1
#endif
#ifndef IPTOS_TOS
#define IPTOS_TOS(a) ((a) & 0x1E)
#endif

#ifndef _WIN32
#include <sys/ioctl.h>
#ifndef __HAIKU__
#include <ifaddrs.h>
#endif
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef __HAIKU__
#include <sys/sockio.h>
#endif

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <cctype>
#include <charconv>

namespace dcpp {

struct Socket::GostUdpState {
    std::shared_ptr<const ProxyRouteSnapshot> route;
    std::unique_ptr<GostUdpRelay> relay;
    uint16_t port = 0;
    uint64_t nextAttempt = 0, connectedAt = 0;
    unsigned failures = 0;
    void failed() {
        relay.reset();
        port = 0;
        if(connectedAt && GET_TICK() - connectedAt >= 30000) failures = 0;
        nextAttempt = GET_TICK() + std::min<uint64_t>(30000, uint64_t(1000) << std::min(failures, 5u));
        failures = std::min(failures + 1, 5u);
        connectedAt = 0;
    }
};

Socket::SocksUdpAssociationPtr Socket::udpAssociation;
std::mutex Socket::udpProxyMutex;
std::mutex Socket::udpProxySetupMutex;

#define checkconnected() if(!isConnected()) throw SocketException(ENOTCONN))

namespace {
int globalProxyMode(DCContext& context) {
    const auto route = context.getProxyRoute()->snapshot();
    // Keep the published GOST generation until the entire settings batch is
    // committed. An outgoing-mode field edited early cannot open a bypass.
    return route->mode == SettingsManager::OUTGOING_GOST ? route->mode :
        context.getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS);
}
int inferAddressFamily(const string& address) {
    if(address.empty()) {
        return AF_UNSPEC;
    }
    return address.find(':') != string::npos ? AF_INET6 : AF_INET;
}

string sockaddrToIp(const sockaddr* sa) {
    if(sa == nullptr) {
        return Util::emptyString;
    }

    char ipbuf[INET6_ADDRSTRLEN] = { 0 };
    switch(sa->sa_family) {
    case AF_INET: {
        const auto* sa4 = reinterpret_cast<const sockaddr_in*>(sa);
        if(inet_ntop(AF_INET, &sa4->sin_addr, ipbuf, sizeof(ipbuf))) {
            return ipbuf;
        }
        break;
    }
    case AF_INET6: {
        const auto* sa6 = reinterpret_cast<const sockaddr_in6*>(sa);
        if(inet_ntop(AF_INET6, &sa6->sin6_addr, ipbuf, sizeof(ipbuf))) {
            return ipbuf;
        }
        break;
    }
    default:
        break;
    }

    return Util::emptyString;
}

string sockaddrToPort(const sockaddr* sa) {
    if(sa == nullptr) {
        return Util::emptyString;
    }

    uint16_t port = 0;
    if(sa->sa_family == AF_INET) {
        port = ntohs(reinterpret_cast<const sockaddr_in*>(sa)->sin_port);
    } else if(sa->sa_family == AF_INET6) {
        port = ntohs(reinterpret_cast<const sockaddr_in6*>(sa)->sin6_port);
    }

    return port > 0 ? Util::toString(port) : Util::emptyString;
}

void populateUdpSendInfo(Socket::UdpSendInfo* sendInfo, const string& logicalIp, const string& logicalPort,
    const sockaddr* physical, int sentBytes, bool proxied)
{
    if(!sendInfo) {
        return;
    }

    sendInfo->logicalIp = logicalIp;
    sendInfo->logicalPort = logicalPort;
    sendInfo->physicalIp = sockaddrToIp(physical);
    sendInfo->physicalPort = sockaddrToPort(physical);
    sendInfo->proxied = proxied;
    sendInfo->bytesSent = sentBytes > 0 ? static_cast<size_t>(sentBytes) : 0;
}
}

#ifdef _DEBUG

SocketException::SocketException(int aError) {
    error = "SocketException: " + errorToString(aError);
    dcdebug("Thrown: %s\n", error.c_str());
}

#else // _DEBUG

SocketException::SocketException(int aError) : Exception(errorToString(aError)) { }

#endif

Socket::Stats Socket::stats = { 0, 0 };

static const uint32_t SOCKS_TIMEOUT = 30000;

string SocketException::errorToString(int aError) {
    string msg = Util::translateError(aError);
    if(msg.empty()) {
        msg = str(F_("Unknown error: 0x%1$x") % aError);
    }
    return msg;
}

void Socket::create(int aType /* = TYPE_TCP */, int aFamily /* = AF_INET */) {
    if(sock != INVALID_SOCKET)
        disconnect();

    if(aFamily != AF_INET && aFamily != AF_INET6) {
        aFamily = AF_INET;
    }

    switch(aType) {
    case TYPE_TCP:
        sock = checksocket(socket(aFamily, SOCK_STREAM, IPPROTO_TCP));
        break;
    case TYPE_UDP:
        sock = checksocket(socket(aFamily, SOCK_DGRAM, IPPROTO_UDP));
        break;
    default:
        dcassert(0);
    }
    type = aType;
    family = aFamily;
    udpClosing = false;

    setBlocking(false);

    if(family == AF_INET6) {
        int no = 0;
        try {
            check(::setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&no, sizeof(no)));
        } catch(...) {
        }
    }

    if (ctx_ && ctx().getSettingsManager()->get(SettingsManager::IP_TOS_VALUE) != -1)
        setSocketOpt(IP_TOS, IPTOS_TOS(ctx().getSettingsManager()->get(SettingsManager::IP_TOS_VALUE)));
}

void Socket::setContext(DCContext* value) {
    ctx_ = value;
    if(ctx_ && connected && type == TYPE_TCP && !streamProxyOverride)
        bindGlobalRoute(ctx().getProxyRoute()->snapshot());
}

void Socket::setWaitWake(std::shared_ptr<SocketWake> wake, std::function<bool()> cancelled) {
    if(cancelled && !wake) throw SocketException("Socket cancellation requires a wake source");
    globalRouteSubscription.reset();
    overrideSubscription.reset();
    overrideNotifier.reset();
    waitWake = std::move(wake);
    waitCancelled = std::move(cancelled);
}

void Socket::bindGlobalRoute(const std::shared_ptr<const ProxyRouteSnapshot>& route) {
    globalRouteSubscription.reset();
    globalRouteRevoked = route->revoked;
    globalRouteNotifier = route->notifier;
}

void Socket::prepareWaitWake() {
    const auto notifier = streamProxyOverride ? streamProxyOverride->cancellationNotifier : nullptr;
    if(overrideNotifier != notifier) {
        overrideSubscription.reset();
        overrideNotifier = notifier;
    }
    if(!waitWake && (globalRouteNotifier || overrideNotifier)) waitWake = std::make_shared<SocketWake>();
    if(globalRouteNotifier && !globalRouteSubscription)
        globalRouteSubscription = globalRouteNotifier->subscribe(waitWake);
    if(overrideNotifier && !overrideSubscription)
        overrideSubscription = overrideNotifier->subscribe(waitWake);
}

void Socket::accept(const Socket& listeningSocket) {
    if(sock != INVALID_SOCKET) {
        disconnect();
    }
    sockaddr_storage sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    socklen_t sz = sizeof(sock_addr);

    do {
        sock = ::accept(listeningSocket.sock, (sockaddr*)&sock_addr, &sz);
    } while (sock == SOCKET_ERROR && getLastError() == EINTR);
    check(sock);

#ifdef _WIN32
    // Make sure we disable any inherited windows message things for this socket.
    ::WSAAsyncSelect(sock, NULL, 0, 0);
#endif

    type = TYPE_TCP;
    family = sock_addr.ss_family;

    setIp(sockaddrToIp(reinterpret_cast<sockaddr*>(&sock_addr)));
    connected = true;
    setBlocking(false);
}


string Socket::getIfaceI4 (const string &iface){
#ifdef _WIN32
    return "0.0.0.0";
#else
    struct ifreq request;
    string s = "0.0.0.0";

    memset(&request, 0, sizeof(struct ifreq));

    if ((size_t)iface.size() <= sizeof(request.ifr_name)){
        memcpy(request.ifr_name, iface.c_str(), iface.size());

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock != -1 ){
            if (ioctl(sock, SIOCGIFADDR, &request) >= 0){
                struct sockaddr *sa = &request.ifr_addr;

                if ( sa && sa->sa_family == AF_INET )
                    s = inet_ntoa(((struct sockaddr_in*)sa)->sin_addr);
            }

            ::close(sock);
        }
    }

    return s;
#endif
}

string Socket::getIfaceI6(const string& iface) {
#ifdef _WIN32
    return "::";
#else
#ifdef HAVE_IFADDRS_H
    struct ifaddrs* ifap = nullptr;
    if(getifaddrs(&ifap) != 0) {
        return "::";
    }

    string s = "::";
    for(struct ifaddrs* i = ifap; i != nullptr; i = i->ifa_next) {
        if(!i->ifa_addr || i->ifa_addr->sa_family != AF_INET6 || !i->ifa_name) {
            continue;
        }
        if(iface != i->ifa_name) {
            continue;
        }

        const auto* sa6 = reinterpret_cast<sockaddr_in6*>(i->ifa_addr);
        char buf[INET6_ADDRSTRLEN] = { 0 };
        if(inet_ntop(AF_INET6, &sa6->sin6_addr, buf, sizeof(buf))) {
            s = buf;
            break;
        }
    }

    freeifaddrs(ifap);
    return s;
#else
    (void)iface;
    return "::";
#endif
#endif
}

const string Socket::bind(const string& aPort, const string& aIp /* = 0.0.0.0 */) {
    if(sock == INVALID_SOCKET) {
        const int inferredFamily = inferAddressFamily(aIp);
        create(type, inferredFamily == AF_UNSPEC ? AF_INET : inferredFamily);
    }

    const bool gostDatagram = type == TYPE_UDP && ctx_ &&
        globalProxyMode(ctx()) == SettingsManager::OUTGOING_GOST;
    const string bindIp = gostDatagram ? (family == AF_INET6 ? "::1" : "127.0.0.1") :
        (aIp.empty() ? (family == AF_INET6 ? "::" : "0.0.0.0") : aIp);
    const string bindPort = gostDatagram || aPort.empty() ? "0" : aPort;
    if(gostDatagram) setBlocking(false);

    addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
    hints.ai_family = family;
    hints.ai_socktype = (type == TYPE_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = (type == TYPE_UDP) ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    if(getaddrinfo(bindIp.c_str(), bindPort.c_str(), &hints, &result) != 0 || result == nullptr) {
        throw SocketException(EADDRNOTAVAIL);
    }

    bool bound = false;
    for(addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        if(::bind(sock, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0) {
            bound = true;
            break;
        }
    }
    freeaddrinfo(result);

    if(!bound && !gostDatagram) {
        const string fallbackIp = (family == AF_INET6) ? "::" : "0.0.0.0";
        if(bindIp != fallbackIp) {
            dcdebug("Bind failed for %s, retrying with %s: %s\n",
                bindIp.c_str(), fallbackIp.c_str(), SocketException(getLastError()).getError().c_str());

            addrinfo* fallback = nullptr;
            if(getaddrinfo(fallbackIp.c_str(), bindPort.c_str(), &hints, &fallback) == 0 && fallback != nullptr) {
                for(addrinfo* ai = fallback; ai != nullptr; ai = ai->ai_next) {
                    if(::bind(sock, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0) {
                        bound = true;
                        break;
                    }
                }
                freeaddrinfo(fallback);
            }
        }
    }

    if(!bound) {
        check(SOCKET_ERROR);
    }

    if(gostDatagram) gostUdpOriginalBind = std::make_pair(aPort, aIp);

    sockaddr_storage sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    socklen_t size = sizeof(sock_addr);
    getsockname(sock, reinterpret_cast<sockaddr*>(&sock_addr), &size);
    family = sock_addr.ss_family;
    return sockaddrToPort(reinterpret_cast<sockaddr*>(&sock_addr));
}

void Socket::listen() {
    check(::listen(sock, 20));
    connected = true;
}

void Socket::connect(const string& aAddr, const string& aPort, const string&) {
    if(ctx_ && !globalProxyExempt && !streamProxyOverride) {
        const auto route = ctx().getProxyRoute()->snapshot();
        if(!route->valid || route->revoked->load())
            throw SocketException("Global proxy route is unavailable");
        const int mode = globalProxyMode(ctx());
        if(mode == SettingsManager::OUTGOING_GOST || mode < SettingsManager::OUTGOING_DIRECT || mode > SettingsManager::OUTGOING_GOST) {
            proxyConnect(aAddr, aPort, 10000);
            return;
        }
        bindGlobalRoute(route);
    }
    if(sock == INVALID_SOCKET) {
        int preferredFamily = (ctx_ && ctx().getSettingsManager()->getBool(SettingsManager::USE_IPV6)) ? AF_INET6 : AF_INET;
        const int inferred = inferAddressFamily(aAddr);
        if(inferred != AF_UNSPEC) {
            preferredFamily = inferred;
        }
        create(TYPE_TCP, preferredFamily);
    }

    addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if(streamProxyOverride && !streamProxyOverride->connectHost.empty()) {
        // Resolved adapter endpoints must never re-enter the blocking resolver.
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    }
#ifdef AI_V4MAPPED
    if(family == AF_INET6) {
        hints.ai_flags |= AI_V4MAPPED;
    }
#endif

    addrinfo* result = nullptr;
    int gai = getaddrinfo(aAddr.c_str(), aPort.c_str(), &hints, &result);
    if(gai != 0 || result == nullptr) {
        throw SocketException(EADDRNOTAVAIL);
    }

    int savedError = EADDRNOTAVAIL;
    for(addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        if(ai->ai_family != family) {
            continue;
        }

        int ret;
        do {
            ret = ::connect(sock, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
        } while(ret < 0 && getLastError() == EINTR);

        if(ret == 0) {
            connected = true;
            setIp(sockaddrToIp(ai->ai_addr));
            freeaddrinfo(result);
            return;
        }

        savedError = getLastError();
#ifdef _WIN32
        if(savedError == WSAEWOULDBLOCK || savedError == WSAEINPROGRESS) {
#else
        if(savedError == EWOULDBLOCK || savedError == EINPROGRESS || savedError == EAGAIN || savedError == ENOBUFS) {
#endif
            connected = true;
            setIp(sockaddrToIp(ai->ai_addr));
            freeaddrinfo(result);
            return;
        }
    }

    freeaddrinfo(result);
    throw SocketException(savedError);
}

namespace {
inline uint64_t timeLeft(uint64_t start, uint64_t timeout) {
    if(timeout == 0) {
        return 0;
    }
    uint64_t now = GET_TICK();
    if(start + timeout <= now)
        throw SocketException(_("Connection timeout"));
    return start + timeout - now;
}

constexpr size_t SHADOWSOCKS_TAG_LEN = 16;
constexpr size_t SHADOWSOCKS_NONCE_LEN = 12;
constexpr size_t SHADOWSOCKS_MAX_CHUNK = 0x3fff;

string normalizeCipherName(string method) {
    transform(method.begin(), method.end(), method.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return method;
}

int shadowsocksMethodId(const string& method) {
    const string normalized = normalizeCipherName(method);
    if(normalized == "aes-128-gcm")
        return Socket::SHADOWSOCKS_AES_128_GCM;
    if(normalized == "aes-256-gcm")
        return Socket::SHADOWSOCKS_AES_256_GCM;
    if(normalized == "chacha20-ietf-poly1305")
        return Socket::SHADOWSOCKS_CHACHA20_IETF_POLY1305;
    if(normalized == "2022-blake3-aes-128-gcm")
        return Socket::SHADOWSOCKS_2022_BLAKE3_AES_128_GCM;
    if(normalized == "2022-blake3-aes-256-gcm")
        return Socket::SHADOWSOCKS_2022_BLAKE3_AES_256_GCM;
    if(normalized == "2022-blake3-chacha20-poly1305")
        return Socket::SHADOWSOCKS_2022_BLAKE3_CHACHA20_POLY1305;
    return Socket::SHADOWSOCKS_NONE;
}

bool isShadowsocks2022Method(int method) {
    return method == Socket::SHADOWSOCKS_2022_BLAKE3_AES_128_GCM ||
        method == Socket::SHADOWSOCKS_2022_BLAKE3_AES_256_GCM ||
        method == Socket::SHADOWSOCKS_2022_BLAKE3_CHACHA20_POLY1305;
}

Shadowsocks2022::Method shadowsocks2022Method(int method) {
    switch(method) {
    case Socket::SHADOWSOCKS_2022_BLAKE3_AES_128_GCM:
        return Shadowsocks2022::Method::Blake3Aes128Gcm;
    case Socket::SHADOWSOCKS_2022_BLAKE3_AES_256_GCM:
        return Shadowsocks2022::Method::Blake3Aes256Gcm;
    case Socket::SHADOWSOCKS_2022_BLAKE3_CHACHA20_POLY1305:
        return Shadowsocks2022::Method::Blake3ChaCha20Poly1305;
    default:
        throw SocketException(_("Unsupported Shadowsocks cipher"));
    }
}

const EVP_CIPHER* shadowsocksCipher(int method) {
    switch(method) {
    case Socket::SHADOWSOCKS_AES_128_GCM:
        return EVP_aes_128_gcm();
    case Socket::SHADOWSOCKS_AES_256_GCM:
        return EVP_aes_256_gcm();
    case Socket::SHADOWSOCKS_CHACHA20_IETF_POLY1305:
        return EVP_chacha20_poly1305();
    default:
        return nullptr;
    }
}

size_t shadowsocksKeyLen(int method) {
    switch(method) {
    case Socket::SHADOWSOCKS_AES_128_GCM:
        return 16;
    case Socket::SHADOWSOCKS_AES_256_GCM:
    case Socket::SHADOWSOCKS_CHACHA20_IETF_POLY1305:
        return 32;
    case Socket::SHADOWSOCKS_2022_BLAKE3_AES_128_GCM:
        return 16;
    case Socket::SHADOWSOCKS_2022_BLAKE3_AES_256_GCM:
    case Socket::SHADOWSOCKS_2022_BLAKE3_CHACHA20_POLY1305:
        return 32;
    default:
        return 0;
    }
}

ByteVector evpBytesToKey(const string& password, size_t keyLen) {
    ByteVector key;
    ByteVector previous;
    const auto* passwordData = reinterpret_cast<const unsigned char*>(password.data());

    while(key.size() < keyLen) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if(!ctx)
            throw SocketException(_("Failed to initialize Shadowsocks key derivation"));

        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen = 0;
        EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
        if(!previous.empty())
            EVP_DigestUpdate(ctx, previous.data(), previous.size());
        EVP_DigestUpdate(ctx, passwordData, password.size());
        EVP_DigestFinal_ex(ctx, digest, &digestLen);
        EVP_MD_CTX_free(ctx);

        previous.assign(digest, digest + digestLen);
        key.insert(key.end(), previous.begin(), previous.end());
    }

    key.resize(keyLen);
    return key;
}

ByteVector hkdfSha1(const ByteVector& key, const ByteVector& salt, const string& info, size_t outLen) {
    unsigned int prkLen = 0;
    unsigned char prk[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha1(), salt.data(), static_cast<int>(salt.size()), key.data(), key.size(), prk, &prkLen);

    ByteVector out;
    ByteVector previous;
    uint8_t counter = 1;
    while(out.size() < outLen) {
        ByteVector input;
        input.reserve(previous.size() + info.size() + 1);
        input.insert(input.end(), previous.begin(), previous.end());
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter);

        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen = 0;
        if(!HMAC(EVP_sha1(), prk, prkLen, input.data(), input.size(), digest, &digestLen))
            throw SocketException(_("Failed to derive Shadowsocks subkey"));

        previous.assign(digest, digest + digestLen);
        out.insert(out.end(), previous.begin(), previous.end());
        ++counter;
    }

    out.resize(outLen);
    return out;
}

void incrementNonce(ByteVector& nonce) {
    for(auto& b : nonce) {
        if(++b != 0)
            break;
    }
}

ByteVector shadowsocksAeadEncrypt(int method, const ByteVector& key, ByteVector& nonce, const uint8_t* data, size_t dataLen) {
    const EVP_CIPHER* cipher = shadowsocksCipher(method);
    if(!cipher)
        throw SocketException(_("Unsupported Shadowsocks cipher"));

    ByteVector out(dataLen + SHADOWSOCKS_TAG_LEN);
    int outLen = 0;
    int finalLen = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if(!ctx)
        throw SocketException(_("Failed to initialize Shadowsocks encryption"));

    if(EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1 ||
       EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, SHADOWSOCKS_NONCE_LEN, nullptr) != 1 ||
       EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1 ||
       EVP_EncryptUpdate(ctx, out.data(), &outLen, data, static_cast<int>(dataLen)) != 1 ||
       EVP_EncryptFinal_ex(ctx, out.data() + outLen, &finalLen) != 1 ||
       EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, SHADOWSOCKS_TAG_LEN, out.data() + outLen + finalLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw SocketException(_("Shadowsocks encryption failed"));
    }

    EVP_CIPHER_CTX_free(ctx);
    out.resize(outLen + finalLen + SHADOWSOCKS_TAG_LEN);
    incrementNonce(nonce);
    return out;
}

bool shadowsocksAeadDecrypt(int method, const ByteVector& key, ByteVector& nonce, const uint8_t* data, size_t dataLen, ByteVector& out) {
    if(dataLen < SHADOWSOCKS_TAG_LEN)
        return false;

    const EVP_CIPHER* cipher = shadowsocksCipher(method);
    if(!cipher)
        return false;

    const size_t cipherLen = dataLen - SHADOWSOCKS_TAG_LEN;
    out.assign(cipherLen, 0);
    int outLen = 0;
    int finalLen = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if(!ctx)
        return false;

    const bool ok = EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, SHADOWSOCKS_NONCE_LEN, nullptr) == 1 &&
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
        EVP_DecryptUpdate(ctx, out.data(), &outLen, data, static_cast<int>(cipherLen)) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, SHADOWSOCKS_TAG_LEN, const_cast<uint8_t*>(data + cipherLen)) == 1 &&
        EVP_DecryptFinal_ex(ctx, out.data() + outLen, &finalLen) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if(!ok)
        return false;

    out.resize(outLen + finalLen);
    incrementNonce(nonce);
    return true;
}

bool appendSocksAddress(ByteVector& out, const string& address, const string& port, bool remoteResolve) {
    if(remoteResolve) {
        if(address.size() > 255)
            return false;
        out.push_back(3);
        out.push_back(static_cast<uint8_t>(address.size()));
        out.insert(out.end(), address.begin(), address.end());
    } else {
        in_addr ipv4;
        in6_addr ipv6;
        const string resolved = Socket::resolve(address);
        const string& ip = resolved.empty() ? address : resolved;

        if(inet_pton(AF_INET, ip.c_str(), &ipv4) == 1) {
            out.push_back(1);
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&ipv4);
            out.insert(out.end(), bytes, bytes + 4);
        } else if(inet_pton(AF_INET6, ip.c_str(), &ipv6) == 1) {
            out.push_back(4);
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&ipv6);
            out.insert(out.end(), bytes, bytes + 16);
        } else {
            return false;
        }
    }

    const uint16_t nport = htons(static_cast<uint16_t>(Util::toInt(port)));
    const uint8_t* portBytes = reinterpret_cast<const uint8_t*>(&nport);
    out.push_back(portBytes[0]);
    out.push_back(portBytes[1]);
    return true;
}

bool resolveSockaddr(const string& host, const string& port, sockaddr_storage& remote) {
    addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    if(getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || result == nullptr)
        return false;

    memset(&remote, 0, sizeof(remote));
    memcpy(&remote, result->ai_addr, min(sizeof(remote), static_cast<size_t>(result->ai_addrlen)));
    freeaddrinfo(result);
    return true;
}

bool parseSocksEndpoint(const ByteVector& in, string& host, string& port, size_t& payloadOffset) {
    if(in.empty())
        return false;

    size_t pos = 0;
    const uint8_t atyp = in[pos++];

    if(atyp == 1) {
        if(in.size() < pos + 4 + 2)
            return false;

        char address[INET_ADDRSTRLEN] = {};
        if(inet_ntop(AF_INET, in.data() + pos, address, sizeof(address)) == nullptr)
            return false;
        host = address;
        pos += 4;
    } else if(atyp == 4) {
        if(in.size() < pos + 16 + 2)
            return false;

        char address[INET6_ADDRSTRLEN] = {};
        if(inet_ntop(AF_INET6, in.data() + pos, address, sizeof(address)) == nullptr)
            return false;
        host = address;
        pos += 16;
    } else if(atyp == 3) {
        if(in.size() < pos + 1)
            return false;

        const size_t hostLen = in[pos++];
        if(in.size() < pos + hostLen + 2)
            return false;

        host.assign(reinterpret_cast<const char*>(in.data() + pos), hostLen);
        pos += hostLen;
    } else {
        return false;
    }

    uint16_t networkPort = 0;
    memcpy(&networkPort, in.data() + pos, 2);
    pos += 2;
    port = Util::toString(ntohs(networkPort));
    payloadOffset = pos;
    return !host.empty() && !port.empty();
}

bool parseSocksAddress(const ByteVector& in, sockaddr_storage& remote, size_t& payloadOffset) {
    string host;
    string port;
    return parseSocksEndpoint(in, host, port, payloadOffset) && resolveSockaddr(host, port, remote);
}
}

Socket::StreamProxyConfig Socket::streamProxyConfig(StreamProxyConfig::Type proxyType) const {
    if(streamProxyOverride)
        return *streamProxyOverride;
    if(!ctx_)
        throw SocketException("No proxy configuration available");
    const auto* sm = ctx().getSettingsManager();
    StreamProxyConfig config;
    const bool shadowsocks = proxyType == StreamProxyConfig::Shadowsocks;
    config.type = proxyType;
    config.host = sm->get(shadowsocks ? SettingsManager::SHADOWSOCKS_SERVER : SettingsManager::SOCKS_SERVER);
    config.port = sm->get(shadowsocks ? SettingsManager::SHADOWSOCKS_PORT : SettingsManager::SOCKS_PORT);
    config.user = sm->get(SettingsManager::SOCKS_USER);
    config.password = sm->get(shadowsocks ? SettingsManager::SHADOWSOCKS_PASSWORD : SettingsManager::SOCKS_PASSWORD);
    config.cipher = sm->get(SettingsManager::SHADOWSOCKS_METHOD);
    config.tls = sm->getBool(SettingsManager::SOCKS_TLS, true);
    config.remoteDns = sm->getBool(SettingsManager::SOCKS_RESOLVE, true);
    config.verifyTls = false; // Preserve the existing DC transport policy.
    return config;
}

namespace {
void validateGostConfig(const Socket::StreamProxyConfig& config) {
    if(config.type != Socket::StreamProxyConfig::Gost || !config.verifyTls ||
       config.host.empty() || config.host.size() > 255 || config.host.find('\0') != string::npos ||
       config.connectHost.find('\0') != string::npos || config.port < 1 || config.port > 65535 ||
       config.user.empty() || config.user.size() > 255 ||
       config.password.empty() || config.password.size() > 255 || config.caPem.size() > 1024 * 1024)
        throw SocketException("Invalid GOST proxy configuration");
}
}

void Socket::proxyConnect(const string& aAddr, const string& aPort,
                          const StreamProxyConfig& config, uint32_t timeout) {
    if(config.type == StreamProxyConfig::Gost) {
        try {
            validateGostConfig(config);
            unsigned port = 0;
            const auto parsed = std::from_chars(aPort.data(), aPort.data() + aPort.size(), port);
            if(parsed.ec != std::errc{} || parsed.ptr != aPort.data() + aPort.size() || !port || port > 65535)
                throw SocketException("Invalid GOST target port");
            ByteVector request;
            try {
                // Standard empty UDP header has exactly the SOCKS request layout.
                request = gost::encodeSocks({aAddr, uint16_t(port), {}});
            } catch(const std::invalid_argument&) {
                throw SocketException("Invalid GOST target address");
            }
            request[0] = 5;
            request[1] = 1;
            disconnect();
            streamProxyOverride = config;
            const auto start = GET_TICK();
            gostHandshake(config, timeLeft(start, timeout));
            gostCommand(request, timeLeft(start, timeout));
            setIp(aAddr);
        } catch(...) {
            disconnect();
            streamProxyOverride.reset();
            throw;
        }
        return;
    }
    if((config.type != StreamProxyConfig::Socks5 && config.type != StreamProxyConfig::Shadowsocks) ||
       config.host.empty() || config.port < 1 || config.port > 65535 ||
       (config.type == StreamProxyConfig::Socks5 && (config.user.size() > 255 || config.password.size() > 255)) ||
       aAddr.empty() || aAddr.size() > 255 || Util::toInt(aPort) < 1 || Util::toInt(aPort) > 65535)
        throw SocketException("Invalid proxy endpoint or target");
    streamProxyOverride = config;
    try {
        if(config.type == StreamProxyConfig::Socks5)
            socksConnect(aAddr, aPort, timeout);
        else
            shadowsocksConnect(aAddr, aPort, timeout);
    } catch(...) {
        disconnect();
        streamProxyOverride.reset();
        throw;
    }
}

void Socket::gostOpenUdpTunnel(const StreamProxyConfig& config, uint32_t timeout) {
    try {
        validateGostConfig(config);
        disconnect();
        streamProxyOverride = config;
        const auto start = GET_TICK();
        gostHandshake(config, timeLeft(start, timeout));
        // ATYP=1 pins GOST's relay to udp4. A domain-form wildcard requests
        // family-neutral UDP; destinations still travel verbatim in frames.
        gostCommand({5, 0xf3, 0, 3, 7, '0', '.', '0', '.', '0', '.', '0', 0, 0}, timeLeft(start, timeout));
    } catch(...) {
        disconnect();
        streamProxyOverride.reset();
        throw;
    }
}

void Socket::gostHandshake(const StreamProxyConfig& config, uint32_t timeout) {
    auto stage = SocketException::ProxyStage::None;
    try {
        const auto start = GET_TICK();
        checkProxyCancellation();
        Socket::connect(config.connectHost.empty() ? config.host : config.connectHost, Util::toString(config.port));
        if(Socket::wait(timeLeft(start, timeout), WAIT_CONNECT) != WAIT_CONNECT)
            throw SocketException("GOST proxy connection timed out");
        stage = SocketException::ProxyStage::Negotiation;
        const uint8_t offer[]{5, 1, 0x82};
        streamWriteAll(offer, sizeof(offer), timeLeft(start, timeout));
        uint8_t response[2]{};
        if(streamReadAll(response, sizeof(response), timeLeft(start, timeout)) != sizeof(response) ||
           response[0] != 5 || response[1] != 0x82)
            throw SocketException("GOST TLS-AUTH negotiation failed");

        stage = SocketException::ProxyStage::Certificate;
        socksStartTls(config.host, timeLeft(start, timeout));
        stage = SocketException::ProxyStage::Authentication;
        ByteVector auth{1, uint8_t(config.user.size())};
        auth.insert(auth.end(), config.user.begin(), config.user.end());
        auth.push_back(uint8_t(config.password.size()));
        auth.insert(auth.end(), config.password.begin(), config.password.end());
        streamWriteAll(auth.data(), auth.size(), timeLeft(start, timeout));
        if(streamReadAll(response, sizeof(response), timeLeft(start, timeout)) != sizeof(response) ||
           response[0] != 1 || response[1] != 0)
            throw SocketException("GOST authentication failed");

    } catch(SocketException& error) {
        error.setProxyStage(stage);
        throw;
    }
}

void Socket::gostCommand(const ByteVector& request, uint32_t timeout) {
    try {
        const auto start = GET_TICK();
        streamWriteAll(request.data(), request.size(), timeLeft(start, timeout));
        auto readExact = [&](void* data, int length) {
            if(streamReadAll(data, length, timeLeft(start, timeout)) != length)
                throw SocketException("GOST command reply truncated");
        };
        uint8_t header[4]{};
        readExact(header, sizeof(header));
        if(header[0] != 5 || header[2] != 0)
            throw SocketException("Invalid GOST command reply");
        if(header[1] != 0)
            throw SocketException("GOST command rejected", header[1]);
        size_t addressLength = 0;
        if(header[3] == 1) addressLength = 4;
        else if(header[3] == 4) addressLength = 16;
        else if(header[3] == 3) {
            uint8_t length = 0;
            readExact(&length, 1);
            if(!length) throw SocketException("Invalid GOST command reply address");
            addressLength = length;
        } else throw SocketException("Invalid GOST command reply address");
        uint8_t bound[257]{};
        readExact(bound, int(addressLength + 2));

    } catch(SocketException& error) {
        error.setProxyStage(SocketException::ProxyStage::Tunnel);
        throw;
    }
}

void Socket::socksConnect(const string& aAddr, const string& aPort, uint32_t timeout) {
    const auto config = streamProxyConfig(StreamProxyConfig::Socks5);
    auto previous = std::move(streamProxyOverride);
    streamProxyOverride = config;
    ScopedFunctor([&] { streamProxyOverride = std::move(previous); });
    checkProxyCancellation();

    if(config.host.empty() || config.port == 0) {
        throw SocketException(_("The socks server failed establish a connection"));
    }

    uint64_t start = GET_TICK();

    Socket::connect(config.connectHost.empty() ? config.host : config.connectHost, Util::toString(config.port));

    if(Socket::wait(timeLeft(start, timeout), WAIT_CONNECT) != WAIT_CONNECT) {
        throw SocketException(_("The socks server failed establish a connection"));
    }

    if(config.tls) {
        socksStartTls(config.host, timeLeft(start, timeout));
    }

    socksAuth(timeLeft(start, timeout));

    ByteVector connStr;

    // Authenticated, let's get on with it...
    connStr.push_back(5);           // SOCKSv5
    connStr.push_back(1);           // Connect
    connStr.push_back(0);           // Reserved

    if(config.remoteDns) {
        connStr.push_back(3);       // Address type: domain name
        connStr.push_back((uint8_t)aAddr.size());
        connStr.insert(connStr.end(), aAddr.begin(), aAddr.end());
    } else {
        connStr.push_back(1);       // Address type: IPv4;
        unsigned long addr = inet_addr(resolve(aAddr).c_str());
        uint8_t* paddr = (uint8_t*)&addr;
        connStr.insert(connStr.end(), paddr, paddr+4);
    }

    uint16_t port = htons(static_cast<uint16_t>(Util::toInt(aPort)));
    uint8_t* pport = (uint8_t*)&port;
    connStr.push_back(pport[0]);
    connStr.push_back(pport[1]);

    streamWriteAll(connStr.data(), connStr.size(), timeLeft(start, timeout));

    uint8_t replyHeader[4] = {};
    if(streamReadAll(replyHeader, sizeof(replyHeader), timeLeft(start, timeout)) != static_cast<int>(sizeof(replyHeader))) {
        throw SocketException(_("The socks server failed establish a connection"));
    }

    if(replyHeader[0] != 5 || replyHeader[2] != 0) {
        throw SocketException(_("The socks server failed establish a connection"));
    }
    if(replyHeader[1] != 0) {
        throw SocketException(_("The socks server failed establish a connection"), replyHeader[1]);
    }

    size_t addressLength = 0;
    switch(replyHeader[3]) {
    case 1:
        addressLength = 4;
        break;
    case 3: {
        uint8_t domainLength = 0;
        if(streamReadAll(&domainLength, 1, timeLeft(start, timeout)) != 1) {
            throw SocketException(_("The socks server failed establish a connection"));
        }
        addressLength = domainLength;
        break;
    }
    case 4:
        addressLength = 16;
        break;
    default:
        throw SocketException(_("The socks server failed establish a connection"));
    }

    ByteVector boundAddress(addressLength + 2);
    if(streamReadAll(boundAddress.data(), boundAddress.size(), timeLeft(start, timeout)) != static_cast<int>(boundAddress.size())) {
        throw SocketException(_("The socks server failed establish a connection"));
    }

    setIp(aAddr);
}

void Socket::proxyConnect(const string& aAddr, const string& aPort, uint32_t timeout) {
    const auto snapshot = ctx().getProxyRoute()->snapshot();
    if(!snapshot->valid || snapshot->revoked->load())
        throw SocketException("Global proxy route is unavailable");
    bindGlobalRoute(snapshot);
    switch(globalProxyMode(ctx())) {
    case SettingsManager::OUTGOING_SOCKS5:
        socksConnect(aAddr, aPort, timeout);
        break;
    case SettingsManager::OUTGOING_SHADOWSOCKS:
        shadowsocksConnect(aAddr, aPort, timeout);
        break;
    case SettingsManager::OUTGOING_GOST: {
        const auto route = ctx().getProxyRoute()->snapshot();
        if(!route->valid || route->revoked->load() || route->mode != SettingsManager::OUTGOING_GOST)
            throw SocketException("Global GOST route is unavailable");
        auto config = route->proxy;
        const auto start = GET_TICK();
        const auto budget = timeout ? std::min<uint32_t>(timeout, 10000) : 10000;
        const auto endpoints = resolveProxyEndpoint(config.host, budget, [this, cancelled = config.cancelled] {
            return cancelled() || (waitCancelled && waitCancelled());
        });
        for(size_t i = 0; i < endpoints.size(); ++i) {
            config.connectHost = endpoints[i];
            try {
                proxyConnect(aAddr, aPort, config, timeLeft(start, budget));
                return;
            } catch(const SocketException& error) {
                // Only try another proxy address when transport setup failed;
                // authentication, identity and policy failures are terminal.
                if(error.getProxyStage() != SocketException::ProxyStage::None ||
                   config.cancelled() || i + 1 == endpoints.size()) throw;
            }
        }
        throw SocketException("Global GOST route is unavailable");
    }
    case SettingsManager::OUTGOING_DIRECT:
        connect(aAddr, aPort);
        break;
    default:
        throw SocketException("Unknown global proxy mode");
    }
}

void Socket::shadowsocksConnect(const string& aAddr, const string& aPort, uint32_t timeout) {
    const auto config = streamProxyConfig(StreamProxyConfig::Shadowsocks);
    auto previous = std::move(streamProxyOverride);
    streamProxyOverride = config;
    ScopedFunctor([&] { streamProxyOverride = std::move(previous); });
    checkProxyCancellation();

    if(config.host.empty() || config.port == 0) {
        throw SocketException(_("The Shadowsocks server failed to establish a connection"));
    }
    if(config.password.empty()) {
        throw SocketException(_("No Shadowsocks password configured"));
    }

    uint64_t start = GET_TICK();
    Socket::connect(config.connectHost.empty() ? config.host : config.connectHost, Util::toString(config.port));

    if(Socket::wait(timeLeft(start, timeout), WAIT_CONNECT) != WAIT_CONNECT) {
        throw SocketException(_("The Shadowsocks server failed to establish a connection"));
    }

    ByteVector target;
    if(!appendSocksAddress(target, aAddr, aPort, config.remoteDns)) {
        throw SocketException(_("The Shadowsocks target address is invalid"));
    }

    const bool targetIncluded = shadowsocksStart(config.cipher,
        config.password, target, timeLeft(start, timeout));
    if(!targetIncluded) {
        streamWriteAll(target.data(), target.size(), timeLeft(start, timeout));
    }
    setIp(aAddr);
}

void Socket::socksAuth(uint32_t timeout) {
    vector<uint8_t> connStr;
    const auto config = streamProxyConfig(StreamProxyConfig::Socks5);

    uint64_t start = GET_TICK();

    if(config.user.empty() && config.password.empty()) {
        // No username and pw, easier...=)
        connStr.push_back(5);           // SOCKSv5
        connStr.push_back(1);           // 1 method
        connStr.push_back(0);           // Method 0: No auth...

        streamWriteAll(connStr.data(), 3, timeLeft(start, timeout));

        if(streamReadAll(connStr.data(), 2, timeLeft(start, timeout)) != 2) {
            throw SocketException(_("The socks server failed establish a connection"));
        }

        if(connStr[1] != 0) {
            throw SocketException(_("The socks server requires authentication"));
        }
    } else {
        // We try the username and password auth type (no, we don't support gssapi)

        connStr.push_back(5);           // SOCKSv5
        connStr.push_back(1);           // 1 method
        connStr.push_back(2);           // Method 2: Name/Password...
        streamWriteAll(connStr.data(), 3, timeLeft(start, timeout));

        if(streamReadAll(connStr.data(), 2, timeLeft(start, timeout)) != 2) {
            throw SocketException(_("The socks server failed establish a connection"));
        }
        if(connStr[1] != 2) {
            throw SocketException(_("The socks server doesn't support login / password authentication"));
        }

        connStr.clear();
        // Now we send the username / pw...
        connStr.push_back(1);
        connStr.push_back((uint8_t)config.user.length());
        connStr.insert(connStr.end(), config.user.begin(), config.user.end());
        connStr.push_back((uint8_t)config.password.length());
        connStr.insert(connStr.end(), config.password.begin(), config.password.end());

        streamWriteAll(connStr.data(), connStr.size(), timeLeft(start, timeout));

        if(streamReadAll(connStr.data(), 2, timeLeft(start, timeout)) != 2) {
            throw SocketException(_("Socks server authentication failed (bad login / password?)"));
        }

        if(connStr[1] != 0) {
            throw SocketException(_("Socks server authentication failed (bad login / password?)"));
        }
    }
}

void Socket::socksTlsReset() {
    socksTlsActive = false;
    socksTlsWait = WAIT_NONE;
    socksTls.reset();
    socksTlsContext.reset();
}

void Socket::socksStartTls(const string& serverName, uint32_t timeout) {
    const uint64_t start = GET_TICK();
    socksTlsReset();

    socksTlsContext.reset(SSL_CTX_new(TLS_client_method()));
    if(!socksTlsContext) {
        throw SocketException(_("Failed to initialize SOCKS TLS context"));
    }

    const bool verifyPeer = streamProxyOverride && streamProxyOverride->verifyTls;
    const bool gost = streamProxyOverride && streamProxyOverride->type == StreamProxyConfig::Gost;
    if(gost && (!verifyPeer || SSL_CTX_set_min_proto_version(socksTlsContext, TLS1_2_VERSION) != 1))
        throw SocketException("Cannot configure mandatory GOST TLS verification");
    SSL_CTX_set_verify(socksTlsContext, verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    if(gost && !streamProxyOverride->caPem.empty()) {
        const auto& pem = streamProxyOverride->caPem;
        // Strictly consume a PEM certificate bundle, never fall back on bad input.
        size_t offset = 0;
        unsigned count = 0;
        while(offset < pem.size()) {
            while(offset < pem.size() && std::isspace(static_cast<unsigned char>(pem[offset]))) ++offset;
            if(offset == pem.size()) break;
            constexpr const char* begin = "-----BEGIN CERTIFICATE-----";
            constexpr const char* end = "-----END CERTIFICATE-----";
            if(pem.compare(offset, strlen(begin), begin) != 0)
                throw SocketException("Invalid GOST TLS trust roots");
            const auto finish = pem.find(end, offset);
            if(finish == string::npos) throw SocketException("Invalid GOST TLS trust roots");
            const auto next = finish + strlen(end);
            // Each bounded block is parsed independently to reject trailing garbage.
            std::unique_ptr<BIO, decltype(&BIO_free)> block(
                BIO_new_mem_buf(pem.data() + offset, int(next - offset)), BIO_free);
            std::unique_ptr<X509, decltype(&X509_free)> cert(
                block ? PEM_read_bio_X509(block.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
            if(!cert || X509_check_ca(cert.get()) <= 0 ||
               X509_STORE_add_cert(SSL_CTX_get_cert_store(socksTlsContext), cert.get()) != 1)
                throw SocketException("Invalid GOST TLS trust roots");
            ++count;
            offset = next;
        }
        if(!count) throw SocketException("Invalid GOST TLS trust roots");
        ERR_clear_error();
    } else if(verifyPeer && SSL_CTX_set_default_verify_paths(socksTlsContext) != 1)
        throw SocketException("Cannot load proxy TLS trust roots");
    SSL_CTX_set_options(socksTlsContext, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

    socksTls.reset(SSL_new(socksTlsContext));
    if(!socksTls) {
        throw SocketException(_("Failed to initialize SOCKS TLS session"));
    }
    if(verifyPeer) {
        in_addr v4{};
        in6_addr v6{};
        const bool ipLiteral = inet_pton(AF_INET, serverName.c_str(), &v4) == 1 ||
                               inet_pton(AF_INET6, serverName.c_str(), &v6) == 1;
        const int verified = ipLiteral
            ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(socksTls), serverName.c_str())
            : SSL_set1_host(socksTls, serverName.c_str());
        if(verified != 1)
            throw SocketException("Cannot configure proxy TLS identity verification");
    }

#ifndef OPENSSL_NO_TLSEXT
    if(!serverName.empty()) {
        SSL_set_tlsext_host_name(socksTls, serverName.c_str());
    }
#endif

    if(SSL_set_fd(socksTls, sock) != 1) {
        throw SocketException(_("Failed to attach SOCKS TLS session to socket"));
    }

    while(true) {
        if(gost) {
            checkProxyCancellation();
            timeLeft(start, timeout);
        }
        const int ret = SSL_connect(socksTls);
        if(ret == 1) {
            if(gost && (SSL_get_verify_result(socksTls) != X509_V_OK || SSL_version(socksTls) < TLS1_2_VERSION))
                throw SocketException("GOST TLS certificate verification failed");
            socksTlsActive = true;
            socksTlsWait = WAIT_NONE;
            return;
        }

        const int err = SSL_get_error(socksTls, ret);
        if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            const int waitFor = err == SSL_ERROR_WANT_READ ? WAIT_READ : WAIT_WRITE;
            if((Socket::wait(timeLeft(start, timeout), waitFor) & waitFor) == waitFor) {
                continue;
            }
            throw SocketException(_("SOCKS TLS handshake timed out"));
        }

        unsigned long sslError = ERR_get_error();
        char errbuf[256] = { 0 };
        if(sslError) {
            ERR_error_string_n(sslError, errbuf, sizeof(errbuf));
        }

        socksTlsReset();
        throw SocketException(str(F_("SOCKS TLS handshake failed: %1%") % (errbuf[0] ? errbuf : "unknown error")));
    }
}

int Socket::socksTlsRead(void* aBuffer, int aBufLen) {
    socksTlsWait = WAIT_NONE;
    const int ret = SSL_read(socksTls, aBuffer, aBufLen);
    if(ret > 0) {
        stats.totalDown += ret;
        return ret;
    }

    const int err = SSL_get_error(socksTls, ret);
    if(err == SSL_ERROR_WANT_READ) {
        socksTlsWait = WAIT_READ;
        return -1;
    }
    if(err == SSL_ERROR_WANT_WRITE) {
        socksTlsWait = WAIT_WRITE;
        return -1;
    }
    if(err == SSL_ERROR_ZERO_RETURN) {
        return 0;
    }

    unsigned long sslError = ERR_get_error();
    char errbuf[256] = { 0 };
    if(sslError) {
        ERR_error_string_n(sslError, errbuf, sizeof(errbuf));
    }
    throw SocketException(str(F_("SOCKS TLS read failed: %1%") % (errbuf[0] ? errbuf : "unknown error")));
}

int Socket::socksTlsWrite(const void* aBuffer, int aLen) {
    socksTlsWait = WAIT_NONE;
    const int ret = SSL_write(socksTls, aBuffer, aLen);
    if(ret > 0) {
        stats.totalUp += ret;
        return ret;
    }

    const int err = SSL_get_error(socksTls, ret);
    if(err == SSL_ERROR_WANT_READ) {
        socksTlsWait = WAIT_READ;
        return -1;
    }
    if(err == SSL_ERROR_WANT_WRITE) {
        socksTlsWait = WAIT_WRITE;
        return -1;
    }
    if(err == SSL_ERROR_ZERO_RETURN) {
        return 0;
    }

    unsigned long sslError = ERR_get_error();
    char errbuf[256] = { 0 };
    if(sslError) {
        ERR_error_string_n(sslError, errbuf, sizeof(errbuf));
    }
    throw SocketException(str(F_("SOCKS TLS write failed: %1%") % (errbuf[0] ? errbuf : "unknown error")));
}

int Socket::tlsWaitTarget(int fallback) const {
    return socksTlsActive && socksTlsWait != WAIT_NONE ? socksTlsWait : fallback;
}

#ifdef _WIN32
int Socket::getLastError() {
    return ::WSAGetLastError();
}

int Socket::checksocket(int ret) {
    if(ret == SOCKET_ERROR) {
        throw SocketException(getLastError());
    }
    return ret;
}

int Socket::check(int ret, bool blockOk) {
    if(ret == SOCKET_ERROR) {
        int error = getLastError();
        if(blockOk && error == WSAEWOULDBLOCK) {
            return -1;
        } else {
            throw SocketException(error);
        }
    }
    return ret;
}
#else
int Socket::getLastError() {
    return errno;
}

int Socket::checksocket(int ret) {
    if(ret < 0) {
        throw SocketException(getLastError());
    }
    return ret;
}

int Socket::check(int ret, bool blockOk) {
    if(ret == -1) {
        int error = getLastError();
        if(blockOk && (error == EWOULDBLOCK || error == ENOBUFS || error == EINPROGRESS || error == EAGAIN) ) {
            return -1;
        } else {
            throw SocketException(error);
        }
    }
    return ret;
}
#endif

int Socket::getSocketOptInt(int option) {
    int val;
    socklen_t len = sizeof(val);
    check(::getsockopt(sock, SOL_SOCKET, option, (char*)&val, &len));
    return val;
}

void Socket::setSocketOpt(int option, int val) {
    int len = sizeof(val);

    try {
        check(::setsockopt(sock, SOL_SOCKET, option, (char*)&val, len));
    }
    catch ( ... ) {}
}

int Socket::read(void* aBuffer, int aBufLen) {
    checkProxyCancellation();
    if(socksTlsActive && type == TYPE_TCP) {
        return socksTlsRead(aBuffer, aBufLen);
    }

    if(shadowsocksActive && type == TYPE_TCP) {
        return shadowsocksRead(aBuffer, aBufLen);
    }

    return rawRead(aBuffer, aBufLen);
}

int Socket::rawRead(void* aBuffer, int aBufLen) {
    int len = 0;

    dcassert(type == TYPE_TCP || type == TYPE_UDP);
    do {
        if(type == TYPE_TCP) {
            len = ::recv(sock, (char*)aBuffer, aBufLen, 0);
        } else {
            len = ::recvfrom(sock, (char*)aBuffer, aBufLen, 0, NULL, NULL);
        }
    } while (len < 0 && getLastError() == EINTR);
    check(len, true);

    if(len > 0) {
        stats.totalDown += len;
    }

    return len;
}

int Socket::read(void* aBuffer, int aBufLen, sockaddr_storage& remote) {
    dcassert(type == TYPE_UDP);
    if(aBufLen <= 0)
        return 0;
    if(ctx_ && globalProxyMode(ctx()) == SettingsManager::OUTGOING_GOST)
        return readGostUdp(aBuffer, aBufLen, remote);
    if(ctx_ && !ctx().getProxyRoute()->snapshot()->valid)
        throw SocketException("Global proxy route is unavailable");

    bool proxyDatagram = false;
    if(ctx_) {
        const auto* settings = ctx().getSettingsManager();
        proxyDatagram = settings->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5 ||
            (settings->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SHADOWSOCKS &&
                settings->get(SettingsManager::SHADOWSOCKS_TRANSPORT) == SettingsManager::SHADOWSOCKS_TRANSPORT_TCP_AND_UDP);
    }
    ByteVector proxyBuffer;
    void* receiveBuffer = aBuffer;
    int receiveLength = aBufLen;
    if(proxyDatagram) {
        proxyBuffer.resize(65535);
        receiveBuffer = proxyBuffer.data();
        receiveLength = static_cast<int>(proxyBuffer.size());
    }

    sockaddr_storage remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    socklen_t addr_length = sizeof(remote_addr);

    int len;
    do {
        len = ::recvfrom(sock, static_cast<char*>(receiveBuffer), receiveLength, 0,
            reinterpret_cast<sockaddr*>(&remote_addr), &addr_length);
    } while (len < 0 && getLastError() == EINTR);

    check(len, true);
    if(len > 0) {
        stats.totalDown += len;
    }

    if(ctx_ && ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5 && len > 0) {
        SocksUdpAssociationPtr association;
        {
            std::lock_guard<std::mutex> lock(udpProxyMutex);
            association = udpAssociation;
        }

        if(association && matchesUdpEndpoint(association->server, association->port, remote_addr)) {
            size_t payloadOffset = 0;
            const auto* received = static_cast<const uint8_t*>(receiveBuffer);
            if(!decodeSocks5UdpPacket(received, static_cast<size_t>(len), remote, payloadOffset)) {
                return 0;
            }

            const size_t payloadLen = static_cast<size_t>(len) - payloadOffset;
            const size_t copyLen = min(payloadLen, static_cast<size_t>(aBufLen));
            memcpy(aBuffer, received + payloadOffset, copyLen);
            return static_cast<int>(copyLen);
        }
    }

    if(ctx_ && ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SHADOWSOCKS &&
            ctx().getSettingsManager()->get(SettingsManager::SHADOWSOCKS_TRANSPORT) == SettingsManager::SHADOWSOCKS_TRANSPORT_TCP_AND_UDP && len > 0) {
        auto* sm = ctx().getSettingsManager();
        const int method = shadowsocksMethodId(sm->get(SettingsManager::SHADOWSOCKS_METHOD));
        const size_t keyLen = shadowsocksKeyLen(method);
        const auto* buf = static_cast<const uint8_t*>(receiveBuffer);
        if(keyLen == 0 || sm->get(SettingsManager::SHADOWSOCKS_PASSWORD).empty()) {
            throw SocketException(_("Shadowsocks UDP relay response is invalid"));
        }

        if(isShadowsocks2022Method(method)) {
            try {
                const Shadowsocks2022UdpMessage message = shadowsocks2022UdpSessionForSettings().decodeResponse(
                    ByteVector(buf, buf + len), static_cast<uint64_t>(time(nullptr)));
                size_t addressLength = 0;
                if(!parseSocksAddress(message.socksAddress, remote, addressLength) ||
                        addressLength != message.socksAddress.size()) {
                    throw SocketException(_("Shadowsocks UDP relay response address is invalid"));
                }
                const size_t copyLen = min(message.payload.size(), static_cast<size_t>(aBufLen));
                memcpy(aBuffer, message.payload.data(), copyLen);
                return static_cast<int>(copyLen);
            } catch(const SocketException&) {
                throw;
            } catch(const std::exception& e) {
                throw SocketException(e.what());
            }
        }

        if(static_cast<size_t>(len) <= keyLen + SHADOWSOCKS_TAG_LEN) {
            throw SocketException(_("Shadowsocks UDP relay response is invalid"));
        }

        const ByteVector salt(buf, buf + keyLen);
        const ByteVector masterKey = evpBytesToKey(sm->get(SettingsManager::SHADOWSOCKS_PASSWORD), keyLen);
        const ByteVector subkey = hkdfSha1(masterKey, salt, "ss-subkey", keyLen);
        ByteVector nonce(SHADOWSOCKS_NONCE_LEN, 0);
        ByteVector plain;
        if(!shadowsocksAeadDecrypt(method, subkey, nonce, buf + keyLen, len - keyLen, plain)) {
            throw SocketException(_("Shadowsocks UDP relay response decryption failed"));
        }

        size_t payloadOffset = 0;
        if(!parseSocksAddress(plain, remote, payloadOffset) || payloadOffset > plain.size()) {
            throw SocketException(_("Shadowsocks UDP relay response address is invalid"));
        }

        const size_t payloadLen = plain.size() - payloadOffset;
        const size_t copyLen = min(payloadLen, static_cast<size_t>(aBufLen));
        memcpy(aBuffer, plain.data() + payloadOffset, copyLen);
        return static_cast<int>(copyLen);
    }

    if(receiveBuffer != aBuffer && len > 0) {
        const size_t copyLen = min(static_cast<size_t>(len), static_cast<size_t>(aBufLen));
        memcpy(aBuffer, receiveBuffer, copyLen);
        len = static_cast<int>(copyLen);
    }
    remote = remote_addr;
    return len;
}

bool Socket::encodeSocks5UdpPacket(const string& address, const string& port,
                                   const void* payload, size_t payloadLen,
                                   bool remoteResolve, ByteVector& packet)
{
    packet.clear();
    packet.push_back(0);
    packet.push_back(0);
    packet.push_back(0);

    if(!appendSocksAddress(packet, address, port, remoteResolve) || (payloadLen > 0 && payload == nullptr)) {
        packet.clear();
        return false;
    }

    if(payloadLen > 0) {
        const auto* payloadBytes = static_cast<const uint8_t*>(payload);
        packet.insert(packet.end(), payloadBytes, payloadBytes + payloadLen);
    }
    return true;
}

bool Socket::decodeSocks5UdpPacket(const uint8_t* packet, size_t packetLen,
                                   sockaddr_storage& remote, size_t& payloadOffset)
{
    payloadOffset = 0;
    if(packet == nullptr || packetLen < 4 || packet[0] != 0 || packet[1] != 0 || packet[2] != 0)
        return false;

    const ByteVector address(packet + 3, packet + packetLen);
    size_t addressLength = 0;
    if(!parseSocksAddress(address, remote, addressLength))
        return false;

    payloadOffset = 3 + addressLength;
    return payloadOffset <= packetLen;
}

bool Socket::resolveUdpEndpoint(const string& host, const string& port, int requestedFamily,
                                vector<sockaddr_storage>& endpoints)
{
    endpoints.clear();

    addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
    hints.ai_family = requestedFamily;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = udpResolverFlags(requestedFamily);

    addrinfo* result = nullptr;
    if(getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || result == nullptr)
        return false;

    for(addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        if(ai->ai_addr == nullptr || ai->ai_addrlen > sizeof(sockaddr_storage))
            continue;

        sockaddr_storage endpoint = {};
        memcpy(&endpoint, ai->ai_addr, static_cast<size_t>(ai->ai_addrlen));
        endpoints.push_back(endpoint);
    }

    freeaddrinfo(result);
    return !endpoints.empty();
}

int Socket::udpResolverFlags(int requestedFamily)
{
    int flags = 0;
#ifdef AI_V4MAPPED
    if(requestedFamily == AF_INET6)
        flags |= AI_V4MAPPED;
#endif
#ifdef AI_ALL
    if(requestedFamily == AF_INET6)
        flags |= AI_ALL;
#endif
    return flags;
}

bool Socket::matchesUdpEndpoint(const string& host, const string& port, const sockaddr_storage& endpoint)
{
    const auto* address = reinterpret_cast<const sockaddr*>(&endpoint);
    if(sockaddrToPort(address) != port)
        return false;

    const string endpointIp = sockaddrToIp(address);
    if(endpointIp == host)
        return true;

    vector<sockaddr_storage> candidates;
    if(!resolveUdpEndpoint(host, port, endpoint.ss_family, candidates))
        return false;

    return std::any_of(candidates.begin(), candidates.end(), [&endpointIp](const sockaddr_storage& candidate) {
        return sockaddrToIp(reinterpret_cast<const sockaddr*>(&candidate)) == endpointIp;
    });
}

int Socket::readAll(void* aBuffer, int aBufLen, uint32_t timeout) {
    uint8_t* buf = (uint8_t*)aBuffer;
    int i = 0;
    while(i < aBufLen) {
        int j = read(buf + i, aBufLen - i);
        if(j == 0) {
            return i;
        } else if(j == -1) {
            const int waitFor = tlsWaitTarget(WAIT_READ);
            if((wait(timeout, waitFor) & waitFor) != waitFor) {
                return i;
            }
            continue;
        }

        i += j;
    }
    return i;
}

int Socket::streamReadAll(void* aBuffer, int aBufLen, uint32_t timeout) {
    uint8_t* buf = static_cast<uint8_t*>(aBuffer);
    int i = 0;
    const uint64_t start = GET_TICK();
    while(i < aBufLen) {
        timeLeft(start, timeout);
        int j = Socket::read(buf + i, aBufLen - i);
        if(j == 0) {
            return i;
        } else if(j == -1) {
            const int waitFor = tlsWaitTarget(WAIT_READ);
            if((Socket::wait(timeLeft(start, timeout), waitFor) & waitFor) != waitFor) {
                throw SocketException(_("Connection timeout"));
            }
            continue;
        }

        i += j;
    }
    return i;
}

void Socket::writeAll(const void* aBuffer, int aLen, uint32_t timeout) {
    const uint8_t* buf = (const uint8_t*)aBuffer;
    int pos = 0;
    // No use sending more than this at a time...
    int sendSize = getSocketOptInt(SO_SNDBUF);

    while(pos < aLen) {
        int i = write(buf+pos, (int)min(aLen-pos, sendSize));
        if(i == -1) {
            const int waitFor = tlsWaitTarget(WAIT_WRITE);
            if((wait(timeout, waitFor) & waitFor) != waitFor)
                throw SocketException(_("Connection timeout"));
        } else {
            pos+=i;
            stats.totalUp += i;
        }
    }

    while(shadowsocksActive && type == TYPE_TCP && !shadowsocksPendingOut.empty()) {
        if(!shadowsocksFlushPending() && wait(timeout, WAIT_WRITE) != WAIT_WRITE)
            throw SocketException(_("Connection timeout"));
    }
}

void Socket::streamWriteAll(const void* aBuffer, int aLen, uint32_t timeout) {
    const uint8_t* buf = static_cast<const uint8_t*>(aBuffer);
    int pos = 0;
    int sendSize = getSocketOptInt(SO_SNDBUF);
    const uint64_t start = GET_TICK();

    while(pos < aLen) {
        timeLeft(start, timeout);
        int i = Socket::write(buf + pos, static_cast<int>(min(aLen - pos, sendSize)));
        if(i == -1) {
            const int waitFor = tlsWaitTarget(WAIT_WRITE);
            if((Socket::wait(timeLeft(start, timeout), waitFor) & waitFor) != waitFor)
                throw SocketException(_("Connection timeout"));
        } else {
            pos += i;
        }
    }

    while(shadowsocksActive && type == TYPE_TCP && !shadowsocksPendingOut.empty()) {
        timeLeft(start, timeout);
        if(!flushProxyOutput() && Socket::wait(timeLeft(start, timeout), WAIT_WRITE) != WAIT_WRITE)
            throw SocketException(_("Connection timeout"));
    }
}

int Socket::write(const void* aBuffer, int aLen) {
    checkProxyCancellation();
    if(socksTlsActive && type == TYPE_TCP) {
        return socksTlsWrite(aBuffer, aLen);
    }

    if(shadowsocksActive && type == TYPE_TCP) {
        return shadowsocksWrite(aBuffer, aLen);
    }

    return rawWrite(aBuffer, aLen);
}

int Socket::rawWrite(const void* aBuffer, int aLen) {
    int sent;
    do {
        sent = ::send(sock, (const char*)aBuffer, aLen, MSG_NOSIGNAL);
    } while (sent < 0 && getLastError() == EINTR);

    check(sent, true);
    if(sent > 0) {
        stats.totalUp += sent;
    }
    return sent;
}

void Socket::shadowsocksReset() {
    shadowsocksActive = false;
    shadowsocksMethod = SHADOWSOCKS_NONE;
    shadowsocksMasterKey.clear();
    shadowsocksSubkey.clear();
    shadowsocksDecSubkey.clear();
    shadowsocksEncNonce.clear();
    shadowsocksDecNonce.clear();
    shadowsocksPlainIn.clear();
    shadowsocksPlainPos = 0;
    shadowsocksCipherIn.clear();
    shadowsocksPendingOut.clear();
    shadowsocksPendingOutPos = 0;
    shadowsocksExpectedPayload = 0;
    shadowsocksReadingPayload = false;
    shadowsocks2022Stream.reset();
    shadowsocks2022ResponseStarted = false;
    shadowsocks2022ResponseHeaderRead = false;
    shadowsocks2022UdpSession.reset();
    shadowsocks2022UdpConfig.clear();
    shadowsocks2022UdpPacketId = 0;
}

bool Socket::shadowsocksStart(const string& method, const string& password,
    const ByteVector& target, uint32_t timeout) {
    shadowsocksReset();

    shadowsocksMethod = shadowsocksMethodId(method);
    const size_t keyLen = shadowsocksKeyLen(shadowsocksMethod);
    if(keyLen == 0) {
        throw SocketException(_("Unsupported Shadowsocks cipher"));
    }

    if(isShadowsocks2022Method(shadowsocksMethod)) {
        try {
            const Shadowsocks2022::Method parsedMethod = shadowsocks2022Method(shadowsocksMethod);
            Shadowsocks2022::PskChain chain = Shadowsocks2022::parsePskChain(
                parsedMethod, password);
            shadowsocks2022Stream = std::make_unique<Shadowsocks2022TcpClient>(
                parsedMethod, std::move(chain));

            ByteVector salt(keyLen);
            uint16_t randomPadding = 0;
            if(RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
                    RAND_bytes(reinterpret_cast<uint8_t*>(&randomPadding), sizeof(randomPadding)) != 1) {
                throw SocketException(_("Failed to create Shadowsocks salt"));
            }
            const size_t paddingLength = static_cast<size_t>(randomPadding % 900) + 1;
            ByteVector padding(paddingLength);
            if(RAND_bytes(padding.data(), static_cast<int>(padding.size())) != 1) {
                throw SocketException(_("Failed to create Shadowsocks padding"));
            }

            const ByteVector request = shadowsocks2022Stream->encodeRequestHeader(
                salt, static_cast<uint64_t>(time(nullptr)), target, padding, {});
            shadowsocksActive = true;
            shadowsocksWriteAll(request.data(), static_cast<int>(request.size()), timeout);
            return true;
        } catch(const SocketException&) {
            shadowsocksReset();
            throw;
        } catch(const std::exception& e) {
            shadowsocksReset();
            throw SocketException(e.what());
        }
    }

    ByteVector salt(keyLen);
    if(RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw SocketException(_("Failed to create Shadowsocks salt"));
    }

    shadowsocksMasterKey = evpBytesToKey(password, keyLen);
    shadowsocksSubkey = hkdfSha1(shadowsocksMasterKey, salt, "ss-subkey", keyLen);
    shadowsocksEncNonce.assign(SHADOWSOCKS_NONCE_LEN, 0);
    shadowsocksDecNonce.assign(SHADOWSOCKS_NONCE_LEN, 0);
    shadowsocksActive = true;

    shadowsocksWriteAll(salt.data(), salt.size(), timeout);
    return false;
}

void Socket::shadowsocksWriteAll(const void* aBuffer, int aLen, uint32_t timeout) {
    const uint8_t* buf = static_cast<const uint8_t*>(aBuffer);
    int pos = 0;
    const uint64_t start = GET_TICK();

    while(pos < aLen) {
        checkProxyCancellation();
        timeLeft(start, timeout);
        int sent = rawWrite(buf + pos, aLen - pos);
        if(sent == -1) {
            if(wait(timeLeft(start, timeout), WAIT_WRITE) != WAIT_WRITE) {
                throw SocketException(_("Connection timeout"));
            }
            continue;
        }
        pos += sent;
    }
}

bool Socket::shadowsocksFlushPending() {
    while(shadowsocksPendingOutPos < shadowsocksPendingOut.size()) {
        int sent = rawWrite(shadowsocksPendingOut.data() + shadowsocksPendingOutPos,
            static_cast<int>(shadowsocksPendingOut.size() - shadowsocksPendingOutPos));
        if(sent == -1)
            return false;

        shadowsocksPendingOutPos += sent;
    }

    shadowsocksPendingOut.clear();
    shadowsocksPendingOutPos = 0;
    return true;
}

bool Socket::hasPendingProxyOutput() const {
    return shadowsocksActive && shadowsocksPendingOutPos < shadowsocksPendingOut.size();
}

bool Socket::flushProxyOutput() {
    checkProxyCancellation();
    return !hasPendingProxyOutput() || shadowsocksFlushPending();
}

bool Socket::shutdownWrite() {
    if(!flushProxyOutput()) return false;
    if(socksTlsActive && socksTls) {
        const int result = SSL_shutdown(socksTls);
        if(result < 0) {
            const int error = SSL_get_error(socksTls, result);
            if(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return false;
            throw SocketException("Proxy TLS shutdown failed");
        }
    }
    if(sock != INVALID_SOCKET) check(::shutdown(sock, 1));
    return true;
}

int Socket::shadowsocksWrite(const void* aBuffer, int aLen) {
    if(aLen <= 0)
        return 0;

    if(!shadowsocksFlushPending())
        return -1;

    const size_t maxChunk = shadowsocks2022Stream ?
        static_cast<size_t>(std::numeric_limits<uint16_t>::max()) : SHADOWSOCKS_MAX_CHUNK;
    const size_t plainLen = min(static_cast<size_t>(aLen), maxChunk);
    const uint8_t* buf = static_cast<const uint8_t*>(aBuffer);

    if(shadowsocks2022Stream) {
        try {
            shadowsocksPendingOut = shadowsocks2022Stream->encodeRequestChunk(
                ByteVector(buf, buf + plainLen));
        } catch(const std::exception& e) {
            throw SocketException(e.what());
        }
        shadowsocksFlushPending();
        return static_cast<int>(plainLen);
    }

    uint8_t lenBuf[2] = {
        static_cast<uint8_t>((plainLen >> 8) & 0xff),
        static_cast<uint8_t>(plainLen & 0xff)
    };
    ByteVector encryptedLen = shadowsocksAeadEncrypt(shadowsocksMethod, shadowsocksSubkey, shadowsocksEncNonce, lenBuf, sizeof(lenBuf));
    ByteVector encryptedPayload = shadowsocksAeadEncrypt(shadowsocksMethod, shadowsocksSubkey, shadowsocksEncNonce, buf, plainLen);

    shadowsocksPendingOut.reserve(encryptedLen.size() + encryptedPayload.size());
    shadowsocksPendingOut.insert(shadowsocksPendingOut.end(), encryptedLen.begin(), encryptedLen.end());
    shadowsocksPendingOut.insert(shadowsocksPendingOut.end(), encryptedPayload.begin(), encryptedPayload.end());

    shadowsocksFlushPending();
    return static_cast<int>(plainLen);
}

bool Socket::shadowsocksEnsureReceiveSubkey() {
    if(!shadowsocksDecSubkey.empty())
        return true;

    const size_t keyLen = shadowsocksKeyLen(shadowsocksMethod);
    if(keyLen == 0 || shadowsocksMasterKey.empty()) {
        throw SocketException(_("Unsupported Shadowsocks cipher"));
    }

    if(shadowsocksCipherIn.size() < keyLen)
        return false;

    ByteVector salt(shadowsocksCipherIn.begin(), shadowsocksCipherIn.begin() + keyLen);
    shadowsocksCipherIn.erase(shadowsocksCipherIn.begin(), shadowsocksCipherIn.begin() + keyLen);
    shadowsocksDecSubkey = hkdfSha1(shadowsocksMasterKey, salt, "ss-subkey", keyLen);
    shadowsocksDecNonce.assign(SHADOWSOCKS_NONCE_LEN, 0);
    return true;
}

bool Socket::shadowsocksTryDecode() {
    if(shadowsocks2022Stream) {
        return shadowsocksTryDecode2022();
    }

    while(true) {
        if(!shadowsocksEnsureReceiveSubkey())
            return false;

        if(!shadowsocksReadingPayload) {
            const size_t required = 2 + SHADOWSOCKS_TAG_LEN;
            if(shadowsocksCipherIn.size() < required)
                return false;

            ByteVector plainLen;
            if(!shadowsocksAeadDecrypt(shadowsocksMethod, shadowsocksDecSubkey, shadowsocksDecNonce,
                    shadowsocksCipherIn.data(), required, plainLen) || plainLen.size() != 2) {
                throw SocketException(_("Shadowsocks decryption failed"));
            }

            shadowsocksCipherIn.erase(shadowsocksCipherIn.begin(), shadowsocksCipherIn.begin() + required);
            shadowsocksExpectedPayload = (static_cast<size_t>(plainLen[0]) << 8) | plainLen[1];
            if(shadowsocksExpectedPayload > SHADOWSOCKS_MAX_CHUNK) {
                throw SocketException(_("Invalid Shadowsocks payload size"));
            }
            shadowsocksReadingPayload = true;
        }

        const size_t required = shadowsocksExpectedPayload + SHADOWSOCKS_TAG_LEN;
        if(shadowsocksCipherIn.size() < required)
            return false;

        ByteVector plainPayload;
        if(!shadowsocksAeadDecrypt(shadowsocksMethod, shadowsocksDecSubkey, shadowsocksDecNonce,
                shadowsocksCipherIn.data(), required, plainPayload)) {
            throw SocketException(_("Shadowsocks decryption failed"));
        }

        shadowsocksCipherIn.erase(shadowsocksCipherIn.begin(), shadowsocksCipherIn.begin() + required);
        shadowsocksPlainIn.insert(shadowsocksPlainIn.end(), plainPayload.begin(), plainPayload.end());
        shadowsocksExpectedPayload = 0;
        shadowsocksReadingPayload = false;

        if(!shadowsocksPlainIn.empty())
            return true;
    }
}

bool Socket::shadowsocksTryDecode2022() {
    try {
        while(true) {
            if(!shadowsocks2022ResponseStarted) {
                const size_t saltSize = shadowsocksKeyLen(shadowsocksMethod);
                if(shadowsocksCipherIn.size() < saltSize)
                    return false;
                const ByteVector salt(shadowsocksCipherIn.begin(),
                    shadowsocksCipherIn.begin() + static_cast<ptrdiff_t>(saltSize));
                shadowsocksCipherIn.erase(shadowsocksCipherIn.begin(),
                    shadowsocksCipherIn.begin() + static_cast<ptrdiff_t>(saltSize));
                shadowsocks2022Stream->beginResponse(salt);
                shadowsocks2022ResponseStarted = true;
            }

            if(!shadowsocks2022ResponseHeaderRead) {
                const size_t required = shadowsocks2022Stream->responseHeaderCiphertextSize();
                if(shadowsocksCipherIn.size() < required)
                    return false;
                const ByteVector header(shadowsocksCipherIn.begin(),
                    shadowsocksCipherIn.begin() + static_cast<ptrdiff_t>(required));
                shadowsocksCipherIn.erase(shadowsocksCipherIn.begin(),
                    shadowsocksCipherIn.begin() + static_cast<ptrdiff_t>(required));
                shadowsocksExpectedPayload = shadowsocks2022Stream->decodeResponseHeader(
                    header, static_cast<uint64_t>(time(nullptr)));
                shadowsocks2022ResponseHeaderRead = true;
                shadowsocksReadingPayload = true;
            } else if(!shadowsocksReadingPayload) {
                constexpr size_t required = 2 + SHADOWSOCKS_TAG_LEN;
                if(shadowsocksCipherIn.size() < required)
                    return false;
                const ByteVector length(shadowsocksCipherIn.begin(),
                    shadowsocksCipherIn.begin() + required);
                shadowsocksCipherIn.erase(shadowsocksCipherIn.begin(),
                    shadowsocksCipherIn.begin() + required);
                shadowsocksExpectedPayload = shadowsocks2022Stream->decodeResponseLength(length);
                shadowsocksReadingPayload = true;
            }

            const size_t required = shadowsocksExpectedPayload + SHADOWSOCKS_TAG_LEN;
            if(shadowsocksCipherIn.size() < required)
                return false;
            const ByteVector payload(shadowsocksCipherIn.begin(),
                shadowsocksCipherIn.begin() + static_cast<ptrdiff_t>(required));
            shadowsocksCipherIn.erase(shadowsocksCipherIn.begin(),
                shadowsocksCipherIn.begin() + static_cast<ptrdiff_t>(required));
            const ByteVector plaintext = shadowsocks2022Stream->decodeResponsePayload(
                payload, static_cast<uint16_t>(shadowsocksExpectedPayload));
            shadowsocksPlainIn.insert(shadowsocksPlainIn.end(), plaintext.begin(), plaintext.end());
            shadowsocksExpectedPayload = 0;
            shadowsocksReadingPayload = false;

            if(!shadowsocksPlainIn.empty())
                return true;
        }
    } catch(const std::exception& e) {
        throw SocketException(e.what());
    }
}

int Socket::shadowsocksRead(void* aBuffer, int aBufLen) {
    if(aBufLen <= 0)
        return 0;

    auto copyPlain = [&]() -> int {
        if(shadowsocksPlainPos >= shadowsocksPlainIn.size())
            return 0;

        const size_t available = shadowsocksPlainIn.size() - shadowsocksPlainPos;
        const size_t copyLen = min(static_cast<size_t>(aBufLen), available);
        memcpy(aBuffer, shadowsocksPlainIn.data() + shadowsocksPlainPos, copyLen);
        shadowsocksPlainPos += copyLen;
        if(shadowsocksPlainPos >= shadowsocksPlainIn.size()) {
            shadowsocksPlainIn.clear();
            shadowsocksPlainPos = 0;
        }
        stats.totalDown += copyLen;
        return static_cast<int>(copyLen);
    };

    if(int copied = copyPlain())
        return copied;

    if(shadowsocksTryDecode()) {
        if(int copied = copyPlain())
            return copied;
    }

    uint8_t buf[8192];
    while(true) {
        int len = rawRead(buf, sizeof(buf));
        if(len == 0) {
            const bool incomplete2022Header = shadowsocks2022Stream &&
                shadowsocks2022ResponseStarted && !shadowsocks2022ResponseHeaderRead;
            if(!shadowsocksCipherIn.empty() || shadowsocksReadingPayload || incomplete2022Header) {
                throw SocketException(_("Shadowsocks stream ended with an incomplete frame"));
            }
            return 0;
        }
        if(len == -1)
            return -1;

        shadowsocksCipherIn.insert(shadowsocksCipherIn.end(), buf, buf + len);
        if(shadowsocksTryDecode()) {
            return copyPlain();
        }
    }
}

Shadowsocks2022UdpSession& Socket::shadowsocks2022UdpSessionForSettings() {
    auto* sm = ctx().getSettingsManager();
    const string methodName = normalizeCipherName(sm->get(SettingsManager::SHADOWSOCKS_METHOD));
    const string password = sm->get(SettingsManager::SHADOWSOCKS_PASSWORD);
    const string config = methodName + '\n' + password;
    if(shadowsocks2022UdpSession && shadowsocks2022UdpConfig == config) {
        return *shadowsocks2022UdpSession;
    }

    try {
        const int methodId = shadowsocksMethodId(methodName);
        const Shadowsocks2022::Method method = shadowsocks2022Method(methodId);
        Shadowsocks2022::PskChain chain = Shadowsocks2022::parsePskChain(method, password);
        ByteVector clientSessionId(8);
        if(RAND_bytes(clientSessionId.data(), static_cast<int>(clientSessionId.size())) != 1) {
            throw SocketException(_("Failed to create Shadowsocks UDP session"));
        }
        shadowsocks2022UdpSession = std::make_unique<Shadowsocks2022UdpSession>(
            method, std::move(chain), std::move(clientSessionId));
        shadowsocks2022UdpConfig = config;
        shadowsocks2022UdpPacketId = 0;
        return *shadowsocks2022UdpSession;
    } catch(const SocketException&) {
        throw;
    } catch(const std::exception& e) {
        throw SocketException(e.what());
    }
}

/**
* Sends data, will block until all data has been sent or an exception occurs
* @param aBuffer Buffer with data
* @param aLen Data length
* @throw SocketExcpetion Send failed.
*/
bool Socket::hasGostUdpTransport() const {
    std::lock_guard<std::mutex> lock(gostUdpMutex);
    return gostUdp && gostUdp->route && !gostUdp->route->revoked->load() &&
        gostUdp->relay && gostUdp->relay->isRunning();
}

void Socket::writeGostUdp(const string& address, const string& port, const void* buffer, int length, UdpSendInfo* info) {
    const auto route = ctx().getProxyRoute()->snapshot();
    if(!route->valid || route->revoked->load() || route->mode != SettingsManager::OUTGOING_GOST)
        throw SocketException("Global GOST route is unavailable");
    unsigned targetPort = 0;
    const auto parsed = std::from_chars(port.data(), port.data() + port.size(), targetPort);
    if(parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || !targetPort || targetPort > 65535 ||
       length > static_cast<int>(gost::MaxPayload))
        throw SocketException("Invalid GOST UDP destination or payload");
    ByteVector packet;
    try {
        const auto* bytes = static_cast<const uint8_t*>(buffer);
        packet = gost::encodeSocks({address, static_cast<uint16_t>(targetPort), ByteVector(bytes, bytes + length)});
    } catch(const std::invalid_argument&) { throw SocketException("Invalid GOST UDP destination"); }
    if(sock == INVALID_SOCKET) {
        create(TYPE_UDP, AF_INET);
        bind("0", "");
    } else if(getLocalPort() == "0") {
        bind("0", "");
    }
    const string local = family == AF_INET6 ? "::1" : "127.0.0.1";
    const auto bound = getLocalIp();
    if(type != TYPE_UDP || (bound != local && bound != "0.0.0.0" && bound != "::"))
        throw SocketException("GOST UDP requires a new loopback-bound socket");
    std::lock_guard<std::mutex> lock(gostUdpMutex);
    if(!gostUdp || gostUdp->route != route) {
        gostUdp.reset();
        // Discard old-generation replies before opening any replacement relay.
        std::array<char, 65535> stale{};
        setBlocking(false);
        for(unsigned i = 0; i < 4096; ++i)
            if(::recv(sock, stale.data(), stale.size(), 0) < 0) break;
        gostUdp = std::make_shared<GostUdpState>();
        gostUdp->route = route;
    }
    auto& state = *gostUdp;
    if(state.relay && !state.relay->isRunning()) state.failed();
    if(!state.relay) {
        if(GET_TICK() < state.nextAttempt) throw SocketException("GOST UDP reconnect backoff is active");
        try {
            if(udpClosing || route->revoked->load()) throw SocketException("GOST UDP cancelled");
            auto slot = ctx().getProxyRoute()->acquireUdpSlot();
            if(!slot) throw SocketException("Global GOST UDP association limit reached");
            auto config = route->proxy;
            config.cancelled = [this, route] { return udpClosing.load() || route->revoked->load(); };
            // udpClosing has no notifier; the inherited route signal alone is insufficient.
            config.cancellationNotifier.reset();
            const auto start = GET_TICK();
            const auto endpoints = resolveProxyEndpoint(config.host, 10000, config.cancelled);
            config.connectHost = endpoints.front();
            state.relay = std::make_unique<GostUdpRelay>(config, timeLeft(start, 10000), endpoints, std::move(slot));
            state.port = state.relay->start(local, local, static_cast<uint16_t>(std::stoi(getLocalPort())));
            state.connectedAt = GET_TICK();
        } catch(...) { state.failed(); throw; }
    }
    if(udpClosing || route->revoked->load()) throw SocketException("GOST UDP cancelled");
    sockaddr_storage endpoint{};
    socklen_t size;
    if(family == AF_INET6) {
        auto& value = reinterpret_cast<sockaddr_in6&>(endpoint);
        value.sin6_family = AF_INET6; value.sin6_port = htons(state.port);
        inet_pton(AF_INET6, "::1", &value.sin6_addr);
        size = sizeof(value);
    } else {
        auto& value = reinterpret_cast<sockaddr_in&>(endpoint);
        value.sin_family = AF_INET; value.sin_port = htons(state.port);
        inet_pton(AF_INET, "127.0.0.1", &value.sin_addr);
        size = sizeof(value);
    }
    int sent;
    do {
        sent = ::sendto(sock, reinterpret_cast<const char*>(packet.data()), packet.size(), 0,
            reinterpret_cast<const sockaddr*>(&endpoint), size);
    } while(sent < 0 && getLastError() == EINTR);
    check(sent);
    populateUdpSendInfo(info, address, port, reinterpret_cast<const sockaddr*>(&endpoint), sent, true);
    stats.totalUp += sent;
}

int Socket::readGostUdp(void* buffer, int length, sockaddr_storage& remote) {
    std::lock_guard<std::mutex> lock(gostUdpMutex);
    std::array<uint8_t, 65535> packet{};
    sockaddr_storage sender{};
    socklen_t size = sizeof(sender);
    int received;
    do {
        received = ::recvfrom(sock, reinterpret_cast<char*>(packet.data()), packet.size(), 0,
            reinterpret_cast<sockaddr*>(&sender), &size);
    } while(received < 0 && getLastError() == EINTR);
    check(received, true);
    if(received <= 0) return received;
    if(!gostUdp || !gostUdp->relay || !gostUdp->relay->isRunning() || gostUdp->route->revoked->load() || udpClosing)
        return -1;
    const string local = family == AF_INET6 ? "::1" : "127.0.0.1";
    if(sockaddrToIp(reinterpret_cast<sockaddr*>(&sender)) != local ||
       sockaddrToPort(reinterpret_cast<sockaddr*>(&sender)) != std::to_string(gostUdp->port)) return -1;
    gost::Datagram decoded;
    if(gost::decodeSocks(std::span(packet.data(), received), decoded) != gost::DecodeResult::Complete)
        return -1;
    sockaddr_storage source{};
    auto& v4 = reinterpret_cast<sockaddr_in&>(source);
    auto& v6 = reinterpret_cast<sockaddr_in6&>(source);
    if(inet_pton(AF_INET, decoded.host.c_str(), &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET; v4.sin_port = htons(decoded.port);
    } else if(inet_pton(AF_INET6, decoded.host.c_str(), &v6.sin6_addr) == 1) {
        v6.sin6_family = AF_INET6; v6.sin6_port = htons(decoded.port);
    } else return -1; // Never resolve a remote reply's hostname locally.
    if(gostUdp->route->revoked->load()) return -1;
    remote = source;
    const auto count = std::min<size_t>(length, decoded.payload.size());
    memcpy(buffer, decoded.payload.data(), count);
    stats.totalDown += received;
    return static_cast<int>(count);
}

void Socket::writeTo(const string& aAddr, const string& aPort, const void* aBuffer, int aLen, bool proxy, UdpSendInfo* sendInfo) {
    if(aLen <= 0)
        return;
    if(ctx_) {
        const auto mode = globalProxyMode(ctx());
        if(mode == SettingsManager::OUTGOING_GOST) {
            writeGostUdp(aAddr, aPort, aBuffer, aLen, sendInfo);
            return;
        }
        if(mode < SettingsManager::OUTGOING_DIRECT || mode > SettingsManager::OUTGOING_GOST ||
           !ctx().getProxyRoute()->snapshot()->valid)
            throw SocketException("Global proxy route is unavailable");
        // Restore the requested bind, not the internal loopback transport bind.
        const auto originalBind = gostUdpOriginalBind;
        if(originalBind) {
            const auto originalFamily = family;
            close();
            create(TYPE_UDP, originalFamily);
            bind(originalBind->first, originalBind->second);
        } else {
            std::lock_guard<std::mutex> lock(gostUdpMutex);
            if(gostUdp) {
                gostUdp.reset();
                std::array<char, 65535> stale{};
                setBlocking(false);
                for(unsigned i = 0; i < 4096; ++i)
                    if(::recv(sock, stale.data(), stale.size(), 0) < 0) break;
            }
        }
    }

    const int targetFamily = inferAddressFamily(aAddr);
    if(sock == INVALID_SOCKET) {
        int preferredFamily = AF_INET;
        if(ctx_ && ctx().getSettingsManager()->getBool(SettingsManager::USE_IPV6)) {
            preferredFamily = AF_INET6;
        }
        if(targetFamily != AF_UNSPEC) {
            preferredFamily = targetFamily;
        }
        create(TYPE_UDP, preferredFamily);
    }

    dcassert(type == TYPE_UDP);

    if(aAddr.empty() || aPort.empty()) {
        throw SocketException(EADDRNOTAVAIL);
    }

    const auto* buf = static_cast<const uint8_t*>(aBuffer);
    int sent = SOCKET_ERROR;
    sockaddr_storage sentAddress = {};
    bool hasSentAddress = false;
    bool usedProxy = false;
    if(ctx_ && ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SHADOWSOCKS && proxy) {
        usedProxy = true;
        auto* sm = ctx().getSettingsManager();
        if(sm->get(SettingsManager::SHADOWSOCKS_TRANSPORT) != SettingsManager::SHADOWSOCKS_TRANSPORT_TCP_AND_UDP) {
            throw SocketException(_("Shadowsocks UDP relay is disabled"));
        }

        const int method = shadowsocksMethodId(sm->get(SettingsManager::SHADOWSOCKS_METHOD));
        const size_t keyLen = shadowsocksKeyLen(method);
        if(keyLen == 0 || sm->get(SettingsManager::SHADOWSOCKS_SERVER).empty() || sm->get(SettingsManager::SHADOWSOCKS_PASSWORD).empty()) {
            throw SocketException(_("Shadowsocks UDP relay is not configured"));
        }

        ByteVector target;
        if(!appendSocksAddress(target, aAddr, aPort, sm->getBool(SettingsManager::SOCKS_RESOLVE, true))) {
            throw SocketException(_("The Shadowsocks target address is invalid"));
        }

        ByteVector packet;
        if(isShadowsocks2022Method(method)) {
            ByteVector nonce;
            if(method == SHADOWSOCKS_2022_BLAKE3_CHACHA20_POLY1305) {
                nonce.resize(XChaCha20Poly1305::NONCE_SIZE);
                if(RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
                    throw SocketException(_("Failed to create Shadowsocks UDP nonce"));
                }
            }
            if(shadowsocks2022UdpPacketId == std::numeric_limits<uint64_t>::max()) {
                throw SocketException(_("Shadowsocks UDP packet counter exhausted"));
            }
            try {
                packet = shadowsocks2022UdpSessionForSettings().encodeRequest(
                    shadowsocks2022UdpPacketId++, static_cast<uint64_t>(time(nullptr)), {},
                    target, ByteVector(buf, buf + aLen), nonce);
            } catch(const SocketException&) {
                throw;
            } catch(const std::exception& e) {
                throw SocketException(e.what());
            }
        } else {
            ByteVector plain = target;
            plain.insert(plain.end(), buf, buf + aLen);

            ByteVector salt(keyLen);
            if(RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
                throw SocketException(_("Failed to create Shadowsocks salt"));
            }

            const ByteVector masterKey = evpBytesToKey(sm->get(SettingsManager::SHADOWSOCKS_PASSWORD), keyLen);
            const ByteVector subkey = hkdfSha1(masterKey, salt, "ss-subkey", keyLen);
            ByteVector nonce(SHADOWSOCKS_NONCE_LEN, 0);
            packet = salt;
            ByteVector encrypted = shadowsocksAeadEncrypt(method, subkey, nonce, plain.data(), plain.size());
            packet.insert(packet.end(), encrypted.begin(), encrypted.end());
        }

        addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
        hints.ai_family = family;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        addrinfo* result = nullptr;
        const string proxyPort = Util::toString(sm->get(SettingsManager::SHADOWSOCKS_PORT));
        if(getaddrinfo(sm->get(SettingsManager::SHADOWSOCKS_SERVER).c_str(), proxyPort.c_str(), &hints, &result) != 0 || result == nullptr) {
            throw SocketException(EADDRNOTAVAIL);
        }

        int savedError = EADDRNOTAVAIL;
        for(addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
            do {
                sent = ::sendto(sock, reinterpret_cast<const char*>(packet.data()), packet.size(), 0, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
            } while(sent < 0 && getLastError() == EINTR);

            if(sent >= 0) {
                memcpy(&sentAddress, ai->ai_addr, std::min(sizeof(sentAddress), static_cast<size_t>(ai->ai_addrlen)));
                hasSentAddress = true;
                break;
            }
            savedError = getLastError();
        }

        freeaddrinfo(result);
        if(sent < 0) {
            throw SocketException(savedError);
        }
    } else if(ctx_ && ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5 && proxy) {
        usedProxy = true;
        const auto association = getSocksUdpAssociation(ctx());
        if(!association) {
            throw SocketException(_("Failed to set up the socks server for UDP relay (check socks address and port)"));
        }

        ByteVector packet;
        if(!encodeSocks5UdpPacket(aAddr, aPort, aBuffer, static_cast<size_t>(aLen),
                ctx().getSettingsManager()->getBool(SettingsManager::SOCKS_RESOLVE, true), packet)) {
            throw SocketException(_("The SOCKS5 UDP target address is invalid"));
        }

        vector<sockaddr_storage> relayEndpoints;
        if(!resolveUdpEndpoint(association->server, association->port, family, relayEndpoints)) {
            throw SocketException(EADDRNOTAVAIL);
        }

        int savedError = EADDRNOTAVAIL;
        for(const auto& endpoint : relayEndpoints) {
            const socklen_t endpointLen = endpoint.ss_family == AF_INET6 ?
                static_cast<socklen_t>(sizeof(sockaddr_in6)) : static_cast<socklen_t>(sizeof(sockaddr_in));
            do {
                sent = ::sendto(sock, reinterpret_cast<const char*>(packet.data()), packet.size(), 0,
                    reinterpret_cast<const sockaddr*>(&endpoint), endpointLen);
            } while(sent < 0 && getLastError() == EINTR);

            if(sent >= 0) {
                sentAddress = endpoint;
                hasSentAddress = true;
                break;
            }
            savedError = getLastError();
        }

        if(sent < 0)
            throw SocketException(savedError);
    } else {
        addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
        hints.ai_family = family;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
#ifdef AI_V4MAPPED
        if(family == AF_INET6) {
            hints.ai_flags |= AI_V4MAPPED;
        }
#endif

        addrinfo* result = nullptr;
        if(getaddrinfo(aAddr.c_str(), aPort.c_str(), &hints, &result) != 0 || result == nullptr) {
            throw SocketException(EADDRNOTAVAIL);
        }

        int savedError = EADDRNOTAVAIL;
        for(addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
            do {
                sent = ::sendto(sock, reinterpret_cast<const char*>(aBuffer), aLen, 0, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
            } while(sent < 0 && getLastError() == EINTR);

            if(sent >= 0) {
                memcpy(&sentAddress, ai->ai_addr, std::min(sizeof(sentAddress), static_cast<size_t>(ai->ai_addrlen)));
                hasSentAddress = true;
                break;
            }

            savedError = getLastError();
        }

        freeaddrinfo(result);
        if(sent < 0) {
            throw SocketException(savedError);
        }
    }

    check(sent);
    populateUdpSendInfo(sendInfo, aAddr, aPort, hasSentAddress ? reinterpret_cast<const sockaddr*>(&sentAddress) : nullptr, sent, usedProxy);
    stats.totalUp += sent;
}

/**
 * Blocks until timeout is reached one of the specified conditions have been fulfilled
 * @param millis Max milliseconds to block.
 * @param waitFor WAIT_*** flags that set what we're waiting for, set to the combination of flags that
 *                triggered the wait stop on return (==WAIT_NONE on timeout)
 * @return WAIT_*** ored together of the current state.
 * @throw SocketException Select or the connection attempt failed.
 */
void Socket::checkProxyCancellation() const {
    if(waitCancelled && waitCancelled())
        throw SocketException("Socket operation cancelled");
    if(globalRouteRevoked && globalRouteRevoked->load())
        throw SocketException("Global proxy route changed");
    if(streamProxyOverride && streamProxyOverride->cancelled && streamProxyOverride->cancelled())
        throw SocketException("Proxy connection cancelled");
}

int Socket::wait(uint32_t millis, int waitFor) {
    prepareWaitWake();
    const bool forever = millis == WAIT_FOREVER;
    const uint64_t deadline = GET_TICK() + millis;
    // Callers without a notification source retain bounded cancellation polling.
    const bool pollCancellation = (waitCancelled && !waitWake) ||
        (streamProxyOverride && streamProxyOverride->cancelled && !overrideNotifier);
    for(;;) {
        checkProxyCancellation();
        const uint64_t now = GET_TICK();
        uint32_t remaining = forever ? WAIT_FOREVER : static_cast<uint32_t>(now < deadline ? deadline - now : 0);
        if(pollCancellation) remaining = min<uint32_t>(remaining, 50);
        int result = nativeWait(remaining, waitFor);
        if(result & WAIT_WAKE) waitWake->consume();
        checkProxyCancellation();
        if(!(waitFor & WAIT_WAKE)) result &= ~WAIT_WAKE;
        if(result || !millis || (!forever && GET_TICK() >= deadline)) return result;
    }
}

int Socket::nativeWait(uint32_t millis, int waitFor) {
    if(sock == INVALID_SOCKET) throw SocketException("Cannot wait on a closed socket");
    // A previous read may have consumed several encrypted records from the OS
    // but returned only the first. Partial records must still wait for the wire.
    if((waitFor & WAIT_READ) && shadowsocksActive &&
       shadowsocksPlainPos >= shadowsocksPlainIn.size())
        shadowsocksTryDecode();
    if((waitFor & WAIT_READ) && ((socksTlsActive && socksTls && SSL_pending(socksTls) > 0) ||
       shadowsocksPlainPos < shadowsocksPlainIn.size())) return WAIT_READ;

    const bool connecting = waitFor & WAIT_CONNECT;
    dcassert(!connecting || !(waitFor & (WAIT_READ | WAIT_WRITE)));
    const bool forever = millis == WAIT_FOREVER;
    const uint64_t deadline = GET_TICK() + millis;
    for(;;) {
        const uint64_t now = GET_TICK();
        const int remaining = static_cast<int>(min<uint64_t>(INT_MAX, now < deadline ? deadline - now : 0));
        bool readable = false, writable = false, wakeReady = false;
#ifdef _WIN32
        fd_set read, write, errors;
        FD_ZERO(&read); FD_ZERO(&write); FD_ZERO(&errors);
        if(waitFor & WAIT_READ) FD_SET(sock, &read);
        if(connecting || (waitFor & WAIT_WRITE)) FD_SET(sock, &write);
        if(connecting) FD_SET(sock, &errors);
        if(waitWake) FD_SET(waitWake->handle(), &read);
        timeval timeout{remaining / 1000, (remaining % 1000) * 1000};
        nativeWaitCount.fetch_add(1, std::memory_order_relaxed);
        const int result = select(0, &read, &write, &errors, forever ? nullptr : &timeout);
        if(result == SOCKET_ERROR && getLastError() == WSAEINTR) {
            if(!forever && GET_TICK() >= deadline) return WAIT_NONE;
            continue;
        }
        check(result);
        readable = FD_ISSET(sock, &read);
        writable = FD_ISSET(sock, &write) || FD_ISSET(sock, &errors);
        wakeReady = waitWake && FD_ISSET(waitWake->handle(), &read);
#else
        pollfd items[2]{{sock, 0, 0}, {waitWake ? waitWake->handle() : -1, POLLIN, 0}};
        if(waitFor & WAIT_READ) items[0].events |= POLLIN;
        if(connecting || (waitFor & WAIT_WRITE)) items[0].events |= POLLOUT;
        nativeWaitCount.fetch_add(1, std::memory_order_relaxed);
        const int result = ::poll(items, waitWake ? 2 : 1, forever ? -1 : remaining);
        if(result < 0 && errno == EINTR) {
            if(!forever && GET_TICK() >= deadline) return WAIT_NONE;
            continue;
        }
        check(result);
        if(items[0].revents & POLLNVAL || items[1].revents & POLLNVAL)
            throw SocketException("Invalid socket wait descriptor");
        readable = (waitFor & WAIT_READ) && (items[0].revents & (POLLIN | POLLHUP | POLLERR));
        writable = (connecting || (waitFor & WAIT_WRITE)) && (items[0].revents & (POLLOUT | POLLHUP | POLLERR));
        wakeReady = items[1].revents & (POLLIN | POLLHUP | POLLERR);
#endif
        int ready = wakeReady ? WAIT_WAKE : WAIT_NONE;
        if(connecting && writable) {
            int error = 0;
            socklen_t length = sizeof(error);
            check(getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length));
            if(error) throw SocketException(error);
            ready |= WAIT_CONNECT;
        } else {
            if(readable) ready |= WAIT_READ;
            if(writable) ready |= WAIT_WRITE;
        }
        return ready;
    }
}

bool Socket::waitConnected(uint32_t millis) {
    return wait(millis, Socket::WAIT_CONNECT) == WAIT_CONNECT;
}

bool Socket::waitAccepted(uint32_t millis) {
    (void)millis;
    // Normal sockets are always connected after a call to accept
    return true;
}

string Socket::resolve(const string& aDns) {
#ifdef _WIN32
    sockaddr_in sock_addr;

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_port = 0;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = inet_addr(aDns.c_str());

    if (sock_addr.sin_addr.s_addr == INADDR_NONE) {   /* server address is a name or invalid */
        hostent* host;
        host = gethostbyname(aDns.c_str());
        if (host == NULL) {
            return Util::emptyString;
        }
        sock_addr.sin_addr.s_addr = *((uint32_t*)host->h_addr);
        return inet_ntoa(sock_addr.sin_addr);
    } else {
        return aDns;
    }
#else
    string address = Util::emptyString;
    addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
    addrinfo* result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = 0;
    hints.ai_protocol = 0;

    if(getaddrinfo(aDns.c_str(), nullptr, &hints, &result) == 0 && result != nullptr) {
        for(addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
            if(ai->ai_addr != nullptr) {
                address = sockaddrToIp(ai->ai_addr);
                if(!address.empty()) {
                    break;
                }
            }
        }

        freeaddrinfo(result);
    }
    return address;
#endif
}

#ifdef _WIN32
void Socket::setBlocking(bool block) {
    u_long b = block ? 0 : 1;
    ioctlsocket(sock, FIONBIO, &b);
}
#else
void Socket::setBlocking(bool block) {
    int flags = fcntl(sock, F_GETFL, 0);
    if(block) {
        fcntl(sock, F_SETFL, flags & (~O_NONBLOCK));
    } else {
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
}
#endif

string Socket::getLocalIp() {
    if(sock == INVALID_SOCKET)
        return Util::emptyString;

    sockaddr_storage sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    socklen_t len = sizeof(sock_addr);
    if(getsockname(sock, (sockaddr*)&sock_addr, &len) == 0) {
        return sockaddrToIp(reinterpret_cast<sockaddr*>(&sock_addr));
    }
    return Util::emptyString;
}

string Socket::getLocalPort() {
    if(sock == INVALID_SOCKET)
        return Util::emptyString;

    sockaddr_storage sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    socklen_t len = sizeof(sock_addr);
    if(getsockname(sock, (sockaddr*)&sock_addr, &len) == 0) {
        return sockaddrToPort(reinterpret_cast<sockaddr*>(&sock_addr));
    }
    return Util::emptyString;
}

Socket::Protocol Socket::getNextProtocol() {
    return proto;
}

Socket::SocksUdpAssociationPtr Socket::buildSocksUdpAssociation(DCContext& ctx) {
    auto* sm = ctx.getSettingsManager();
    if(sm->get(SettingsManager::OUTGOING_CONNECTIONS) != SettingsManager::OUTGOING_SOCKS5)
        return nullptr;

    try {
        auto candidate = std::make_shared<Socket>();
        candidate->setContext(&ctx);
        candidate->setBlocking(false);
        candidate->connect(sm->get(SettingsManager::SOCKS_SERVER), Util::toString(sm->get(SettingsManager::SOCKS_PORT)));
        if(candidate->wait(SOCKS_TIMEOUT, Socket::WAIT_CONNECT) != Socket::WAIT_CONNECT)
            return nullptr;
        if(sm->getBool(SettingsManager::SOCKS_TLS, true))
            candidate->socksStartTls(sm->get(SettingsManager::SOCKS_SERVER), SOCKS_TIMEOUT);
        candidate->socksAuth(SOCKS_TIMEOUT);

        const uint8_t request[10] = { 5, 3, 0, 1, 0, 0, 0, 0, 0, 0 };
        candidate->writeAll(request, sizeof(request), SOCKS_TIMEOUT);

        uint8_t header[4] = {};
        if(candidate->readAll(header, sizeof(header), SOCKS_TIMEOUT) != static_cast<int>(sizeof(header)) ||
                header[0] != 5 || header[1] != 0 || header[2] != 0)
            return nullptr;

        ByteVector address;
        address.push_back(header[3]);
        size_t addressBytes = 0;
        if(header[3] == 1) {
            addressBytes = 4 + 2;
        } else if(header[3] == 4) {
            addressBytes = 16 + 2;
        } else if(header[3] == 3) {
            uint8_t domainLength = 0;
            if(candidate->readAll(&domainLength, 1, SOCKS_TIMEOUT) != 1)
                return nullptr;
            address.push_back(domainLength);
            addressBytes = static_cast<size_t>(domainLength) + 2;
        } else {
            return nullptr;
        }

        const size_t oldSize = address.size();
        address.resize(oldSize + addressBytes);
        if(candidate->readAll(address.data() + oldSize, static_cast<int>(addressBytes), SOCKS_TIMEOUT) != static_cast<int>(addressBytes))
            return nullptr;

        string server;
        string port;
        size_t ignoredOffset = 0;
        if(!parseSocksEndpoint(address, server, port, ignoredOffset))
            return nullptr;

        if(server == "0.0.0.0" || server == "::") {
            sockaddr_storage peer = {};
            socklen_t peerLength = sizeof(peer);
            if(::getpeername(candidate->sock, reinterpret_cast<sockaddr*>(&peer), &peerLength) != 0)
                return nullptr;
            server = sockaddrToIp(reinterpret_cast<const sockaddr*>(&peer));
        }
        if(server.empty() || port.empty())
            return nullptr;

        return std::make_shared<SocksUdpAssociation>(std::move(candidate), std::move(server), std::move(port));
    } catch(const SocketException&) {
        dcdebug("Socket: Failed to register with socks server\n");
        return nullptr;
    }
}

Socket::SocksTlsControlProbeDecision Socket::classifySocksTlsControlProbe(
    int result, int sslError, int systemError) {
    if(result > 0)
        return SocksTlsControlProbeDecision::Alive;
    if(sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE)
        return SocksTlsControlProbeDecision::Alive;
    if(sslError != SSL_ERROR_SYSCALL)
        return SocksTlsControlProbeDecision::Dead;
#ifdef _WIN32
    return systemError == WSAEINTR ? SocksTlsControlProbeDecision::Retry : SocksTlsControlProbeDecision::Dead;
#else
    return systemError == EINTR ? SocksTlsControlProbeDecision::Retry : SocksTlsControlProbeDecision::Dead;
#endif
}

bool Socket::isSocksUdpControlAlive() {
    if(globalRouteRevoked && globalRouteRevoked->load())
        return false;
    if(sock == INVALID_SOCKET)
        return false;

    if(socksTlsActive && socksTls) {
        while(true) {
            uint8_t byte = 0;
            const int ret = SSL_peek(socksTls, &byte, 1);
            const int systemError = getLastError();
            const int error = SSL_get_error(socksTls, ret);
            const auto decision = classifySocksTlsControlProbe(ret, error, systemError);
            if(decision == SocksTlsControlProbeDecision::Alive)
                return true;
            if(decision == SocksTlsControlProbeDecision::Dead)
                return false;
        }
    }

    while(true) {
        uint8_t byte = 0;
        const int ret = ::recv(sock, reinterpret_cast<char*>(&byte), 1, MSG_PEEK);
        if(ret > 0)
            return true;
        if(ret == 0)
            return false;

        const int error = getLastError();
#ifdef _WIN32
        if(error == WSAEINTR)
            continue;
        return error == WSAEWOULDBLOCK;
#else
        if(error == EINTR)
            continue;
        return error == EWOULDBLOCK || error == EAGAIN;
#endif
    }
}

Socket::SocksUdpAssociationPtr Socket::getActiveSocksUdpAssociation() {
    SocksUdpAssociationPtr staleAssociation;
    {
        std::lock_guard<std::mutex> lock(udpProxyMutex);
        if(udpAssociation && udpAssociation->control->isSocksUdpControlAlive()) {
            return udpAssociation;
        }

        staleAssociation = std::move(udpAssociation);
    }
    return nullptr;
}

void Socket::publishSocksUdpAssociation(SocksUdpAssociationPtr association) {
    SocksUdpAssociationPtr previous;
    {
        std::lock_guard<std::mutex> lock(udpProxyMutex);
        previous.swap(udpAssociation);
        udpAssociation = std::move(association);
    }
}

void Socket::socksUpdated(DCContext& ctx) {
    std::lock_guard<std::mutex> setupLock(udpProxySetupMutex);
    publishSocksUdpAssociation(buildSocksUdpAssociation(ctx));
}

Socket::SocksUdpAssociationPtr Socket::getSocksUdpAssociation(DCContext& ctx) {
    auto association = getActiveSocksUdpAssociation();
    if(association)
        return association;

    std::lock_guard<std::mutex> setupLock(udpProxySetupMutex);
    association = getActiveSocksUdpAssociation();
    if(association)
        return association;

    association = buildSocksUdpAssociation(ctx);
    if(association && !association->control->isSocksUdpControlAlive())
        return nullptr;
    publishSocksUdpAssociation(association);
    return association;
}

bool Socket::getSocksUdpRelay(DCContext& ctx, string& server, string& port) {
    const auto association = getSocksUdpAssociation(ctx);
    if(!association)
        return false;

    server = association->server;
    port = association->port;
    return true;
}

bool Socket::getUdpProxyEndpoint(DCContext& ctx, string& server, string& port) {
    server.clear();
    port.clear();

    if(ctx.getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) != SettingsManager::OUTGOING_SOCKS5)
        return false;

    return getSocksUdpRelay(ctx, server, port);
}

void Socket::shutdown() {
    udpClosing = true;
    if(socksTlsActive && socksTls) {
        SSL_shutdown(socksTls);
    }
    if(sock != INVALID_SOCKET)
        ::shutdown(sock, 2);
}

void Socket::close() {
    udpClosing = true;
    {
        std::lock_guard<std::mutex> lock(gostUdpMutex);
        gostUdp.reset();
    }
    gostUdpOriginalBind.reset();
    shadowsocksReset();
    socksTlsReset();
    streamProxyOverride.reset();
    globalRouteRevoked.reset();
    globalRouteSubscription.reset();
    globalRouteNotifier.reset();
    overrideSubscription.reset();
    overrideNotifier.reset();
    if(sock != INVALID_SOCKET) {
#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        connected = false;
        sock = INVALID_SOCKET;
    }
}

void Socket::disconnect() {
    shutdown();
    close();
}

} // namespace dcpp
