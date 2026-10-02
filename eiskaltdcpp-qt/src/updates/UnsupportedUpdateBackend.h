#pragma once
#include "UpdateBackend.h"
namespace updates {
class UnsupportedUpdateBackend final : public UpdateBackend {
    Q_OBJECT
public:
    using UpdateBackend::UpdateBackend;
    void check(quint64 operation, Channel, CheckKind) override;
    void download(quint64 operation, const UpdateOffer&) override;
    void cancel() override;
    [[nodiscard]] bool stopWorkersForQuit() override;
    void armInstallOnQuit(const UpdateOffer&, bool) override;
    void revokeInstallConsent() override;
private:
    void reject(quint64);
    quint64 epoch = 0, operation = 0;
    bool stopped = false;
};
}
