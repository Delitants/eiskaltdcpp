#pragma once
#include "ApplicationQuitCoordinator.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>

class QMessageBox;

namespace updates {
// Owned outside the retained GUI QObject tree. Borrowed context and updater
// must survive a pending or refused quit; this object never exits the process.
class ApplicationQuitSession final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Preparing, AwaitingHandoff, ExitQueued, ExitReady, Refused };
    explicit ApplicationQuitSession(QtContext&, UpdateController* = nullptr);
    ~ApplicationQuitSession() override;
    State state() const { return current; }
    void request(uint32_t socketDrainTimeoutMs);
signals:
    void exitReady();
    void refused(QString component);
private:
    void queueExit();
    void refuse(const QString&);
    void advance();
    void showRecoveryPrompt(const QString&);
    QString workerTimeoutMessage(const char* branch) const;
    ApplicationQuitCoordinator coordinator;
    QPointer<UpdateController> updater;
    bool hadUpdater;
    State current = State::Idle;
    QTimer workerPoll;
    QElapsedTimer workerDeadline;
    uint32_t drainTimeout = 0;
    QString lastObservedComponent;
    ApplicationQuitCoordinator::Status lastObservedStatus = ApplicationQuitCoordinator::Status::NotAdmitted;
    qint64 lastPrepareMs = 0;
    bool workerRetryAllowed = false;
    bool checkpointRetryAllowed = false, retryingCheckpoint = false;
    std::unique_ptr<QMessageBox> recoveryPrompt;
};
}
