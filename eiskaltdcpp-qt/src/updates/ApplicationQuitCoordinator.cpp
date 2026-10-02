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
    result = {Status::Refused, "Preparing",
              {dcpp::DCContext::ShutdownStatus::UnsupportedContext, "Not prepared"}};
    const char* failed = nullptr;
    const auto check = [&](const char* name, auto&& operation) {
        bool saved = false;
        try { saved = operation(); } catch (...) {}
        if (!saved && !failed) failed = name;
        return saved;
    };
    // Close updater admission before intentional core ProxyRoute revocation.
    if (hadUpdater) check("Updater", [&] { return updater->stopWorkersForQuit(); });
    const bool guiStopped = check("GUI", [&] { return window->stopWorkersForQuit(); });
    result.core = context.dcCtx().prepareShutdownChecked(timeout);
    if (result.core.status != dcpp::DCContext::ShutdownStatus::Ready && !failed)
        failed = result.core.component;
    // A failed GUI receipt may leave hub callbacks live. Read auxiliary intent
    // only after either GUI listener retirement or a successful core drain.
    if (guiStopped || result.core.status == dcpp::DCContext::ShutdownStatus::Ready) {
        if (context.searchBlacklist())
            check("Search blacklist", [&] { return context.searchBlacklist()->saveForQuitChecked(); });
        if (context.antiSpam())
            check("Anti-spam", [&] { return context.antiSpam()->saveForQuitChecked(); });
    }
    check("GUI settings", [&] { return context.settings()->saveChecked(); });
    result.status = failed ? Status::Refused : Status::Ready;
    result.component = failed ? failed : "Ready";
    prepared = true;
    preparing = false;
    return result;
}
}
