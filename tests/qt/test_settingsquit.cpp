#include "dcpp/stdinc.h"
#include "ApplicationQuitTestFixture.h"
#include "updates/ApplicationQuitCoordinator.h"
#include "updates/ApplicationQuitSession.h"
#include "updates/UpdateController.h"
#include "SearchBlacklist.h"
#include "Antispam.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QTimer>
#include <QThread>
#include <atomic>
#include <future>

namespace {
enum class SaveResult { Missing, Pending, Ready, Refused };
template<class Store> constexpr bool hasQuitSave() {
    return requires(Store& store) { store.saveForQuitChecked(true); };
}
template<class Store> SaveResult quitSave(Store& store, bool asynchronous = true) {
    if constexpr(hasQuitSave<Store>()) {
        const auto result = store.saveForQuitChecked(asynchronous);
        if(result == Store::QuitSaveStatus::Ready) return SaveResult::Ready;
        if(result == Store::QuitSaveStatus::Pending) return SaveResult::Pending;
        return SaveResult::Refused;
    } else {
        store.freezeForQuit();
        return store.saveChecked() ? SaveResult::Ready : SaveResult::Refused;
    }
}
}

TEST_CASE("Frozen settings preserve types and normalized reads without accepting late mutations", "[qt][settings-quit][gui-settings-freeze]") {
    IsolatedMainWindow fixture;
    auto& store = *fixture.gui->settings();
    store.setStr("/qa//value/", "before");
    store.setInt("qa/int", 42);
    store.setBool("qa/bool", true);
    store.setVar("qa/bytes", QByteArray("\0x", 2));
    store.setVar("qa/list", QStringList{"a", "b"});
    store.setVar("qa/invalid", QVariant());
    store.setStr("qa/empty", "");
    store.setStr(QString::fromUtf8("qa/\xc3\xa9"), "unicode");
    store.setStr("qa/Case", "upper");
    store.setStr(WS_APP_THEME, "frozen-theme");
    CHECK_FALSE(store.hasKey("qa/value"));
    const auto lowerCase = store.getStr("qa/case", "missing");
    store.freezeForQuit();
    store.freezeForQuit();
    store.setStr("qa/value", "late");
    store.setInt("qa/int", 9);
    store.setBool("qa/bool", false);
    store.setVar("qa/bytes", "late");
    store.load();
    store.loadTheme();
    CHECK(store.getStr("qa/value") == "before");
    CHECK(store.getStr("/qa//value/") == "before");
    CHECK(store.getInt("qa/int") == 42);
    CHECK(store.getBool("qa/bool"));
    CHECK(store.getVar("qa/bytes").toByteArray() == QByteArray("\0x", 2));
    CHECK(store.getVar("qa/list").toStringList() == QStringList{"a", "b"});
    CHECK_FALSE(store.getVar("qa/invalid", "fallback").isValid());
    CHECK(store.getStr("qa/empty", "fallback").isEmpty());
    CHECK(store.getStr("qa/missing", "fallback") == "fallback");
    CHECK(store.getInt("qa/missing-int", -7) == -7);
    CHECK(store.getBool("qa/missing-bool", true));
    CHECK(store.getStr(QString::fromUtf8("qa/\xc3\xa9")) == "unicode");
    CHECK(store.getStr("qa/case", "missing") == lowerCase);
    CHECK(store.getStr(WS_APP_THEME) == "frozen-theme");
    CHECK_FALSE(store.hasKey("qa/value"));
    CHECK(hasQuitSave<WulforSettings>());
    CHECK(quitSave(store, false) == SaveResult::Ready);
}

#ifdef __APPLE__
#include "../macos/updater/CoreWriteProbe.h"
TEST_CASE("Owned settings write and flush keep frozen readers and the GUI responsive", "[.gui-settings-storage-retirement]") {
    IsolatedMainWindow fixture;
    auto& store = *fixture.gui->settings();
    int mode = 1;
    SECTION("physical write") {}
    SECTION("physical flush") { mode = 3; }
    REQUIRE(store.saveChecked());
    store.setStr("qa/storage-probe", "frozen");
    store.setVar("qa/bytes", QByteArray("\0x", 2));
    store.freezeForQuit();
    CoreWriteProbe probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; });
    heartbeat.start(1);
    const auto config = fixture.home.filePath("EiskaltDC++_Qt.conf");
    probe.arm(QFile::encodeName(config).constData(), mode);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start();
    const auto initial = quitSave(store);
    CHECK(hasQuitSave<WulforSettings>());
    CHECK(elapsed.elapsed() < 100);
    CHECK(initial == SaveResult::Pending);
    REQUIRE(waitFor([&] { return probe.entered(); }));
    CHECK(probe.writer() != probe.gui);
    if(!probe.released()) {
        CHECK(quitSave(store) == SaveResult::Pending);
        CHECK_FALSE(store.saveChecked());
    }
    CHECK(store.getStr("qa/storage-probe") == "frozen");
    CHECK(store.getVar("qa/bytes").toByteArray() == QByteArray("\0x", 2));
    REQUIRE(waitFor([&] { return probe.released(); }));
    REQUIRE(watchdog.get()); CHECK(advanced.load());
    REQUIRE(waitFor([&] { return quitSave(store) == SaveResult::Ready; }));
    QFile physical(config); REQUIRE(physical.open(QIODevice::ReadOnly));
    CHECK(physical.readAll().contains("storage-probe=frozen"));
    physical.close();
    const QByteArray marker("[qa]\nstorage-probe=physical-marker\n");
    REQUIRE(physical.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(physical.write(marker) == marker.size()); physical.close();
    CHECK(quitSave(store) == SaveResult::Ready);
    CHECK(store.saveChecked());
    QCoreApplication::processEvents();
    REQUIRE(physical.open(QIODevice::ReadOnly)); CHECK(physical.readAll() == marker);
}
#endif

#ifdef __APPLE__
TEST_CASE("Held GUI settings quit writes preserve event-loop progress and refuse late exit", "[.gui-settings-write-retirement]") {
    IsolatedMainWindow fixture;
    int mode = 1;
    SECTION("settings write") {}
    SECTION("settings flush") { mode = 3; }
    SECTION("settings thread exit") { mode = 6; }
    REQUIRE(waitFor([&] {
        return fixture.core->prepareShutdownAsyncChecked(3000).status != dcpp::DCContext::ShutdownStatus::Pending;
    }));
    REQUIRE(fixture.core->prepareShutdownAsyncChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready);
    REQUIRE(fixture.gui->settings()->saveChecked());
    fixture.gui->settings()->setStr("qa/gui-settings-probe", "frozen-value");
    REQUIRE(fixture.gui->settings()->getStr("qa/gui-settings-probe") == "frozen-value");
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    updates::ApplicationQuitSession session(*fixture.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    const auto config = fixture.home.filePath("EiskaltDC++_Qt.conf");
    probe.arm(QFile::encodeName(config).constData(), mode);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start(); session.request(100);
    const auto duration = elapsed.elapsed();
    CHECK(duration < 100);
    REQUIRE(waitFor([&] { return probe.entered(); }));
    CHECK(probe.writer() != probe.gui);
    CHECK(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    REQUIRE(watchdog.get()); CHECK(advanced.load());
    fixture.gui->settings()->setStr("qa/gui-settings-probe", "late-value");
    QTest::qWait(30); CHECK(ready.isEmpty());
    CHECK(fixture.gui->settings()->getStr("qa/gui-settings-probe") == "frozen-value");
    // Releasing a held syscall does not complete the physical save receipt.
    REQUIRE(waitFor([&] { return quitSave(*fixture.gui->settings()) != SaveResult::Pending; }));
    REQUIRE(quitSave(*fixture.gui->settings()) == SaveResult::Ready);
    QFile physical(config); REQUIRE(physical.open(QIODevice::ReadOnly));
    const auto bytes = physical.readAll();
    CHECK(bytes.contains("gui-settings-probe=frozen-value")); CHECK_FALSE(bytes.contains("late-value"));
    CHECK(backend.fences == 1); CHECK(backend.installs == 0);
    std::fprintf(stderr, "GUI_SETTINGS_IO mode=%d request_ms=%lld on_gui=%d heartbeat=%d state=%d exits=%d\n",
        mode, static_cast<long long>(duration), probe.writer() == probe.gui, advanced.load(),
        int(session.state()), int(ready.count()));
}
#endif

#ifdef __APPLE__
TEST_CASE("Held GUI settings cannot commit a ready update after the quit deadline", "[.gui-settings-ready-retirement]") {
    IsolatedMainWindow fixture;
    int mode = 1;
    SECTION("ready update settings write") {}
    SECTION("ready update settings flush") { mode = 3; }
    SECTION("ready update settings thread exit") { mode = 6; }
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    REQUIRE(waitFor([&] {
        return fixture.core->prepareShutdownAsyncChecked(3000).status != dcpp::DCContext::ShutdownStatus::Pending;
    }));
    REQUIRE(fixture.core->prepareShutdownAsyncChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready);
    REQUIRE(fixture.gui->settings()->saveChecked());
    fixture.gui->settings()->setStr("qa/gui-settings-probe", "ready-frozen-value");
    updates::ApplicationQuitSession session(*fixture.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    const auto config = fixture.home.filePath("EiskaltDC++_Qt.conf");
    probe.arm(QFile::encodeName(config).constData(), mode);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start(); session.request(100);
    const auto duration = elapsed.elapsed();
    CHECK(duration < 100);
    REQUIRE(waitFor([&] { return probe.entered(); })); CHECK(probe.writer() != probe.gui);
    REQUIRE(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(backend.installs == 0); CHECK(ready.isEmpty());
    REQUIRE(watchdog.get()); CHECK(advanced.load());
    const bool consented = backend.handoffConsent && backend.handoffConsent();
    CHECK_FALSE(consented);
    // The fake backend produces no real installer side effect. Delivering its
    // matching completion checks whether this expired request can exit later.
    if(backend.installs) emit backend.installHandoffFinished(backend.handoffOperation, true);
    QTest::qWait(30); CHECK(ready.isEmpty());
    REQUIRE(waitFor([&] { return quitSave(*fixture.gui->settings()) != SaveResult::Pending; }));
    REQUIRE(quitSave(*fixture.gui->settings()) == SaveResult::Ready);
    QFile physical(config); REQUIRE(physical.open(QIODevice::ReadOnly));
    CHECK(physical.readAll().contains("gui-settings-probe=ready-frozen-value"));
    CHECK(backend.fences == 1);
    std::fprintf(stderr, "GUI_SETTINGS_READY mode=%d request_ms=%lld on_gui=%d heartbeat=%d state=%d installs=%d consent=%d exits=%d\n",
        mode, static_cast<long long>(duration), probe.writer() == probe.gui, advanced.load(),
        int(session.state()), backend.installs, consented, int(ready.count()));
}
#endif

#include "WulforSettingsTestAccess.h"
TEST_CASE("Foreign quit requests do not poison the owner's settings receipt", "[qt][settings-quit]") {
    IsolatedMainWindow fixture;
    auto& store = *fixture.gui->settings();
    store.setStr("qa/owner", "frozen");
    auto foreign = std::async(std::launch::async, [&] { return quitSave(store); });
    CHECK(foreign.get() == SaveResult::Refused);
    CHECK(quitSave(store, false) == SaveResult::Ready);
    CHECK(store.getStr("qa/owner") == "frozen");
    CHECK_FALSE(store.getVar("", 123).isValid());
}

TEST_CASE("Parented settings storage refuses quit without resetting frozen intent", "[qt][settings-quit]") {
    IsolatedMainWindow fixture;
    auto& store = *fixture.gui->settings();
    REQUIRE(store.saveChecked());
    const auto config = fixture.home.filePath("EiskaltDC++_Qt.conf");
    QFile physical(config); REQUIRE(physical.open(QIODevice::ReadOnly));
    const auto before = physical.readAll(); physical.close();
    auto* storage = WulforSettingsTestAccess::storage(store);
    REQUIRE(storage);
    storage->setParent(&store);
    store.setStr("qa/intent", "retained");
    CHECK(quitSave(store) == SaveResult::Refused);
    store.setStr("qa/intent", "late");
    CHECK(store.getStr("qa/intent") == "retained");
    CHECK_FALSE(store.saveChecked());
    CHECK(quitSave(store) == SaveResult::Refused);
    QCoreApplication::sendPostedEvents(storage, QEvent::UpdateRequest);
    QCoreApplication::processEvents();
    REQUIRE(physical.open(QIODevice::ReadOnly));
    CHECK(physical.readAll() == before);
}

#ifdef __APPLE__
TEST_CASE("Pending GUI settings closes core admission before timeout without early handoff",
          "[.gui-settings-admission-retirement]") {
    IsolatedMainWindow fixture;
    auto& store = *fixture.gui->settings();
    REQUIRE(store.saveChecked());
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(store, backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    bool withUpdate = false;
    SECTION("ordinary quit") {}
    SECTION("ready update") { withUpdate = true; }
    if(withUpdate) {
        const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
            QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
        updater.checkNow(); emit backend.available(backend.operation, offer);
        REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
        updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
        REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
        updater.installOnNextQuit();
    }
    store.setStr("qa/admission", "frozen");
    const auto route = fixture.core->getProxyRoute()->snapshot();
    REQUIRE_FALSE(route->revoked->load());
    updates::ApplicationQuitSession session(*fixture.gui, withUpdate ? &updater : nullptr);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    const auto config = fixture.home.filePath("EiskaltDC++_Qt.conf");
    probe.arm(QFile::encodeName(config).constData(), 1);
    auto watchdog = probe.watchdog(ticks, advanced);
    session.request(100);
    CHECK(route->revoked->load());
    REQUIRE(waitFor([&] { return probe.entered(); }));
    CHECK(probe.writer() != probe.gui);
    REQUIRE(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    REQUIRE(watchdog.get()); CHECK(advanced.load());
    QTest::qWait(30); CHECK(ready.isEmpty());
    if(withUpdate) { CHECK(backend.fences == 1); CHECK(backend.routeWasRunning); }
    CHECK(fixture.core->isRunning());
    CHECK(store.getStr("qa/admission") == "frozen");
}
#endif

TEST_CASE("Malformed and previously failed settings stores stay refused after repair", "[qt][settings-quit]") {
    IsolatedMainWindow fixture;
    struct Restore {
        dcpp::Util::PathsMap paths;
        ~Restore() { dcpp::Util::uninitialize(); dcpp::Util::initialize(paths); }
    } restore;
    for(int i = 0; i < dcpp::Util::PATH_LAST; ++i) {
        const auto key = static_cast<dcpp::Util::Paths>(i);
        restore.paths[key] = dcpp::Util::getPath(key);
    }
    const auto root = fixture.home.filePath("broken-store");
    REQUIRE(QDir().mkpath(root));
    auto paths = restore.paths;
    paths[dcpp::Util::PATH_USER_CONFIG] = (root + "/").toStdString();
    dcpp::Util::uninitialize(); dcpp::Util::initialize(paths);
    const auto config = root + "/EiskaltDC++_Qt.conf";
    bool malformed = false, repaired = false;
    SECTION("unwritable destination") {}
    SECTION("sticky access error after destination repair") { repaired = true; }
    SECTION("malformed input") { malformed = true; }
    if(malformed) {
        QFile file(config); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write("[unterminated\nkey=value\n") > 0);
    } else REQUIRE(QDir().mkpath(config));
    WulforSettings store(*fixture.core);
    store.setStr("qa/intent", "retained");
    CHECK_FALSE(store.saveChecked());
    if(repaired) REQUIRE(QDir(config).removeRecursively());
    CHECK(quitSave(store) == SaveResult::Pending);
    REQUIRE(waitFor([&] { return quitSave(store) != SaveResult::Pending; }));
    CHECK(quitSave(store) == SaveResult::Refused);
    CHECK_FALSE(store.saveChecked());
    CHECK(store.getStr("qa/intent") == "retained");
    if(repaired) {
        QSettings physical(config, QSettings::IniFormat);
        CHECK(physical.value("qa/intent").toString() == "retained");
        CHECK(physical.status() == QSettings::NoError);
    }
}

#ifdef __APPLE__
TEST_CASE("Settings readiness waits for storage destruction and physical thread exit", "[.gui-settings-storage-retirement]") {
    IsolatedMainWindow fixture;
    auto& store = *fixture.gui->settings();
    REQUIRE(store.saveChecked());
    store.setStr("qa/storage-probe", "frozen");
    struct Observation {
        std::atomic_bool entered{false}, released{false}, advanced{false}, onGui{true};
        std::atomic_int ticks{0};
    };
    auto observation = std::make_shared<Observation>();
    bool tls = false;
    SECTION("storage destructor") {}
    SECTION("thread-local teardown") { tls = true; }
    CoreWriteProbe probe;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [observation] { ++observation->ticks; });
    heartbeat.start(1);
    std::future<bool> watchdog;
    if(tls) {
        probe.arm(QFile::encodeName(fixture.home.filePath("EiskaltDC++_Qt.conf")).constData(), 6);
        watchdog = probe.watchdog(observation->ticks, observation->advanced);
    } else {
        auto* storage = WulforSettingsTestAccess::storage(store);
        REQUIRE(storage);
        auto* gui = QThread::currentThread();
        QObject::connect(storage, &QObject::destroyed, &store, [observation, gui] {
            observation->onGui = QThread::currentThread() == gui;
            observation->entered = true;
            while(!observation->released) QThread::msleep(1);
        }, Qt::DirectConnection);
        watchdog = std::async(std::launch::async, [observation] {
            QElapsedTimer timer; timer.start();
            while(!observation->entered && timer.elapsed() < 3000) QThread::msleep(1);
            const bool hit = observation->entered;
            const auto before = observation->ticks.load();
            if(hit) QThread::msleep(350);
            observation->advanced = observation->ticks > before;
            observation->released = true;
            return hit;
        });
    }
    CHECK(quitSave(store) == SaveResult::Pending);
    REQUIRE(waitFor([&] { return tls ? probe.entered() : observation->entered.load(); }));
    CHECK(quitSave(store) == SaveResult::Pending);
    CHECK_FALSE(store.saveChecked());
    CHECK(store.getStr("qa/storage-probe") == "frozen");
    CHECK((tls ? probe.writer() != probe.gui : !observation->onGui));
    REQUIRE(waitFor([&] { return tls ? bool(probe.released()) : observation->released.load(); }));
    REQUIRE(watchdog.get()); CHECK(observation->advanced.load());
    REQUIRE(waitFor([&] { return quitSave(store) == SaveResult::Ready; }));
}

TEST_CASE("Failed settings worker launch retains dirty storage without a GUI flush", "[.gui-settings-storage-retirement]") {
    IsolatedMainWindow fixture;
    auto& store = *fixture.gui->settings();
    store.setStr("qa/intent", "previous");
    REQUIRE(store.saveChecked());
    QFile file(fixture.home.filePath("EiskaltDC++_Qt.conf"));
    REQUIRE(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
    store.setStr("qa/intent", "frozen"); store.freezeForQuit();
    CoreWriteProbe probe;
    probe.arm(QFile::encodeName(file.fileName()).constData(), 7);
    QElapsedTimer elapsed; elapsed.start();
    CHECK(quitSave(store) == SaveResult::Refused);
    CHECK(elapsed.elapsed() < 100);
    CHECK(probe.entered()); probe.release();
    QTest::qWait(30);
    CHECK(store.getStr("qa/intent") == "frozen");
    CHECK(quitSave(store) == SaveResult::Refused);
    CHECK_FALSE(store.saveChecked());
    REQUIRE(file.open(QIODevice::ReadOnly)); CHECK(file.readAll() == original);
}
#endif
TEST_CASE("A first Ready preparation cannot commit an expired zero-budget quit", "[qt][settings-quit][settings-first-ready]") {
    IsolatedMainWindow fixture;
    bool withUpdate = false;
    SECTION("ordinary exit") {}
    SECTION("consented update") { withUpdate = true; }
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    if(withUpdate) {
        updater.checkNow(); emit backend.available(backend.operation, offer);
        REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
        updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
        REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
        updater.installOnNextQuit();
    }
    updates::ApplicationQuitCoordinator prior(*fixture.gui);
    REQUIRE(waitFor([&] { return prior.prepare(3000).status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    REQUIRE(prior.prepare(3000).status == updates::ApplicationQuitCoordinator::Status::Ready);
    updates::ApplicationQuitSession session(*fixture.gui, withUpdate ? &updater : nullptr);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    session.request(0);
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(backend.installs == 0);
    QCoreApplication::processEvents();
    CHECK(ready.isEmpty());
    if(session.state() == updates::ApplicationQuitSession::State::Refused) {
        session.request(3000);
        if(withUpdate) {
            REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::AwaitingHandoff; }));
            CHECK(backend.installs == 1);
            REQUIRE(backend.handoffConsent);
            CHECK(backend.handoffConsent());
            CHECK(ready.isEmpty());
            emit backend.installHandoffFinished(backend.handoffOperation, true);
        }
        REQUIRE(waitFor([&] { return ready.count() == 1; }));
        CHECK(session.state() == updates::ApplicationQuitSession::State::ExitReady);
    }
}
#ifdef __APPLE__
TEST_CASE("Explicit retry after settings timeout consumes successful receipts without replay", "[.gui-settings-ready-retirement][settings-timeout-retry]") {
    IsolatedMainWindow fixture;
    fixture.gui->createSearchBlacklist(); fixture.gui->createAntiSpam();
    fixture.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"original-rule"});
    fixture.gui->antiSpam()->setPhrase("initialize");
    fixture.gui->antiSpam()->setPhrase("original-question");
    REQUIRE(fixture.gui->searchBlacklist()->saveForQuitChecked());
    REQUIRE(fixture.gui->antiSpam()->saveForQuitChecked());
#ifdef USE_TORRENT
    fixture.gui->mainWindow()->showTorrents();
#endif
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    REQUIRE(fixture.core->prepareShutdownChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready);
    REQUIRE(fixture.gui->settings()->saveChecked());
    fixture.gui->settings()->setStr("qa/retry", "frozen");
    const auto config = fixture.home.filePath("EiskaltDC++_Qt.conf");
    updates::ApplicationQuitSession session(*fixture.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    probe.arm(QFile::encodeName(config).constData(), 1);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start(); session.request(100);
    CHECK(elapsed.elapsed() < 100);
    REQUIRE(waitFor([&] { return probe.entered(); }));
    REQUIRE(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(backend.installs == 0); CHECK(ready.isEmpty());
    REQUIRE(watchdog.get()); CHECK(advanced.load());
    REQUIRE(waitFor([&] { return quitSave(*fixture.gui->settings()) == SaveResult::Ready; }));
    const QByteArray marker("already-saved-retry-marker\n");
    const auto mark = [&](const QString& filename) {
        QFile file(filename); REQUIRE(file.exists());
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(marker) == marker.size());
    };
    QStringList cached{config, fixture.home.filePath("DCPlusPlus.xml")};
#ifdef USE_TORRENT
    const auto torrentRoot = QFileInfo(TorrentRuntime::settingsPath()).absolutePath();
    cached.append(QDir(torrentRoot).filePath("window.ini"));
    cached.append(TorrentRuntime::settingsPath());
#endif
    for(const auto& filename : cached) mark(filename);
    // These auxiliary owners were fully saved before arming the settings hold.
    // Mark completed bytes only; no competing writer is introduced by the test.
    for(const auto& filename : QStringList{"searchblacklist", "antispam", "blacklist", "graylist", "whitelist"}) {
        cached.append(fixture.home.filePath(filename)); mark(cached.last());
    }
    if(session.state() == updates::ApplicationQuitSession::State::Refused) {
        session.request(3000);
        REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::AwaitingHandoff; }));
        CHECK(backend.installs == 1); CHECK(backend.fences == 1);
        REQUIRE(backend.handoffConsent); CHECK(backend.handoffConsent()); CHECK(ready.isEmpty());
        for(const auto& filename : cached) {
            QFile file(filename); REQUIRE(file.open(QIODevice::ReadOnly)); CHECK(file.readAll() == marker);
        }
        emit backend.installHandoffFinished(backend.handoffOperation, true);
        REQUIRE(waitFor([&] { return ready.count() == 1; }));
    }
}
#endif
