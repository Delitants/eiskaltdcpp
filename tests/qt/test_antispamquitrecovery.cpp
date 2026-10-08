#include "dcpp/stdinc.h"
#include "ApplicationQuitTestFixture.h"
#include "updates/ApplicationQuitSession.h"
#include "updates/ApplicationQuitCoordinator.h"
#include "updates/UpdateController.h"
#include "SearchBlacklist.h"
#include "Antispam.h"
#include <catch2/generators/catch_generators.hpp>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTimer>
#include <QThread>
#include <atomic>
#include <future>
#include <sys/stat.h>

namespace {
const QStringList filenames{"antispam", "blacklist", "graylist", "whitelist"};
QByteArray readBytes(const QString& path) {
    QFile file(path); REQUIRE(file.open(QIODevice::ReadOnly)); return file.readAll();
}
void writeMarker(const QString& path) {
    QFile file(path); REQUIRE(file.exists());
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray marker("already-saved-receipt\n");
    REQUIRE(file.write(marker) == marker.size());
}
void populate(IsolatedMainWindow& f) {
    f.gui->createAntiSpam(); f.gui->createSearchBlacklist();
    f.gui->antiSpam()->setPhrase("initialize"); f.gui->antiSpam()->setPhrase("frozen-question");
    f.gui->antiSpam()->addToBlack({"blocked-user"});
    f.gui->antiSpam()->addToGray({"gray-user"}); f.gui->antiSpam()->addToWhite({"allowed-user"});
    f.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"frozen-rule"});
}
struct MarkedFile {
    QString path;
    struct stat identity{};
    explicit MarkedFile(QString name) : path(std::move(name)) {
        writeMarker(path); REQUIRE(::stat(QFile::encodeName(path).constData(), &identity) == 0);
    }
    void unchanged() const {
        CHECK(readBytes(path) == QByteArray("already-saved-receipt\n"));
        struct stat current{}; REQUIRE(::stat(QFile::encodeName(path).constData(), &current) == 0);
        CHECK(current.st_dev == identity.st_dev); CHECK(current.st_ino == identity.st_ino);
    }
};
void consent(updates::UpdateController& updater, QuitBackend& backend) {
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
}
}

TEST_CASE("Session AntiSpam recovery preserves successful file commits", "[qt][antispam-file-recovery]") {
    const auto blocked = QString::fromLatin1(GENERATE("antispam", "blacklist", "graylist", "whitelist"));
    IsolatedMainWindow f; f.gui->createAntiSpam();
    auto& spam = *f.gui->antiSpam();
    spam.setPhrase("initialize"); spam.setPhrase("frozen-question");
    spam.addToBlack({"blocked-user"}); spam.addToGray({"gray-user"}); spam.addToWhite({"allowed-user"});
    const auto blockedPath = f.home.filePath(blocked);
    REQUIRE(QDir().mkpath(blockedPath));
    updates::ApplicationQuitSession session(*f.gui);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(ready.isEmpty());
    for(const auto& name : filenames)
        if(name != blocked) writeMarker(f.home.filePath(name));
    REQUIRE(QDir(blockedPath).removeRecursively());
    session.request(3000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    for(const auto& name : filenames) {
        INFO("blocked=" << blocked.toStdString() << " saved=" << name.toStdString());
        if(name != blocked)
            CHECK(readBytes(f.home.filePath(name)) == QByteArray("already-saved-receipt\n"));
    }
}

TEST_CASE("Lost AntiSpam owner blocks partial recovery", "[qt][antispam-file-recovery]") {
    IsolatedMainWindow f; populate(f);
    const auto blocked = f.home.filePath("blacklist"); REQUIRE(QDir().mkpath(blocked));
    updates::ApplicationQuitCoordinator coordinator(*f.gui);
    updates::ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitFor([&] { result = coordinator.prepare(3000, true);
        return result.status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    REQUIRE(result.status == updates::ApplicationQuitCoordinator::Status::Refused);
    REQUIRE(coordinator.canRetryCheckpoint());
    f.gui->destroyAntiSpam();
    SECTION("destroyed owner") {}
    SECTION("replacement at same paths") { f.gui->createAntiSpam(); }
    REQUIRE(QDir(blocked).removeRecursively());
    CHECK(coordinator.retryCheckpoint(true).status == updates::ApplicationQuitCoordinator::Status::NotAdmitted);
    CHECK_FALSE(QFile::exists(blocked));
}

TEST_CASE("AntiSpam receipts survive a separate SearchBlacklist failure", "[qt][antispam-file-recovery]") {
    IsolatedMainWindow f; populate(f);
    const auto spamPath = f.home.filePath("blacklist"), searchPath = f.home.filePath("searchblacklist");
    REQUIRE(QDir().mkpath(spamPath)); REQUIRE(QDir().mkpath(searchPath));
    updates::ApplicationQuitSession session(*f.gui);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    std::vector<MarkedFile> markers;
    for(const auto& name : QStringList{"antispam", "graylist", "whitelist"}) markers.emplace_back(f.home.filePath(name));
    REQUIRE(QDir(spamPath).removeRecursively()); REQUIRE(QDir(searchPath).removeRecursively());
    session.request(3000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    for(const auto& marker : markers) marker.unchanged();
    CHECK(readBytes(searchPath) == QByteArray("NAME:frozen-rule\n"));
    CHECK(readBytes(spamPath) == QByteArray("blocked-user\n"));
}

TEST_CASE("Consented update stays disarmed through AntiSpam recovery", "[qt][antispam-file-recovery]") {
    IsolatedMainWindow f; populate(f);
    const auto blocked = f.home.filePath("blacklist"); REQUIRE(QDir().mkpath(blocked));
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    consent(updater, backend);
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(backend.installs == 0); CHECK(backend.fences == 1); CHECK(backend.revocations > 0);
    REQUIRE(QDir(blocked).removeRecursively());
    session.request(3000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(backend.installs == 0); CHECK(backend.fences == 1);
    CHECK(updater.quitDisposition() == updates::UpdateController::QuitDisposition::NoUpdate);
}

#ifdef __APPLE__
#include "../macos/updater/CoreWriteProbe.h"
TEST_CASE("Frozen AntiSpam owner cannot be replaced during primary retirement", "[.antispam-file-retirement]") {
    const bool replacement = GENERATE(false, true);
    IsolatedMainWindow f; populate(f);
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    consent(updater, backend);
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe; std::atomic_int ticks{0}; std::atomic_bool advanced{false}; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    probe.arm(QFile::encodeName(f.home.filePath("EiskaltDC++_Qt.conf")).constData(), 1);
    auto watchdog = probe.watchdog(ticks, advanced);
    session.request(100); REQUIRE(waitFor([&] { return probe.entered(); }));
    f.gui->antiSpam()->setPhrase("late-question");
    REQUIRE(f.gui->antiSpam()->getPhrase() == "frozen-question");
    f.gui->destroyAntiSpam();
    if(replacement) { f.gui->createAntiSpam(); f.gui->antiSpam()->setAttempts(19); }
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    REQUIRE(watchdog.get()); CHECK(advanced.load()); QTest::qWait(30);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    for(const auto& name : filenames) CHECK_FALSE(QFile::exists(f.home.filePath(name)));
    if(replacement) { f.gui->antiSpam()->setAttempts(29); CHECK(f.gui->antiSpam()->getAttempts() == 29); }
}
namespace {
void heldRecovery(int mode) {
    IsolatedMainWindow f; populate(f);
    const auto blocked = f.home.filePath("blacklist"), last = f.home.filePath("graylist");
    REQUIRE(QDir().mkpath(blocked)); REQUIRE(QDir().mkpath(last));
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    consent(updater, backend);
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    std::vector<MarkedFile> markers;
    for(const auto& name : QStringList{"antispam", "whitelist", "searchblacklist", "DCPlusPlus.xml", "EiskaltDC++_Qt.conf"})
        markers.emplace_back(f.home.filePath(name));
    REQUIRE(QDir(blocked).removeRecursively());
    CoreWriteProbe probe;
    auto writes = reinterpret_cast<unsigned long long(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_matching_writes"));
    REQUIRE(writes);
    std::atomic_int ticks{0}; std::atomic_bool advanced{false}; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    probe.arm(QFile::encodeName(blocked).constData(), mode);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start(); session.request(100); CHECK(elapsed.elapsed() < 100);
    REQUIRE(waitFor([&] { return probe.entered(); })); CHECK(probe.writer() != probe.gui);
    const auto admittedWrites = writes(); CHECK(admittedWrites > 0);
    for(int i = 0; i < 5; ++i) {
        CHECK_FALSE(f.gui->antiSpam()->saveForQuitChecked());
        QTest::qWait(10); CHECK(writes() == admittedWrites);
    }
    REQUIRE(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused); CHECK(ready.isEmpty());
    REQUIRE(watchdog.get()); CHECK(advanced.load()); QTest::qWait(30); CHECK(ready.isEmpty());
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(ready.isEmpty()); CHECK(writes() == admittedWrites);
    markers.emplace_back(blocked);
    REQUIRE(QDir(last).removeRecursively()); session.request(3000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    const auto completedWrites = writes(); REQUIRE(f.gui->antiSpam()->saveForQuitChecked());
    CHECK(writes() == completedWrites);
    for(const auto& marker : markers) marker.unchanged();
    CHECK(readBytes(last) == QByteArray("gray-user\n")); CHECK(backend.fences == 1); CHECK(backend.installs == 0);
    CHECK(updater.quitDisposition() == updates::UpdateController::QuitDisposition::NoUpdate);
}
}
TEST_CASE("Held AntiSpam partial recovery retains one writer", "[.antispam-file-retirement]") {
    SECTION("write") { heldRecovery(1); }
    SECTION("flush") { heldRecovery(3); }
}
TEST_CASE("AntiSpam thread-exit hold refuses late recovery", "[.antispam-file-retirement]") {
    heldRecovery(6);
}

TEST_CASE("Pending AntiSpam receipt cannot adopt a replacement owner", "[.antispam-file-retirement]") {
    IsolatedMainWindow f; populate(f);
    REQUIRE(waitFor([&] { return f.gui->mainWindow()->stopWorkersForQuit(); }));
    REQUIRE(waitFor([&] { return f.core->prepareShutdownAsyncChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready; }));
    updates::ApplicationQuitCoordinator coordinator(*f.gui);
    CoreWriteProbe probe; std::atomic_int ticks{0}; std::atomic_bool advanced{false}; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    probe.arm(QFile::encodeName(f.home.filePath("blacklist")).constData(), 1);
    auto watchdog = probe.watchdog(ticks, advanced);
    REQUIRE(coordinator.prepare(3000, true).status == updates::ApplicationQuitCoordinator::Status::Pending);
    REQUIRE(waitFor([&] { return probe.entered(); }));
    f.gui->destroyAntiSpam();
    SECTION("destroyed owner") {}
    SECTION("replacement at same paths") { f.gui->createAntiSpam(); }
    REQUIRE(waitFor([&] { return probe.released(); }));
    REQUIRE(watchdog.get()); CHECK(advanced.load());
    updates::ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitFor([&] { result = coordinator.prepare(3000, true);
        return result.status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    CHECK(result.status == updates::ApplicationQuitCoordinator::Status::Refused);
    CHECK_FALSE(coordinator.canRetryCheckpoint());
    if(f.gui->antiSpam()) { f.gui->antiSpam()->setAttempts(19); CHECK(f.gui->antiSpam()->getAttempts() == 19); }
}

TEST_CASE("Commit refusal preserves earlier AntiSpam receipts", "[.antispam-file-retirement]") {
    IsolatedMainWindow f; populate(f);
    REQUIRE(waitFor([&] { return f.gui->mainWindow()->stopWorkersForQuit(); }));
    REQUIRE(waitFor([&] { return f.core->prepareShutdownAsyncChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready; }));
    updates::ApplicationQuitSession session(*f.gui);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    const auto blocked = f.home.filePath("blacklist");
    CoreWriteProbe probe; std::atomic_int ticks{0}; std::atomic_bool advanced{false}; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    probe.arm(QFile::encodeName(blocked).constData(), 3); auto watchdog = probe.watchdog(ticks, advanced);
    session.request(100); REQUIRE(waitFor([&] { return probe.entered(); }));
    REQUIRE_FALSE(QFile::exists(blocked)); REQUIRE(QDir().mkpath(blocked));
    REQUIRE(watchdog.get());
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(ready.isEmpty());
    // Collect the original commit failure without starting a replacement writer.
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    std::vector<MarkedFile> markers;
    for(const auto& name : QStringList{"antispam", "graylist", "whitelist"}) markers.emplace_back(f.home.filePath(name));
    REQUIRE(QDir(blocked).removeRecursively()); session.request(3000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    for(const auto& marker : markers) marker.unchanged();
    CHECK(readBytes(blocked) == QByteArray("blocked-user\n"));
}

TEST_CASE("AntiSpam launch refusal retains snapshot and disarms consent", "[.antispam-file-retirement]") {
    IsolatedMainWindow f; populate(f);
    REQUIRE(waitFor([&] { return f.gui->mainWindow()->stopWorkersForQuit(); }));
    REQUIRE(waitFor([&] { return f.core->prepareShutdownAsyncChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready; }));
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    consent(updater, backend);
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe; probe.arm("", 7);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    REQUIRE(probe.entered()); CHECK(ready.isEmpty()); CHECK(backend.installs == 0); probe.release();
    f.gui->antiSpam()->setPhrase("late-question");
    session.request(3000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(readBytes(f.home.filePath("antispam")) == QByteArray("|ANTISPAM_PHRASE|frozen-question\n|ANTISPAM_KEY|\n|ATTEMPTS|0\n"));
    CHECK(backend.fences == 1); CHECK(backend.installs == 0);
    CHECK(updater.quitDisposition() == updates::UpdateController::QuitDisposition::NoUpdate);
}

namespace {
void receiptControl(bool earlyException) {
    IsolatedMainWindow f; populate(f);
    const auto blocked = f.home.filePath("blacklist"); REQUIRE(QDir().mkpath(blocked));
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    consent(updater, backend);
    updates::ApplicationQuitCoordinator coordinator(*f.gui, &updater);
    updates::ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitFor([&] { result = coordinator.prepare(3000, true);
        return result.status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    REQUIRE(result.status == updates::ApplicationQuitCoordinator::Status::Refused);
    REQUIRE(coordinator.canRetryCheckpoint());
    std::vector<MarkedFile> markers;
    for(const auto& name : QStringList{"antispam", "graylist", "whitelist", "searchblacklist"})
        markers.emplace_back(f.home.filePath(name));
    REQUIRE(QDir(blocked).removeRecursively());
    std::unique_ptr<CoreWriteProbe> probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false}; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    std::future<bool> watchdog;
    if(earlyException) {
        probe = std::make_unique<CoreWriteProbe>();
        probe->arm(QFile::encodeName(blocked).constData(), 6); watchdog = probe->watchdog(ticks, advanced);
    }
    CHECK(coordinator.retryCheckpoint(true).status == updates::ApplicationQuitCoordinator::Status::Pending);
    if(earlyException) REQUIRE(waitFor([&] { return probe->entered(); }));
    QElapsedTimer elapsed; elapsed.start();
    REQUIRE(waitFor([&] { result = coordinator.retryCheckpoint(true);
        return result.status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    if(earlyException) { CHECK(elapsed.elapsed() < 100); CHECK_FALSE(probe->released()); }
    CHECK(result.status == updates::ApplicationQuitCoordinator::Status::Refused);
    CHECK_FALSE(coordinator.canRetryCheckpoint()); CHECK_FALSE(f.gui->antiSpam()->saveForQuitChecked());
    CHECK(coordinator.retryCheckpoint(true).status == updates::ApplicationQuitCoordinator::Status::Refused);
    CHECK(backend.installs == 0); CHECK(backend.fences == 1); CHECK(backend.revocations > 0);
    for(const auto& marker : markers) marker.unchanged();
    if(earlyException) {
        REQUIRE(waitFor([&] { return probe->released(); }));
        REQUIRE(watchdog.get()); CHECK(advanced.load());
    }
}
}
TEST_CASE("Untrusted AntiSpam receipt is terminal", "[.antispam-receipt-control]") { receiptControl(false); }
TEST_CASE("Early exceptional AntiSpam receipt retains physical worker", "[.antispam-receipt-control]") { receiptControl(true); }

TEST_CASE("Incomplete AntiSpam capture is terminal", "[.antispam-allocation-control]") {
    IsolatedMainWindow f; populate(f);
    updates::ApplicationQuitCoordinator coordinator(*f.gui);
    updates::ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitFor([&] { result = coordinator.prepare(3000, true);
        return result.status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    CHECK(result.status == updates::ApplicationQuitCoordinator::Status::Refused);
    CHECK_FALSE(coordinator.canRetryCheckpoint()); CHECK_FALSE(f.gui->antiSpam()->freezeForQuitChecked());
    f.gui->antiSpam()->setPhrase("late-question"); CHECK(f.gui->antiSpam()->getPhrase() == "frozen-question");
    CHECK_FALSE(f.gui->antiSpam()->saveForQuitChecked());
    for(const auto& name : filenames) CHECK_FALSE(QFile::exists(f.home.filePath(name)));
}
TEST_CASE("Failed attempt copy retains committed receipts", "[.antispam-allocation-control]") {
    IsolatedMainWindow f; populate(f);
    const auto blocked = f.home.filePath("blacklist"); REQUIRE(QDir().mkpath(blocked));
    updates::ApplicationQuitCoordinator coordinator(*f.gui);
    updates::ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitFor([&] { result = coordinator.prepare(3000, true);
        return result.status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    REQUIRE(result.status == updates::ApplicationQuitCoordinator::Status::Refused);
    REQUIRE(coordinator.canRetryCheckpoint());
    std::vector<MarkedFile> markers;
    for(const auto& name : QStringList{"antispam", "graylist", "whitelist", "searchblacklist"})
        markers.emplace_back(f.home.filePath(name));
    REQUIRE(QDir(blocked).removeRecursively());
    CHECK(coordinator.retryCheckpoint(true).status == updates::ApplicationQuitCoordinator::Status::Refused);
    CHECK(coordinator.canRetryCheckpoint()); CHECK_FALSE(QFile::exists(blocked));
    for(const auto& marker : markers) marker.unchanged();
    REQUIRE(waitFor([&] { result = coordinator.retryCheckpoint(true);
        return result.status != updates::ApplicationQuitCoordinator::Status::Pending; }));
    CHECK(result.status == updates::ApplicationQuitCoordinator::Status::Ready);
    for(const auto& marker : markers) marker.unchanged();
    CHECK(readBytes(blocked) == QByteArray("blocked-user\n"));
}
#endif
