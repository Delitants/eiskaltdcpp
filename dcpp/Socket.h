/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#pragma once

#ifdef _WIN32
#include "w.h"
typedef int socklen_t;
typedef SOCKET socket_t;
#else
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

typedef int socket_t;
const int INVALID_SOCKET = -1;
#define SOCKET_ERROR -1
#endif

#include "GetSet.h"
#include "Util.h"
#include "Exception.h"
#include "SSL.h"
#include "Shadowsocks2022.h"

#include <memory>
#include <functional>
#include <optional>
#include <mutex>
#include <utility>
#include <atomic>

namespace dcpp {

class DCContext;
class SocketWake;
class WakeNotifier;
struct ProxyRouteSnapshot;

class SocketException : public Exception {
public:
    enum class ProxyStage { None, Negotiation, Certificate, Authentication, Tunnel };
#ifdef _DEBUG
    SocketException(const string& aError) : Exception("SocketException: " + aError) { }
#else //_DEBUG
    SocketException(const string& aError) : Exception(aError) { }
#endif // _DEBUG

    SocketException(int aError);
    SocketException(const string& aError, uint8_t aSocksReplyCode) : SocketException(aError) {
        socksReplyCode = aSocksReplyCode;
    }
    std::optional<uint8_t> getSocksReplyCode() const noexcept { return socksReplyCode; }
    ProxyStage getProxyStage() const noexcept { return proxyStage; }
    void setProxyStage(ProxyStage stage) noexcept { proxyStage = stage; }
    virtual ~SocketException() throw() { }

private:
    std::optional<uint8_t> socksReplyCode;
    ProxyStage proxyStage = ProxyStage::None;
    static string errorToString(int aError);
};

class Socket
{
public:
    enum {
        WAIT_NONE = 0x00,
        WAIT_CONNECT = 0x01,
        WAIT_READ = 0x02,
        WAIT_WRITE = 0x04,
        WAIT_WAKE = 0x08
    };
    static constexpr uint32_t WAIT_FOREVER = UINT32_MAX;

    enum {
        TYPE_TCP,
        TYPE_UDP
    };

    enum Protocol {
        PROTO_DEFAULT = 0,
        PROTO_NMDC = 1,
        PROTO_ADC = 2
    };

    struct UdpSendInfo {
        string logicalIp;
        string logicalPort;
        string physicalIp;
        string physicalPort;
        bool proxied = false;
        size_t bytesSent = 0;
    };

    enum {
        SHADOWSOCKS_NONE = 0,
        SHADOWSOCKS_AES_128_GCM,
        SHADOWSOCKS_AES_256_GCM,
        SHADOWSOCKS_CHACHA20_IETF_POLY1305,
        SHADOWSOCKS_2022_BLAKE3_AES_128_GCM,
        SHADOWSOCKS_2022_BLAKE3_AES_256_GCM,
        SHADOWSOCKS_2022_BLAKE3_CHACHA20_POLY1305
    };

    Socket() : sock(INVALID_SOCKET), type(TYPE_TCP), connected(false), proto(PROTO_DEFAULT), family(AF_INET), ctx_(nullptr) { }
    Socket(const string& aIp, const string& aPort) : sock(INVALID_SOCKET), type(TYPE_TCP), connected(false), proto(PROTO_DEFAULT), family(AF_INET), ctx_(nullptr) { connect(aIp, aPort); }
    virtual ~Socket() { disconnect(); }

    void setContext(DCContext* ctx);
    // Configure before handing the socket to its I/O owner.
    void setWaitWake(std::shared_ptr<SocketWake> wake, std::function<bool()> cancelled = {});
    uint64_t getNativeWaitCount() const { return nativeWaitCount.load(std::memory_order_relaxed); }
    // Only the explicitly configured public HTTP proxy uses this exception.
    void setGlobalProxyExempt(bool value) { globalProxyExempt = value; }
    DCContext& ctx() const { return *ctx_; }

    /**
     * Connects a socket to an address/ip, closing any other connections made with
     * this instance.
     * @param aAddr Server address, in dns or xxx.xxx.xxx.xxx format.
     * @param aPort Server port.
     * @throw SocketException If any connection error occurs.
     */
    virtual void connect(const string& aIp, const string &aPort, const string &localPort = Util::emptyString);
    /**
     * Same as connect(), but through the SOCKS5 server
     */
    void socksConnect(const string& aIp, const string &aPort, uint32_t timeout = 0);
    /**
     * Connects through the configured outbound proxy type.
     */
    void proxyConnect(const string& aIp, const string &aPort, uint32_t timeout = 0);
    struct StreamProxyConfig {
        enum Type { None, Socks5, Shadowsocks, Gost } type = None;
        string host, user, password, cipher;
        // Optional numeric dial address; host remains the TLS peer identity.
        string connectHost;
        std::function<bool()> cancelled;
        // Set only when every cause in cancelled signals this one-shot notifier.
        // Reset when composing it with a callback-only cancellation source.
        std::shared_ptr<WakeNotifier> cancellationNotifier;
        int port = 0;
        bool tls = false;
        bool remoteDns = true;
        bool verifyTls = true;
        // Immutable profile-specific PEM roots; empty selects system roots.
        std::string caPem;
    };
    // A per-connection snapshot avoids changing shared application settings.
    void proxyConnect(const string& aIp, const string& aPort,
                      const StreamProxyConfig& config, uint32_t timeout);
    // Opens authenticated UDP-TUN (0xF3); use GostProtocol with stream read/write.
    void gostOpenUdpTunnel(const StreamProxyConfig& config, uint32_t timeout);
    /**
     * Same as connect(), but through a Shadowsocks AEAD server.
     */
    void shadowsocksConnect(const string& aIp, const string &aPort, uint32_t timeout = 0);

    /**
     * Sends data, will block until all data has been sent or an exception occurs
     * @param aBuffer Buffer with data
     * @param aLen Data length
     * @throw SocketExcpetion Send failed.
     */
    void writeAll(const void* aBuffer, int aLen, uint32_t timeout = 0);
    virtual int write(const void* aBuffer, int aLen);
    int write(const string& aData) { return write(aData.data(), (int)aData.length()); }
    virtual void writeTo(const string& aIp, const std::string &aPort, const void* aBuffer, int aLen,
        bool proxy = true, UdpSendInfo* sendInfo = nullptr);
    void writeTo(const string& aIp, const string& aPort, const string& aData) { writeTo(aIp, aPort, aData.data(), (int)aData.length()); }
    virtual void shutdown();
    virtual void close();
    void disconnect();

    virtual bool waitConnected(uint32_t millis);
    virtual bool waitAccepted(uint32_t millis);

    /**
     * Reads zero to aBufLen characters from this socket,
     * @param aBuffer A buffer to store the data in.
     * @param aBufLen Size of the buffer.
     * @return Number of bytes read, 0 if disconnected and -1 if the call would block.
     * @throw SocketException On any failure.
     */
    virtual int read(void* aBuffer, int aBufLen);
    /**
     * Reads zero to aBufLen characters from this socket,
     * @param aBuffer A buffer to store the data in.
     * @param aBufLen Size of the buffer.
     * @param aIP Remote IP address
     * @return Number of bytes read, 0 if disconnected and -1 if the call would block.
     * @throw SocketException On any failure.
     */
    virtual int read(void* aBuffer, int aBufLen, sockaddr_storage& remote);
    /**
     * Reads data until aBufLen bytes have been read or an error occurs.
     * If the socket is closed, or the timeout is reached, the number of bytes read
     * actually read is returned.
     * On exception, an unspecified amount of bytes might have already been read.
     */
    int readAll(void* aBuffer, int aBufLen, uint32_t timeout = 0);

    virtual int wait(uint32_t millis, int waitFor);
    bool isConnected() { return connected; }

    static string resolve(const string& aDns);
    static uint64_t getTotalDown() { return stats.totalDown; }
    static uint64_t getTotalUp() { return stats.totalUp; }

    static bool encodeSocks5UdpPacket(const string& address, const string& port,
                                      const void* payload, size_t payloadLen,
                                      bool remoteResolve, ByteVector& packet);
    static bool decodeSocks5UdpPacket(const uint8_t* packet, size_t packetLen,
                                      sockaddr_storage& remote, size_t& payloadOffset);
    static bool resolveUdpEndpoint(const string& host, const string& port, int requestedFamily,
                                   vector<sockaddr_storage>& endpoints);
    static int udpResolverFlags(int requestedFamily);
    static bool matchesUdpEndpoint(const string& host, const string& port, const sockaddr_storage& endpoint);

    enum class SocksTlsControlProbeDecision { Alive, Retry, Dead };
    static SocksTlsControlProbeDecision classifySocksTlsControlProbe(int result, int sslError, int systemError);

    void setBlocking(bool block);

    string getLocalIp();
    string getLocalPort();

    Protocol getNextProtocol();

    // Low level interface
    virtual void create(int aType = TYPE_TCP, int aFamily = AF_INET);

    /** Binds a socket to a certain local port and possibly IP. */
    virtual const string bind(const string &aPort = Util::emptyString, const string& aIp = "0.0.0.0");
    virtual void listen();
    virtual void accept(const Socket& listeningSocket);

    int getSocketOptInt(int option);
    void setSocketOpt(int option, int value);

    virtual bool isSecure() const { return false; }
    virtual bool isTrusted() const { return false; }
    virtual string getCipherName() const { return Util::emptyString; }
    virtual ByteVector getKeyprint() const { return ByteVector(); }
    bool hasStreamProxy() const { return shadowsocksActive || socksTlsActive; }
    bool hasGostUdpTransport() const;
    bool hasPendingProxyOutput() const;
    bool flushProxyOutput();
    // Nonblocking, including a TLS close-notify retry. Reads remain usable.
    bool shutdownWrite();

    /** When socks settings are updated, this has to be called... */
    static void socksUpdated(DCContext& ctx);
    /** Returns the public UDP relay endpoint when the active proxy provides one. */
    static bool getUdpProxyEndpoint(DCContext& ctx, string& server, string& port);
    string getIfaceI4 (const string &iface);
    string getIfaceI6 (const string &iface);
    int getFamily() const { return family; }

    GETSET(string, ip, Ip);

    socket_t sock;

protected:
    void checkProxyCancellation() const;
    int type;
    bool connected;
    Protocol proto;
    int family;

    class Stats {
    public:
        uint64_t totalDown;
        uint64_t totalUp;
    };
    static Stats stats;

    struct SocksUdpAssociation {
        SocksUdpAssociation(std::shared_ptr<Socket> aControl, string aServer, string aPort) :
            control(std::move(aControl)), server(std::move(aServer)), port(std::move(aPort)) { }

        std::shared_ptr<Socket> control;
        const string server;
        const string port;
    };
    using SocksUdpAssociationPtr = std::shared_ptr<const SocksUdpAssociation>;

    static SocksUdpAssociationPtr udpAssociation;
    static std::mutex udpProxyMutex;
    static std::mutex udpProxySetupMutex;

private:
    Socket(const Socket&);
    Socket& operator=(const Socket&);

    DCContext* ctx_;
    bool globalProxyExempt = false;
    std::shared_ptr<std::atomic_bool> globalRouteRevoked;
    std::shared_ptr<SocketWake> waitWake;
    std::atomic<uint64_t> nativeWaitCount{0};
    std::function<bool()> waitCancelled;
    std::shared_ptr<WakeNotifier> globalRouteNotifier, overrideNotifier;
    std::shared_ptr<void> globalRouteSubscription, overrideSubscription;
    void bindGlobalRoute(const std::shared_ptr<const ProxyRouteSnapshot>& route);
    void prepareWaitWake();
    struct GostUdpState;
    std::shared_ptr<GostUdpState> gostUdp;
    std::optional<std::pair<string, string>> gostUdpOriginalBind;
    mutable std::mutex gostUdpMutex;
    std::atomic_bool udpClosing{false};
    void writeGostUdp(const string& address, const string& port, const void* buffer, int length, UdpSendInfo* info);
    int readGostUdp(void* buffer, int length, sockaddr_storage& remote);

    void socksAuth(uint32_t timeout);
    void gostHandshake(const StreamProxyConfig& config, uint32_t timeout);
    void gostCommand(const ByteVector& request, uint32_t timeout);
    void socksTlsReset();
    void socksStartTls(const string& serverName, uint32_t timeout);
    int socksTlsRead(void* aBuffer, int aBufLen);
    int socksTlsWrite(const void* aBuffer, int aLen);
    int tlsWaitTarget(int fallback) const;

    void shadowsocksReset();
    bool shadowsocksStart(const string& method, const string& password,
        const ByteVector& target, uint32_t timeout);
    int shadowsocksRead(void* aBuffer, int aBufLen);
    int shadowsocksWrite(const void* aBuffer, int aLen);
    void shadowsocksWriteAll(const void* aBuffer, int aLen, uint32_t timeout);
    bool shadowsocksEnsureReceiveSubkey();
    bool shadowsocksTryDecode();
    bool shadowsocksTryDecode2022();
    bool shadowsocksFlushPending();
    Shadowsocks2022UdpSession& shadowsocks2022UdpSessionForSettings();
    int streamReadAll(void* aBuffer, int aBufLen, uint32_t timeout);
    void streamWriteAll(const void* aBuffer, int aLen, uint32_t timeout);
    int rawRead(void* aBuffer, int aBufLen);
    int rawWrite(const void* aBuffer, int aLen);
    bool isSocksUdpControlAlive();
    static SocksUdpAssociationPtr buildSocksUdpAssociation(DCContext& ctx);
    static SocksUdpAssociationPtr getActiveSocksUdpAssociation();
    static void publishSocksUdpAssociation(SocksUdpAssociationPtr association);
    static SocksUdpAssociationPtr getSocksUdpAssociation(DCContext& ctx);
    static bool getSocksUdpRelay(DCContext& ctx, string& server, string& port);

    bool shadowsocksActive = false;
    std::optional<StreamProxyConfig> streamProxyOverride;
    StreamProxyConfig streamProxyConfig(StreamProxyConfig::Type proxyType) const;
    int nativeWait(uint32_t millis, int waitFor);
    int shadowsocksMethod = SHADOWSOCKS_NONE;
    ByteVector shadowsocksMasterKey;
    ByteVector shadowsocksSubkey;
    ByteVector shadowsocksDecSubkey;
    ByteVector shadowsocksEncNonce;
    ByteVector shadowsocksDecNonce;
    ByteVector shadowsocksPlainIn;
    size_t shadowsocksPlainPos = 0;
    ByteVector shadowsocksCipherIn;
    ByteVector shadowsocksPendingOut;
    size_t shadowsocksPendingOutPos = 0;
    size_t shadowsocksExpectedPayload = 0;
    bool shadowsocksReadingPayload = false;
    std::unique_ptr<Shadowsocks2022TcpClient> shadowsocks2022Stream;
    bool shadowsocks2022ResponseStarted = false;
    bool shadowsocks2022ResponseHeaderRead = false;
    std::unique_ptr<Shadowsocks2022UdpSession> shadowsocks2022UdpSession;
    string shadowsocks2022UdpConfig;
    uint64_t shadowsocks2022UdpPacketId = 0;

    ssl::SSL_CTX socksTlsContext;
    ssl::SSL socksTls;
    bool socksTlsActive = false;
    int socksTlsWait = WAIT_NONE;

    static int getLastError();
    static int checksocket(int ret);
    static int check(int ret, bool blockOk = false);

};

} // namespace dcpp
