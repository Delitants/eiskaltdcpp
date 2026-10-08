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
#include <future>
#include <thread>

namespace updates {
struct ApplicationQuitCoordinator::AuxiliaryWrite {
    struct Receipt { bool search = true; std::optional<antispamquit::Receipt> spam; };
    std::thread worker;
    std::future<Receipt> receipt;
    std::optional<antispamquit::Token> expectedSpam;
    bool untrustedCompletion = false;
    ~AuxiliaryWrite() { if(worker.joinable()) worker.join(); }
};

ApplicationQuitCoordinator::ApplicationQuitCoordinator(QtContext& ctx, UpdateController* update)
    : context(ctx), updater(update), hadUpdater(update != nullptr) {}
ApplicationQuitCoordinator::~ApplicationQuitCoordinator() = default;

void ApplicationQuitCoordinator::bindAuxiliaryOwners() {
    if(auxiliaryOwnersBound) return;
    QPointer<SearchBlacklist> search = context.searchBlacklist();
    QPointer<AntiSpam> spam = context.antiSpam();
    searchOwner = std::move(search); spamOwner = std::move(spam);
    searchRequired = bool(searchOwner); spamRequired = bool(spamOwner);
    auxiliaryOwnersBound = true;
}

bool ApplicationQuitCoordinator::auxiliaryOwnersMatch() const noexcept {
    return auxiliaryOwnersBound && (!searchRequired || searchOwner) && context.searchBlacklist() == searchOwner &&
        (!spamRequired || spamOwner) && context.antiSpam() == spamOwner;
}

ApplicationQuitCoordinator::AuxiliaryResult ApplicationQuitCoordinator::saveAuxiliary(bool asynchronous) noexcept {
    if(auxiliary) {
        if(auxiliary->untrustedCompletion) return {Status::Refused, "Auxiliary receipt"};
        AuxiliaryWrite::Receipt saved;
        const auto token = auxiliary->expectedSpam;
        try {
            if(auxiliary->receipt.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                return {Status::Pending, searchRequired ? "Search blacklist" : "Anti-spam", true};
            saved = auxiliary->receipt.get();
        } catch(...) {
            // An exceptional/broken future can be ready before physical exit.
            // Retain the worker: joining here could block the GUI in TLS teardown.
            auxiliary->untrustedCompletion = true;
            if(auxiliaryOwnersMatch() && spamOwner && token) spamOwner->finishQuitSave(*token, std::nullopt);
            return {Status::Refused, "Auxiliary receipt"};
        }
        // Readiness is published at thread exit, including physical TLS teardown.
        auxiliary.reset();
        if(!auxiliaryOwnersMatch())
            return {Status::Refused, "Retained checkpoint owners"};
        if(searchOwner) searchOwner->quitSaved = saved.search;
        auto spam = antispamquit::Status::Ready;
        if(token) spam = spamOwner->finishQuitSave(*token, saved.spam);
        else if(saved.spam || (spamOwner && !spamOwner->quitSaved)) spam = antispamquit::Status::TerminalFailure;
        if(spam == antispamquit::Status::TerminalFailure) return {Status::Refused, "Anti-spam receipt"};
        if(!saved.search) return {Status::Refused, "Search blacklist", true};
        if(spam != antispamquit::Status::Ready) return {Status::Refused, "Anti-spam", true};
        return {Status::Ready, "Ready"};
    }
    if(!auxiliaryOwnersMatch()) return {Status::Refused, "Retained checkpoint owners"};
    if(!searchOwner && !spamOwner) return {Status::Ready, "Ready"};
    if(!asynchronous) {
        bool search = true, spam = true;
        try { if(searchOwner) search = searchOwner->saveForQuitChecked(); } catch(...) { search = false; }
        try { if(spamOwner) spam = spamOwner->saveForQuitChecked(); } catch(...) { spam = false; }
        return {!search || !spam ? Status::Refused : Status::Ready,
                !search ? "Search blacklist" : !spam ? "Anti-spam" : "Ready",
                (!search || !spam) && (!spamOwner || spamOwner->quitSaveRetryable())};
    }
    std::optional<antispamquit::Token> admitted;
    try {
        if(searchOwner && !searchOwner->freezeForQuitChecked()) return {Status::Refused, "Search blacklist"};
        if(spamOwner && !spamOwner->freezeForQuitChecked()) return {Status::Refused, "Anti-spam"};
        auto search = searchOwner ? searchOwner->quitSaveOperation() : std::function<bool()>{};
        auto write = std::make_unique<AuxiliaryWrite>();
        std::promise<AuxiliaryWrite::Receipt> receipt;
        write->receipt = receipt.get_future();
        std::optional<antispamquit::Attempt> spam;
        if(spamOwner && !spamOwner->quitSaved) {
            spam = spamOwner->beginQuitSave();
            if(!spam) return {Status::Refused, "Anti-spam admission"};
            admitted = spam->token;
            write->expectedSpam = admitted;
        }
        // These closures own only immutable paths and bytes, never GUI/context owners.
        write->worker = std::thread([search = std::move(search), spam = std::move(spam),
                                     receipt = std::move(receipt)]() mutable {
            AuxiliaryWrite::Receipt saved;
            try { if(search) saved.search = search(); } catch(...) { saved.search = false; }
            if(spam) saved.spam = antispamquit::execute(*spam);
            try { receipt.set_value_at_thread_exit(saved); } catch(...) {}
        });
        auxiliary = std::move(write);
        return {Status::Pending, searchRequired ? "Search blacklist" : "Anti-spam", true};
    } catch(...) {
        // No worker was installed: only this matching admission may roll back.
        const bool canceled = !admitted || (spamOwner && spamOwner->cancelUnstartedQuitSave(*admitted));
        return {Status::Refused, searchRequired ? "Search blacklist" : "Anti-spam",
                canceled && (!spamOwner || spamOwner->quitSaveRetryable())};
    }
}

ApplicationQuitCoordinator::Result ApplicationQuitCoordinator::prepare(uint32_t timeout, bool asynchronousPersistence) noexcept {
    const auto reject = [](const char* name) {
        return Result{Status::NotAdmitted, name,
                      {dcpp::DCContext::ShutdownStatus::UnsupportedContext, "Not prepared"}};
    };
    if (QThread::currentThread() != thread()) return reject("GUI owner");
    if (prepared) {
        if(auxiliary && checkpointRetryAllowed) return retryCheckpoint(asynchronousPersistence);
        return result;
    }
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
    try { guiStopped = window->stopWorkersForQuit(asynchronousPersistence); } catch (...) {}
    const bool guiPending = !guiStopped && window->quitWorkersPending();
    // GUI retirement has fenced input even while settings or workers remain
    // pending. Freeze rules now, before yielding to the live event loop.
    if(asynchronousPersistence && !auxiliary && (guiStopped || guiPending)) {
        // The first freeze owns the identity even across primary-worker waits.
        if(check("Retained checkpoint owners", [&] { bindAuxiliaryOwners(); return auxiliaryOwnersMatch(); })) {
            if(searchOwner) check("Search blacklist", [&] { return searchOwner->freezeForQuitChecked(); });
            if(spamOwner) check("Anti-spam", [&] { return spamOwner->freezeForQuitChecked(); });
        }
    }
    if (guiPending && !window->quitProducersRetired()) {
        result.status = Status::Pending;
        result.component = "GUI workers";
        preparing = false;
        return result;
    }
    if (!guiStopped && !guiPending && !failed) failed = "GUI";
    result.core = asynchronousPersistence ? context.dcCtx().prepareShutdownAsyncChecked(timeout)
                                         : context.dcCtx().prepareShutdownChecked(timeout);
    if(result.core.status == dcpp::DCContext::ShutdownStatus::Pending) {
        result.status = Status::Pending;
        result.component = result.core.component;
        preparing = false;
        return result;
    }
    // Settings I/O can remain pending after GUI producers are physically safe.
    // Close core admission now, but do not admit auxiliary saves or commitment.
    if (guiPending) {
        result.status = Status::Pending;
        result.component = "GUI workers";
        preparing = false;
        return result;
    }
    if (result.core.status != dcpp::DCContext::ShutdownStatus::Ready && !failed)
        failed = result.core.component;
    const bool ownersReady = !failed && guiStopped &&
        result.core.status == dcpp::DCContext::ShutdownStatus::Ready;
    // A failed GUI receipt may leave hub callbacks live. Read auxiliary intent
    // only after either GUI listener retirement or a successful core drain.
    bool auxiliaryRetryable = false;
    if (guiStopped || result.core.status == dcpp::DCContext::ShutdownStatus::Ready) {
        check("Retained checkpoint owners", [&] { bindAuxiliaryOwners(); return auxiliaryOwnersMatch(); });
        const auto saved = saveAuxiliary(asynchronousPersistence);
        auxiliaryRetryable = saved.retryable;
        if(saved.status == Status::Pending) {
            result.status = Status::Pending;
            result.component = saved.component;
            preparing = false;
            return result;
        }
        if(saved.status != Status::Ready && !failed) failed = saved.component;
    }
    const bool settingsSaved = check("GUI settings", [&] { return context.settings()->saveChecked(); });
    checkpointRetryAllowed = ownersReady && settingsSaved && failed && auxiliaryRetryable;
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

ApplicationQuitCoordinator::Result ApplicationQuitCoordinator::retryCheckpoint(bool asynchronousPersistence) noexcept {
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
    // Successful receipts stay cached; failed writes retain frozen intent.
    // No producer, core checkpoint, GUI/Torrent fence or settings sync is replayed.
    const auto saved = saveAuxiliary(asynchronousPersistence);
    result.status = saved.status;
    result.component = saved.component;
    checkpointRetryAllowed = saved.status == Status::Pending ||
        (saved.status == Status::Refused && saved.retryable);
    preparing = false;
    return result;
}
}
