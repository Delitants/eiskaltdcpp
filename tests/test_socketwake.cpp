#include "dcpp/stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "dcpp/SocketWake.h"
#include "TestContext.h"
#include "dcpp/ProxyRoute.h"
#include "dcpp/SettingsManager.h"
#include <future>
#include <thread>
#ifndef _WIN32
#include <csignal>
#include <pthread.h>
#include <sys/resource.h>
#endif

using namespace dcpp;

TEST_CASE("Wake signals coalesce and rearm after consumption", "[socket-wake]") {
    test::TestContext context;
    auto wake = std::make_shared<SocketWake>();
    CHECK_FALSE(wake->wait(0));
    for(int i = 0; i < 10000; ++i) wake->signal();
    CHECK(wake->wait(0));
    wake->consume();
    CHECK_FALSE(wake->wait(0));
    wake->signal();
    CHECK(wake->wait(0));
    wake->consume();
    CHECK_FALSE(wake->wait(0));
}

TEST_CASE("Wake registry latches revocation without retaining unsubscribed owners", "[socket-wake]") {
    test::TestContext context;
    WakeNotifier notifier;
    auto wake = std::make_shared<SocketWake>();
    auto removed = std::make_shared<SocketWake>();
    auto subscription = notifier.subscribe(wake);
    { auto forgotten = notifier.subscribe(removed); }
    notifier.notify();
    CHECK(wake->wait(0));
    CHECK_FALSE(removed->wait(0));
    wake->consume();
    auto late = std::make_shared<SocketWake>();
    auto lateSubscription = notifier.subscribe(late);
    CHECK(late->wait(0));
    std::weak_ptr<SocketWake> lifetime = late;
    late.reset(); lateSubscription.reset();
    CHECK(lifetime.expired());
}

TEST_CASE("Wake registry registration and revocation race cannot strand a waiter", "[socket-wake]") {
    test::TestContext context;
    for(int i = 0; i < 50; ++i) {
        WakeNotifier notifier;
        auto wake = std::make_shared<SocketWake>();
        auto publisher = std::async(std::launch::async, [&] { notifier.notify(); });
        auto subscription = notifier.subscribe(wake);
        publisher.get();
        REQUIRE(wake->wait(1000));
        wake->consume();
        CHECK_FALSE(wake->wait(0));
    }
}

TEST_CASE("Socket waits distinguish requested task wakeups from payload readiness", "[socket-wake]") {
    test::TestContext context;
    Socket socket;
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");
    auto wake = std::make_shared<SocketWake>();
    CHECK_THROWS_AS(socket.setWaitWake({}, [] { return false; }), SocketException);
    socket.setWaitWake(wake);
    wake->signal();
    CHECK(socket.wait(1000, Socket::WAIT_READ | Socket::WAIT_WAKE) == Socket::WAIT_WAKE);
    CHECK_FALSE(wake->wait(0));
    wake->signal();
    CHECK(socket.wait(10, Socket::WAIT_READ) == Socket::WAIT_NONE);
    Socket peer;
    peer.create(Socket::TYPE_UDP, AF_INET);
    peer.writeTo("127.0.0.1", socket.getLocalPort(), "x", 1);
    CHECK(socket.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
}

TEST_CASE("Notified socket cancellation interrupts a blocked wait without polling", "[socket-wake]") {
    test::TestContext context;
    Socket socket;
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");
    auto wake = std::make_shared<SocketWake>();
    std::atomic<bool> cancelled{false};
    std::atomic<int> checks{0};
    std::promise<void> entered;
    socket.setWaitWake(wake, [&] {
        if(++checks == 1) entered.set_value();
        return cancelled.load();
    });
    auto waiter = std::async(std::launch::async, [&] {
        try { socket.wait(5000, Socket::WAIT_READ); return false; }
        catch(const SocketException&) { return true; }
    });
    entered.get_future().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const int idleChecks = checks.load();
    cancelled = true;
    wake->signal();
    CHECK(waiter.get());
    CHECK(idleChecks == 1);
}

TEST_CASE("Connected socket route revocation interrupts idle payload waits", "[socket-wake][gost-global]") {
    test::TestContext context;
    Socket listener;
    listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1");
    listener.listen();
    Socket client;
    client.setContext(context.ownedCtx.get());
    client.connect("127.0.0.1", listener.getLocalPort());
    REQUIRE(client.waitConnected(1000));
    Socket peer;
    REQUIRE(listener.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
    peer.accept(listener);
    auto waiter = std::async(std::launch::async, [&] {
        try { client.wait(5000, Socket::WAIT_READ); return false; }
        catch(const SocketException&) { return true; }
    });
    context.ownedCtx->getProxyRoute()->stop();
    CHECK(waiter.get());
}

TEST_CASE("Concurrent wake drain and signal preserve each published task epoch", "[socket-wake]") {
    test::TestContext context;
    SocketWake wake;
    std::atomic<int> published{0}, acknowledged{0};
    std::atomic<bool> stop{false};
    auto producer = std::async(std::launch::async, [&] {
        for(int i = 1; i <= 1000 && !stop; ++i) {
            while(acknowledged != i - 1 && !stop) std::this_thread::yield();
            published = i;
            wake.signal();
        }
    });
    int observed = 0;
    while(observed < 1000 && wake.wait(1000)) {
        wake.consume();
        observed = published.load();
        acknowledged = observed;
    }
    stop = true;
    producer.get();
    CHECK(observed == 1000);
}

#ifndef _WIN32
namespace {
volatile sig_atomic_t wakeSignalHandled = 0;
void handleWakeSignal(int) { wakeSignalHandled = 1; }

class WakeSignalBurst {
public:
    WakeSignalBurst() : target(pthread_self()) {
        maskSaved = pthread_sigmask(SIG_SETMASK, nullptr, &previousMask) == 0;
        if(!maskSaved) return;
        struct sigaction action{};
        action.sa_handler = handleWakeSignal;
        sigemptyset(&action.sa_mask);
        installed = sigaction(SIGUSR1, &action, &previousAction) == 0;
        if(!installed) return;
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGUSR1);
        unblocked = pthread_sigmask(SIG_UNBLOCK, &mask, nullptr) == 0;
        wakeSignalHandled = 0;
    }
    ~WakeSignalBurst() {
        stop();
        if(installed) sigaction(SIGUSR1, &previousAction, nullptr);
        if(maskSaved) pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);
    }
    bool ready() const { return installed && unblocked; }
    void start() {
        sender = std::thread([this] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
            while(!stopping && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if(!stopping && pthread_kill(target, SIGUSR1) != 0) ++errors;
            }
        });
    }
    void stop() {
        stopping = true;
        if(sender.joinable()) sender.join();
    }
    int signalErrors() const { return errors.load(); }
private:
    pthread_t target;
    sigset_t previousMask{};
    struct sigaction previousAction{};
    bool maskSaved = false, installed = false, unblocked = false;
    std::atomic<bool> stopping{false};
    std::atomic<int> errors{0};
    std::thread sender;
};
}

TEST_CASE("Wake allocation fails cleanly under descriptor exhaustion and recovers", "[socket-wake][wake-fault]") {
    struct LimitGuard {
        rlimit saved{};
        bool active = false;
        ~LimitGuard() { if(active) setrlimit(RLIMIT_NOFILE, &saved); }
    } limit;
    REQUIRE(getrlimit(RLIMIT_NOFILE, &limit.saved) == 0);
    const rlimit exhausted{0, limit.saved.rlim_max};
    const int limited = setrlimit(RLIMIT_NOFILE, &exhausted);
    limit.active = limited == 0;
    bool failed = false;
    // Do not run Catch reporters or create a TestContext while descriptors are exhausted.
    try { SocketWake unavailable; }
    catch(const SocketException&) { failed = true; }
    const int restored = setrlimit(RLIMIT_NOFILE, &limit.saved);
    if(restored == 0) limit.active = false;
    REQUIRE(restored == 0);
    REQUIRE(limited == 0);
    REQUIRE(failed);
    SocketWake recovered;
    recovered.signal();
    REQUIRE(recovered.wait(1000));
    recovered.consume();
    CHECK_FALSE(recovered.wait(0));
}

TEST_CASE("Wake wait preserves its deadline across repeated POSIX signals", "[socket-wake][wake-eintr]") {
    SocketWake wake;
    WakeSignalBurst signals;
    REQUIRE(signals.ready());
    signals.start();
    const auto start = std::chrono::steady_clock::now();
    const bool ready = wake.wait(250);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    signals.stop();
    INFO("Interrupted wake wait elapsed ms: " << elapsed);
    CHECK(wakeSignalHandled != 0);
    CHECK(signals.signalErrors() == 0);
    CHECK(wake.getInterruptedWaitCount() >= 5);
    CHECK_FALSE(ready);
    CHECK(elapsed >= 200);
    CHECK(elapsed < 1000); // A reset timeout would outlast the 1500 ms signal burst.
    wake.signal();
    REQUIRE(wake.wait(1000));
    wake.consume();
    CHECK_FALSE(wake.wait(0));
}

TEST_CASE("Socket native wait retries EINTR without restarting its timeout", "[socket-wake][wake-eintr]") {
    test::TestContext context;
    Socket socket;
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");
    auto wake = std::make_shared<SocketWake>();
    socket.setWaitWake(wake);
    WakeSignalBurst signals;
    REQUIRE(signals.ready());
    const auto calls = socket.getNativeWaitCount();
    signals.start();
    const auto start = std::chrono::steady_clock::now();
    const int result = socket.wait(250, Socket::WAIT_READ | Socket::WAIT_WAKE);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    signals.stop();
    INFO("Interrupted socket wait elapsed ms: " << elapsed);
    CHECK(wakeSignalHandled != 0);
    CHECK(signals.signalErrors() == 0);
    CHECK(socket.getNativeWaitCount() - calls >= 5);
    CHECK(result == Socket::WAIT_NONE);
    CHECK(elapsed >= 200);
    CHECK(elapsed < 1000);
    wake->signal();
    REQUIRE(socket.wait(1000, Socket::WAIT_READ | Socket::WAIT_WAKE) == Socket::WAIT_WAKE);
    Socket peer;
    peer.create(Socket::TYPE_UDP, AF_INET);
    peer.writeTo("127.0.0.1", socket.getLocalPort(), "x", 1);
    CHECK(socket.wait(1000, Socket::WAIT_READ) == Socket::WAIT_READ);
}

TEST_CASE("POSIX native waits support descriptors beyond select capacity", "[socket-wake]") {
    test::TestContext context;
    Socket socket;
    socket.create(Socket::TYPE_UDP, AF_INET);
    socket.bind("0", "127.0.0.1");
    const int high = fcntl(socket.sock, F_DUPFD, FD_SETSIZE + 8);
    REQUIRE(high >= FD_SETSIZE);
    ::close(socket.sock);
    socket.sock = high;
    auto wake = std::make_shared<SocketWake>();
    CHECK((fcntl(wake->handle(), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK((fcntl(wake->handle(), F_GETFL) & O_NONBLOCK) != 0);
    socket.setWaitWake(wake);
    wake->signal();
    CHECK(socket.wait(1000, Socket::WAIT_READ | Socket::WAIT_WAKE) == Socket::WAIT_WAKE);
}
#endif
