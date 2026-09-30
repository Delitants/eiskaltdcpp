#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/ProxyRoute.h"
#include "MacUpdateBackend.h"
#include "MacUpdateDiscovery.h"
#include "UpdateArchiveVerifier.h"
#include <QFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <thread>
#import <Foundation/Foundation.h>

namespace updates {
namespace {
bool sameOffer(const UpdateOffer& a, const UpdateOffer& b) {
    return a.build == b.build && a.sha256 == b.sha256 && a.size == b.size && a.version == b.version &&
        a.bundleId == b.bundleId && a.cpu == b.cpu && a.minimumOs == b.minimumOs && a.channel == b.channel && a.notes == b.notes;
}
QByteArray pinnedKey(const QString& fixture) {
    NSBundle* bundle = fixture.isEmpty() ? NSBundle.mainBundle : [NSBundle bundleWithPath:
        [NSString stringWithUTF8String:fixture.toUtf8().constData()]];
    id key = [bundle objectForInfoDictionaryKey:@"SUPublicEDKey"];
    return [key isKindOfClass:NSString.class] ? QByteArray([key UTF8String]) : QByteArray{};
}
bool routeValid(const std::shared_ptr<const dcpp::ProxyRouteSnapshot>& route) {
    return route && route->valid && route->revoked && !route->revoked->load();
}
}
struct MacUpdateBackend::Impl {
    enum Phase { Idle, Feed, Discovering, Available, Archive, Verifying, Ready };
    struct Stage {
        QTemporaryDir directory;
        QString feed() const { return directory.filePath("appcast.xml"); }
        QString archive() const { return directory.filePath("update.zip"); }
    };
    struct Verification {
        std::shared_ptr<Stage> stage;
        std::atomic_bool cancelled{false}, done{false};
        bool valid = false;
        quint64 epoch = 0;
        std::thread worker;
        ~Verification() { if(worker.joinable()) worker.join(); }
    };
    MacUpdateBackend& owner;
    dcpp::DCContext& context;
    QString fixture;
    std::unique_ptr<UpdateTransport> transport;
    std::unique_ptr<MacUpdateDiscovery> discovery;
    std::shared_ptr<Stage> stage, transferStage;
    std::shared_ptr<const dcpp::ProxyRouteSnapshot> route;
    std::optional<SignedUpdate> selected;
    std::unique_ptr<Verification> verification;
    QTimer timer;
    Phase phase = Idle;
    Channel channel = Channel::Stable;
    quint64 operation = 0, epoch = 0, request = 0;
    QByteArray feedBytes;

    Impl(MacUpdateBackend& o, dcpp::DCContext& c, QString bundle, transport_detail::Backend wire)
        : owner(o), context(c), fixture(std::move(bundle)),
          transport(new UpdateTransport(c, std::move(wire), nullptr)),
          discovery(new MacUpdateDiscovery(fixture, nullptr)) {
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] { poll(); });
        QObject::connect(transport.get(), &UpdateTransport::finished, &owner,
            [this](quint64 id, const QString& path) { transferred(id, path); }, Qt::QueuedConnection);
        QObject::connect(transport.get(), &UpdateTransport::failed, &owner, [this](quint64 id, const QString&) {
            if(id != request) return;
            request = 0; transferStage.reset();
            if(phase == Feed || phase == Archive) fail(FailureCategory::Network);
        }, Qt::QueuedConnection);
        QObject::connect(transport.get(), &UpdateTransport::progress, &owner, [this](quint64 id, quint64 bytes, quint64) {
            if(id != request || phase != Archive || !selected) return;
            if(bytes > quint64(selected->offer.size)) { fail(FailureCategory::Verification); return; }
            emit owner.progress(operation, qint64(bytes), selected->offer.size);
        }, Qt::QueuedConnection);
        QObject::connect(discovery.get(), &MacUpdateDiscovery::available, &owner, [this](quint64 id, SignedUpdate value) {
            if(id != epoch || phase != Discovering) return;
            if(!routeValid(route)) { fail(FailureCategory::Network); return; }
            selected = std::move(value); phase = Available;
            emit owner.available(operation, selected->offer);
        });
        QObject::connect(discovery.get(), &MacUpdateDiscovery::noUpdate, &owner, [this](quint64 id) {
            if(id != epoch || phase != Discovering) return;
            if(!routeValid(route)) { fail(FailureCategory::Network); return; }
            const auto op = operation; clear(); emit owner.noUpdate(op);
        });
        QObject::connect(discovery.get(), &MacUpdateDiscovery::failed, &owner, [this](quint64 id) {
            if(id == epoch && phase == Discovering) fail(FailureCategory::Verification);
        });
    }
    ~Impl() {
        clear(); timer.stop();
        // Transport joins its cancelable network worker before QObject teardown.
        transport.reset();
        verification.reset();
    }
    bool busy() const { return request || verification || phase == Discovering; }
    void clear() {
        ++epoch;
        phase = Idle;
        discovery->cancel();
        if(request) transport->cancel(request);
        if(verification) verification->cancelled.store(true);
        selected.reset(); stage.reset(); route.reset(); feedBytes.clear();
        // transferStage / Verification retain files until their worker exits.
    }
    void report(quint64 op, FailureCategory category) {
        const auto generation = epoch;
        QTimer::singleShot(0, &owner, [this, op, category, generation] {
            if(epoch != generation) return;
            const auto message = category == FailureCategory::Unsupported ? owner.tr("Update installation is not available in this build.") :
                category == FailureCategory::Verification ? owner.tr("Update verification failed. No update was installed.") :
                category == FailureCategory::Storage ? owner.tr("Unable to create private update staging.") :
                owner.tr("Update operation cancelled, busy, or unavailable on the selected connection route.");
            emit owner.failed(op, message, category);
        });
    }
    void fail(FailureCategory category) { const auto op = operation; clear(); report(op, category); }
    void fetch(const QUrl& url, ResourceKind kind, const QString& path) {
        transferStage = stage;
        request = transport->fetch(url, kind, path, route);
        timer.start();
    }
    void transferred(quint64 id, const QString& path) {
        if(id != request) return;
        request = 0; transferStage.reset();
        if(phase != Feed && phase != Archive) return;
        if(!stage || !routeValid(route)) { fail(FailureCategory::Network); return; }
        if(phase == Feed) {
            QFile file(path);
            if(path != stage->feed() || !file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024) {
                fail(FailureCategory::Verification); return;
            }
            feedBytes = file.readAll(); phase = Discovering;
            if(!discovery->check(epoch, feedBytes, channel, route)) fail(FailureCategory::Verification);
        } else {
            if(path != stage->archive() || !selected) { fail(FailureCategory::Verification); return; }
            const auto key = pinnedKey(fixture);
            // Reauthenticate against the currently installed key before archive work.
            const auto feed = verifyUpdateFeed(feedBytes, key);
            bool bound = false;
            if(feed) for(const auto& item : feed->releases)
                if(sameOffer(item.offer, selected->offer) && item.archiveUrl == selected->archiveUrl &&
                    item.archiveSignatureBase64 == selected->archiveSignatureBase64) bound = true;
            if(!bound) { fail(FailureCategory::Verification); return; }
            auto job = std::make_unique<Verification>();
            job->stage = stage; job->epoch = epoch;
            auto* value = job.get();
            const auto selectedRoute = route;
            const ArchiveExpectation expected{selected->offer.size, selected->offer.sha256, selected->archiveSignatureBase64};
            try {
                job->worker = std::thread([value, key, expected, selectedRoute] {
                    value->valid = verifyUpdateArchive(value->stage->archive(), expected, key,
                        [value, selectedRoute] { return value->cancelled.load() || !routeValid(selectedRoute); });
                    value->done.store(true);
                });
            } catch(...) { fail(FailureCategory::Storage); return; }
            verification = std::move(job); phase = Verifying;
        }
    }
    void poll() {
        if(phase != Idle && !routeValid(route)) { fail(FailureCategory::Network); return; }
        if(verification && verification->done.load()) {
            const bool current = verification->epoch == epoch && phase == Verifying;
            const bool valid = verification->valid && !verification->cancelled.load();
            verification.reset();
            if(current) {
                if(!valid || !selected) { fail(FailureCategory::Verification); return; }
                phase = Ready;
                emit owner.downloadReady(operation, selected->offer.build, selected->offer.sha256);
                return;
            }
        }
        if(phase == Idle && !busy()) timer.stop();
    }
};
MacUpdateBackend::MacUpdateBackend(dcpp::DCContext& c, QObject* p) : MacUpdateBackend(c, {}, {}, p) {}
MacUpdateBackend::MacUpdateBackend(dcpp::DCContext& c, const QString& fixture, transport_detail::Backend wire, QObject* p)
    : UpdateBackend(p), impl(new Impl(*this, c, fixture, std::move(wire))) {}
MacUpdateBackend::~MacUpdateBackend() = default;
void MacUpdateBackend::check(quint64 op, Channel channel, CheckKind) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(!op) return;
    if(impl->busy()) { if(op != impl->operation) impl->report(op, FailureCategory::Network); return; }
    impl->clear(); impl->operation = op; impl->channel = channel;
    const auto cpu = QSysInfo::buildCpuArchitecture();
    if((cpu != "arm64" && cpu != "x86_64") || (channel != Channel::Stable && channel != Channel::Preview)) {
        impl->fail(FailureCategory::Unsupported); return;
    }
    impl->route = impl->context.getProxyRoute() ? impl->context.getProxyRoute()->snapshot() : nullptr;
    if(!routeValid(impl->route)) { impl->fail(FailureCategory::Network); return; }
    impl->stage = std::make_shared<Impl::Stage>();
    if(!impl->stage->directory.isValid()) { impl->fail(FailureCategory::Storage); return; }
    impl->phase = Impl::Feed;
    impl->fetch(QUrl("https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/" + cpu + ".xml"),
        ResourceKind::Feed, impl->stage->feed());
}
void MacUpdateBackend::download(quint64 op, const UpdateOffer& requested) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(!op) return;
    if(impl->busy()) { if(op != impl->operation) impl->report(op, FailureCategory::Network); return; }
    impl->operation = op;
    if(impl->phase != Impl::Available || !impl->selected || !sameOffer(requested, impl->selected->offer)) {
        impl->fail(FailureCategory::Verification); return;
    }
    if(!routeValid(impl->route)) { impl->fail(FailureCategory::Network); return; }
    impl->phase = Impl::Archive;
    impl->fetch(impl->selected->archiveUrl, ResourceKind::Archive, impl->stage->archive());
}
void MacUpdateBackend::cancel() { Q_ASSERT(QThread::currentThread() == thread()); impl->clear(); }
void MacUpdateBackend::armInstallOnQuit(const UpdateOffer&, bool) {
    // Fail closed until the committed-quit installer handoff is integrated.
    impl->fail(FailureCategory::Unsupported);
}
void MacUpdateBackend::revokeInstallConsent() { /* No installer can be armed by this checkpoint. */ }
}
