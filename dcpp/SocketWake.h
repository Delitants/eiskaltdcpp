#pragma once

#include "Socket.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace dcpp {

// A shared wake endpoint, never a payload socket or an owner callback.
class SocketWake {
public:
    SocketWake();
    ~SocketWake();
    SocketWake(const SocketWake&) = delete;
    SocketWake& operator=(const SocketWake&) = delete;
    socket_t handle() const { return reader; }
    void signal() noexcept;
    void consume();
    bool wait(uint32_t millis) const;
    uint64_t getInterruptedWaitCount() const { return interruptedWaitCount.load(std::memory_order_relaxed); }
private:
    socket_t reader = INVALID_SOCKET, writer = INVALID_SOCKET;
    std::mutex mutex;
    bool pending = false, failed = false;
    mutable std::atomic<uint64_t> interruptedWaitCount{0};
};

class WakeNotifier {
public:
    std::shared_ptr<void> subscribe(const std::shared_ptr<SocketWake>& wake);
    void notify() noexcept;
private:
    struct Entry { std::shared_ptr<SocketWake> wake; };
    std::mutex mutex;
    bool notified = false;
    std::vector<std::weak_ptr<Entry>> entries;
};
}
