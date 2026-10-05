#pragma once
#include "UpdateBackend.h"
#include "UpdateTransport.h"
#include "UpdateRuntimeIdentity.h"
#include <memory>

namespace updates {
struct MacUpdateBackendTestAccess;
class MacUpdateBackend final : public UpdateBackend {
    Q_OBJECT
public:
    explicit MacUpdateBackend(dcpp::DCContext&, QObject* parent = nullptr);
    // Current process bundle only. Structural eligibility does not enable
    // installation capability or start a scheduler.
    static std::unique_ptr<UpdateBackend> create(dcpp::DCContext&);
    static UpdateBackendSelection createSelection(dcpp::DCContext&);
    ~MacUpdateBackend() override;
    void check(quint64 operation, Channel, CheckKind) override;
    void download(quint64 operation, const UpdateOffer&) override;
    void cancel() override;
    [[nodiscard]] bool stopWorkersForQuit() override;
    bool quitWorkersPending() const override;
    void beginInstallHandoff(quint64 operation, const UpdateOffer&, std::function<bool()>) override;
    void armInstallOnQuit(const UpdateOffer&, bool relaunch) override;
    void revokeInstallConsent() override;
private:
    friend struct MacUpdateBackendTestAccess;
    MacUpdateBackend(dcpp::DCContext&, const QString& fixtureBundle, transport_detail::Backend, const QString& cacheDirectory, QObject*);
    MacUpdateBackend(dcpp::DCContext&, const QString& fixtureBundle, transport_detail::Backend,
        const QString& cacheDirectory, UpdateRuntimeConfig, QObject*);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
