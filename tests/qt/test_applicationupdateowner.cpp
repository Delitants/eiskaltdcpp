#include "dcpp/stdinc.h"
#include "tests/TestContext.h"
#include "WulforSettings.h"
#include "updates/UpdateBackend.h"
#include "updates/UpdateController.h"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include "updates/ApplicationUpdateOwner.h"
namespace updates {
struct ApplicationUpdateOwnerTestAccess {
    static std::unique_ptr<ApplicationUpdateOwner> make(WulforSettings& settings,
        std::unique_ptr<UpdateBackend> backend, UpdateIdentity identity) {
        return std::unique_ptr<ApplicationUpdateOwner>(new ApplicationUpdateOwner(settings, std::move(backend), std::move(identity)));
    }
};
}
namespace {
using namespace updates;
struct Backend final : UpdateBackend {
    int& destroyed;
    quint64 operation = 0, handoff = 0;
    explicit Backend(int& count) : destroyed(count) {}
    ~Backend() override { ++destroyed; }
    bool supportsInstallation() const override { return true; }
    bool stopWorkersForQuit() override { return true; }
    void check(quint64 op, Channel, CheckKind) override { operation = op; }
    void download(quint64 op, const UpdateOffer&) override { operation = op; }
    void cancel() override {}
    void armInstallOnQuit(const UpdateOffer&, bool) override { FAIL("Legacy handoff"); }
    void revokeInstallConsent() override {}
    void beginInstallHandoff(quint64 op, const UpdateOffer&, std::function<bool()> consent) override {
        REQUIRE(consent()); handoff = op;
    }
};
void events() { for(int i = 0; i < 12; ++i) QCoreApplication::processEvents(); }
}
TEST_CASE("Application updater owner is unsupported and unfenced by GUI parenting", "[updates][update-app-owner]") {
    dcpp::test::TestContext context;
    WulforSettings settings(*context.ownedCtx);
    auto owner = std::make_unique<ApplicationUpdateOwner>(settings, *context.ownedCtx);
    REQUIRE(owner->controller()); CHECK(owner->controller()->parent() == nullptr);
    CHECK_FALSE(owner->controller()->supportsInstallation());
    CHECK_FALSE(ApplicationUpdateOwner::finishNormalQuit(owner)); REQUIRE(owner);
    REQUIRE(owner->controller()->stopWorkersForQuit()); owner->controller()->normalQuitCommitted();
    REQUIRE(owner->controller()->quitDisposition() == UpdateController::QuitDisposition::NoUpdate);
    CHECK(ApplicationUpdateOwner::finishNormalQuit(owner)); CHECK_FALSE(owner);
}
TEST_CASE("Application updater lifetime releases only a checked terminal disposition", "[updates][update-app-owner]") {
    dcpp::test::TestContext context;
    WulforSettings settings(*context.ownedCtx);
    int destroyed = 0;
    auto wire = std::make_unique<Backend>(destroyed); auto* backend = wire.get();
    auto owner = ApplicationUpdateOwnerTestAccess::make(settings, std::move(wire),
        {1, Channel::Preview, "org.test.app-owner", "arm64", QVersionNumber(26, 0)});
    auto* controller = owner->controller();
    CHECK_FALSE(ApplicationUpdateOwner::finishNormalQuit(owner)); REQUIRE(owner); CHECK(destroyed == 0);
    bool prepared = false, noUpdate = false, nativeRefusal = false;
    SECTION("no update disposes before settings") { noUpdate = true; }
    SECTION("pending retains all owners") {}
    SECTION("native refusal retains owners") { nativeRefusal = true; }
    SECTION("prepared retains native owner to process exit") { prepared = true; }
    if(!noUpdate) {
        const UpdateOffer offer{2, "3.0.0-pre.2", {}, "org.test.app-owner", "arm64",
            QVersionNumber(26, 0), Channel::Preview, 100, QByteArray(32, 'a')};
        controller->checkNow(); emit backend->available(backend->operation, offer); events();
        REQUIRE(controller->state() == UpdateController::State::Available);
        controller->download(); emit backend->downloadReady(backend->operation, offer.build, offer.sha256); events();
        REQUIRE(controller->state() == UpdateController::State::Ready); controller->installOnNextQuit();
    }
    REQUIRE(controller->stopWorkersForQuit()); controller->normalQuitCommitted();
    if(noUpdate) {
        CHECK(ApplicationUpdateOwner::finishNormalQuit(owner)); CHECK_FALSE(owner); CHECK(destroyed == 1);
    } else {
        CHECK_FALSE(ApplicationUpdateOwner::finishNormalQuit(owner)); REQUIRE(owner); CHECK(destroyed == 0);
        if(prepared) {
            emit backend->installHandoffFinished(backend->handoff, true); events();
            REQUIRE(controller->quitDisposition() == UpdateController::QuitDisposition::Prepared);
            auto* retained = owner.get(); CHECK(ApplicationUpdateOwner::finishNormalQuit(owner));
            CHECK_FALSE(owner); CHECK(destroyed == 0);
            // Fixture-only reclamation after observing the process-exit lease.
            delete retained; CHECK(destroyed == 1);
        } else {
            if(nativeRefusal) emit backend->installHandoffFinished(backend->handoff, false);
            else controller->shutdown();
            events();
            REQUIRE(controller->quitDisposition() == UpdateController::QuitDisposition::Refused);
            CHECK_FALSE(ApplicationUpdateOwner::finishNormalQuit(owner)); REQUIRE(owner); CHECK(destroyed == 0);
        }
    }
}
