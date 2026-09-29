#ifdef USE_TORRENT
#include "dcpp/stdinc.h"
#include "TorrentProxyAdapter.h"
#include "GostUdpRelay.h"
#include "dcpp/Socket.h"
#include <QElapsedTimer>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTimer>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#ifndef _WIN32
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#endif

namespace {
constexpr int BufferLimit = 512 * 1024;
constexpr int ChunkSize = 64 * 1024;
constexpr size_t ConnectionLimit = 64;
constexpr size_t AssociationLimit = 8;
constexpr int SetupTimeout = 10000;

struct AdapterLimits {
    std::atomic<size_t> workers{0}, associations{0};
    std::mutex mutex;
    std::condition_variable changed;
    unsigned failures = 0;
    bool establishing = false;
    std::chrono::steady_clock::time_point nextAttempt{};

    bool enter(const std::function<bool()>& cancelled) {
        std::unique_lock<std::mutex> lock(mutex);
        while(!cancelled()) {
            if(!establishing && std::chrono::steady_clock::now() >= nextAttempt) {
                establishing = true;
                return true;
            }
            changed.wait_for(lock, std::chrono::milliseconds(25));
        }
        return false;
    }
    void finish(bool success, bool cancelled) {
        std::lock_guard<std::mutex> lock(mutex);
        if(success) { failures = 0; nextAttempt = {}; }
        else if(!cancelled) {
            const unsigned delay = failures < 5 ? (1U << failures) : 30;
            failures = std::min(failures + 1, 6U);
            nextAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
        }
        establishing = false;
        changed.notify_all();
    }
};

struct Reservation {
    std::atomic<size_t>* count = nullptr;
    Reservation(std::atomic<size_t>& counter, size_t limit) {
        auto value = counter.load();
        while(value < limit) {
            if(counter.compare_exchange_weak(value, value + 1)) { count = &counter; break; }
        }
    }
    ~Reservation() { if(count) --*count; }
    explicit operator bool() const { return count != nullptr; }
};

struct Establishment {
    AdapterLimits& limits;
    std::function<bool()> cancelled;
    bool enabled, held = false;
    Establishment(AdapterLimits& value, std::function<bool()> cancel, bool active)
        : limits(value), cancelled(std::move(cancel)), enabled(active) { }
    bool enter() { return !enabled || (held = limits.enter(cancelled)); }
    void finish(bool success) {
        if(held) { limits.finish(success, cancelled()); held = false; }
    }
    ~Establishment() { finish(false); }
};

class RelayServer : public QTcpServer {
public:
    std::function<void(qintptr)> acceptDescriptor;
protected:
    void incomingConnection(qintptr descriptor) override { acceptDescriptor(descriptor); }
};

// Qt's accepted-socket wrapper closes both directions on peer EOF. Own the
// native handle instead so a client can finish uploading and still read a reply.
class LocalConnection {
public:
    explicit LocalConnection(qintptr descriptor) : socket(static_cast<socket_t>(descriptor)) {
#ifdef _WIN32
        u_long nonblocking = 1;
        const bool failed = ioctlsocket(socket, FIONBIO, &nonblocking) != 0;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        const bool failed = flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0;
#endif
        if(failed) { close(); throw dcpp::SocketException("Cannot initialize local proxy socket"); }
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#endif
    }
    LocalConnection(LocalConnection&& other) noexcept : socket(std::exchange(other.socket, INVALID_SOCKET)) { }
    LocalConnection(const LocalConnection&) = delete;
    LocalConnection& operator=(const LocalConnection&) = delete;
    ~LocalConnection() { close(); }
    int read(char* buffer, int size) { return check(static_cast<int>(::recv(socket, buffer, size, 0))); }
    int write(const char* buffer, int size) {
#ifdef MSG_NOSIGNAL
        return check(static_cast<int>(::send(socket, buffer, size, MSG_NOSIGNAL)));
#else
        return check(static_cast<int>(::send(socket, buffer, size, 0)));
#endif
    }
    void shutdownWrite() { ::shutdown(socket, 1); }
    bool controlClosed() const {
        // A UDP control channel has no data phase. EOF or unexpected bytes
        // cancel setup as well as an already running association.
        char byte;
        try { return check(static_cast<int>(::recv(socket, &byte, 1, MSG_PEEK))) >= 0; }
        catch(...) { return true; }
    }
    QHostAddress peerAddress() const {
        sockaddr_in address{};
        socklen_t size = sizeof(address);
        if(getpeername(socket, reinterpret_cast<sockaddr*>(&address), &size) != 0 || address.sin_family != AF_INET)
            return {};
        return QHostAddress(ntohl(address.sin_addr.s_addr));
    }
    void wait(bool readable, bool writable) {
#ifdef _WIN32
        if(!readable && !writable) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); return; }
        fd_set reads, writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        if(readable) FD_SET(socket, &reads);
        if(writable) FD_SET(socket, &writes);
        timeval timeout{0, 10000};
        ::select(0, readable ? &reads : nullptr, writable ? &writes : nullptr, nullptr, &timeout);
#else
        pollfd descriptor{socket, static_cast<short>((readable ? POLLIN : 0) | (writable ? POLLOUT : 0)), 0};
        // Do not poll a closed/full read direction: persistent HUP would spin.
        ::poll(&descriptor, readable || writable ? 1 : 0, 10);
#endif
    }
private:
    socket_t socket;
    static int check(int result) {
        if(result >= 0) return result;
#ifdef _WIN32
        const int error = WSAGetLastError();
        if(error == WSAEWOULDBLOCK || error == WSAEINTR) return -1;
#else
        const int error = errno;
        if(error == EAGAIN || error == EWOULDBLOCK || error == EINTR) return -1;
#endif
        throw dcpp::SocketException(error);
    }
    void close() {
        if(socket == INVALID_SOCKET) return;
#ifdef _WIN32
        closesocket(socket);
#else
        ::close(socket);
#endif
        socket = INVALID_SOCKET;
    }
};

void pauseRelay() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }

bool readExact(LocalConnection &socket, QByteArray &out, int size,
               const std::atomic<bool> &stopping, QElapsedTimer &deadline) {
    out.clear();
    out.resize(size);
    int offset = 0;
    while(offset < size && !stopping && deadline.elapsed() < SetupTimeout) {
        const int received = socket.read(out.data() + offset, size - offset);
        if(received == 0) return false;
        if(received > 0) offset += received;
        else pauseRelay();
    }
    return offset == size;
}

bool writeExact(LocalConnection &socket, const char* bytes, int size, const std::atomic<bool>& stopping) {
    QElapsedTimer deadline;
    deadline.start();
    int offset = 0;
    while(offset < size && !stopping && deadline.elapsed() < 200) {
        const int sent = socket.write(bytes + offset, size - offset);
        if(sent > 0) offset += sent;
        else if(sent == 0) return false;
        else pauseRelay();
    }
    return offset == size;
}

bool reply(LocalConnection &socket, unsigned char result, const std::atomic<bool>& stopping, quint16 port = 0) {
    const char response[] = {5, static_cast<char>(result), 0, 1, 127, 0, 0, 1,
                            static_cast<char>(port >> 8), static_cast<char>(port)};
    return writeExact(socket, response, sizeof(response), stopping);
}

struct Resolution {
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false, cancelled = false;
    int lookupId = -1;
    std::string address;
};

void cancelResolution(const std::shared_ptr<Resolution>& result) {
    int lookupId;
    {
        std::lock_guard<std::mutex> lock(result->mutex);
        result->cancelled = true;
        lookupId = std::exchange(result->lookupId, -1);
    }
    result->changed.notify_all();
    if(lookupId != -1) QHostInfo::abortHostLookup(lookupId);
}

std::string resolveUpstream(QObject& owner, const std::string& host,
                            const std::shared_ptr<Resolution>& result, const std::function<bool()>& cancelled,
                            int timeout) {
    QHostAddress numeric;
    const QString name = QString::fromStdString(host);
    if(numeric.setAddress(name)) return numeric.toString().toStdString();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    QMetaObject::invokeMethod(&owner, [context = &owner, name, result] {
        {
            std::lock_guard<std::mutex> lock(result->mutex);
            if(result->cancelled) return;
        }
        const int id = QHostInfo::lookupHost(name, context, [result](const QHostInfo& info) {
            {
                std::lock_guard<std::mutex> lock(result->mutex);
                result->lookupId = -1;
                if(result->cancelled) return;
                if(info.error() == QHostInfo::NoError) {
                    for(const auto& address : info.addresses()) {
                        if(address.protocol() == QAbstractSocket::IPv4Protocol ||
                           address.protocol() == QAbstractSocket::IPv6Protocol) {
                            result->address = address.toString().toStdString();
                            break;
                        }
                    }
                }
                result->ready = true;
            }
            result->changed.notify_all();
        });
        bool cancelled;
        {
            std::lock_guard<std::mutex> lock(result->mutex);
            cancelled = result->cancelled;
            if(!result->ready && !cancelled) result->lookupId = id;
        }
        if(cancelled) QHostInfo::abortHostLookup(id);
    }, Qt::QueuedConnection);
    std::unique_lock<std::mutex> lock(result->mutex);
    while(!result->ready && !result->cancelled && !cancelled() && std::chrono::steady_clock::now() < deadline)
        result->changed.wait_for(lock, std::chrono::milliseconds(25));
    if(!result->ready || cancelled()) result->cancelled = true;
    if(result->cancelled || result->address.empty())
        throw dcpp::SocketException("Proxy name resolution failed, timed out or was cancelled");
    return result->address;
}

void relay(LocalConnection client, dcpp::Socket::StreamProxyConfig upstream,
           QByteArray user, QByteArray password, const std::atomic<bool> &stopping,
           QObject& resolverOwner, const std::shared_ptr<Resolution>& resolution, AdapterLimits& limits, bool udpEnabled) {
    QElapsedTimer handshake;
    handshake.start();
    QByteArray bytes;
    if(!readExact(client, bytes, 2, stopping, handshake) || bytes[0] != 5) return;
    const int methods = static_cast<unsigned char>(bytes[1]);
    if(!methods || !readExact(client, bytes, methods, stopping, handshake)) return;
    if(!bytes.contains(char(2))) {
        writeExact(client, "\x05\xff", 2, stopping);
        return;
    }
    if(!writeExact(client, "\x05\x02", 2, stopping)) return;
    if(!readExact(client, bytes, 2, stopping, handshake) || bytes[0] != 1) return;
    const int userLength = static_cast<unsigned char>(bytes[1]);
    QByteArray providedUser, providedPassword;
    if(!readExact(client, providedUser, userLength, stopping, handshake) ||
       !readExact(client, bytes, 1, stopping, handshake)) return;
    const int passwordLength = static_cast<unsigned char>(bytes[0]);
    if(!readExact(client, providedPassword, passwordLength, stopping, handshake)) return;
    if(providedUser != user || providedPassword != password) {
        writeExact(client, "\x01\x01", 2, stopping);
        return;
    }
    if(!writeExact(client, "\x01\x00", 2, stopping)) return;
    if(!readExact(client, bytes, 4, stopping, handshake) || bytes[0] != 5 || bytes[2] != 0) return;
    const bool gost = upstream.type == dcpp::Socket::StreamProxyConfig::Gost;
    const bool udp = bytes[1] == 3;
    if(bytes[1] != 1 && !(udp && gost && udpEnabled)) {
        reply(client, 7, stopping);
        return;
    }
    const int addressType = static_cast<unsigned char>(bytes[3]);
    QString target;
    if(addressType == 3) {
        if(!readExact(client, bytes, 1, stopping, handshake)) return;
        const int length = static_cast<unsigned char>(bytes[0]);
        if(!length || !readExact(client, bytes, length, stopping, handshake) || bytes.contains('\0')) return;
        target = QString::fromLatin1(bytes);
    } else if(addressType == 1) {
        if(!readExact(client, bytes, 4, stopping, handshake)) return;
        const auto *v = reinterpret_cast<const unsigned char*>(bytes.constData());
        target = QHostAddress((quint32(v[0]) << 24) | (quint32(v[1]) << 16) |
                              (quint32(v[2]) << 8) | v[3]).toString();
    } else if(addressType == 4) {
        if(!readExact(client, bytes, 16, stopping, handshake)) return;
        Q_IPV6ADDR address{};
        std::copy(bytes.cbegin(), bytes.cend(), address.c);
        target = QHostAddress(address).toString();
    } else {
        reply(client, 8, stopping);
        return;
    }
    if(!readExact(client, bytes, 2, stopping, handshake)) return;
    const int port = (static_cast<unsigned char>(bytes[0]) << 8) | static_cast<unsigned char>(bytes[1]);
    if((!port && !udp) || stopping) return;
    const auto sender = client.peerAddress();
    if(udp) {
        QHostAddress requested;
        if(!requested.setAddress(target) || !sender.isLoopback() ||
           (!requested.isNull() && requested != QHostAddress::AnyIPv4 && requested != QHostAddress::AnyIPv6 && requested != sender)) {
            reply(client, 2, stopping);
            return;
        }
    }
    Reservation association(limits.associations, udp ? AssociationLimit : 0);
    Reservation udpWorker(limits.workers, udp ? ConnectionLimit : 0);
    if(udp && (!association || !udpWorker)) { reply(client, 1, stopping); return; }
    const auto routeCancelled = upstream.cancelled;
    const std::function<bool()> cancelled = [&stopping, &client, udp, routeCancelled] {
        if (routeCancelled && routeCancelled()) return true;
        return stopping.load() || (udp && client.controlClosed());
    };
    Establishment establishment(limits, cancelled, gost);
    if(!establishment.enter()) return;
    // Backoff precedes the ten-second DNS+TCP+TLS+AUTH+command setup budget.
    QElapsedTimer budget;
    budget.start();
    const auto setupDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(SetupTimeout);
    const auto setupComplete = std::make_shared<std::atomic<bool>>(false);
    dcpp::Socket remote;
    std::unique_ptr<GostUdpRelay> udpRelay;
    quint16 boundPort = 0;
    try {
        upstream.connectHost = resolveUpstream(resolverOwner, upstream.host, resolution, cancelled, SetupTimeout);
        upstream.cancelled = [cancelled, setupDeadline, setupComplete] {
            return cancelled() || (!setupComplete->load() && std::chrono::steady_clock::now() >= setupDeadline);
        };
        if(stopping || budget.elapsed() >= SetupTimeout) throw dcpp::SocketException("Proxy setup timed out");
        if(udp) {
            udpRelay = std::make_unique<GostUdpRelay>(upstream);
            QString error;
            boundPort = udpRelay->start(QHostAddress::LocalHost, sender, quint16(port), &error);
            if(!boundPort) throw dcpp::SocketException("GOST UDP establishment failed");
        } else remote.proxyConnect(target.toStdString(), std::to_string(port), upstream, SetupTimeout - budget.elapsed());
        establishment.finish(true);
    } catch(const dcpp::SocketException& error) {
        // A CONNECT target refusal proves the authenticated proxy is healthy.
        establishment.finish(!udp && error.getSocksReplyCode().has_value());
        reply(client, 5, stopping);
        return;
    } catch(...) {
        reply(client, 5, stopping);
        return;
    }
    // Copied callbacks share only an atomic setup phase, not a mutable timer.
    *setupComplete = true;
    if(!reply(client, 0, stopping, boundPort)) return;
    if(udp) {
        char byte;
        while(!stopping && udpRelay->isRunning()) {
            const int received = client.read(&byte, 1);
            if(received >= 0) break; // EOF or unexpected control-channel data.
            client.wait(true, false);
        }
        udpRelay->requestStop();
        udpRelay->join();
        return;
    }
    QByteArray incoming, inFlight, outgoing;
    int writeOffset = 0;
    bool clientEof = false, remoteEof = false, clientWriteClosed = false, remoteWriteClosed = false;
    QElapsedTimer drain;
    std::array<char, ChunkSize> buffer{};
    try {
        while(!stopping) {
            if(drain.isValid() && drain.elapsed() >= 30000) break;
            bool progressed = false;
            const int space = BufferLimit - int(incoming.size() + inFlight.size() - writeOffset);
            if(!clientEof && space > 0) {
                const int received = client.read(buffer.data(), qMin(ChunkSize, space));
                if(received == 0) { clientEof = true; progressed = true; }
                else if(received > 0) { incoming.append(buffer.data(), received); progressed = true; }
            }
            if(inFlight.isEmpty() && !incoming.isEmpty()) {
                inFlight = incoming.left(ChunkSize);
                incoming.remove(0, inFlight.size());
                writeOffset = 0;
            }
            // Do not mutate this allocation or retry length after SSL WANT_*.
            if(!inFlight.isEmpty()) {
                const int sent = remote.write(inFlight.constData() + writeOffset, inFlight.size() - writeOffset);
                if(sent == 0) break;
                if(sent > 0) {
                    writeOffset += sent;
                    progressed = true;
                    if(writeOffset == inFlight.size()) { inFlight.clear(); writeOffset = 0; }
                }
            }
            if(remote.hasPendingProxyOutput() && remote.flushProxyOutput()) progressed = true;
            // Always attempt a read: decrypted frames can outlive fd readability.
            if(!remoteEof && outgoing.size() < BufferLimit) {
                const int received = remote.read(buffer.data(), qMin(ChunkSize, BufferLimit - int(outgoing.size())));
                if(received == 0) { remoteEof = true; progressed = true; }
                if(received > 0) {
                    outgoing.append(buffer.data(), received);
                    progressed = true;
                }
            }
            if(!outgoing.isEmpty()) {
                const int sent = client.write(outgoing.constData(), outgoing.size());
                if(sent == 0) break;
                if(sent > 0) { outgoing.remove(0, sent); progressed = true; }
            }
            if((clientEof || remoteEof) && !drain.isValid()) drain.start();
            if(clientEof && incoming.isEmpty() && inFlight.isEmpty() && !remoteWriteClosed && remote.shutdownWrite()) {
                remoteWriteClosed = true;
                progressed = true;
            }
            if(remoteEof && outgoing.isEmpty() && !clientWriteClosed) {
                client.shutdownWrite();
                clientWriteClosed = true;
                progressed = true;
            }
            if(remoteWriteClosed && clientWriteClosed) break;
            // A full input buffer must not turn readable-fd backpressure into a spin.
            if(!progressed) client.wait(!clientEof && incoming.size() + inFlight.size() - writeOffset < BufferLimit,
                                        !outgoing.isEmpty());
        }
    } catch(...) { }
    remote.disconnect();
}
}

struct TorrentProxyAdapter::Impl {
    struct Worker {
        std::atomic<bool> done{false};
        std::thread thread;
        std::shared_ptr<Resolution> resolution = std::make_shared<Resolution>();
        ~Worker() { if(thread.joinable()) thread.join(); }
    };
    RelayServer server;
    QTimer reaper;
    std::atomic<bool> stopping{false};
    std::vector<std::unique_ptr<Worker>> workers;
    AdapterLimits limits;
    eiskalt::torrent::ProxyConfig local;
    dcpp::Socket::StreamProxyConfig upstream;
    Impl() { workers.reserve(ConnectionLimit); }

    void reap() {
        workers.erase(std::remove_if(workers.begin(), workers.end(),
            [](const auto &worker) {
                if(!worker->done) return false;
                cancelResolution(worker->resolution);
                return true;
            }), workers.end());
    }
    void accept(qintptr descriptor) {
        try {
            LocalConnection client(descriptor);
            reap();
            if(stopping || (upstream.cancelled && upstream.cancelled()) || workers.size() >= ConnectionLimit) return;
            auto worker = std::make_unique<Worker>();
            auto slot = std::make_shared<Reservation>(limits.workers, ConnectionLimit);
            if(!*slot) return;
            auto *ptr = worker.get();
            const auto config = upstream;
            const bool udpEnabled = local.udp;
            const auto user = local.user.toLatin1(), password = local.password.toLatin1();
            worker->thread = std::thread([this, ptr, slot, client = std::move(client), config, user, password, udpEnabled]() mutable {
                try {
#ifndef _WIN32
                    sigset_t blockedSignals;
                    sigemptyset(&blockedSignals);
                    sigaddset(&blockedSignals, SIGPIPE);
                    // Do not restore: this is a dedicated thread, including SSL cleanup.
                    if(pthread_sigmask(SIG_BLOCK, &blockedSignals, nullptr) != 0)
                        throw dcpp::SocketException("Cannot protect proxy worker from SIGPIPE");
#endif
                    relay(std::move(client), config, user, password, stopping, server, ptr->resolution, limits, udpEnabled);
                }
                catch(...) { }
                ptr->done = true;
            });
            workers.push_back(std::move(worker));
        } catch(...) { }
    }
};

TorrentProxyAdapter::TorrentProxyAdapter(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {
#ifndef QT_NO_NETWORKPROXY
    d->server.setProxy(QNetworkProxy::NoProxy);
#endif
    d->server.acceptDescriptor = [this](qintptr descriptor) { d->accept(descriptor); };
    connect(&d->reaper, &QTimer::timeout, this, [this] { d->reap(); });
}

TorrentProxyAdapter::~TorrentProxyAdapter() { stop(); }

bool TorrentProxyAdapter::start(const eiskalt::torrent::ProxyConfig &proxy, QString *error) {
    stop();
    using eiskalt::torrent::ProxyType;
    if((proxy.type != ProxyType::Socks5 && proxy.type != ProxyType::Socks5Tls && proxy.type != ProxyType::Shadowsocks && proxy.type != ProxyType::Gost) ||
       proxy.host.trimmed().isEmpty() ||
       proxy.port < 1 || proxy.port > 65535 ||
       (proxy.type == ProxyType::Shadowsocks && (proxy.password.isEmpty() || proxy.cipher.isEmpty())) ||
       (proxy.type == ProxyType::Gost && (proxy.user.isEmpty() || proxy.password.isEmpty() ||
        proxy.user.toUtf8().size() > 255 || proxy.password.toUtf8().size() > 255))) {
        if(error) *error = tr("A valid upstream proxy is required.");
        return false;
    }
    // Torrent Gost=4, native Gost=3. Never cast between the enum domains.
    d->upstream.type = proxy.type == ProxyType::Gost ? dcpp::Socket::StreamProxyConfig::Gost :
        proxy.type == ProxyType::Shadowsocks ? dcpp::Socket::StreamProxyConfig::Shadowsocks : dcpp::Socket::StreamProxyConfig::Socks5;
    d->upstream.host = proxy.host.trimmed().toStdString();
    d->upstream.port = proxy.port;
    d->upstream.user = proxy.user.toStdString();
    d->upstream.password = proxy.password.toStdString();
    d->upstream.cipher = proxy.cipher.toStdString();
    d->upstream.tls = proxy.type == ProxyType::Socks5Tls;
    d->upstream.remoteDns = true;
    d->upstream.verifyTls = true;
    d->upstream.caPem = proxy.caPem.toStdString();
    d->upstream.cancelled = [token = proxy.revoked] { return token && token->load(); };
    if(!d->server.listen(QHostAddress::LocalHost, 0)) {
        if(error) *error = tr("Cannot start the local proxy adapter.");
        return false;
    }
    d->stopping = false;
    d->local.type = ProxyType::Socks5;
    d->local.host = QStringLiteral("127.0.0.1");
    d->local.port = d->server.serverPort();
    d->local.user = QStringLiteral("eiskalt-torrent");
    d->local.password.clear();
    for(int i = 0; i != 8; ++i)
        d->local.password += QString::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0');
    d->local.remoteDns = true;
    d->local.udp = proxy.type == ProxyType::Gost && proxy.udp;
    d->reaper.start(1000);
    if(error) error->clear();
    return true;
}

void TorrentProxyAdapter::stop() {
    d->stopping = true;
    d->limits.changed.notify_all();
    d->server.close();
    d->reaper.stop();
    for(const auto& worker : d->workers) cancelResolution(worker->resolution);
    d->workers.clear();
    d->limits.finish(true, true);
    d->local = {};
}

eiskalt::torrent::ProxyConfig TorrentProxyAdapter::endpoint() const { return d->local; }
#endif
