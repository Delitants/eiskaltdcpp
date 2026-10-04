#include "ApplicationUpdateOwner.h"
#include "UpdateController.h"
#include "UnsupportedUpdateBackend.h"
#include "UpdateRuntimeIdentity.h"
#ifdef EISKALT_MAC_UPDATER_NATIVE
#include "MacUpdateBackend.h"
#endif

namespace updates {
namespace {
UpdateBackendSelection selectBackend(dcpp::DCContext& context) {
#ifdef EISKALT_MAC_UPDATER_NATIVE
    return MacUpdateBackend::createSelection(context);
#else
    (void)context;
    const auto runtime = currentMacUpdateRuntime();
    return {std::make_unique<UnsupportedUpdateBackend>(), runtime ? runtime->identity : UpdateIdentity{}};
#endif
}
}
ApplicationUpdateOwner::ApplicationUpdateOwner(WulforSettings& settings, dcpp::DCContext& context)
    : ApplicationUpdateOwner(settings, selectBackend(context)) {}
ApplicationUpdateOwner::ApplicationUpdateOwner(WulforSettings& settings, UpdateBackendSelection selected)
    : ApplicationUpdateOwner(settings, std::move(selected.backend), std::move(selected.identity)) {}
ApplicationUpdateOwner::ApplicationUpdateOwner(WulforSettings& settings,
    std::unique_ptr<UpdateBackend> wire, UpdateIdentity identity)
    : backend(std::move(wire)),
      control(std::make_unique<UpdateController>(settings, *backend, std::move(identity))) {}
ApplicationUpdateOwner::~ApplicationUpdateOwner() = default;
bool ApplicationUpdateOwner::finishNormalQuit(std::unique_ptr<ApplicationUpdateOwner>& owner) {
    if(!owner) return false;
    switch(owner->controller()->quitDisposition()) {
    case UpdateController::QuitDisposition::NoUpdate:
        owner.reset(); return true;
    case UpdateController::QuitDisposition::Prepared:
        // Irreversible native preparation is not cancellation. Retain this
        // sealed owner until OS process cleanup rather than revoking its stage.
        (void)owner.release(); return true;
    default: return false;
    }
}
}
