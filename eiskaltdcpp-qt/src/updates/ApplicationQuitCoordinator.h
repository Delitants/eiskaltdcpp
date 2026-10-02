#pragma once

#include "dcpp/DCContext.h"
#include <QObject>
#include <QPointer>

class QtContext;
namespace updates {
class UpdateController;

// The application owns and must retain the entire context and GUI on Refused.
// This coordinator borrows them; it never disposes owners or grants install consent.
class ApplicationQuitCoordinator final : public QObject {
public:
    enum class Status { NotAdmitted, Ready, Refused };
    struct Result {
        Status status;
        const char* component;
        dcpp::DCContext::ShutdownResult core;
    };
    // Keep this object and updater outside the GUI roots fenced by MainWindow.
    explicit ApplicationQuitCoordinator(QtContext&, UpdateController* updater = nullptr);
    [[nodiscard]] Result prepare(uint32_t socketDrainTimeoutMs) noexcept;
private:
    QtContext& context;
    QPointer<UpdateController> updater;
    bool hadUpdater, preparing = false, prepared = false;
    Result result{Status::NotAdmitted, "Not prepared",
                  {dcpp::DCContext::ShutdownStatus::UnsupportedContext, "Not prepared"}};
};
}
