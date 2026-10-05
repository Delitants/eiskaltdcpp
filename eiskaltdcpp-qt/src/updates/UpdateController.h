#pragma once
#include "UpdateTypes.h"
#include <QDateTime>
#include <QObject>
#include <functional>
#include <memory>

class WulforSettings;
namespace updates {
class UpdateBackend;
class UpdateController final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Checking, Available, Downloading, Ready, Handoff, Stopped };
    Q_ENUM(State)
    enum class QuitDisposition { NotCommitted, NoUpdate, AwaitingHandoff, Prepared, Refused };
    Q_ENUM(QuitDisposition)
    using Clock = std::function<QDateTime()>;
    // Settings and backend outlive this object. Identity comes from verified
    // installed-bundle metadata, never a displayed prerelease version string.
    UpdateController(WulforSettings&, UpdateBackend&, UpdateIdentity,
        Clock = QDateTime::currentDateTimeUtc, QObject* parent = nullptr);
    ~UpdateController() override;
    UpdatePreferences preferences() const;
    State state() const;
    QuitDisposition quitDisposition() const;
    UpdateOffer offer() const;
    QDateTime lastAttempt() const;
    QDateTime lastSuccess() const;
    bool supportsInstallation() const;
    // Recheck policy after a notification crosses another queued delivery boundary.
    bool notificationsAllowed(const QString& offerKey) const;
    void start();
    void poll();
    void applyPreferences(UpdatePreferences);
    void checkNow();
    void download();
    void installOnNextQuit();
    void later();
    void skipVersion();
    void cancel();
    // Terminal input/worker fence, retaining Ready/version consent. The
    // application must still check every other shutdown owner before commit.
    [[nodiscard]] bool stopWorkersForQuit();
    // Permanently disarm a prepared update before recovering a refused save.
    // A successful receipt permits only ordinary quit after repaired checkpoints.
    [[nodiscard]] bool disarmForCheckpointRecovery();
    // Called only after application shutdown has successfully committed.
    // AwaitingHandoff is not permission to exit; retain owners/event loop until
    // a matching terminal receipt. NoUpdate needs no asynchronous handoff.
    void normalQuitCommitted();
    void shutdown();
signals:
    // Only a matching terminal backend receipt can certify Prepared. NoUpdate
    // needs no asynchronous receipt; the application may inspect disposition.
    void installHandoffFinished(bool prepared);
    void stateChanged(updates::UpdateController::State);
    void notificationRequested(updates::UpdateOffer);
    void checkFinished(bool updateAvailable, updates::CheckKind);
    void errorReported(QString redactedMessage, bool manual);
    void progress(qint64 received, qint64 total);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
