#include "dcpp/stdinc.h"
#include "ApplicationQuitCoordinator.h"
#include "UpdateController.h"
#include "QtContext.h"
#include "MainWindow.h"
#include "WulforSettings.h"
#include "SearchBlacklist.h"
#include "Antispam.h"
#include "ArenaWidget.h"
#include "Notification.h"
#include <QApplication>
#include <QThread>

namespace updates {
ApplicationQuitCoordinator::ApplicationQuitCoordinator(QtContext& ctx, UpdateController* update)
    : context(ctx), updater(update), hadUpdater(update != nullptr) {}

ApplicationQuitCoordinator::Result ApplicationQuitCoordinator::prepare(uint32_t timeout) noexcept {
    const auto reject = [](const char* name) {
        return Result{Status::NotAdmitted, name,
                      {dcpp::DCContext::ShutdownStatus::UnsupportedContext, "Not prepared"}};
    };
    if (QThread::currentThread() != thread()) return reject("GUI owner");
    if (prepared) return result;
    if (preparing) return reject("Preparation active");
    auto* window = context.mainWindow();
    if (!window || !context.settings() || !context.dcCtx().isRunning() ||
        window->thread() != thread()) return reject("Context");
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return reject("Modal input");
#ifdef USE_JS
    if (context.scriptEngine()) return reject("Script engine");
#endif
    if (hadUpdater && (!updater || updater->thread() != thread())) return reject("Updater owner");
    for (QObject* owner = updater.data(); owner; owner = owner->parent())
        if (owner == window || qobject_cast<ArenaWidget*>(owner) || owner == context.notification() ||
            owner == context.searchBlacklist() || owner == context.antiSpam())
            return reject("Updater GUI parent");

    preparing = true;
    retainedWindow = window;
    result = {Status::Refused, "Preparing",
              {dcpp::DCContext::ShutdownStatus::UnsupportedContext, "Not prepared"}};
    const char*& failed = firstFailure;
    const auto check = [&](const char* name, auto&& operation) {
        bool saved = false;
        try { saved = operation(); } catch (...) {}
        if (!saved && !failed) failed = name;
        return saved;
    };
    // Close updater admission before intentional core ProxyRoute revocation.
    if (hadUpdater && !updaterFenced) {
        bool stopped = false;
        try { stopped = updater->stopWorkersForQuit(); } catch(...) {}
        if(!stopped && updater->quitWorkersPending()) {
            result.status = Status::Pending;
            result.component = "Updater workers";
            preparing = false;
            return result;
        }
        if(!stopped && !failed) failed = "Updater";
        updaterFenced = true;
    }
    bool guiStopped = false;
    try { guiStopped = window->stopWorkersForQuit(); } catch (...) {}
    if (!guiStopped && window->quitWorkersPending()) {
        result.status = Status::Pending;
        result.component = "GUI workers";
        preparing = false;
        return result;
    }
    if (!guiStopped && !failed) failed = "GUI";
    result.core = context.dcCtx().prepareShutdownChecked(timeout);
    if (result.core.status != dcpp::DCContext::ShutdownStatus::Ready && !failed)
        failed = result.core.component;
    const bool ownersReady = !failed && guiStopped &&
        result.core.status == dcpp::DCContext::ShutdownStatus::Ready;
    // A failed GUI receipt may leave hub callbacks live. Read auxiliary intent
    // only after either GUI listener retirement or a successful core drain.
    if (guiStopped || result.core.status == dcpp::DCContext::ShutdownStatus::Ready) {
        searchOwner = context.searchBlacklist(); searchRequired = bool(searchOwner);
        spamOwner = context.antiSpam(); spamRequired = bool(spamOwner);
        if (context.searchBlacklist())
            check("Search blacklist", [&] { return context.searchBlacklist()->saveForQuitChecked(); });
        if (context.antiSpam())
            check("Anti-spam", [&] { return context.antiSpam()->saveForQuitChecked(); });
    }
    const bool settingsSaved = check("GUI settings", [&] { return context.settings()->saveChecked(); });
    checkpointRetryAllowed = ownersReady && settingsSaved && failed;
    if(checkpointRetryAllowed && hadUpdater) {
        bool disarmed = false;
        try { disarmed = updater && updater->disarmForCheckpointRecovery(); } catch(...) {}
        if(!disarmed) {
            checkpointRetryAllowed = false;
            failed = "Updater recovery disarm";
        }
    }
    result.status = failed ? Status::Refused : Status::Ready;
    result.component = failed ? failed : "Ready";
    prepared = true;
    preparing = false;
    return result;
}

bool ApplicationQuitCoordinator::canRetryCheckpoint() const {
    return QThread::currentThread() == thread() && prepared && checkpointRetryAllowed;
}

ApplicationQuitCoordinator::Result ApplicationQuitCoordinator::retryCheckpoint() noexcept {
    if(QThread::currentThread() != thread())
        return {Status::NotAdmitted, "GUI owner",
                {dcpp::DCContext::ShutdownStatus::UnsupportedContext, "Not prepared"}};
    const auto reject = [&](const char* name) {
        return Result{Status::NotAdmitted, name, result.core};
    };
    if(!canRetryCheckpoint()) return result;
    if(preparing) return reject("Preparation active");
    if(!retainedWindow || context.mainWindow() != retainedWindow || !context.dcCtx().isRunning() ||
       (searchRequired && !searchOwner) || context.searchBlacklist() != searchOwner ||
       (spamRequired && !spamOwner) || context.antiSpam() != spamOwner)
        return reject("Retained checkpoint owners");
    if(QApplication::activeModalWidget() || QApplication::activePopupWidget()) return reject("Modal input");
    if(hadUpdater && (!updater || updater->thread() != thread())) return reject("Updater owner");
    preparing = true;
    const char* failed = nullptr;
    const auto check = [&](const char* name, auto&& operation) {
        bool saved = false;
        try { saved = operation(); } catch(...) {}
        if(!saved && !failed) failed = name;
    };
    // Successful receipts stay cached; failed writes retain frozen intent.
    // No producer, core checkpoint, GUI/Torrent fence or settings sync is replayed.
    if(searchOwner) check("Search blacklist", [&] { return searchOwner->saveForQuitChecked(); });
    if(spamOwner) check("Anti-spam", [&] { return spamOwner->saveForQuitChecked(); });
    result.status = failed ? Status::Refused : Status::Ready;
    result.component = failed ? failed : "Ready";
    checkpointRetryAllowed = failed != nullptr;
    preparing = false;
    return result;
}
}
