/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "SocketInputStream.h"
#include <ranges>
#include <condition_variable>
#include <iostream>
#define private public
#include "DCContext.h"
#include "BufferedSocket.h"
#undef private
#include "TestContext.h"
#include "SettingsManager.h"
#include "Streams.h"
#include "TimerManager.h"
#include "ClientManager.h"
#include "UploadManager.h"
#include "ThrottleManager.h"
#include <zlib.h>

using namespace dcpp;

TEST_CASE("BufferedSocket drains the final Shadowsocks frame before send completion", "[socket-output][shadowsocks-buffered]") {
    for(const auto* method : {"aes-256-gcm", "2022-blake3-aes-256-gcm"}) {
        for(const auto* operation : {"command", "file", "disconnect", "cancel"}) {
            DYNAMIC_SECTION(method << " / " << operation) {
                test::TestContext context;
                context.ownedCtx->timerManager_ = std::make_unique<TimerManager>(*context.ownedCtx);
                context.ownedCtx->clientManager_ = std::make_unique<ClientManager>(*context.ownedCtx);
                context.ownedCtx->uploadManager_ = std::make_unique<UploadManager>(*context.ownedCtx);
                context.ownedCtx->throttleManager_ = std::make_unique<ThrottleManager>(*context.ownedCtx);
                context.ownedCtx->getSettingsManager()->set(SettingsManager::SOCKET_OUT_BUFFER, 4096);
                Socket acceptor;
                acceptor.create(Socket::TYPE_TCP, AF_INET);
                acceptor.bind("0", "127.0.0.1");
                acceptor.listen();
                struct BackpressuredSocket : Socket {
                    std::atomic<bool> pressured{false};
                    std::atomic<size_t> wireBytes{0};
                    int write(const void* bytes, int length) override {
                        // Prefill using valid encrypted records until the final
                        // accepted record is pending in the real transport.
                        for(int i = 0; i < 8192; ++i) {
                            const int accepted = Socket::write(bytes, length);
                            if(accepted <= 0) throw SocketException("Unexpected prefill failure");
                            wireBytes += accepted + 34;
                            if(hasPendingProxyOutput()) {
                                pressured = true;
                                return accepted;
                            }
                        }
                        throw SocketException("Fixture failed to create backpressure");
                    }
                };
                auto transport = std::make_unique<BackpressuredSocket>();
                auto* observed = transport.get();
                Socket::StreamProxyConfig route;
                route.type = Socket::StreamProxyConfig::Shadowsocks;
                route.host = "127.0.0.1";
                route.port = std::stoi(acceptor.getLocalPort());
                route.cipher = method;
                route.password = route.cipher == "aes-256-gcm" ? "test-password" :
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
                transport->proxyConnect("example.invalid", "443", route, 2000);
                REQUIRE(acceptor.wait(2000, Socket::WAIT_READ) == Socket::WAIT_READ);
                Socket peer;
                peer.accept(acceptor);
                std::array<char, 65536> wire{};
                // Account for data records separately from the flushed handshake.
                while(peer.wait(20, Socket::WAIT_READ) == Socket::WAIT_READ)
                    REQUIRE(peer.read(wire.data(), wire.size()) > 0);
                struct Listener : BufferedSocketListener {
                    Socket* transport = nullptr;
                    std::atomic<bool> completed{false}, pendingAtCompletion{false};
                    void on(Updated) override {
                        pendingAtCompletion = transport->hasPendingProxyOutput();
                        completed = true;
                    }
                    void on(TransmitDone) override { on(Updated()); }
                    void on(Failed, const string&) override { completed = true; }
                } listener;
                listener.transport = observed;
                const string payload(2048, 'x');
                MemoryInputStream input(payload);
                auto cleanup = [](BufferedSocket* socket) {
                    BufferedSocket::putSocket(socket);
                    BufferedSocket::waitShutdown();
                };
                std::unique_ptr<BufferedSocket, decltype(cleanup)> socket(
                    BufferedSocket::getSocket('\n', *context.ownedCtx), cleanup);
                socket->addListener(&listener);
                socket->setSocket(std::move(transport));
                {
                    Lock lock(socket->cs);
                    socket->addTask(BufferedSocket::ACCEPTED, nullptr);
                }
                if(string(operation) == "file") socket->transmitFile(&input);
                else socket->write(payload);
                if(string(operation) == "disconnect") socket->disconnect();
                else socket->updated();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                while(!observed->pressured && !listener.completed && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                REQUIRE(observed->pressured.load());
                if(string(operation) == "cancel") {
                    const auto start = std::chrono::steady_clock::now();
                    socket.reset();
                    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(750));
                    continue;
                }
                size_t received = 0;
                while(std::chrono::steady_clock::now() < deadline) {
                    if(peer.wait(10, Socket::WAIT_READ) & Socket::WAIT_READ) {
                        const int count = peer.read(wire.data(), wire.size());
                        if(count == 0) break;
                        if(count > 0) received += count;
                    }
                    if(listener.completed && received == observed->wireBytes) break;
                }
                CHECK(listener.completed.load());
                CHECK_FALSE(listener.pendingAtCompletion.load());
                CHECK(received == observed->wireBytes.load());
            }
        }
    }
}

TEST_CASE("Idle buffered sockets wake for tasks without timed polling", "[socket-wake][socket-output]") {
    test::TestContext context;
    Socket acceptor;
    acceptor.create(Socket::TYPE_TCP, AF_INET);
    acceptor.bind("0", "127.0.0.1");
    acceptor.listen();
    struct CountingSocket : Socket {
        std::atomic<int> waits{0};
        int wait(uint32_t millis, int flags) override {
            if(flags & WAIT_READ) ++waits;
            return Socket::wait(millis, flags);
        }
    };
    struct Listener : BufferedSocketListener {
        std::mutex mutex;
        std::condition_variable changed;
        bool connected = false;
        int updates = 0;
        void on(Connected) override {
            std::lock_guard lock(mutex); connected = true; changed.notify_all();
        }
        void on(Updated) override {
            std::lock_guard lock(mutex); ++updates; changed.notify_all();
        }
    } listener;
    auto cleanup = [&](BufferedSocket* socket) {
        BufferedSocket::putSocket(socket);
        BufferedSocket::waitShutdown();
    };
    std::unique_ptr<BufferedSocket, decltype(cleanup)> socket(
        BufferedSocket::getSocket('\n', *context.ownedCtx), cleanup);
    socket->addListener(&listener);
    auto transport = std::make_unique<CountingSocket>();
    auto* counted = transport.get();
    transport->create(Socket::TYPE_TCP, AF_INET);
    socket->setSocket(std::move(transport));
    {
        Lock lock(socket->cs);
        socket->addTask(BufferedSocket::CONNECT, new BufferedSocket::ConnectInfo(
            "127.0.0.1", acceptor.getLocalPort(), "", BufferedSocket::NAT_NONE, false));
    }
    REQUIRE(acceptor.wait(5000, Socket::WAIT_READ) == Socket::WAIT_READ);
    Socket peer;
    peer.accept(acceptor);
    {
        std::unique_lock lock(listener.mutex);
        REQUIRE(listener.changed.wait_for(lock, std::chrono::seconds(5), [&] { return listener.connected; }));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int before = counted->waits.load();
    const auto nativeBefore = counted->getNativeWaitCount();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const int idleWaits = counted->waits.load() - before;
    const auto idleNativeWaits = counted->getNativeWaitCount() - nativeBefore;
    std::vector<double> latency;
    for(int i = 1; i <= 12; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto start = std::chrono::steady_clock::now();
        socket->updated();
        std::unique_lock lock(listener.mutex);
        REQUIRE(listener.changed.wait_for(lock, std::chrono::seconds(5), [&] { return listener.updates >= i; }));
        latency.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(latency.begin(), latency.end());
    std::cout << "[socket-wake] idle wait reentries/1.1s=" << idleWaits
              << " native wait entries=" << idleNativeWaits
              << " update latency ms min/median/p95/max=" << latency.front() << "/"
              << latency[latency.size()/2] << "/" << latency.back() << "/" << latency.back() << '\n';
    CHECK(idleWaits == 0);
    CHECK(nativeBefore > 0);
    CHECK(idleNativeWaits == 0);
}

TEST_CASE("BufferedSocket: data mode stops when the current data block is exhausted", "[socket]") {
    REQUIRE_FALSE(BufferedSocket::dataModeCanConsumeMore(0, 7));
    REQUIRE(BufferedSocket::dataModeCanConsumeMore(1, 7));
    REQUIRE_FALSE(BufferedSocket::dataModeCanConsumeMore(1, 0));
}

namespace {
struct InputFixture {
    SocketInputStream input;
    char separator = '\n';
    size_t limit = 16;
    vector<string> lines;
    string data;
    int modeChanges = 0;
    bool active = true;
    std::function<void(const string&)> onLine;
    std::function<void(uint8_t*, size_t)> onData;
    SocketInputStream::Handlers handlers{
        [this](const string& text) { lines.push_back(text); if(onLine) onLine(text); },
        [this](uint8_t* bytes, size_t size) {
            if(onData) onData(bytes, size);
            else data.append(reinterpret_cast<char*>(bytes), size);
        },
        [this] { ++modeChanges; },
        [this] { return active; }
    };
    void feed(string text) {
        input.feed(reinterpret_cast<uint8_t*>(text.data()), text.size(), separator, limit, handlers);
    }
};

string compressed(const string& text) {
    uLongf size = compressBound(text.size());
    string result(size, '\0');
    REQUIRE(compress2(reinterpret_cast<Bytef*>(result.data()), &size,
        reinterpret_cast<const Bytef*>(text.data()), text.size(), Z_DEFAULT_COMPRESSION) == Z_OK);
    result.resize(size);
    return result;
}
}

TEST_CASE("Socket input retains fragments and detects both command delimiters", "[socket-input]") {
    for(char delimiter : {'\n', '|'}) {
        InputFixture f;
        f.separator = 0;
        f.feed(delimiter == '|' ? "$one" : "one");
        CHECK(f.lines.empty());
        f.feed(string(1, delimiter) + string(1, delimiter) + "two" + delimiter + "par");
        f.feed("tial" + string(1, delimiter));
        CHECK(f.lines == vector<string>{delimiter == '|' ? "$one" : "one", "two", "partial"});
    }
}

TEST_CASE("Socket command limits apply before dispatch and per command", "[socket-input]") {
    InputFixture f;
    f.limit = 4;
    SECTION("Complete boundary") {
        f.feed("abcd\nxy\n\n1234\n");
        CHECK(f.lines == vector<string>{"abcd", "xy", "1234"});
        CHECK_THROWS(f.feed("abcde\n"));
        CHECK(f.lines.size() == 3);
    }
    SECTION("Fragment boundary") {
        f.feed("ab"); f.feed("cd");
        CHECK(f.lines.empty());
        CHECK_THROWS(f.feed("e"));
        CHECK(f.lines.empty());
    }
    SECTION("Lowered limit") {
        f.feed("abcd"); f.limit = 3;
        CHECK_THROWS(f.feed("\n"));
        CHECK(f.lines.empty());
    }
    SECTION("Zero limit") {
        f.limit = 0;
        CHECK_NOTHROW(f.feed("\n\n"));
        CHECK_THROWS(f.feed("a\n"));
    }
}

TEST_CASE("Socket compressed commands use the same bounded consumer", "[socket-input]") {
    InputFixture f;
    f.limit = 4;
    f.input.setMode(SocketInputStream::MODE_ZPIPE);
    SECTION("Ordinary commands plus raw tail") {
        f.feed(compressed("ab\n1234\npart") + "\nraw\n");
        CHECK(f.lines == vector<string>{"ab", "1234", "part", "raw"});
        CHECK(f.input.getMode() == SocketInputStream::MODE_LINE);
    }
    SECTION("Bound before dispatch") {
        CHECK_THROWS(f.feed(compressed("abcde\n")));
        CHECK(f.lines.empty());
    }
    SECTION("Bound unfinished command") {
        CHECK_THROWS(f.feed(compressed("abcde")));
        CHECK(f.lines.empty());
    }
}

TEST_CASE("Socket compression handles fragmented headers footer and output draining", "[socket-input]") {
    for(size_t step : {size_t(1), size_t(7), size_t(4096)}) {
        InputFixture f;
        f.limit = 2;
        f.input.setMode(SocketInputStream::MODE_ZPIPE);
        string plain;
        for(int i = 0; i < 1600; ++i) plain += "ab\n";
        const auto wire = compressed(plain) + "ok\n";
        for(size_t pos = 0; pos < wire.size(); pos += step) f.feed(wire.substr(pos, step));
        REQUIRE(f.lines.size() == 1601);
        CHECK(std::count(f.lines.begin(), f.lines.end(), "ab") == 1600);
        CHECK(f.lines.back() == "ok");
    }
}

TEST_CASE("Socket input preserves callback compression boundaries", "[socket-input]") {
    InputFixture f;
    f.onLine = [&](const string& line) {
        if(line == "ZON") f.input.setMode(SocketInputStream::MODE_ZPIPE);
        if(line == "ZOF") f.input.setMode(SocketInputStream::MODE_LINE);
    };
    const auto first = compressed("one\nZOF\n");
    f.feed("ZON\n" + first.substr(0, first.size() - 2));
    f.feed(first.substr(first.size() - 2) + "two\nZON\n" + compressed("three\n") + "four\n");
    CHECK(f.lines == vector<string>{"ZON", "one", "ZOF", "two", "ZON", "three", "four"});
}

TEST_CASE("Socket inflater empty drain is not mistaken for stream end", "[socket-input]") {
    InputFixture f;
    f.limit = 1023;
    f.input.setMode(SocketInputStream::MODE_ZPIPE);
    const string text(1023, 'a');
    const auto wire = compressed(text + "\n");
    f.feed(wire.substr(0, wire.size() - 4));
    CHECK(f.lines == vector<string>{text});
    CHECK(f.input.getMode() == SocketInputStream::MODE_ZPIPE);
    f.feed(wire.substr(wire.size() - 4) + "ok\n");
    CHECK(f.lines == vector<string>{text, "ok"});
    CHECK(f.input.getMode() == SocketInputStream::MODE_LINE);
}

TEST_CASE("Socket data mode excludes payloads from command limits", "[socket-input]") {
    InputFixture f;
    f.limit = 4;
    const string payload("a\n\0b|0123456789", 15);
    f.onLine = [&](const string& line) {
        if(line == "data") f.input.setDataMode(payload.size());
    };
    f.feed("da");
    f.feed("ta\n" + payload.substr(0, 3));
    f.feed(payload.substr(3) + "done\n");
    CHECK(f.lines == vector<string>{"data", "done"});
    CHECK(f.data == payload);
    CHECK(f.modeChanges == 1);
}

TEST_CASE("Socket data mode supports zero bytes and unknown-length rollback", "[socket-input]") {
    InputFixture f;
    SECTION("Zero length") {
        f.onLine = [&](const string&) { if(f.lines.size() == 1) f.input.setDataMode(0); };
        f.feed("zero\nnext\n");
        CHECK(f.lines == vector<string>{"zero", "next"});
        CHECK(f.data.empty());
        CHECK(f.modeChanges == 1);
    }
    SECTION("Rollback") {
        f.input.setDataMode();
        f.onData = [&](uint8_t* bytes, size_t size) {
            f.data.append(reinterpret_cast<char*>(bytes), 3);
            f.input.setMode(SocketInputStream::MODE_LINE, size - 3);
        };
        f.feed("binrest\n");
        CHECK(f.data == "bin");
        CHECK(f.lines == vector<string>{"rest"});
    }
    SECTION("Invalid rollback is rejected") {
        f.input.setDataMode();
        f.onData = [&](uint8_t*, size_t size) { f.input.setMode(SocketInputStream::MODE_LINE, size + 1); };
        CHECK_THROWS(f.feed("bin"));
    }
}

TEST_CASE("Socket decoded payloads retain compressed framing", "[socket-input]") {
    InputFixture f;
    f.limit = 4;
    f.input.setMode(SocketInputStream::MODE_ZPIPE);
    f.onLine = [&](const string& text) { if(text == "data") f.input.setDataMode(6); };
    f.feed(compressed("data\n123456done\n") + "raw\n");
    CHECK(f.data == "123456");
    CHECK(f.lines == vector<string>{"data", "done", "raw"});
}

TEST_CASE("Socket completion callbacks can begin compression in the same input chunk", "[socket-input]") {
    InputFixture f;
    f.onLine = [&](const string& text) { if(text == "data") f.input.setDataMode(3); };
    f.handlers.modeChange = [&] {
        ++f.modeChanges;
        f.input.setMode(SocketInputStream::MODE_ZPIPE);
    };
    f.feed("data\nabc" + compressed("after\n"));
    CHECK(f.data == "abc");
    CHECK(f.lines == vector<string>{"data", "after"});
    CHECK(f.modeChanges == 1);
}

TEST_CASE("Socket callbacks stop further input dispatch on disconnect", "[socket-input]") {
    for(bool zipped : {false, true}) {
        InputFixture f;
        f.onLine = [&](const string&) { f.active = false; };
        if(zipped) f.input.setMode(SocketInputStream::MODE_ZPIPE);
        f.feed(zipped ? compressed("one\ntwo\n") : "one\ntwo\n");
        CHECK(f.lines == vector<string>{"one"});
    }
}

TEST_CASE("Socket data callbacks stop dispatch without emitting a completion", "[socket-input]") {
    for(int64_t count : {int64_t(-1), int64_t(3)}) {
        InputFixture f;
        f.input.setDataMode(count);
        f.onData = [&](uint8_t*, size_t) { f.active = false; };
        f.feed("binrest\n");
        CHECK(f.lines.empty());
        CHECK(f.modeChanges == 0);
    }
}

TEST_CASE("Buffered socket output coalesces wake tasks without crossing barriers", "[socket-output]") {
    test::TestContext context;
    context.ownedCtx->getSettingsManager()->set(SettingsManager::USE_IPV6, false);
    Socket acceptor;
    acceptor.create(Socket::TYPE_TCP, AF_INET);
    acceptor.bind("0", "127.0.0.1");
    acceptor.listen();
    struct Listener : BufferedSocketListener {
        std::mutex mutex;
        std::condition_variable condition;
        bool entered = false, released = false;
        std::atomic<int> updates{0};
        void on(Connected) override {
            std::unique_lock lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return released; });
        }
        void on(Updated) override { ++updates; }
        void release() {
            std::lock_guard lock(mutex);
            released = true;
            condition.notify_all();
        }
    } listener;
    auto cleanup = [&](BufferedSocket* socket) {
        listener.release();
        BufferedSocket::putSocket(socket);
        BufferedSocket::waitShutdown();
    };
    std::unique_ptr<BufferedSocket, decltype(cleanup)> socket(
        BufferedSocket::getSocket('\n', *context.ownedCtx), cleanup);
    socket->addListener(&listener);
    socket->connect("127.0.0.1", acceptor.getLocalPort(), false, false, false, Socket::PROTO_DEFAULT);
    {
        std::unique_lock lock(listener.mutex);
        REQUIRE(listener.condition.wait_for(lock, std::chrono::seconds(5), [&] { return listener.entered; }));
    }
    Socket peer;
    REQUIRE(acceptor.wait(5000, Socket::WAIT_READ) == Socket::WAIT_READ);
    peer.accept(acceptor);
    socket->write("", 0);
    {
        Lock lock(socket->cs);
        CHECK(socket->tasks.empty());
    }
    socket->updated(); socket->updated();
    socket->write("abc"); socket->write("def");
    socket->updated(); socket->updated();
    {
        Lock lock(socket->cs);
        REQUIRE(socket->tasks.size() == 3);
        CHECK(socket->tasks[0].first == BufferedSocket::UPDATED);
        CHECK(socket->tasks[1].first == BufferedSocket::SEND_DATA);
        CHECK(socket->tasks[2].first == BufferedSocket::UPDATED);
        CHECK(string(socket->writeBuf.begin(), socket->writeBuf.end()) == "abcdef");
    }
    listener.release();
    char bytes[6];
    REQUIRE(peer.readAll(bytes, sizeof(bytes), 5000) == sizeof(bytes));
    CHECK(string(bytes, sizeof(bytes)) == "abcdef");
    for(int n = 0; n < 100 && listener.updates < 2; ++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(listener.updates == 2);
}

TEST_CASE("Buffered socket write sizes fit the transport integer API", "[socket-output-size]") {
    const auto limit = std::numeric_limits<int>::max();
    CHECK(BufferedSocket::writeChunkSize(0) == 0);
    CHECK(BufferedSocket::writeChunkSize(6) == 6);
    CHECK(BufferedSocket::writeChunkSize(limit) == limit);
    CHECK(BufferedSocket::writeChunkSize(size_t(limit) + 1) == limit);
    CHECK(BufferedSocket::writeChunkSize(std::numeric_limits<size_t>::max()) == limit);
}

TEST_CASE("Buffered socket graceful disconnect stops input but drains queued output", "[socket-output]") {
    test::TestContext context;
    context.ownedCtx->getSettingsManager()->set(SettingsManager::USE_IPV6, false);
    Socket acceptor;
    acceptor.create(Socket::TYPE_TCP, AF_INET);
    acceptor.bind("0", "127.0.0.1");
    acceptor.listen();
    struct Listener : BufferedSocketListener {
        BufferedSocket* socket = nullptr;
        std::mutex mutex;
        std::condition_variable condition;
        bool entered = false, released = false;
        std::atomic<int> lines{0};
        std::atomic<bool> failed{false};
        void on(Connected) override {
            std::unique_lock lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return released; });
        }
        void on(Line, const string&) override {
            if(++lines == 1) {
                socket->write("bye\n");
                socket->disconnect();
            }
        }
        void on(Failed, const string&) override { failed = true; }
        void release() {
            std::lock_guard lock(mutex);
            released = true;
            condition.notify_all();
        }
    } listener;
    auto cleanup = [&](BufferedSocket* socket) {
        listener.release();
        BufferedSocket::putSocket(socket);
        BufferedSocket::waitShutdown();
    };
    std::unique_ptr<BufferedSocket, decltype(cleanup)> socket(
        BufferedSocket::getSocket('\n', *context.ownedCtx), cleanup);
    listener.socket = socket.get();
    socket->addListener(&listener);
    socket->connect("127.0.0.1", acceptor.getLocalPort(), false, false, false, Socket::PROTO_DEFAULT);
    {
        std::unique_lock lock(listener.mutex);
        REQUIRE(listener.condition.wait_for(lock, std::chrono::seconds(5), [&] { return listener.entered; }));
    }
    REQUIRE(acceptor.wait(5000, Socket::WAIT_READ) == Socket::WAIT_READ);
    Socket peer;
    peer.accept(acceptor);
    peer.writeAll("first\nsecond\n", 13, 5000);
    listener.release();
    char bytes[4];
    REQUIRE(peer.readAll(bytes, sizeof(bytes), 5000) == sizeof(bytes));
    CHECK(string(bytes, sizeof(bytes)) == "bye\n");
    for(int n = 0; n < 100 && !listener.failed; ++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(listener.failed);
    CHECK(listener.lines == 1);
}

TEST_CASE("Buffered file sends preserve queued tasks and can be interrupted under backpressure", "[socket-wake][socket-output]") {
    test::TestContext context;
    // Minimal contexts omit transfer managers; assemble the real file-send path
    // without application startup, real listeners or saved profile settings.
    context.ownedCtx->timerManager_ = std::make_unique<TimerManager>(*context.ownedCtx);
    context.ownedCtx->clientManager_ = std::make_unique<ClientManager>(*context.ownedCtx);
    context.ownedCtx->uploadManager_ = std::make_unique<UploadManager>(*context.ownedCtx);
    context.ownedCtx->throttleManager_ = std::make_unique<ThrottleManager>(*context.ownedCtx);
    context.ownedCtx->getSettingsManager()->set(SettingsManager::USE_IPV6, false);
    context.ownedCtx->getSettingsManager()->set(SettingsManager::SOCKET_OUT_BUFFER, 4096);
    Socket acceptor;
    acceptor.create(Socket::TYPE_TCP, AF_INET);
    acceptor.bind("0", "127.0.0.1");
    acceptor.listen();
    struct Listener : BufferedSocketListener {
        std::mutex mutex;
        std::condition_variable changed;
        bool connected = false, released = false, updated = false, wrote = false;
        void on(Connected) override {
            std::unique_lock lock(mutex);
            connected = true; changed.notify_all();
            changed.wait(lock, [&] { return released; });
        }
        void on(Updated) override { std::lock_guard lock(mutex); updated = true; changed.notify_all(); }
        void on(BytesSent, size_t, size_t sent) override {
            std::lock_guard lock(mutex); wrote |= sent > 0; changed.notify_all();
        }
        void release() { std::lock_guard lock(mutex); released = true; changed.notify_all(); }
    } listener;
    const string payload(2 * 1024 * 1024, 'f');
    MemoryInputStream input(payload);
    auto cleanup = [&](BufferedSocket* socket) {
        listener.release();
        BufferedSocket::putSocket(socket);
        BufferedSocket::waitShutdown();
    };
    std::unique_ptr<BufferedSocket, decltype(cleanup)> socket(
        BufferedSocket::getSocket('\n', *context.ownedCtx), cleanup);
    socket->addListener(&listener);
    socket->connect("127.0.0.1", acceptor.getLocalPort(), false, false, false, Socket::PROTO_DEFAULT);
    {
        std::unique_lock lock(listener.mutex);
        REQUIRE(listener.changed.wait_for(lock, std::chrono::seconds(5), [&] { return listener.connected; }));
    }
    REQUIRE(acceptor.wait(5000, Socket::WAIT_READ) == Socket::WAIT_READ);
    Socket peer;
    peer.accept(acceptor);
    socket->transmitFile(&input);
    socket->updated();
    listener.release();
    SECTION("A file send must not strand the following update") {
        string received(payload.size(), '\0');
        REQUIRE(peer.readAll(received.data(), received.size(), 5000) == int(received.size()));
        CHECK(received == payload);
        std::unique_lock lock(listener.mutex);
        REQUIRE(listener.changed.wait_for(lock, std::chrono::seconds(5), [&] { return listener.updated; }));
    }
    SECTION("Shutdown wakes a file sender even when its peer does not read") {
        std::unique_lock lock(listener.mutex);
        REQUIRE(listener.changed.wait_for(lock, std::chrono::seconds(5), [&] { return listener.wrote; }));
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        socket.reset();
        CHECK(BufferedSocket::sockets == 0);
    }
}
