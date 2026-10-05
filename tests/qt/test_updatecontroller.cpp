#include "dcpp/stdinc.h"
#include "tests/TestContext.h"
#include "WulforSettings.h"
#include "updates/UpdateBackend.h"
#include "updates/UpdateController.h"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QMetaEnum>
#include <future>

namespace {
using namespace updates;
void events() { for(int i = 0; i < 8; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents); }
struct Backend : UpdateBackend {
    int checks = 0, downloads = 0, installs = 0, cancellations = 0, revocations = 0;
    quint64 operation = 0;
    CheckKind kind = CheckKind::Automatic;
    UpdateOffer selected;
    int fences = 0;
    bool fenceResult = true, fencePending = false;
    bool quitWorkersPending() const override { return fencePending; }
    bool supported = true, throwsOnHandoff = false, defaultHandoff = false;
    std::optional<bool> synchronousReceipt;
    quint64 handoffOperation = 0;
    std::function<bool()> handoffConsent;
    bool supportsInstallation() const override { return supported; }
    void beginInstallHandoff(quint64 op, const UpdateOffer& offer, std::function<bool()> consent) override {
        if(throwsOnHandoff) throw std::runtime_error("fixture handoff refusal");
        if(defaultHandoff) { UpdateBackend::beginInstallHandoff(op, offer, std::move(consent)); return; }
        handoffOperation = op; handoffConsent = std::move(consent); ++installs; selected = offer;
        if(synchronousReceipt) emit installHandoffFinished(op, *synchronousReceipt);
    }
    bool stopWorkersForQuit() override { ++fences; return fenceResult; }
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
template<class T> bool controllerFence(T& controller) {
    if constexpr(requires { controller.stopWorkersForQuit(); }) return controller.stopWorkersForQuit();
    else return false;
}
template<class T> bool quitDispositionIs(T& controller, const char* expected) {
    if constexpr(requires { controller.quitDisposition(); }) {
        const auto value = controller.quitDisposition();
        return QByteArray(QMetaEnum::fromType<decltype(value)>().valueToKey(int(value))) == expected;
    } else return false;
}
template<class T> void handoffReceipt(T& backend, quint64 op, bool ready) {
    if constexpr(requires { backend.installHandoffFinished(op, ready); }) emit backend.installHandoffFinished(op, ready);
}
}

TEST_CASE("Manual checks on an invalid installed identity report unsupported without contacting a backend", "[updates][update-ui-owner]") {
    Fixture f; f.identity.build = 0; f.restart();
    QSignalSpy errors(f.controller.get(), &UpdateController::errorReported);
    f.controller->start(); f.controller->poll(); events();
    CHECK(errors.isEmpty()); CHECK(f.backend.checks == 0);
    f.controller->checkNow();
    CHECK(errors.isEmpty()); // The UI receiver may safely be destroyed before delivery.
    events();
    REQUIRE(errors.count() == 1);
    CHECK(errors.front().at(1).toBool());
    CHECK_FALSE(errors.front().at(0).toString().isEmpty());
    CHECK(f.backend.checks == 0);
    CHECK_FALSE(f.controller->lastAttempt().isValid());
    CHECK_FALSE(f.controller->lastSuccess().isValid());
    CHECK(f.controller->state() == UpdateController::State::Idle);
    f.controller->checkNow(); f.controller->shutdown(); events();
    CHECK(errors.count() == 1);
}

TEST_CASE("Committed quit passes live version consent to the asynchronous handoff", "[updates][update-handoff-contract]") {
    Fixture f; f.ready(); f.controller->installOnNextQuit();
    f.controller->normalQuitCommitted();
    CHECK(f.backend.handoffOperation != 0);
    REQUIRE(f.backend.handoffConsent);
    CHECK(f.backend.handoffConsent());
    f.settings.setBool("updates/autoInstall", false); f.settings.setBool("updates/checks", false);
    CHECK(f.backend.handoffConsent()); // Explicit version consent does not require automatic checks.
    CHECK(quitDispositionIs(*f.controller, "AwaitingHandoff"));
    CHECK_FALSE(std::async(std::launch::async, [&] { return f.backend.handoffConsent(); }).get());
    CHECK(f.backend.handoffConsent());
    f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 1);
    QSignalSpy finished(f.controller.get(), SIGNAL(installHandoffFinished(bool))); REQUIRE(finished.isValid());
    handoffReceipt(f.backend, f.backend.handoffOperation + 1, true); events();
    CHECK(finished.isEmpty()); CHECK(quitDispositionIs(*f.controller, "AwaitingHandoff"));
    handoffReceipt(f.backend, f.backend.handoffOperation, true); events();
    REQUIRE(finished.count() == 1); CHECK(finished.front().front().toBool());
    CHECK(quitDispositionIs(*f.controller, "Prepared")); CHECK_FALSE(f.backend.handoffConsent());
    handoffReceipt(f.backend, f.backend.handoffOperation, false); events();
    CHECK(finished.count() == 1); CHECK(quitDispositionIs(*f.controller, "Prepared"));
}

TEST_CASE("Handoff consent rechecks settings and never revives after observed revocation", "[updates][update-handoff-contract]") {
    Fixture f; f.ready();
    auto preferences = f.controller->preferences(); preferences.autoInstall = true;
    f.controller->applyPreferences(preferences); f.controller->normalQuitCommitted();
    REQUIRE(f.backend.handoffConsent); REQUIRE(f.backend.handoffConsent());
    SECTION("automatic installation") { f.settings.setBool("updates/autoInstall", false); }
    SECTION("automatic checks") { f.settings.setBool("updates/checks", false); }
    SECTION("channel") { f.settings.setStr("updates/channel", "stable"); }
    SECTION("deferred") { f.settings.setStr("updates/deferred", f.offer().key()); }
    SECTION("skipped") { f.settings.setStr("updates/skipped", f.offer().key()); }
    CHECK_FALSE(f.backend.handoffConsent());
    f.settings.setBool("updates/autoInstall", true); f.settings.setBool("updates/checks", true);
    f.settings.setStr("updates/channel", "preview"); f.settings.setStr("updates/deferred", {}); f.settings.setStr("updates/skipped", {});
    CHECK_FALSE(f.backend.handoffConsent());
    QSignalSpy finished(f.controller.get(), SIGNAL(installHandoffFinished(bool))); REQUIRE(finished.isValid());
    handoffReceipt(f.backend, f.backend.handoffOperation, true); events();
    REQUIRE(finished.count() == 1); CHECK_FALSE(finished.front().front().toBool());
    CHECK(quitDispositionIs(*f.controller, "Refused"));
}

TEST_CASE("Handoff consent expires before owner teardown or explicit shutdown", "[updates][update-handoff-contract]") {
    Fixture f; f.ready(); f.controller->installOnNextQuit(); f.controller->normalQuitCommitted();
    REQUIRE(f.backend.handoffConsent); const auto consent = f.backend.handoffConsent;
    const auto revoked = f.backend.revocations;
    SECTION("owner destruction") { f.controller.reset(); }
    SECTION("shutdown") { f.controller->shutdown(); }
    CHECK_FALSE(consent()); CHECK(f.backend.revocations == revoked + 1);
}

TEST_CASE("Committed quit reports no-update and handoff refusals distinctly", "[updates][update-handoff-contract]") {
    Fixture f;
    SECTION("no pending update") {
        f.controller->normalQuitCommitted(); CHECK(quitDispositionIs(*f.controller, "NoUpdate")); CHECK(f.backend.installs == 0);
    }
    SECTION("no consent") {
        f.ready(); f.controller->normalQuitCommitted(); CHECK(quitDispositionIs(*f.controller, "NoUpdate")); CHECK(f.backend.installs == 0);
    }
    SECTION("unsupported backend") {
        f.ready(); f.controller->installOnNextQuit(); f.backend.supported = false;
        f.controller->normalQuitCommitted(); CHECK(quitDispositionIs(*f.controller, "NoUpdate")); CHECK(f.backend.installs == 0);
    }
    SECTION("worker refusal") {
        f.ready(); f.controller->installOnNextQuit(); f.backend.fenceResult = false;
        f.controller->normalQuitCommitted(); CHECK(quitDispositionIs(*f.controller, "Refused")); CHECK(f.backend.installs == 0);
    }
    SECTION("handoff throws") {
        f.ready(); f.controller->installOnNextQuit(); f.backend.throwsOnHandoff = true;
        CHECK_NOTHROW(f.controller->normalQuitCommitted()); CHECK(quitDispositionIs(*f.controller, "Refused"));
    }
    SECTION("negative receipt") {
        f.ready(); f.controller->installOnNextQuit(); f.controller->normalQuitCommitted();
        REQUIRE(f.backend.handoffOperation != 0);
        QSignalSpy finished(f.controller.get(), SIGNAL(installHandoffFinished(bool))); REQUIRE(finished.isValid());
        handoffReceipt(f.backend, f.backend.handoffOperation, false); events();
        REQUIRE(finished.count() == 1); CHECK_FALSE(finished.front().front().toBool());
        CHECK(quitDispositionIs(*f.controller, "Refused")); CHECK_FALSE(f.backend.handoffConsent());
    }
    SECTION("unknown endpoint refuses without legacy fallback") {
        f.ready(); f.controller->installOnNextQuit(); f.backend.defaultHandoff = true;
        f.controller->normalQuitCommitted(); events();
        CHECK(quitDispositionIs(*f.controller, "Refused")); CHECK(f.backend.installs == 0);
    }
}

TEST_CASE("Synchronous backend receipts remain deferred and allow owner destruction", "[updates][update-handoff-contract]") {
    Fixture f; f.ready(); f.controller->installOnNextQuit();
    f.backend.synchronousReceipt = true;
    bool received = false;
    QObject::connect(f.controller.get(), &UpdateController::installHandoffFinished, qApp, [&](bool prepared) {
        CHECK(prepared); received = true; f.controller.reset();
    });
    f.controller->normalQuitCommitted();
    CHECK_FALSE(received); REQUIRE(f.controller); CHECK(quitDispositionIs(*f.controller, "AwaitingHandoff"));
    events(); CHECK(received); CHECK_FALSE(f.controller); CHECK_FALSE(f.backend.handoffConsent());
}

TEST_CASE("Update controller retained fence keeps Ready consent but closes every input", "[updates][updater-controller-fence]") {
    Fixture f; f.ready(); f.controller->installOnNextQuit();
    CHECK_FALSE(std::async(std::launch::async, [&] { return controllerFence(*f.controller); }).get());
    CHECK(f.backend.fences == 0);
    REQUIRE(controllerFence(*f.controller));
    CHECK(f.backend.fences == 1); CHECK(f.backend.installs == 0);
    const auto cancellations = f.backend.cancellations, revocations = f.backend.revocations;
    const auto key = f.controller->offer().key();
    auto changed = f.controller->preferences(); changed.checks = false; changed.autoInstall = true; changed.channel = Channel::Stable;
    f.controller->applyPreferences(changed);
    f.controller->checkNow(); f.controller->download(); f.controller->start(); f.controller->poll();
    f.controller->later(); f.controller->skipVersion(); f.controller->cancel();
    CHECK(f.controller->preferences().checks); CHECK_FALSE(f.controller->preferences().autoInstall);
    CHECK(f.controller->offer().key() == key); CHECK(f.controller->state() == UpdateController::State::Ready);
    CHECK(f.backend.checks == 1); CHECK(f.backend.downloads == 1);
    CHECK(f.backend.cancellations == cancellations); CHECK(f.backend.revocations == revocations);
    REQUIRE(controllerFence(*f.controller)); CHECK(f.backend.fences == 1);
    f.controller->normalQuitCommitted();
    CHECK(f.backend.installs == 1); CHECK(f.controller->state() == UpdateController::State::Handoff);
    f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 1);
}

TEST_CASE("Update controller fence suppresses queued readiness notifications and auto download", "[updates][updater-controller-fence]") {
    Fixture f; auto p = f.controller->preferences(); p.autoInstall = true; f.controller->applyPreferences(p);
    int states = 0, checks = 0, progresses = 0;
    QObject::connect(f.controller.get(), &UpdateController::stateChanged, [&] { ++states; });
    QObject::connect(f.controller.get(), &UpdateController::checkFinished, [&] { ++checks; });
    QObject::connect(f.controller.get(), &UpdateController::progress, [&] { ++progresses; });
    f.controller->start(); f.controller->poll();
    SECTION("queued offer") { emit f.backend.available(f.backend.operation, f.offer()); }
    SECTION("pending notification and automatic download") { QCoreApplication::processEvents(); emit f.backend.available(f.backend.operation, f.offer()); QCoreApplication::processEvents(); }
    SECTION("queued download readiness") {
        f.available(); REQUIRE(f.backend.downloads == 1);
        emit f.backend.progress(f.backend.operation, 10, 100);
        emit f.backend.downloadReady(f.backend.operation, 2, f.offer().sha256);
    }
    const auto priorStates = states, priorChecks = checks, priorNotices = f.notices, priorDownloads = f.backend.downloads;
    REQUIRE(controllerFence(*f.controller)); events();
    CHECK(states == priorStates); CHECK(checks == priorChecks); CHECK(f.notices == priorNotices);
    CHECK(f.backend.downloads == priorDownloads); CHECK(progresses == 0);
    f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 0);
}

TEST_CASE("Update controller permanently refuses a failed worker fence", "[updates][updater-controller-fence]") {
    Fixture f; f.ready(); f.controller->installOnNextQuit(); f.backend.fenceResult = false;
    CHECK_FALSE(controllerFence(*f.controller)); CHECK(f.backend.fences == 1);
    f.backend.fenceResult = true;
    CHECK_FALSE(controllerFence(*f.controller)); CHECK(f.backend.fences == 1);
    f.controller->normalQuitCommitted();
    CHECK(f.backend.installs == 0); CHECK(f.controller->state() == UpdateController::State::Stopped);
}

TEST_CASE("Update controller polls a pending fence without reopening admission", "[updates][updater-retirement-controller]") {
    Fixture f;
    f.ready(); f.controller->installOnNextQuit();
    f.backend.fenceResult = false; f.backend.fencePending = true;
    CHECK_FALSE(f.controller->stopWorkersForQuit());
    CHECK_FALSE(f.controller->stopWorkersForQuit()); CHECK(f.backend.fences == 2);
    const auto checks = f.backend.checks, downloads = f.backend.downloads;
    f.controller->checkNow(); f.controller->download(); f.controller->cancel();
    events(); CHECK(f.backend.checks == checks); CHECK(f.backend.downloads == downloads);
    CHECK(f.backend.installs == 0);
    f.backend.fencePending = false; f.backend.fenceResult = true;
    CHECK(f.controller->stopWorkersForQuit()); CHECK(f.backend.fences == 3);
    CHECK(f.controller->stopWorkersForQuit()); CHECK(f.backend.fences == 3);
    CHECK(f.backend.installs == 0);
}

TEST_CASE("Update controller revalidates persisted consent rules after the worker fence", "[updates][updater-controller-fence]") {
    Fixture f; f.ready(); f.controller->installOnNextQuit(); REQUIRE(controllerFence(*f.controller));
    SECTION("deferred") { f.settings.setStr("updates/deferred", f.offer().key()); }
    SECTION("skipped") { f.settings.setStr("updates/skipped", f.offer().key()); }
    SECTION("channel changed") { f.settings.setStr("updates/channel", "stable"); }
    f.controller->normalQuitCommitted(); CHECK(f.backend.installs == 0);
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
TEST_CASE("Cancelled and failed downloads discard invalidated offers before retry", "[updates][update-controller][update-cancel-refresh]") {
    Fixture f; f.controller->checkNow(); f.available(); f.controller->download();
    const auto old = f.backend.operation;
    SECTION("cancel") { f.controller->cancel(); }
    SECTION("failure") { emit f.backend.failed(old, "failure", FailureCategory::Verification); events(); }
    CHECK(f.controller->state() == UpdateController::State::Idle);
    CHECK(f.controller->offer().build == 0);
    f.controller->download(); CHECK(f.backend.downloads == 1);
    f.controller->checkNow(); f.available(); f.controller->download(); CHECK(f.backend.downloads == 2);
    emit f.backend.downloadReady(old, 2, f.offer().sha256); events();
    CHECK(f.controller->state() == UpdateController::State::Downloading);
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
    // Cancellation invalidates the backend's offer; manual retry rechecks first.
    f.controller->checkNow(); f.available();
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
