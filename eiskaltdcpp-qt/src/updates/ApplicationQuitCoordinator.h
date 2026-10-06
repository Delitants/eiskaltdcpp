#pragma once

#include "dcpp/DCContext.h"
#include <QObject>
#include <QPointer>

class QtContext;
class MainWindow;
class SearchBlacklist;
class AntiSpam;
namespace updates {
class UpdateController;

// The application owns and must retain the entire context and GUI on Refused.
// This coordinator borrows them; it never disposes owners or grants install consent.
class ApplicationQuitCoordinator final : public QObject {
public:
    enum class Status { NotAdmitted, Ready, Refused, Pending };
    struct Result {
        Status status;
        const char* component;
        dcpp::DCContext::ShutdownResult core;
    };
    // Keep this object and updater outside the GUI roots fenced by MainWindow.
    explicit ApplicationQuitCoordinator(QtContext&, UpdateController* updater = nullptr);
    [[nodiscard]] Result prepare(uint32_t socketDrainTimeoutMs, bool asynchronousPersistence = false) noexcept;
    bool canRetryCheckpoint() const;
    // Retry only frozen auxiliary snapshots after all primary owners are safe.
    // This never repeats shutdown or grants updater installation consent.
    [[nodiscard]] Result retryCheckpoint() noexcept;
private:
    QtContext& context;
    QPointer<UpdateController> updater;
    bool hadUpdater, preparing = false, prepared = false;
    bool updaterFenced = false;
    const char* firstFailure = nullptr;
    bool checkpointRetryAllowed = false, searchRequired = false, spamRequired = false;
    QPointer<MainWindow> retainedWindow;
    QPointer<SearchBlacklist> searchOwner;
    QPointer<AntiSpam> spamOwner;
    Result result{Status::NotAdmitted, "Not prepared",
                  {dcpp::DCContext::ShutdownStatus::UnsupportedContext, "Not prepared"}};
};
}
