#include "ApplicationUpdateOwner.h"
#include "UpdateController.h"
#include "UnsupportedUpdateBackend.h"

namespace updates {
ApplicationUpdateOwner::ApplicationUpdateOwner(WulforSettings& settings, dcpp::DCContext&)
    : ApplicationUpdateOwner(settings, std::make_unique<UnsupportedUpdateBackend>(), {}) {}
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
