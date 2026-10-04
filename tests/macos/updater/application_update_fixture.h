#pragma once
// Compiled only into the disposable entrypoint translation unit, never product.
#include "updates/ApplicationUpdateOwner.h"
#include "updates/MacUpdateBackend.h"
#include "updates/UpdateBackend.h"
#include "updates/UpdateController.h"
#include "dcpp/HttpRequestPolicy.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOperatingSystemVersion>
#include <QPointer>
#include <QSysInfo>
#include <atomic>
namespace updates {
struct ApplicationUpdateOwnerTestAccess {
    static std::unique_ptr<ApplicationUpdateOwner> make(WulforSettings& settings,
        std::unique_ptr<UpdateBackend> backend, UpdateIdentity identity) {
        return std::unique_ptr<ApplicationUpdateOwner>(new ApplicationUpdateOwner(settings, std::move(backend), std::move(identity)));
    }
};
struct MacUpdateBackendTestAccess {
    static std::unique_ptr<MacUpdateBackend> make(dcpp::DCContext& context, transport_detail::Backend wire) {
        return std::unique_ptr<MacUpdateBackend>(new MacUpdateBackend(context, {}, std::move(wire), {}, nullptr));
    }
};
inline QPointer<UpdateController> fixtureController;
inline std::shared_ptr<std::atomic_int> fixtureRequests = std::make_shared<std::atomic_int>(0);
inline QByteArray fixtureRead(const QString& path) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) qFatal("Missing signed application fixture");
    return file.readAll();
}
class FixtureApplicationBackend final : public UpdateBackend {
    std::unique_ptr<MacUpdateBackend> native;
public:
    FixtureApplicationBackend(dcpp::DCContext& context, QByteArray feed, QByteArray archive) {
        native = MacUpdateBackendTestAccess::make(context,
            [feed = std::move(feed), archive = std::move(archive), count = fixtureRequests]
            (const auto& policy, const std::string& url, const auto& sink) {
                if(policy.cancelled()) throw std::runtime_error("Cancelled fixture request");
                ++*count;
                const auto& bytes = url.find("raw.githubusercontent.com") != std::string::npos ? feed : archive;
                sink(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size());
            });
        connect(native.get(), &UpdateBackend::available, this, &UpdateBackend::available);
        connect(native.get(), &UpdateBackend::noUpdate, this, &UpdateBackend::noUpdate);
        connect(native.get(), &UpdateBackend::downloadReady, this, &UpdateBackend::downloadReady);
        connect(native.get(), &UpdateBackend::failed, this, &UpdateBackend::failed);
        connect(native.get(), &UpdateBackend::progress, this, &UpdateBackend::progress);
        connect(native.get(), &UpdateBackend::installHandoffFinished, this, &UpdateBackend::installHandoffFinished);
    }
    // Fixture capability only; MacUpdateBackend's product capability stays OFF.
    bool supportsInstallation() const override { return true; }
    void check(quint64 id, Channel channel, CheckKind kind) override { native->check(id, channel, kind); }
    void download(quint64 id, const UpdateOffer& offer) override { native->download(id, offer); }
    void cancel() override { native->cancel(); }
    bool stopWorkersForQuit() override { return native->stopWorkersForQuit(); }
    void beginInstallHandoff(quint64 id, const UpdateOffer& offer, std::function<bool()> consent) override {
        native->beginInstallHandoff(id, offer, std::move(consent));
    }
    void armInstallOnQuit(const UpdateOffer&, bool) override { qFatal("Legacy fixture installer call"); }
    void revokeInstallConsent() override { native->revokeInstallConsent(); }
};
class FixtureUpdateOwner final {
    std::unique_ptr<ApplicationUpdateOwner> owner;
public:
    FixtureUpdateOwner(WulforSettings& settings, dcpp::DCContext& context) {
        const auto root = qEnvironmentVariable("EISKALT_APPLICATION_FIXTURE");
        if(!root.startsWith("/private/tmp/eiskalt-app-quit-native-") ||
            !qEnvironmentVariable("HOME").startsWith(root + "/")) qFatal("Not a disposable application fixture");
        const auto metadata = QJsonDocument::fromJson(fixtureRead(root + "/fixture/fixture.json")).object();
        const auto bundle = metadata.value("bundle_id").toString();
        if(!bundle.startsWith("org.eiskaltdcpp.updater-probe.")) qFatal("Foreign fixture identity");
        const auto os = QOperatingSystemVersion::current();
        UpdateIdentity identity{1, Channel::Stable, bundle, QSysInfo::buildCpuArchitecture(),
            QVersionNumber(os.majorVersion(), os.minorVersion(), qMax(0, os.microVersion()))};
        owner = ApplicationUpdateOwnerTestAccess::make(settings,
            std::make_unique<FixtureApplicationBackend>(context, fixtureRead(root + "/fixture/appcast.xml"),
                fixtureRead(root + "/fixture/update.zip")), std::move(identity));
        fixtureController = owner->controller();
        QObject::connect(fixtureController.data(), &UpdateController::errorReported, fixtureController.data(),
            [](const QString& error, bool) { qFatal("Signed full-application fixture rejected: %s", qPrintable(error)); });
    }
    UpdateController* controller() const { return owner->controller(); }
    static bool finishNormalQuit(std::unique_ptr<FixtureUpdateOwner>& lease) {
        if(!lease || !ApplicationUpdateOwner::finishNormalQuit(lease->owner)) return false;
        lease.reset(); return true;
    }
};
}
