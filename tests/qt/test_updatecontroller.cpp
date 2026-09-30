#include "dcpp/stdinc.h"
#include "tests/TestContext.h"
#include "WulforSettings.h"
#include "updates/UpdateBackend.h"
#include "updates/UpdateController.h"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QEventLoop>

namespace {
using namespace updates;
void events() { for(int i = 0; i < 8; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents); }
struct Backend : UpdateBackend {
    int checks = 0, downloads = 0, installs = 0, cancellations = 0, revocations = 0;
    quint64 operation = 0;
    CheckKind kind = CheckKind::Automatic;
    UpdateOffer selected;
    void check(quint64 op, Channel, CheckKind k) override { ++checks; operation = op; kind = k; }
    void download(quint64 op, const UpdateOffer& offer) override { ++downloads; operation = op; selected = offer; }
    void cancel() override { ++cancellations; }
    void armInstallOnQuit(const UpdateOffer& offer, bool relaunch) override { CHECK_FALSE(relaunch); ++installs; selected = offer; }
    void revokeInstallConsent() override { ++revocations; }
};
struct Fixture {
    dcpp::test::TestContext context;
    WulforSettings settings{*context.ownedCtx};
    Backend backend;
    QDateTime now = QDateTime::fromSecsSinceEpoch(0, Qt::UTC);
    UpdateIdentity identity{1, Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)};
    std::unique_ptr<UpdateController> controller;
    int notices = 0, errors = 0, manualErrors = 0;
    Fixture() { restart(); }
    void restart() {
        controller.reset();
        controller = std::make_unique<UpdateController>(settings, backend, identity, [&] { return now; });
        QObject::connect(controller.get(), &UpdateController::notificationRequested, controller.get(), [&](auto) { ++notices; });
        QObject::connect(controller.get(), &UpdateController::errorReported, controller.get(), [&](auto, bool manual) { ++errors; if(manual) ++manualErrors; });
    }
    UpdateOffer offer(quint64 build = 2) const {
        return {build, "3.0.0-pre.2", "plain notes", identity.bundleId, identity.cpu, QVersionNumber(26, 0), Channel::Preview, 100, QByteArray(32, 'a')};
    }
    void available() { emit backend.available(backend.operation, offer()); events(); }
    void ready() {
        controller->checkNow(); available(); controller->download();
        emit backend.downloadReady(backend.operation, 2, offer().sha256); events();
        REQUIRE(controller->state() == UpdateController::State::Ready);
    }
};
}

TEST_CASE("Updater preferences retain explicit OFF and build channel defaults", "[updates][update-controller]") {
    Fixture f;
    auto p = f.controller->preferences(); CHECK(p.checks); CHECK(p.notifications); CHECK_FALSE(p.autoInstall);
    CHECK(p.channel == Channel::Preview);
    p.checks = p.notifications = false; p.autoInstall = true; p.channel = Channel::Stable;
    f.controller->applyPreferences(p); f.settings.save(); f.restart();
    CHECK_FALSE(f.controller->preferences().checks); CHECK_FALSE(f.controller->preferences().notifications);
    CHECK(f.controller->preferences().autoInstall); CHECK(f.controller->preferences().channel == Channel::Stable);
    WulforSettings reloaded(*f.context.ownedCtx);
    CHECK_FALSE(reloaded.getBool("updates/checks", true)); CHECK_FALSE(reloaded.getBool("updates/notifications", true));
}
TEST_CASE("Updater schedules one daily attempt without bursts after restart or clock reversal", "[updates][update-controller]") {
    Fixture f; f.controller->start(); f.controller->start(); f.controller->poll();
    REQUIRE(f.backend.checks == 1); CHECK(f.controller->lastAttempt() == f.now); CHECK_FALSE(f.controller->lastSuccess().isValid());
    f.controller->checkNow(); CHECK(f.backend.checks == 1);
    emit f.backend.noUpdate(f.backend.operation); events(); CHECK(f.controller->lastSuccess() == f.now);
    f.now = f.now.addSecs(86399); f.controller->poll(); CHECK(f.backend.checks == 1);
    f.now = f.now.addSecs(1); f.controller->poll(); CHECK(f.backend.checks == 2);
    emit f.backend.noUpdate(f.backend.operation); events();
    f.now = f.now.addSecs(-100000); f.controller->poll(); CHECK(f.backend.checks == 2);
    f.restart(); f.controller->start(); f.controller->poll(); CHECK(f.backend.checks == 2);
    f.now = f.now.addDays(10); f.controller->poll(); f.controller->poll(); CHECK(f.backend.checks == 3);
}
TEST_CASE("Manual update checks work with scheduling off and stale replies cannot finish newer jobs", "[updates][update-controller]") {
    Fixture f; auto p = f.controller->preferences(); p.checks = false; f.controller->applyPreferences(p);
    f.controller->start(); f.controller->poll(); CHECK(f.backend.checks == 0);
    f.controller->checkNow(); REQUIRE(f.backend.checks == 1); CHECK(f.backend.kind == CheckKind::Manual);
    const auto old = f.backend.operation; f.controller->cancel(); f.controller->checkNow();
    CHECK(f.backend.checks == 2);
    emit f.backend.available(old, f.offer()); events(); CHECK(f.controller->state() == UpdateController::State::Checking);
    f.available(); CHECK(f.controller->state() == UpdateController::State::Available); CHECK(f.notices == 0);
}
TEST_CASE("Update notification Later and Skip choices persist and reject old offers", "[updates][update-controller]") {
    Fixture f; f.controller->start(); f.controller->poll(); f.available(); REQUIRE(f.notices == 1);
    SECTION("notification disabled") {
        auto p = f.controller->preferences(); p.notifications = false; f.controller->applyPreferences(p);
        f.now = f.now.addDays(1); f.controller->poll(); emit f.backend.available(f.backend.operation, f.offer(3)); events();
        CHECK(f.notices == 1);
    }
    SECTION("Later survives restart") {
        f.controller->later(); f.restart(); f.controller->start(); f.now = f.now.addDays(1); f.controller->poll(); f.available();
        CHECK(f.notices == 1);
    }
    SECTION("Skip survives restart") {
        f.controller->skipVersion(); f.restart(); f.controller->checkNow(); f.available();
        CHECK(f.controller->state() == UpdateController::State::Idle); CHECK(f.notices == 1);
    }
}
TEST_CASE("Verified downloads require matching operation build and digest", "[updates][update-controller]") {
    Fixture f; f.controller->checkNow(); f.available(); f.controller->download(); REQUIRE(f.backend.downloads == 1);
    emit f.backend.downloadReady(f.backend.operation - 1, 2, f.offer().sha256); events();
    CHECK(f.controller->state() == UpdateController::State::Downloading);
    emit f.backend.downloadReady(f.backend.operation, 2, QByteArray(32, 'x')); events();
    CHECK_FALSE(f.controller->state() == UpdateController::State::Ready);
    f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 0);
}
TEST_CASE("Update installation requires version bound consent at committed normal quit", "[updates][update-controller][update-shutdown]") {
    Fixture f; f.ready(); CHECK(f.backend.installs == 0);
    SECTION("ordinary shutdown is not consent") { f.controller->installOnNextQuit(); f.controller->shutdown(); }
    SECTION("no manual or automatic consent") { f.controller->normalQuitCommitted(); }
    SECTION("manual consent survives scheduling off") {
        f.controller->installOnNextQuit(); auto p = f.controller->preferences(); p.checks = false; f.controller->applyPreferences(p);
        f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 1); return;
    }
    SECTION("later revokes consent") { f.controller->installOnNextQuit(); f.controller->later(); f.controller->normalQuitCommitted(); }
    SECTION("a replacement offer cannot use old consent") {
        f.controller->installOnNextQuit(); f.controller->checkNow();
        emit f.backend.available(f.backend.operation, f.offer(3)); events(); f.controller->download();
        emit f.backend.downloadReady(f.backend.operation, 3, f.offer(3).sha256); events(); f.controller->normalQuitCommitted();
    }
    CHECK(f.backend.installs == 0);
}
TEST_CASE("Automatic updates revalidate saved settings before handoff and Later defers installation", "[updates][update-controller][update-shutdown]") {
    Fixture f; auto p = f.controller->preferences(); p.autoInstall = true; f.controller->applyPreferences(p);
    f.controller->start(); f.controller->poll(); f.available(); REQUIRE(f.backend.downloads == 1);
    emit f.backend.downloadReady(f.backend.operation, 2, f.offer().sha256); events();
    REQUIRE(f.controller->state() == UpdateController::State::Ready);
    SECTION("normal quit") { f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 1); f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 1); return; }
    SECTION("settings changed externally") { f.settings.setBool("updates/autoInstall", false); }
    SECTION("later") { f.controller->later(); }
    f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 0);
}
TEST_CASE("Disabling automatic checks cancels automatic activity without hiding manual errors", "[updates][update-controller]") {
    Fixture f; f.controller->start(); f.controller->poll(); const auto op = f.backend.operation;
    auto p = f.controller->preferences(); p.checks = false; f.controller->applyPreferences(p);
    CHECK(f.backend.cancellations > 0);
    emit f.backend.available(op, f.offer()); events(); CHECK_FALSE(f.controller->state() == UpdateController::State::Available);
    for(int i = 0; i < 2; ++i) {
        f.controller->checkNow(); emit f.backend.failed(f.backend.operation, "Failure", FailureCategory::Network); events();
    }
    CHECK(f.manualErrors == 2);
}

TEST_CASE("Cancel from the available notification cannot restart an automatic download", "[updates][update-controller]") {
    Fixture f; auto p = f.controller->preferences(); p.autoInstall = true; f.controller->applyPreferences(p);
    bool cancelled = false;
    QObject::connect(f.controller.get(), &UpdateController::stateChanged, f.controller.get(), [&](auto state) {
        if(state == UpdateController::State::Available && !cancelled) { cancelled = true; f.controller->cancel(); }
    });
    f.controller->start(); f.controller->poll(); f.available();
    CHECK(cancelled); CHECK(f.backend.downloads == 0); CHECK(f.notices == 0);
}

TEST_CASE("Replies from a destroyed controller cannot match a new controller operation", "[updates][update-controller]") {
    Fixture f; f.controller->checkNow(); const auto old = f.backend.operation;
    f.restart(); f.controller->checkNow();
    CHECK(f.backend.operation != old);
    emit f.backend.available(old, f.offer()); events();
    CHECK(f.controller->state() == UpdateController::State::Checking);
}

TEST_CASE("Turning automatic installation off cancels only automatic downloads", "[updates][update-controller]") {
    Fixture f; auto p = f.controller->preferences(); p.autoInstall = true; f.controller->applyPreferences(p);
    f.controller->start(); f.controller->poll(); f.available();
    REQUIRE(f.controller->state() == UpdateController::State::Downloading);
    const auto old = f.backend.operation;
    p.autoInstall = false; f.controller->applyPreferences(p);
    CHECK_FALSE(f.controller->state() == UpdateController::State::Downloading);
    emit f.backend.downloadReady(old, 2, f.offer().sha256); events();
    CHECK_FALSE(f.controller->state() == UpdateController::State::Ready);
    f.controller->download(); REQUIRE(f.controller->state() == UpdateController::State::Downloading);
    p.checks = false; f.controller->applyPreferences(p);
    CHECK(f.controller->state() == UpdateController::State::Downloading);
}

TEST_CASE("Background errors are limited per category per hour across restart", "[updates][update-controller]") {
    Fixture f;
    const auto fail = [&](FailureCategory category) {
        f.settings.setStr("updates/lastAttempt", {}); f.settings.save(); f.restart();
        f.controller->start(); f.controller->poll();
        emit f.backend.failed(f.backend.operation, "private-secret-message", category); events();
    };
    QString message;
    fail(FailureCategory::Network); CHECK(f.errors == 1);
    fail(FailureCategory::Network); CHECK(f.errors == 1);
    fail(FailureCategory::Verification); CHECK(f.errors == 2);
    f.now = f.now.addSecs(3599); fail(FailureCategory::Network); CHECK(f.errors == 2);
    f.now = f.now.addSecs(1); fail(FailureCategory::Network); CHECK(f.errors == 3);
    f.controller->checkNow();
    QObject::connect(f.controller.get(), &UpdateController::errorReported, f.controller.get(), [&](auto text, bool) { message = text; });
    emit f.backend.failed(f.backend.operation, "private-secret-message", FailureCategory::Network); events();
    CHECK(f.errors == 4); CHECK(f.manualErrors == 1); CHECK_FALSE(message.contains("private-secret"));
}

TEST_CASE("Update default channel follows the installed build and ignored offers do not notify", "[updates][update-controller]") {
    Fixture f; f.identity.defaultChannel = Channel::Stable; f.restart();
    CHECK(f.controller->preferences().channel == Channel::Stable);
    for(const auto change : {0, 1, 2, 3}) {
        f.controller->checkNow(); auto offer = f.offer();
        if(change == 0) offer.build = 1;
        if(change == 1) offer.cpu = "x86_64";
        if(change == 2) offer.minimumOs = QVersionNumber(99);
        emit f.backend.available(f.backend.operation, offer); events();
        CHECK(f.controller->state() == UpdateController::State::Idle);
    }
    CHECK(f.notices == 0); CHECK(f.backend.downloads == 0);
    f.controller->checkNow(); auto stable = f.offer(); stable.channel = Channel::Stable;
    emit f.backend.available(f.backend.operation, stable); events();
    CHECK(f.controller->state() == UpdateController::State::Available);
}

TEST_CASE("Update controller may be destroyed safely by a UI notification", "[updates][update-controller]") {
    Fixture f;
    QObject::connect(f.controller.get(), &UpdateController::notificationRequested, f.controller.get(), [&](auto) { f.controller.reset(); });
    f.controller->start(); f.controller->poll(); f.available();
    CHECK_FALSE(f.controller); CHECK(f.backend.downloads == 0); CHECK(f.backend.installs == 0);
}

TEST_CASE("A manual check reports its result before opt-in automatic download starts", "[updates][update-controller]") {
    Fixture f; auto p = f.controller->preferences(); p.autoInstall = true; p.notifications = false;
    f.controller->applyPreferences(p);
    int results = 0;
    QObject::connect(f.controller.get(), &UpdateController::checkFinished, f.controller.get(), [&](bool found, auto kind) {
        CHECK(found); CHECK(kind == CheckKind::Manual); ++results;
    });
    f.controller->checkNow(); f.available();
    CHECK(results == 1); CHECK(f.backend.downloads == 1); CHECK(f.notices == 0);
}
