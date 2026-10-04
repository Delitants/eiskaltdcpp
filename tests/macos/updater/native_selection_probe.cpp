// Linked against the actual native-enabled application's objects, then run only
// inside a private ad-hoc-signed bundle with all external networking denied.
#include "dcpp/stdinc.h"
#include "tests/TestContext.h"
#include "WulforSettings.h"
#include "updates/ApplicationUpdateOwner.h"
#include "updates/MacUpdateBackend.h"
#include "updates/UpdateController.h"
#include "updates/UpdateRuntimeIdentity.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QThread>
#ifndef EISKALT_MAC_UPDATER_NATIVE
#error Native selection probe requires the explicit native application build
#endif
namespace updates {
struct ApplicationUpdateOwnerTestAccess {
    static UpdateBackend* backend(ApplicationUpdateOwner& owner) { return owner.backend.get(); }
};
}
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    using namespace updates;
    if(!qEnvironmentVariable("HOME").startsWith("/private/tmp/")) qFatal("Private fixture HOME required");
    const auto runtime = currentMacUpdateRuntime();
    if(!runtime) qFatal("Current fixture bundle was not structurally admitted");
    dcpp::test::TestContext context;
    WulforSettings settings(*context.ownedCtx);
    auto owner = std::make_unique<ApplicationUpdateOwner>(settings, *context.ownedCtx);
    auto* backend = ApplicationUpdateOwnerTestAccess::backend(*owner);
    auto* controller = owner->controller();
    if(!qobject_cast<MacUpdateBackend*>(backend) || backend->parent() || controller->parent())
        qFatal("Application owner did not consume an unparented native selection");
    if(controller->preferences().channel != runtime->identity.defaultChannel || controller->supportsInstallation())
        qFatal("Runtime identity or installation gate mismatch");
    // Longer than the controller's startup delay: compilation/selection must
    // never activate scheduling just because valid metadata/frameworks exist.
    QElapsedTimer wait; wait.start();
    while(wait.elapsed() < 3600) { application.processEvents(); QThread::msleep(10); }
    if(controller->lastAttempt().isValid() || controller->lastSuccess().isValid() ||
        controller->state() != UpdateController::State::Idle)
        qFatal("Construction activated updater work");
    if(!controller->stopWorkersForQuit()) qFatal("Native worker fence refused");
    controller->normalQuitCommitted();
    if(!ApplicationUpdateOwner::finishNormalQuit(owner) || owner) qFatal("No-update ownership was not released");
    puts("PASS native application owner: correlated identity, no scheduler/capability, checked no-update disposal");
}
