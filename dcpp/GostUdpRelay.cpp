#include "stdinc.h"
#include "GostUdpRelay.h"
#include "GostProtocol.h"
#include "TimerManager.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#ifndef _WIN32
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#endif

namespace dcpp {
namespace {
constexpr size_t BufferLimit = 512 * 1024;
constexpr size_t FrameLimit = gost::MaxTunnelFrame;
// Reserve space for the input frame, local packet and both codec temporaries.
// The active SSL write stays in the queue and counts in full until completed.
constexpr size_t QueueBytes = BufferLimit - 4 * FrameLimit;
constexpr size_t QueueDatagrams = 64;

int numericFamily(const std::string& address, bool loopbackOnly) {
    in_addr v4{};
    in6_addr v6{};
    if(inet_pton(AF_INET, address.c_str(), &v4) == 1)
        return !loopbackOnly || (ntohl(v4.s_addr) >> 24) == 127 ? AF_INET : AF_UNSPEC;
    if(inet_pton(AF_INET6, address.c_str(), &v6) == 1)
        return !loopbackOnly || IN6_IS_ADDR_LOOPBACK(&v6) ? AF_INET6 : AF_UNSPEC;
    return AF_UNSPEC;
}
bool expectedSource(const sockaddr_storage& source, const std::string& address, uint16_t& port) {
    if(source.ss_family == AF_INET) {
        const auto& v4 = reinterpret_cast<const sockaddr_in&>(source);
        in_addr wanted{};
        port = ntohs(v4.sin_port);
        return inet_pton(AF_INET, address.c_str(), &wanted) == 1 && wanted.s_addr == v4.sin_addr.s_addr;
    }
    if(source.ss_family == AF_INET6) {
        const auto& v6 = reinterpret_cast<const sockaddr_in6&>(source);
        in6_addr wanted{};
        port = ntohs(v6.sin6_port);
        return inet_pton(AF_INET6, address.c_str(), &wanted) == 1 &&
            memcmp(&wanted, &v6.sin6_addr, sizeof(wanted)) == 0 && v6.sin6_scope_id == 0;
    }
    return false;
}

void waitForInput(Socket& remote, Socket& local, bool acceptLocal) {
#ifdef _WIN32
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(remote.sock, &reads);
    if(acceptLocal) FD_SET(local.sock, &reads);
    timeval timeout{0, 25000};
    ::select(0, &reads, nullptr, nullptr, &timeout);
#else
    pollfd descriptors[] = {{remote.sock, POLLIN, 0},
        {static_cast<int>(local.sock), static_cast<short>(acceptLocal ? POLLIN : 0), 0}};
    ::poll(descriptors, acceptLocal ? 2 : 1, 25);
#endif
    // Do not poll POLLOUT unconditionally: SSL WANT_READ can leave the socket
    // writable. The bounded wait also retries backpressured writes every 25ms.
}
}

struct GostUdpRelay::Impl {
    const Socket::StreamProxyConfig config;
    std::atomic<bool> stopping{false}, running{false};
    std::thread worker;
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false;
    uint16_t port = 0;
    std::string failure;
    std::exception_ptr setupFailure;
    std::vector<std::string> candidates;
    std::shared_ptr<void> admission;
    uint32_t setupTimeoutMs = 10000;

    explicit Impl(Socket::StreamProxyConfig value) : config(std::move(value)) { }
    bool cancelled() const { return stopping || (config.cancelled && config.cancelled()); }
    void publish(uint16_t boundPort, std::string error = {}) {
        std::lock_guard<std::mutex> lock(mutex);
        port = boundPort;
        failure = std::move(error);
        ready = true;
        changed.notify_all();
    }
    void run(std::string bind, std::string expected, uint16_t senderPort) {
        auto activeAdmission = std::move(admission);
        try {
#ifndef _WIN32
            sigset_t blockedSignals;
            sigemptyset(&blockedSignals);
            sigaddset(&blockedSignals, SIGPIPE);
            // Keep blocked through Socket/SSL destruction until thread exit.
            if(pthread_sigmask(SIG_BLOCK, &blockedSignals, nullptr) != 0)
                throw SocketException("Cannot protect GOST UDP worker from SIGPIPE");
#endif
            if(cancelled()) throw SocketException("GOST UDP setup cancelled");
            const auto dial = config.connectHost.empty() ? config.host : config.connectHost;
            if(numericFamily(dial, false) == AF_UNSPEC)
                throw SocketException("GOST UDP requires an asynchronously resolved proxy address");
            const auto family = numericFamily(bind, true);
            if(family == AF_UNSPEC || family != numericFamily(expected, true))
                throw SocketException("GOST UDP requires a loopback sender");
            Socket local;
            local.create(Socket::TYPE_UDP, family);
            local.bind("0", bind);
            local.setBlocking(false);
            local.setSocketOpt(SO_RCVBUF, 64 * 1024);
            local.setSocketOpt(SO_SNDBUF, 64 * 1024);
            Socket remote;
            auto connection = config;
            connection.connectHost = dial;
            connection.cancelled = [this] { return cancelled(); };
            // Local requestStop is not covered by the profile's route notifier.
            connection.cancellationNotifier.reset();
            if(candidates.empty()) candidates.push_back(dial);
            const auto began = GET_TICK();
            for(size_t i = 0; i < candidates.size(); ++i) {
                const auto elapsed = GET_TICK() - began;
                if(cancelled() || elapsed >= setupTimeoutMs)
                    throw SocketException("GOST UDP setup cancelled or timed out");
                if(numericFamily(candidates[i], false) == AF_UNSPEC)
                    throw SocketException("GOST UDP requires resolved proxy addresses");
                connection.connectHost = candidates[i];
                try {
                    const auto remaining = setupTimeoutMs - elapsed;
                    const auto attempt = std::max<uint32_t>(1, remaining / (candidates.size() - i));
                    remote.gostOpenUdpTunnel(connection, attempt);
                    break;
                } catch(const SocketException& error) {
                    if(error.getProxyStage() != SocketException::ProxyStage::None ||
                       cancelled() || i + 1 == candidates.size()) throw;
                }
            }
            if(cancelled()) throw SocketException("GOST UDP setup cancelled");
            running = true;
            publish(static_cast<uint16_t>(std::stoi(local.getLocalPort())));

            std::deque<ByteVector> queued;
            size_t queuedBytes = 0, writeOffset = 0;
            ByteVector input;
            input.reserve(FrameLimit);
            std::array<uint8_t, FrameLimit> scratch{};
            while(!cancelled()) {
                bool progressed = false;
                const bool room = queued.size() < QueueDatagrams && queuedBytes + FrameLimit <= QueueBytes;
                if(room) {
                    sockaddr_storage sender{};
                    uint16_t sourcePort = 0;
                    const auto received = local.read(scratch.data(), scratch.size(), sender);
                    progressed = received >= 0;
                    if(received > 0 && expectedSource(sender, expected, sourcePort) &&
                       (!senderPort || senderPort == sourcePort)) {
                        gost::Datagram datagram;
                        if(gost::decodeSocks(std::span(scratch.data(), size_t(received)), datagram) ==
                           gost::DecodeResult::Complete && !datagram.payload.empty()) {
                            // Malformed/empty packets cannot acquire an unspecified port.
                            if(!senderPort) senderPort = sourcePort;
                            auto frame = gost::encodeTunnel(datagram);
                            queuedBytes += frame.capacity();
                            queued.push_back(std::move(frame));
                        }
                    }
                }
                if(!queued.empty()) {
                    auto& frame = queued.front();
                    const int sent = remote.write(frame.data() + writeOffset, int(frame.size() - writeOffset));
                    if(sent == 0) break;
                    if(sent > 0) {
                        progressed = true;
                        writeOffset += size_t(sent);
                        if(writeOffset == frame.size()) {
                            queuedBytes -= frame.capacity();
                            queued.pop_front();
                            writeOffset = 0;
                        }
                    }
                }
                // read() also drains SSL_pending; fd readability alone is insufficient.
                const int received = remote.read(scratch.data(), int(FrameLimit - input.size()));
                if(received == 0) break;
                if(received > 0) {
                    progressed = true;
                    input.insert(input.end(), scratch.begin(), scratch.begin() + received);
                }
                while(!input.empty()) {
                    gost::Datagram datagram;
                    size_t used = 0;
                    const auto result = gost::decodeTunnel(input, datagram, used);
                    if(result == gost::DecodeResult::Invalid ||
                       (result == gost::DecodeResult::NeedMore && input.size() == FrameLimit))
                        throw SocketException("Invalid GOST UDP tunnel frame");
                    if(result == gost::DecodeResult::NeedMore) break;
                    if(senderPort) {
                        const auto packet = gost::encodeSocks(datagram);
                        // UDP delivery is best-effort. Never buffer or resolve a target
                        // locally; send only to the authenticated control's sender.
                        try {
                            local.writeTo(expected, std::to_string(senderPort), packet.data(), packet.size());
                        } catch(const SocketException&) { }
                    }
                    input.erase(input.begin(), input.begin() + used);
                }
                if(!progressed) waitForInput(remote, local,
                    queued.size() < QueueDatagrams && queuedBytes + FrameLimit <= QueueBytes);
            }
            // No reconnection: destroying these queues prevents stale replay.
        } catch(const Exception& error) {
            std::lock_guard<std::mutex> lock(mutex);
            if(!ready) { failure = error.getError(); setupFailure = std::current_exception(); }
        } catch(...) {
            std::lock_guard<std::mutex> lock(mutex);
            if(!ready) failure = "GOST UDP relay failed";
        }
        // The admission belongs to the live worker, not an idle consumer's Socket.
        activeAdmission.reset();
        running = false;
        std::lock_guard<std::mutex> lock(mutex);
        ready = true;
        changed.notify_all();
    }
};

GostUdpRelay::GostUdpRelay(Socket::StreamProxyConfig config, uint32_t setupTimeoutMs,
    std::vector<std::string> candidates, std::shared_ptr<void> admission) : d(std::make_unique<Impl>(std::move(config))) {
    d->setupTimeoutMs = std::max<uint32_t>(1, std::min<uint32_t>(10000, setupTimeoutMs));
    d->candidates = std::move(candidates);
    d->admission = std::move(admission);
}
GostUdpRelay::~GostUdpRelay() { requestStop(); join(); }
uint16_t GostUdpRelay::start(const std::string& bind, const std::string& sender, uint16_t port) {
    if(d->worker.joinable() || d->ready) {
        throw SocketException("GOST UDP relay already started");
    }
    d->worker = std::thread([this, bind, sender, port] { d->run(bind, sender, port); });
    std::unique_lock<std::mutex> lock(d->mutex);
    while(!d->ready) d->changed.wait_for(lock, std::chrono::milliseconds(25));
    if(!d->port) {
        if(d->setupFailure) std::rethrow_exception(d->setupFailure);
        throw SocketException(d->failure.empty() ? "GOST UDP relay failed" : d->failure);
    }
    return d->port;
}
void GostUdpRelay::requestStop() { d->stopping = true; d->changed.notify_all(); }
void GostUdpRelay::join() { if(d->worker.joinable()) d->worker.join(); }
bool GostUdpRelay::isRunning() const { return d->running; }
} // namespace dcpp
