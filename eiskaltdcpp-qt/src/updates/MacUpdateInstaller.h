#pragma once
#include "UpdateFeedVerifier.h"
#include <QObject>
#include <functional>
#include <memory>
#include <thread>

namespace updates {
// Retain outside fenced GUI roots through process exit. The caller exclusively
// owns immutable staged files through handoff. This object never quits the app.
class MacUpdateInstaller final : public QObject {
    Q_OBJECT
public:
    enum class Stage { Verifying, Checking, Downloading, Extracting, Ready, HandingOff };
    Q_ENUM(Stage)
    explicit MacUpdateInstaller(QObject* parent = nullptr);
    ~MacUpdateInstaller() override;
    bool active() const;
    QString failureReason() const;
signals:
    void stageChanged(quint64 operation, Stage stage);
    void readyToExit(quint64 operation);
    void failed(quint64 operation);
private:
    friend class MacUpdateBackend;
    friend struct MacUpdateInstallerTestAccess;
    using WorkerStarter = std::function<std::thread(std::function<void()>)>;
    // The launcher returns the sole owned worker or throws before starting one.
    MacUpdateInstaller(QObject* parent, WorkerStarter);
    bool start(quint64 operation, const SignedUpdate&, const QString& stagedFeed,
               const QString& stagedArchive, bool normalQuitCommitted,
               std::function<bool()> currentConsent, int deadlineMilliseconds = 30000);
    // Acceptance is only a request. Retain the owner until a terminal receipt;
    // expiry cannot authorize process exit or cancellation after handoff.
    bool cancel(bool expired = false);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
