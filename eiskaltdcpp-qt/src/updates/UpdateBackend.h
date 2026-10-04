#pragma once
#include "UpdateTypes.h"
#include <QObject>
#include <functional>

namespace updates {
class UpdateBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    // Explicit capability, never inferred from the host OS. Unaccepted native
    // handoff/recovery implementations must leave this disabled.
    virtual bool supportsInstallation() const { return false; }
    virtual void check(quint64 operation, Channel, CheckKind) = 0;
    virtual void download(quint64 operation, const UpdateOffer&) = 0;
    virtual void cancel() = 0;
    /// Owner-thread terminal admission/worker fence. Does not authorize an
    /// installer or certify staging; an unknown backend must fail closed.
    [[nodiscard]] virtual bool stopWorkersForQuit() { return false; }
    // Start only after committed application quit. Recheck currentConsent on
    // the owner thread before irreversible handoff; emit one terminal receipt.
    // Throw only before admitting work; retain owners through the receipt.
    // Unknown implementations refuse, never fall back to the old arm API.
    virtual void beginInstallHandoff(quint64 operation, const UpdateOffer&, std::function<bool()>) {
        emit installHandoffFinished(operation, false);
    }
    virtual void armInstallOnQuit(const UpdateOffer&, bool relaunch) = 0;
    virtual void revokeInstallConsent() = 0;
signals:
    void installHandoffFinished(quint64 operation, bool prepared);
    void available(quint64 operation, updates::UpdateOffer);
    void downloadReady(quint64 operation, quint64 build, QByteArray sha256);
    void progress(quint64 operation, qint64 received, qint64 total);
    void failed(quint64 operation, QString redactedMessage, updates::FailureCategory category);
    void noUpdate(quint64 operation);
};
}
