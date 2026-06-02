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

#include "format.h"
#include "SettingsManager.h"
#include "TimerManager.h"

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
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/rand.h>

#include <cctype>

namespace dcpp {

string Socket::udpServer;
string Socket::udpPort;

#define checkconnected() if(!isConnected()) throw SocketException(ENOTCONN))

namespace {
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

    const string bindIp = aIp.empty() ? (family == AF_INET6 ? "::" : "0.0.0.0") : aIp;
    const string bindPort = aPort.empty() ? "0" : aPort;

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

    if(!bound) {
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
    if(start + timeout < now)
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
    return Socket::SHADOWSOCKS_NONE;
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
        HMAC_CTX* ctx = HMAC_CTX_new();
        if(!ctx)
            throw SocketException(_("Failed to initialize Shadowsocks subkey derivation"));

        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen = 0;
        HMAC_Init_ex(ctx, prk, prkLen, EVP_sha1(), nullptr);
        if(!previous.empty())
            HMAC_Update(ctx, previous.data(), previous.size());
        HMAC_Update(ctx, reinterpret_cast<const unsigned char*>(info.data()), info.size());
        HMAC_Update(ctx, &counter, 1);
        HMAC_Final(ctx, digest, &digestLen);
        HMAC_CTX_free(ctx);

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

bool parseSocksAddress(const ByteVector& in, sockaddr_storage& remote, size_t& payloadOffset) {
    if(in.empty())
        return false;

    size_t pos = 0;
    const uint8_t atyp = in[pos++];

    if(atyp == 1) {
        if(in.size() < pos + 4 + 2)
            return false;

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        memcpy(&addr.sin_addr, in.data() + pos, 4);
        pos += 4;
        memcpy(&addr.sin_port, in.data() + pos, 2);
        pos += 2;

        memset(&remote, 0, sizeof(remote));
        memcpy(&remote, &addr, sizeof(addr));
    } else if(atyp == 4) {
        if(in.size() < pos + 16 + 2)
            return false;

        sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        memcpy(&addr.sin6_addr, in.data() + pos, 16);
        pos += 16;
        memcpy(&addr.sin6_port, in.data() + pos, 2);
        pos += 2;

        memset(&remote, 0, sizeof(remote));
        memcpy(&remote, &addr, sizeof(addr));
    } else if(atyp == 3) {
        if(in.size() < pos + 1)
            return false;

        const size_t hostLen = in[pos++];
        if(in.size() < pos + hostLen + 2)
            return false;

        const string host(reinterpret_cast<const char*>(in.data() + pos), hostLen);
        pos += hostLen;

        uint16_t nport = 0;
        memcpy(&nport, in.data() + pos, 2);
        pos += 2;

        if(!resolveSockaddr(host, Util::toString(ntohs(nport)), remote))
            return false;
    } else {
        return false;
    }

    payloadOffset = pos;
    return true;
}
}

void Socket::socksConnect(const string& aAddr, const string& aPort, uint32_t timeout) {
    auto* sm = ctx().getSettingsManager();

    if(sm->get(SettingsManager::SOCKS_SERVER).empty() || sm->get(SettingsManager::SOCKS_PORT) == 0) {
        throw SocketException(_("The socks server failed establish a connection"));
    }

    uint64_t start = GET_TICK();

    Socket::connect(sm->get(SettingsManager::SOCKS_SERVER), Util::toString(sm->get(SettingsManager::SOCKS_PORT)));

    if(Socket::wait(timeLeft(start, timeout), WAIT_CONNECT) != WAIT_CONNECT) {
        throw SocketException(_("The socks server failed establish a connection"));
    }

    socksAuth(timeLeft(start, timeout));

    ByteVector connStr;

    // Authenticated, let's get on with it...
    connStr.push_back(5);           // SOCKSv5
    connStr.push_back(1);           // Connect
    connStr.push_back(0);           // Reserved

    if(sm->getBool(SettingsManager::SOCKS_RESOLVE, true)) {
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

    if(replyHeader[0] != 5 || replyHeader[1] != 0) {
        throw SocketException(_("The socks server failed establish a connection"));
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
    auto* sm = ctx().getSettingsManager();
    switch(sm->get(SettingsManager::OUTGOING_CONNECTIONS)) {
    case SettingsManager::OUTGOING_SOCKS5:
        socksConnect(aAddr, aPort, timeout);
        break;
    case SettingsManager::OUTGOING_SHADOWSOCKS:
        shadowsocksConnect(aAddr, aPort, timeout);
        break;
    default:
        connect(aAddr, aPort);
        break;
    }
}

void Socket::shadowsocksConnect(const string& aAddr, const string& aPort, uint32_t timeout) {
    auto* sm = ctx().getSettingsManager();

    if(sm->get(SettingsManager::SHADOWSOCKS_SERVER).empty() || sm->get(SettingsManager::SHADOWSOCKS_PORT) == 0) {
        throw SocketException(_("The Shadowsocks server failed to establish a connection"));
    }
    if(sm->get(SettingsManager::SHADOWSOCKS_PASSWORD).empty()) {
        throw SocketException(_("No Shadowsocks password configured"));
    }

    uint64_t start = GET_TICK();
    Socket::connect(sm->get(SettingsManager::SHADOWSOCKS_SERVER), Util::toString(sm->get(SettingsManager::SHADOWSOCKS_PORT)));

    if(Socket::wait(timeLeft(start, timeout), WAIT_CONNECT) != WAIT_CONNECT) {
        throw SocketException(_("The Shadowsocks server failed to establish a connection"));
    }

    shadowsocksStart(sm->get(SettingsManager::SHADOWSOCKS_METHOD),
        sm->get(SettingsManager::SHADOWSOCKS_PASSWORD), timeLeft(start, timeout));

    ByteVector target;
    if(!appendSocksAddress(target, aAddr, aPort, sm->getBool(SettingsManager::SOCKS_RESOLVE, true))) {
        throw SocketException(_("The Shadowsocks target address is invalid"));
    }

    streamWriteAll(target.data(), target.size(), timeLeft(start, timeout));
    setIp(aAddr);
}

void Socket::socksAuth(uint32_t timeout) {
    vector<uint8_t> connStr;
    auto* sm = ctx().getSettingsManager();

    uint64_t start = GET_TICK();

    if(sm->get(SettingsManager::SOCKS_USER).empty() && sm->get(SettingsManager::SOCKS_PASSWORD).empty()) {
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
        connStr.push_back((uint8_t)sm->get(SettingsManager::SOCKS_USER).length());
        connStr.insert(connStr.end(), sm->get(SettingsManager::SOCKS_USER).begin(), sm->get(SettingsManager::SOCKS_USER).end());
        connStr.push_back((uint8_t)sm->get(SettingsManager::SOCKS_PASSWORD).length());
        connStr.insert(connStr.end(), sm->get(SettingsManager::SOCKS_PASSWORD).begin(), sm->get(SettingsManager::SOCKS_PASSWORD).end());

        streamWriteAll(connStr.data(), connStr.size(), timeLeft(start, timeout));

        if(streamReadAll(connStr.data(), 2, timeLeft(start, timeout)) != 2) {
            throw SocketException(_("Socks server authentication failed (bad login / password?)"));
        }

        if(connStr[1] != 0) {
            throw SocketException(_("Socks server authentication failed (bad login / password?)"));
        }
    }
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

    sockaddr_storage remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    socklen_t addr_length = sizeof(remote_addr);

    int len;
    do {
        len = ::recvfrom(sock, (char*)aBuffer, aBufLen, 0, (sockaddr*)&remote_addr, &addr_length);
    } while (len < 0 && getLastError() == EINTR);

    check(len, true);
    if(len > 0) {
        stats.totalDown += len;
    }

    if(ctx_ && ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SHADOWSOCKS && len > 0) {
        auto* sm = ctx().getSettingsManager();
        const int method = shadowsocksMethodId(sm->get(SettingsManager::SHADOWSOCKS_METHOD));
        const size_t keyLen = shadowsocksKeyLen(method);
        const auto* buf = static_cast<const uint8_t*>(aBuffer);
        if(keyLen == 0 || static_cast<size_t>(len) <= keyLen + SHADOWSOCKS_TAG_LEN || sm->get(SettingsManager::SHADOWSOCKS_PASSWORD).empty()) {
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

    remote = remote_addr;
    return len;
}

int Socket::readAll(void* aBuffer, int aBufLen, uint32_t timeout) {
    uint8_t* buf = (uint8_t*)aBuffer;
    int i = 0;
    while(i < aBufLen) {
        int j = read(buf + i, aBufLen - i);
        if(j == 0) {
            return i;
        } else if(j == -1) {
            if(wait(timeout, WAIT_READ) != WAIT_READ) {
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
    while(i < aBufLen) {
        int j = Socket::read(buf + i, aBufLen - i);
        if(j == 0) {
            return i;
        } else if(j == -1) {
            if(Socket::wait(timeout, WAIT_READ) != WAIT_READ) {
                return i;
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
            if(wait(timeout, WAIT_WRITE) != WAIT_WRITE)
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

    while(pos < aLen) {
        int i = Socket::write(buf + pos, static_cast<int>(min(aLen - pos, sendSize)));
        if(i == -1) {
            if(Socket::wait(timeout, WAIT_WRITE) != WAIT_WRITE)
                throw SocketException(_("Connection timeout"));
        } else {
            pos += i;
        }
    }

    while(shadowsocksActive && type == TYPE_TCP && !shadowsocksPendingOut.empty()) {
        if(!shadowsocksFlushPending() && Socket::wait(timeout, WAIT_WRITE) != WAIT_WRITE)
            throw SocketException(_("Connection timeout"));
    }
}

int Socket::write(const void* aBuffer, int aLen) {
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
    shadowsocksSubkey.clear();
    shadowsocksEncNonce.clear();
    shadowsocksDecNonce.clear();
    shadowsocksPlainIn.clear();
    shadowsocksPlainPos = 0;
    shadowsocksCipherIn.clear();
    shadowsocksPendingOut.clear();
    shadowsocksPendingOutPos = 0;
    shadowsocksExpectedPayload = 0;
    shadowsocksReadingPayload = false;
}

void Socket::shadowsocksStart(const string& method, const string& password, uint32_t timeout) {
    shadowsocksReset();

    shadowsocksMethod = shadowsocksMethodId(method);
    const size_t keyLen = shadowsocksKeyLen(shadowsocksMethod);
    if(keyLen == 0) {
        throw SocketException(_("Unsupported Shadowsocks cipher"));
    }

    ByteVector salt(keyLen);
    if(RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw SocketException(_("Failed to create Shadowsocks salt"));
    }

    const ByteVector masterKey = evpBytesToKey(password, keyLen);
    shadowsocksSubkey = hkdfSha1(masterKey, salt, "ss-subkey", keyLen);
    shadowsocksEncNonce.assign(SHADOWSOCKS_NONCE_LEN, 0);
    shadowsocksDecNonce.assign(SHADOWSOCKS_NONCE_LEN, 0);
    shadowsocksActive = true;

    shadowsocksWriteAll(salt.data(), salt.size(), timeout);
}

void Socket::shadowsocksWriteAll(const void* aBuffer, int aLen, uint32_t timeout) {
    const uint8_t* buf = static_cast<const uint8_t*>(aBuffer);
    int pos = 0;

    while(pos < aLen) {
        int sent = rawWrite(buf + pos, aLen - pos);
        if(sent == -1) {
            if(wait(timeout, WAIT_WRITE) != WAIT_WRITE) {
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

int Socket::shadowsocksWrite(const void* aBuffer, int aLen) {
    if(aLen <= 0)
        return 0;

    if(!shadowsocksFlushPending())
        return -1;

    const size_t plainLen = min(static_cast<size_t>(aLen), SHADOWSOCKS_MAX_CHUNK);
    const uint8_t* buf = static_cast<const uint8_t*>(aBuffer);

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

bool Socket::shadowsocksTryDecode() {
    while(true) {
        if(!shadowsocksReadingPayload) {
            const size_t required = 2 + SHADOWSOCKS_TAG_LEN;
            if(shadowsocksCipherIn.size() < required)
                return false;

            ByteVector plainLen;
            if(!shadowsocksAeadDecrypt(shadowsocksMethod, shadowsocksSubkey, shadowsocksDecNonce,
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
        if(!shadowsocksAeadDecrypt(shadowsocksMethod, shadowsocksSubkey, shadowsocksDecNonce,
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

    uint8_t buf[8192];
    while(true) {
        int len = rawRead(buf, sizeof(buf));
        if(len == 0)
            return 0;
        if(len == -1)
            return -1;

        shadowsocksCipherIn.insert(shadowsocksCipherIn.end(), buf, buf + len);
        if(shadowsocksTryDecode()) {
            return copyPlain();
        }
    }
}

/**
* Sends data, will block until all data has been sent or an exception occurs
* @param aBuffer Buffer with data
* @param aLen Data length
* @throw SocketExcpetion Send failed.
*/
void Socket::writeTo(const string& aAddr, const string& aPort, const void* aBuffer, int aLen, bool proxy) {
    if(aLen <= 0)
        return;

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
    if(ctx_ && ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SHADOWSOCKS && proxy) {
        auto* sm = ctx().getSettingsManager();
        const int method = shadowsocksMethodId(sm->get(SettingsManager::SHADOWSOCKS_METHOD));
        const size_t keyLen = shadowsocksKeyLen(method);
        if(keyLen == 0 || sm->get(SettingsManager::SHADOWSOCKS_SERVER).empty() || sm->get(SettingsManager::SHADOWSOCKS_PASSWORD).empty()) {
            throw SocketException(_("Shadowsocks UDP relay is not configured"));
        }

        ByteVector plain;
        if(!appendSocksAddress(plain, aAddr, aPort, sm->getBool(SettingsManager::SOCKS_RESOLVE, true))) {
            throw SocketException(_("The Shadowsocks target address is invalid"));
        }
        plain.insert(plain.end(), buf, buf + aLen);

        ByteVector salt(keyLen);
        if(RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
            throw SocketException(_("Failed to create Shadowsocks salt"));
        }

        const ByteVector masterKey = evpBytesToKey(sm->get(SettingsManager::SHADOWSOCKS_PASSWORD), keyLen);
        const ByteVector subkey = hkdfSha1(masterKey, salt, "ss-subkey", keyLen);
        ByteVector nonce(SHADOWSOCKS_NONCE_LEN, 0);
        ByteVector packet = salt;
        ByteVector encrypted = shadowsocksAeadEncrypt(method, subkey, nonce, plain.data(), plain.size());
        packet.insert(packet.end(), encrypted.begin(), encrypted.end());

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
                break;
            }
            savedError = getLastError();
        }

        freeaddrinfo(result);
        if(sent < 0) {
            throw SocketException(savedError);
        }
    } else if(ctx_ && ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5 && proxy) {
        sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));

        if(udpServer.empty() || udpPort.empty()) {
            throw SocketException(_("Failed to set up the socks server for UDP relay (check socks address and port)"));
        }

        serv_addr.sin_port = htons(static_cast<uint16_t>(Util::toInt(udpPort)));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(udpServer.c_str());

        string s = ctx().getSettingsManager()->getBool(SettingsManager::SOCKS_RESOLVE, true) ? resolve(ip) : ip;

        vector<uint8_t> connStr;

        connStr.push_back(0);       // Reserved
        connStr.push_back(0);       // Reserved
        connStr.push_back(0);       // Fragment number, always 0 in our case...

        if(ctx().getSettingsManager()->getBool(SettingsManager::SOCKS_RESOLVE, true)) {
            connStr.push_back(3);
            connStr.push_back((uint8_t)s.size());
            connStr.insert(connStr.end(), aAddr.begin(), aAddr.end());
        } else {
            connStr.push_back(1);       // Address type: IPv4;
            unsigned long addr = inet_addr(resolve(aAddr).c_str());
            uint8_t* paddr = (uint8_t*)&addr;
            connStr.insert(connStr.end(), paddr, paddr+4);
        }

        connStr.insert(connStr.end(), buf, buf + aLen);

        do {
            sent = ::sendto(sock, (const char*)&connStr[0], connStr.size(), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        } while (sent < 0 && getLastError() == EINTR);
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
int Socket::wait(uint32_t millis, int waitFor) {
    timeval tv;
    fd_set rfd, wfd, efd;
    fd_set *rfdp = NULL, *wfdp = NULL;
    tv.tv_sec = millis/1000;
    tv.tv_usec = (millis%1000)*1000;

    if(waitFor & WAIT_CONNECT) {
        dcassert(!(waitFor & WAIT_READ) && !(waitFor & WAIT_WRITE));

        int result;
        do {
            FD_ZERO(&wfd);
            FD_ZERO(&efd);

            FD_SET(sock, &wfd);
            FD_SET(sock, &efd);
            result = select((int)(sock+1), 0, &wfd, &efd, &tv);
        } while (result < 0 && getLastError() == EINTR);
        check(result);

        // fix buffer overflow during shutdown
        if(sock == INVALID_SOCKET)
            return WAIT_NONE;

        if(FD_ISSET(sock, &wfd)) {
            return WAIT_CONNECT;
        }

        if(FD_ISSET(sock, &efd)) {
            int y = 0;
            socklen_t z = sizeof(y);
            check(getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&y, &z));

            if(y != 0)
                throw SocketException(y);
            // No errors! We're connected (?)...
            return WAIT_CONNECT;
        }
        return WAIT_NONE;
    }

    int result;
    do {
        if(waitFor & WAIT_READ) {
            dcassert(!(waitFor & WAIT_CONNECT));
            rfdp = &rfd;
            FD_ZERO(rfdp);
            FD_SET(sock, rfdp);
        }
        if(waitFor & WAIT_WRITE) {
            dcassert(!(waitFor & WAIT_CONNECT));
            wfdp = &wfd;
            FD_ZERO(wfdp);
            FD_SET(sock, wfdp);
        }

        result = select((int)(sock+1), rfdp, wfdp, NULL, &tv);
    } while (result < 0 && getLastError() == EINTR);
    check(result);

    waitFor = WAIT_NONE;

    // fix buffer overflow during shutdown
    if(sock == INVALID_SOCKET)
        return WAIT_NONE;

    if(rfdp && FD_ISSET(sock, rfdp)) {
        waitFor |= WAIT_READ;
    }
    if(wfdp && FD_ISSET(sock, wfdp)) {
        waitFor |= WAIT_WRITE;
    }

    return waitFor;
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

void Socket::socksUpdated(DCContext& ctx) {
    udpServer.clear();
    udpPort.clear();

    auto* sm = ctx.getSettingsManager();
    if(sm->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5) {
        try {
            Socket s;
            s.setContext(&ctx);
            s.setBlocking(false);
            s.connect(sm->get(SettingsManager::SOCKS_SERVER), Util::toString(sm->get(SettingsManager::SOCKS_PORT)));
            s.socksAuth(SOCKS_TIMEOUT);

            char connStr[10];
            connStr[0] = 5;         // SOCKSv5
            connStr[1] = 3;         // UDP Associate
            connStr[2] = 0;         // Reserved
            connStr[3] = 1;         // Address type: IPv4;
            *((uint32_t*)(&connStr[4])) = 0;    // No specific outgoing UDP address
            *((uint16_t*)(&connStr[8])) = 0;    // No specific port...

            s.writeAll(connStr, 10, SOCKS_TIMEOUT);

            // We assume we'll get a ipv4 address back...therefore, 10 bytes...if not, things
            // will break, but hey...noone's perfect (and I'm tired...)...
            if(s.readAll(connStr, 10, SOCKS_TIMEOUT) != 10) {
                return;
            }

            if(connStr[0] != 5 || connStr[1] != 0) {
                return;
            }

            udpPort = Util::toString(ntohs(*((uint16_t*)(&connStr[8]))));

            in_addr serv_addr;

            memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.s_addr = *((long*)(&connStr[4]));
            udpServer = inet_ntoa(serv_addr);
        } catch(const SocketException&) {
            dcdebug("Socket: Failed to register with socks server\n");
        }
    }
}

void Socket::shutdown() {
    if(sock != INVALID_SOCKET)
        ::shutdown(sock, 2);
}

void Socket::close() {
    shadowsocksReset();
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
