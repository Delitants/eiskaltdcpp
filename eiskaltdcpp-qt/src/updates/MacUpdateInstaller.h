#pragma once
#include "UpdateFeedVerifier.h"
#include <QObject>
#include <functional>
#include <memory>

namespace updates {
// Retain outside fenced GUI roots through process exit. The caller exclusively
// owns immutable staged files through handoff. This object never quits the app.
class MacUpdateInstaller final : public QObject {
    Q_OBJECT
public:
    explicit MacUpdateInstaller(QObject* parent = nullptr);
    ~MacUpdateInstaller() override;
    bool active() const;
    QString failureReason() const;
signals:
    void readyToExit(quint64 operation);
    void failed(quint64 operation);
private:
    friend class MacUpdateBackend;
    friend struct MacUpdateInstallerTestAccess;
    bool start(quint64 operation, const SignedUpdate&, const QString& stagedFeed,
               const QString& stagedArchive, bool normalQuitCommitted,
               std::function<bool()> currentConsent);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
