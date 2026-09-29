#include "stdinc.h"
#include "SocketWake.h"
#include <chrono>
#ifndef _WIN32
#include <poll.h>
#include <unistd.h>
#else
#include <mstcpip.h>
#endif

namespace dcpp {
namespace {
int wakeError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
bool interrupted(int error) {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}
bool wouldBlock(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}
void closeWake(socket_t& fd) noexcept {
    if(fd == INVALID_SOCKET) return;
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
    fd = INVALID_SOCKET;
}
void configureWake(socket_t fd) {
#ifdef _WIN32
    u_long nonblocking = 1;
    if(ioctlsocket(fd, FIONBIO, &nonblocking) ||
       !SetHandleInformation(reinterpret_cast<HANDLE>(fd), HANDLE_FLAG_INHERIT, 0))
        throw SocketException("Cannot configure socket wakeup");
#else
    const int flags = fcntl(fd, F_GETFL);
    if(flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        throw SocketException("Cannot configure socket wakeup");
#ifdef SO_NOSIGPIPE
    int value = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value)))
        throw SocketException("Cannot configure socket wakeup");
#endif
#endif
}
bool readable(socket_t fd, uint32_t millis, std::atomic<uint64_t>* interruptions = nullptr) {
    const auto start = std::chrono::steady_clock::now();
    for(;;) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        const int remaining = static_cast<int>(std::min<int64_t>(INT_MAX, std::max<int64_t>(0, int64_t(millis) - elapsed)));
#ifdef _WIN32
        fd_set read; FD_ZERO(&read); FD_SET(fd, &read);
        timeval timeout{remaining / 1000, (remaining % 1000) * 1000};
        const int result = select(0, &read, nullptr, nullptr, &timeout);
#else
        pollfd item{fd, POLLIN, 0};
        const int result = ::poll(&item, 1, remaining);
        if(result > 0 && (item.revents & POLLNVAL)) throw SocketException("Invalid socket wakeup");
#endif
        if(result >= 0) return result > 0;
        const int error = wakeError();
        if(!interrupted(error)) throw SocketException(error);
        if(interruptions) interruptions->fetch_add(1, std::memory_order_relaxed);
        if(elapsed >= millis) return false;
    }
}
}

SocketWake::SocketWake() {
#ifdef _WIN32
    socket_t listener = INVALID_SOCKET;
    try {
        listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(listener == INVALID_SOCKET || writer == INVALID_SOCKET) throw SocketException(wakeError());
        configureWake(listener); configureWake(writer);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof(address);
        if(::bind(listener, reinterpret_cast<sockaddr*>(&address), length) || ::listen(listener, 1) ||
           getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length))
            throw SocketException(wakeError());
        if(::connect(writer, reinterpret_cast<sockaddr*>(&address), length) == SOCKET_ERROR && !wouldBlock(wakeError()))
            throw SocketException(wakeError());
        fd_set write, errors; FD_ZERO(&write); FD_ZERO(&errors);
        FD_SET(writer, &write); FD_SET(writer, &errors);
        timeval timeout{1, 0};
        int error = 0, errorLength = sizeof(error);
        if(select(0, nullptr, &write, &errors, &timeout) <= 0 ||
           getsockopt(writer, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &errorLength) || error ||
           !readable(listener, 1000)) throw SocketException("Cannot connect socket wakeup");
        sockaddr_in peer{}, local{};
        length = sizeof(peer);
        reader = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &length);
        length = sizeof(local);
        if(reader == INVALID_SOCKET || getsockname(writer, reinterpret_cast<sockaddr*>(&local), &length) ||
           peer.sin_addr.s_addr != local.sin_addr.s_addr || peer.sin_port != local.sin_port)
            throw SocketException("Invalid socket wakeup peer");
        configureWake(reader);
        int nodelay = 1;
        if(setsockopt(writer, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&nodelay), sizeof(nodelay)))
            throw SocketException(wakeError());
        closeWake(listener);
    } catch(...) { closeWake(listener); closeWake(reader); closeWake(writer); throw; }
#else
    socket_t pair[2];
    if(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) throw SocketException(wakeError());
    reader = pair[0]; writer = pair[1];
    try { configureWake(reader); configureWake(writer); }
    catch(...) { closeWake(reader); closeWake(writer); throw; }
#endif
}

SocketWake::~SocketWake() { closeWake(reader); closeWake(writer); }

void SocketWake::signal() noexcept {
    std::lock_guard lock(mutex);
    if(pending || failed) return;
    const char byte = 1;
    int result;
    do {
#ifdef MSG_NOSIGNAL
        result = ::send(writer, &byte, 1, MSG_NOSIGNAL);
#else
        result = ::send(writer, &byte, 1, 0);
#endif
    } while(result < 0 && interrupted(wakeError()));
    if(result == 1 || (result < 0 && wouldBlock(wakeError()))) pending = true;
    else {
        failed = true;
        // EOF wakes the reader even when the notification write itself failed.
        closeWake(writer);
    }
}

void SocketWake::consume() {
    std::lock_guard lock(mutex);
    if(failed) throw SocketException("Socket wakeup failed");
    char bytes[32];
    for(;;) {
        const int count = ::recv(reader, bytes, sizeof(bytes), 0);
        if(count > 0) continue;
        if(count < 0 && interrupted(wakeError())) continue;
        if(count < 0 && wouldBlock(wakeError())) break;
        throw SocketException("Socket wakeup closed");
    }
    // Serializing this reset with signal prevents a concurrent wake being lost.
    pending = false;
}

bool SocketWake::wait(uint32_t millis) const { return readable(reader, millis, &interruptedWaitCount); }

std::shared_ptr<void> WakeNotifier::subscribe(const std::shared_ptr<SocketWake>& wake) {
    auto entry = std::make_shared<Entry>();
    entry->wake = wake;
    bool signal;
    {
        std::lock_guard lock(mutex);
        signal = notified;
        if(!signal) {
            entries.erase(std::remove_if(entries.begin(), entries.end(), [](const auto& item) { return item.expired(); }), entries.end());
            entries.push_back(entry);
        }
    }
    if(signal) wake->signal();
    return entry;
}

void WakeNotifier::notify() noexcept {
    std::vector<std::weak_ptr<Entry>> pendingEntries;
    {
        std::lock_guard lock(mutex);
        if(notified) return;
        notified = true;
        pendingEntries.swap(entries);
    }
    for(auto& item : pendingEntries)
        if(auto entry = item.lock()) entry->wake->signal();
}
}
