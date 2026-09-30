#pragma once
#include "UpdateTypes.h"
#include <QObject>

namespace updates {
class UpdateBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void check(quint64 operation, Channel, CheckKind) = 0;
    virtual void download(quint64 operation, const UpdateOffer&) = 0;
    virtual void cancel() = 0;
    virtual void armInstallOnQuit(const UpdateOffer&, bool relaunch) = 0;
    virtual void revokeInstallConsent() = 0;
signals:
    void available(quint64 operation, updates::UpdateOffer);
    void downloadReady(quint64 operation, quint64 build, QByteArray sha256);
    void progress(quint64 operation, qint64 received, qint64 total);
    void failed(quint64 operation, QString redactedMessage, updates::FailureCategory category);
    void noUpdate(quint64 operation);
};
}
