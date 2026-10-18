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
    // Current process bundle only. Installation additionally requires the
    // explicit qualified-package build gate; selection never starts scheduling.
    static std::unique_ptr<UpdateBackend> create(dcpp::DCContext&);
    static UpdateBackendSelection createSelection(dcpp::DCContext&);
    ~MacUpdateBackend() override;
    bool supportsInstallation() const override;
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
    bool hasPendingWork() const;
    MacUpdateBackend(dcpp::DCContext&, const QString& fixtureBundle, transport_detail::Backend, const QString& cacheDirectory, QObject*);
    MacUpdateBackend(dcpp::DCContext&, const QString& fixtureBundle, transport_detail::Backend,
        const QString& cacheDirectory, UpdateRuntimeConfig, QObject*);
#ifdef EISKALT_MAC_UPDATER_INSTALLATION
    enum class QualifiedPackage { Admitted };
    MacUpdateBackend(dcpp::DCContext&, UpdateRuntimeConfig, QualifiedPackage);
#endif
    const bool installationAdmitted = false;
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
