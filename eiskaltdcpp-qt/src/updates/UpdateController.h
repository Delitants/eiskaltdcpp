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
    using Clock = std::function<QDateTime()>;
    // Settings and backend outlive this object. Identity comes from verified
    // installed-bundle metadata, never a displayed prerelease version string.
    UpdateController(WulforSettings&, UpdateBackend&, UpdateIdentity,
        Clock = QDateTime::currentDateTimeUtc, QObject* parent = nullptr);
    ~UpdateController() override;
    UpdatePreferences preferences() const;
    State state() const;
    UpdateOffer offer() const;
    QDateTime lastAttempt() const;
    QDateTime lastSuccess() const;
    bool supportsInstallation() const;
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
    // Called only after application shutdown has successfully committed.
    void normalQuitCommitted();
    void shutdown();
signals:
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
