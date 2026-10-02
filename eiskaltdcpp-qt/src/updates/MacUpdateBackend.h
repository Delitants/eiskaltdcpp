#pragma once
#include "UpdateBackend.h"
#include "UpdateTransport.h"
#include <memory>

namespace updates {
struct MacUpdateBackendTestAccess;
class MacUpdateBackend final : public UpdateBackend {
    Q_OBJECT
public:
    explicit MacUpdateBackend(dcpp::DCContext&, QObject* parent = nullptr);
    ~MacUpdateBackend() override;
    void check(quint64 operation, Channel, CheckKind) override;
    void download(quint64 operation, const UpdateOffer&) override;
    void cancel() override;
    [[nodiscard]] bool stopWorkersForQuit() override;
    void armInstallOnQuit(const UpdateOffer&, bool relaunch) override;
    void revokeInstallConsent() override;
private:
    friend struct MacUpdateBackendTestAccess;
    MacUpdateBackend(dcpp::DCContext&, const QString& fixtureBundle, transport_detail::Backend, QObject*);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
