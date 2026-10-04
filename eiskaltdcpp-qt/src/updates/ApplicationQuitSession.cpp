#include "dcpp/stdinc.h"
#include "ApplicationQuitSession.h"
#include "UpdateController.h"
#include <QThread>
#include <QTimer>

namespace updates {
ApplicationQuitSession::ApplicationQuitSession(QtContext& context, UpdateController* update)
    : coordinator(context, update), updater(update), hadUpdater(update != nullptr) {
    if(update) {
        connect(update, &UpdateController::installHandoffFinished, this, [this](bool prepared) {
            if(current != State::AwaitingHandoff) return;
            if(prepared && updater && updater->quitDisposition() == UpdateController::QuitDisposition::Prepared)
                queueExit();
            else refuse(QStringLiteral("Update handoff"));
        }, Qt::QueuedConnection);
        connect(update, &QObject::destroyed, this, [this] {
            if(current == State::AwaitingHandoff) refuse(QStringLiteral("Updater owner"));
        });
    }
}
void ApplicationQuitSession::refuse(const QString& component) {
    if(current == State::ExitReady || current == State::Refused) return;
    current = State::Refused;
    emit refused(component);
}
void ApplicationQuitSession::queueExit() {
    if(current == State::ExitQueued || current == State::ExitReady || current == State::Refused) return;
    current = State::ExitQueued;
    QTimer::singleShot(0, this, [this] {
        if(current != State::ExitQueued) return;
        current = State::ExitReady;
        emit exitReady();
    });
}
void ApplicationQuitSession::request(uint32_t timeout) {
    if(QThread::currentThread() != thread() || current != State::Idle) return;
    current = State::Preparing;
    const auto result = coordinator.prepare(timeout);
    if(result.status == ApplicationQuitCoordinator::Status::NotAdmitted) {
        current = State::Idle;
        emit refused(QString::fromLatin1(result.component));
        return;
    }
    if(result.status != ApplicationQuitCoordinator::Status::Ready) {
        refuse(QString::fromLatin1(result.component)); return;
    }
    if(!hadUpdater) { queueExit(); return; }
    if(!updater) { refuse(QStringLiteral("Updater owner")); return; }
    try { updater->normalQuitCommitted(); }
    catch(...) { refuse(QStringLiteral("Update commitment")); return; }
    switch(updater->quitDisposition()) {
    case UpdateController::QuitDisposition::NoUpdate:
    case UpdateController::QuitDisposition::Prepared: queueExit(); break;
    case UpdateController::QuitDisposition::AwaitingHandoff: current = State::AwaitingHandoff; break;
    default:
        // Refused can be synthesized before native cancellation completes. It
        // is not a terminal disarm receipt and cannot authorize process exit.
        refuse(QStringLiteral("Update handoff")); break;
    }
}
}
