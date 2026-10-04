#pragma once
#include "UpdateTypes.h"
#include <memory>

class WulforSettings;
namespace dcpp { class DCContext; }
namespace updates {
class UpdateBackend;
class UpdateController;
struct UpdateBackendSelection;
struct ApplicationUpdateOwnerTestAccess;
// Unparented backend/controller lifetime; borrowed settings/core remain alive
// until checked quit. No production scheduler or native capability is enabled.
class ApplicationUpdateOwner final {
public:
    ApplicationUpdateOwner(WulforSettings&, dcpp::DCContext&);
    ~ApplicationUpdateOwner();
    UpdateController* controller() const { return control.get(); }
    // Call only after aggregate application Ready. Pending/refused owners cannot
    // be disposed; Prepared retains its native stage through process exit.
    static bool finishNormalQuit(std::unique_ptr<ApplicationUpdateOwner>&);
private:
    friend struct ApplicationUpdateOwnerTestAccess;
    ApplicationUpdateOwner(WulforSettings&, UpdateBackendSelection);
    ApplicationUpdateOwner(WulforSettings&, std::unique_ptr<UpdateBackend>, UpdateIdentity);
    std::unique_ptr<UpdateBackend> backend;
    std::unique_ptr<UpdateController> control;
};
}
