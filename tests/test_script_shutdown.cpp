#include <catch2/catch_test_macros.hpp>
#include "stdinc.h"
#ifdef LUA_SCRIPT
#include <future>
#include <ranges>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#define private public
#define protected public
#include "DCContext.h"
#include "ScriptManager.h"
#include "ClientManager.h"
#include "Client.h"
#include "UserConnection.h"
#include "NmdcHub.h"
#undef protected
#undef private
#include "TestContext.h"
#include "File.h"
#include "ScopedFunctor.h"
#include "Semaphore.h"

using namespace dcpp;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
template<class T> void stopScript(T& manager) {
    if constexpr(requires { manager.stopForShutdown(); }) manager.stopForShutdown();
}
struct Deadline {
    Semaphore done;
    std::thread watchdog{[&] {
        if(!done.wait(15000)) { std::fputs("Lua shutdown fixture exceeded 15 seconds\n", stderr); std::_Exit(1); }
    }};
    ~Deadline() { done.signal(); watchdog.join(); }
};
struct Fixture {
    Deadline deadline;
    test::TestContext fixture;
    Fixture(bool load = true) {
        REQUIRE(fs::equivalent(fs::u8path(Util::getPath(Util::PATH_USER_CONFIG)), fixture.tmpDir));
        auto& c = context();
        c.timerManager_ = std::make_unique<TimerManager>(c);
        c.clientManager_ = std::make_unique<ClientManager>(c);
        c.scriptManager_ = std::make_unique<ScriptManager>(c);
        if(load) script().load();
    }
    ~Fixture() {
        context().scriptManager_.reset();
        // Keep the legacy RED destructor's dangling global from poisoning the
        // next fixture; tests inspect it before this safety-only cleanup.
        ScriptInstance::L = nullptr;
    }
    DCContext& context() { return *fixture.ownedCtx; }
    ScriptManager& script() { return *context().getScriptManager(); }
    lua_Integer value(const char* name) {
        Lock lock(ScriptInstance::cs); REQUIRE(ScriptInstance::L);
        lua_getglobal(ScriptInstance::L, name);
        auto result = lua_tointeger(ScriptInstance::L, -1); lua_pop(ScriptInstance::L, 1); return result;
    }
    void file(const string& name, const string& data) {
        File out((fixture.tmpDir / name).string(), File::WRITE, File::CREATE | File::TRUNCATE);
        out.write(data); out.flush();
    }
};
struct BlockingLua {
    Semaphore entered, release;
    ScriptManager* manager = nullptr;
    static int call(lua_State* state) {
        auto& self = *static_cast<BlockingLua*>(lua_touserdata(state, lua_upvalueindex(1)));
        self.entered.signal(); self.release.wait();
        self.manager->EvaluateChunk("nested = 17");
        return 0;
    }
    void install(Fixture& f) {
        manager = &f.script(); Lock lock(ScriptInstance::cs);
        lua_pushlightuserdata(ScriptInstance::L, this);
        lua_pushcclosure(ScriptInstance::L, call, 1); lua_setglobal(ScriptInstance::L, "blocked");
    }
};
}

TEST_CASE("Terminal Lua stop rejects file chunk and shared peer hub chat hooks", "[script-stop][shutdown]") {
    Fixture f;
    f.script().EvaluateChunk("dcpp={}; calls=0; dcpp.OnCommandEnter=function() calls=calls+1; return true end; "
        "dcpp.UserDataIn=dcpp.OnCommandEnter; dcpp.UserDataOut=dcpp.OnCommandEnter; "
        "nmdch={DataArrival=dcpp.OnCommandEnter}; "
        "dcpp.FormatChatText=function() calls=calls+1; return 'scripted' end");
    ClientScriptInstance chat; UserConnectionScriptInstance peer; NmdcHubScriptInstance hub;
    CHECK(chat.onHubFrameEnter(nullptr, "before"));
    CHECK(peer.onUserConnectionMessageIn(nullptr, "before"));
    CHECK(hub.onClientMessage(nullptr, "before"));
    CHECK(chat.formatChatMessage("before") == "scripted");
    REQUIRE(f.value("calls") == 4);
    auto* state = ScriptInstance::L;
    stopScript(f.script());
    CHECK(ScriptInstance::L == state);
    f.file("late.lua", "calls=calls+100");
    f.script().EvaluateFile((f.fixture.tmpDir / "late.lua").string());
    f.script().EvaluateChunk("calls=calls+1000");
    CHECK_FALSE(chat.onHubFrameEnter(nullptr, "after"));
    CHECK_FALSE(peer.onUserConnectionMessageIn(nullptr, "after"));
    CHECK_FALSE(peer.onUserConnectionMessageOut(nullptr, "after"));
    CHECK_FALSE(hub.onClientMessage(nullptr, "after"));
    CHECK(chat.formatChatMessage("plain {text}") == "plain {text}");
    CHECK(f.value("calls") == 4);
    stopScript(f.script()); f.script().load();
    CHECK(ScriptInstance::L == state); CHECK(f.value("calls") == 4);
}

TEST_CASE("Lua default chat formatter remains usable on the configured Lua runtime", "[script-stop][shutdown]") {
    Fixture f;
    ClientScriptInstance chat;
    const auto formatted = chat.formatChatMessage("plain {text}");
    CHECK(formatted.starts_with("{\\urtf1"));
    CHECK(formatted.find("\\{text\\}") != string::npos);
    auto* state = ScriptInstance::L;
    f.script().load(); CHECK(ScriptInstance::L == state);
    stopScript(f.script());
    CHECK(chat.formatChatMessage("plain {text}") == "plain {text}");
}

TEST_CASE("Terminal Lua stop drains admitted nested work and concurrent stops", "[script-stop][shutdown]") {
    Fixture f; BlockingLua block; block.install(f);
    auto active = std::async(std::launch::async, [&] { f.script().EvaluateChunk("blocked(); completed=23"); });
    REQUIRE(block.entered.wait(3000));
    auto unblock = makeScopedFunctor([&] { block.release.signal(); if(active.valid()) active.wait(); });
    Semaphore starting;
    auto first = std::async(std::launch::async, [&] { starting.signal(); stopScript(f.script()); });
    REQUIRE(starting.wait(3000)); CHECK(first.wait_for(60ms) == std::future_status::timeout);
    auto second = std::async(std::launch::async, [&] { stopScript(f.script()); });
    CHECK(second.wait_for(60ms) == std::future_status::timeout);
    auto late = std::async(std::launch::async, [&] { f.script().EvaluateChunk("late=31"); });
    CHECK(late.wait_for(60ms) == std::future_status::ready);
    block.release.signal(); active.get(); first.get(); second.get(); late.get();
    CHECK(f.value("completed") == 23); CHECK(f.value("nested") == 17); CHECK(f.value("late") == 0);
}

TEST_CASE("Lua timer remains reusable before terminal detach", "[script-stop][shutdown]") {
    Fixture f;
    f.script().EvaluateChunk("dcpp={}; dc=DC(); dc:RunTimer(1); dc:RunTimer(0); dc:RunTimer(1); "
        "ticks=0; dcpp.OnTimer=function() ticks=ticks+1 end; nmdch={OnHubAdded=function() ticks=ticks+10 end}");
    REQUIRE(f.script().getTimerEnabled());
    f.context().getTimerManager()->fire(TimerManagerListener::Second(), 0);
    f.context().getClientManager()->fire(ClientManagerListener::ClientConnected(), nullptr);
    REQUIRE(f.value("ticks") == 11);
    stopScript(f.script()); CHECK_FALSE(f.script().getTimerEnabled());
    f.context().getTimerManager()->fire(TimerManagerListener::Second(), 0);
    f.context().getClientManager()->fire(ClientManagerListener::ClientConnected(), nullptr);
    f.script().EvaluateChunk("dc:RunTimer(1)");
    CHECK_FALSE(f.script().getTimerEnabled()); CHECK(f.value("ticks") == 11);
    auto* clientListener = static_cast<ClientManagerListener*>(&f.script());
    CHECK(std::ranges::find(f.context().getClientManager()->listeners, clientListener) ==
        f.context().getClientManager()->listeners.end());
}

TEST_CASE("Terminal Lua stop drains a timer snapshot without holding Lua or listener locks", "[script-stop][shutdown]") {
    Fixture f; BlockingLua block; block.install(f);
    f.script().EvaluateChunk("dcpp={OnTimer=blocked}; dc=DC(); dc:RunTimer(1)");
    auto timer = std::async(std::launch::async, [&] {
        f.context().getTimerManager()->fire(TimerManagerListener::Second(), 0);
    });
    REQUIRE(block.entered.wait(3000));
    auto unblock = makeScopedFunctor([&] { block.release.signal(); if(timer.valid()) timer.wait(); });
    auto stopping = std::async(std::launch::async, [&] { stopScript(f.script()); });
    CHECK(stopping.wait_for(60ms) == std::future_status::timeout);
    auto late = std::async(std::launch::async, [&] { f.script().EvaluateChunk("late=31"); });
    CHECK(late.wait_for(60ms) == std::future_status::ready);
    block.release.signal(); timer.get(); stopping.get(); late.get();
    CHECK(f.value("nested") == 17); CHECK(f.value("late") == 0);
    CHECK_FALSE(f.script().getTimerEnabled());
    f.context().getTimerManager()->fire(TimerManagerListener::Second(), 0);
}

TEST_CASE("Terminal unloaded Lua manager cannot initialize or stop foreign runtime", "[script-stop][shutdown]") {
    SECTION("stop before first load") {
        Fixture f(false); stopScript(f.script()); f.script().load(); CHECK(ScriptInstance::L == nullptr);
    }
    SECTION("unloaded manager leaves loaded runtime usable") {
        Fixture f; ScriptManager unused(f.context());
        REQUIRE_THROWS_AS(unused.load(), std::runtime_error);
        stopScript(unused);
        f.script().EvaluateChunk("stillRunning=7"); CHECK(f.value("stillRunning") == 7);
    }
}

TEST_CASE("Lua destruction detaches clears shared state and prevents finalizer revival", "[script-stop][shutdown]") {
    Fixture f;
    f.script().EvaluateChunk("dc=DC(); finalizer=setmetatable({}, {__gc=function() dc:RunTimer(1) end})");
    auto* timerListener = static_cast<TimerManagerListener*>(&f.script());
    auto* clientListener = static_cast<ClientManagerListener*>(&f.script());
    f.context().scriptManager_.reset();
    CHECK(ScriptInstance::L == nullptr);
    CHECK(std::ranges::find(f.context().getTimerManager()->listeners, timerListener) ==
        f.context().getTimerManager()->listeners.end());
    CHECK(std::ranges::find(f.context().getClientManager()->listeners, clientListener) ==
        f.context().getClientManager()->listeners.end());
    // Clear the legacy global before Fixture's idempotent teardown.
    ScriptInstance::L = nullptr;
}
#endif
