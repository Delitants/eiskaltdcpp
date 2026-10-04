#pragma once
#include "ApplicationQuitCoordinator.h"
#include <QObject>
#include <QPointer>

namespace updates {
// Owned outside the retained GUI QObject tree. Borrowed context and updater
// must survive a pending or refused quit; this object never exits the process.
class ApplicationQuitSession final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Preparing, AwaitingHandoff, ExitQueued, ExitReady, Refused };
    explicit ApplicationQuitSession(QtContext&, UpdateController* = nullptr);
    State state() const { return current; }
    void request(uint32_t socketDrainTimeoutMs);
signals:
    void exitReady();
    void refused(QString component);
private:
    void queueExit();
    void refuse(const QString&);
    ApplicationQuitCoordinator coordinator;
    QPointer<UpdateController> updater;
    bool hadUpdater;
    State current = State::Idle;
};
}
