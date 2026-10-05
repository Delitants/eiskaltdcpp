#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/ProxyRoute.h"
#include "MacUpdateBackend.h"
#include "MacUpdateDiscovery.h"
#include "MacUpdateInstaller.h"
#include "UpdateArchiveVerifier.h"
#include "UpdateStagingCache.h"
#include "UpdateUrlPolicy.h"
#include "UnsupportedUpdateBackend.h"
#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QPointer>
#include <QSet>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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
    return [key isKindOfClass:NSString.class] ? QString::fromCFString((__bridge CFStringRef)key).toLatin1() : QByteArray{};
}
bool routeValid(const std::shared_ptr<const dcpp::ProxyRouteSnapshot>& route) {
    return route && route->valid && route->revoked && !route->revoked->load();
}
bool prepareCache(const QString& path) {
    if(path.isEmpty() || path.contains(QChar('\0')) || !QFileInfo(path).isAbsolute()) return false;
    const QFileInfo info(path);
    const auto parentName = QFile::encodeName(info.absolutePath());
    const auto leaf = QFile::encodeName(info.fileName());
    const int parent = ::open(parentName.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if(parent < 0) return false;
    struct stat before{}, named{}, node{};
    const bool safe = ::fstat(parent, &before) == 0 && before.st_uid == getuid() && !(before.st_mode & 0022) &&
        !leaf.isEmpty() && leaf != "." && leaf != ".." &&
        (::mkdirat(parent, leaf.constData(), 0700) == 0 || errno == EEXIST) &&
        ::fstatat(parent, leaf.constData(), &node, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(node.st_mode) &&
        node.st_uid == getuid() && !(node.st_mode & 0077) &&
        ::lstat(parentName.constData(), &named) == 0 && named.st_dev == before.st_dev && named.st_ino == before.st_ino;
    ::close(parent);
    return safe;
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
        std::atomic_bool cancelled{false};
        bool valid = false;
        bool restoring = false, authenticated = false;
        quint64 epoch = 0;
        std::unique_ptr<QThread> worker;
        ~Verification() { if(worker) worker->wait(); }
    };
    MacUpdateBackend& owner;
    dcpp::DCContext& context;
    QString fixture, cacheDirectory;
    const std::optional<UpdateRuntimeConfig> runtime;
    const bool requiresRuntime;
    const QByteArray publicKey;
    const QUrl feedUrl;
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
    bool quitFenced = false, quitFenceSucceeded = false, quitFenceFailed = false, quitWasReady = false;
    std::unique_ptr<MacUpdateInstaller> installer;
    std::shared_ptr<Stage> installerStage;
    QSet<quint64> handoffRequests;
    quint64 handoffOperation = 0;
    bool handoffPending = false, handoffRevoked = false;

    Impl(MacUpdateBackend& o, dcpp::DCContext& c, QString bundle, transport_detail::Backend wire, QString cache,
        std::optional<UpdateRuntimeConfig> config, bool required)
        : owner(o), context(c), fixture(std::move(bundle)), cacheDirectory(std::move(cache)),
          runtime(std::move(config)), requiresRuntime(required),
          publicKey(runtime ? runtime->publicKeyBase64 : QByteArray{}),
          feedUrl(runtime ? runtime->feedUrl : feedUrlForCpu(QSysInfo::buildCpuArchitecture())),
          transport(new UpdateTransport(c, std::move(wire), nullptr)),
          discovery(runtime || required ? new MacUpdateDiscovery(fixture, runtime, nullptr) :
              new MacUpdateDiscovery(fixture, nullptr)) {
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] { poll(); });
        QObject::connect(transport.get(), &UpdateTransport::finished, &owner,
            [this](quint64 id, const QString& path) { transferred(id, path); }, Qt::QueuedConnection);
        QObject::connect(transport.get(), &UpdateTransport::failed, &owner, [this](quint64 id, const QString&) {
            if(quitFenced || id != request) return;
            request = 0; transferStage.reset();
            if(phase == Feed || phase == Archive) fail(FailureCategory::Network);
        }, Qt::QueuedConnection);
        QObject::connect(transport.get(), &UpdateTransport::progress, &owner, [this](quint64 id, quint64 bytes, quint64) {
            if(quitFenced || id != request || phase != Archive || !selected) return;
            if(bytes > quint64(selected->offer.size)) { fail(FailureCategory::Verification); return; }
            emit owner.progress(operation, qint64(bytes), selected->offer.size);
        }, Qt::QueuedConnection);
        QObject::connect(discovery.get(), &MacUpdateDiscovery::available, &owner, [this](quint64 id, SignedUpdate value) {
            if(quitFenced || id != epoch || phase != Discovering) return;
            if(!routeValid(route)) { fail(FailureCategory::Network); return; }
            selected = std::move(value); phase = Available;
            emit owner.available(operation, selected->offer);
        });
        QObject::connect(discovery.get(), &MacUpdateDiscovery::noUpdate, &owner, [this](quint64 id) {
            if(quitFenced || id != epoch || phase != Discovering) return;
            if(!routeValid(route)) { fail(FailureCategory::Network); return; }
            const auto op = operation; clear(); emit owner.noUpdate(op);
        });
        QObject::connect(discovery.get(), &MacUpdateDiscovery::failed, &owner, [this](quint64 id) {
            if(!quitFenced && id == epoch && phase == Discovering) fail(FailureCategory::Verification);
        });
    }
    ~Impl() {
        clear(); timer.stop();
        // The installer joins its verifier before releasing the staged files.
        installer.reset(); installerStage.reset();
        // Transport joins its cancelable network worker before QObject teardown.
        transport.reset();
        verification.reset();
    }
    bool busy() const { return request || verification || phase == Discovering; }
    void clear() {
        revokeHandoff();
        ++epoch;
        phase = Idle;
        discovery->cancel();
        if(request) transport->cancel(request);
        if(verification) verification->cancelled.store(true);
        selected.reset(); stage.reset(); route.reset(); feedBytes.clear();
        // transferStage / Verification retain files until their worker exits.
    }
    void revokeHandoff() {
        if(!handoffPending) return;
        handoffRevoked = true;
        if(installer) installer->cancel();
        // Cancellation is a request, not a terminal receipt. Keep its stage.
    }
    void receipt(quint64 op, bool prepared) {
        QTimer::singleShot(0, &owner, [this, op, prepared] { emit owner.installHandoffFinished(op, prepared); });
    }
    void refuseHandoff(quint64 op) {
        if(handoffRequests.contains(op)) return;
        handoffRequests.insert(op);
        receipt(op, false);
    }
    void finishHandoff(quint64 op, bool prepared) {
        if(!handoffPending || op != handoffOperation) return;
        handoffPending = false;
        if(!prepared) {
            stage.reset(); selected.reset(); route.reset(); feedBytes.clear(); phase = Idle;
            installerStage.reset();
        }
        // Forward the native terminal fact, never invent a disarm receipt after
        // irreversible preparation. Prepared owners survive through process exit.
        receipt(op, prepared);
    }
    void beginHandoff(quint64 op, const UpdateOffer& offer, std::function<bool()> consent) {
        if(handoffRequests.contains(op)) return;
        if(!op || handoffOperation || !quitFenced || !quitFenceSucceeded || phase != Ready ||
            !stage || !selected || !sameOffer(offer, selected->offer) || !consent ||
            !QCoreApplication::instance() || owner.thread() != QCoreApplication::instance()->thread()) {
            refuseHandoff(op); return;
        }
        handoffRequests.insert(op);
        handoffOperation = op; handoffPending = true;
        installerStage = stage;
        const QPointer<MacUpdateBackend> guard(&owner);
        auto* const ownerThread = owner.thread();
        auto liveConsent = [guard, ownerThread, op, consent = std::move(consent)] {
            if(QThread::currentThread() != ownerThread || !guard) return false;
            auto* state = guard->impl.get();
            if(!state->handoffPending || state->handoffOperation != op || state->handoffRevoked) return false;
            bool allowed = false;
            try { allowed = consent(); } catch(...) {}
            if(!guard) return false;
            state = guard->impl.get();
            if(!allowed) state->handoffRevoked = true;
            return allowed && state->handoffPending && state->handoffOperation == op && !state->handoffRevoked;
        };
        if(!liveConsent()) { if(guard) finishHandoff(op, false); return; }
        try {
            installer = std::make_unique<MacUpdateInstaller>();
            QObject::connect(installer.get(), &MacUpdateInstaller::readyToExit, &owner,
                [this](quint64 id) { finishHandoff(id, true); }, Qt::QueuedConnection);
            QObject::connect(installer.get(), &MacUpdateInstaller::failed, &owner,
                [this](quint64 id) { finishHandoff(id, false); }, Qt::QueuedConnection);
            // The route was retired by core shutdown after the fence. Only the
            // existing private local-stage installer bridge may run now.
            if(!installer->start(op, *selected, installerStage->feed(), installerStage->archive(), true,
                std::move(liveConsent))) finishHandoff(op, false);
        } catch(...) {
            if(installer && installer->active()) revokeHandoff();
            else finishHandoff(op, false);
        }
    }
    void report(quint64 op, FailureCategory category) {
        if(quitFenced) return;
        const auto generation = epoch;
        QTimer::singleShot(0, &owner, [this, op, category, generation] {
            if(quitFenced || epoch != generation) return;
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
    void verify(bool restoring) {
        auto job = std::make_unique<Verification>();
        job->stage = stage; job->epoch = epoch; job->restoring = restoring;
        auto* value = job.get();
        const auto selectedRoute = route;
        const auto selection = *selected;
        const auto bytes = feedBytes;
        // Legacy private fixtures edit their plist before discovery. Do not
        // preload NSBundle there; production always uses the admitted snapshot.
        const auto key = runtime ? publicKey : pinnedKey(fixture);
        const auto cache = cacheDirectory;
        try {
            job->worker.reset(QThread::create([value, key, selection, bytes, selectedRoute, cache] {
                const auto stopped = [value, selectedRoute] { return value->cancelled.load() || !routeValid(selectedRoute); };
                try {
                    // Reauthenticate the exact selection under the current installed key.
                    const auto feed = verifyUpdateFeed(bytes, key, stopped);
                    if(feed) for(const auto& item : feed->releases)
                        if(sameOffer(item.offer, selection.offer) && item.archiveUrl == selection.archiveUrl &&
                            item.archiveSignatureBase64 == selection.archiveSignatureBase64) value->authenticated = true;
                    if(value->authenticated && !stopped()) {
                        if(value->restoring) {
                            value->valid = prepareCache(cache) && restoreVerifiedStage(cache, bytes, selection, key,
                                value->stage->archive(), stopped);
                        } else {
                            const ArchiveExpectation expected{selection.offer.size, selection.offer.sha256, selection.archiveSignatureBase64};
                            value->valid = verifyUpdateArchive(value->stage->archive(), expected, key, stopped);
                            // Cache storage is optional; its failure cannot revoke a verified download.
                            if(value->valid && !stopped() && prepareCache(cache))
                                saveVerifiedStage(cache, bytes, selection, key, value->stage->archive(), stopped);
                        }
                    }
                } catch(...) { value->valid = false; }
            }));
            job->worker->start();
        } catch(...) { fail(FailureCategory::Storage); return; }
        verification = std::move(job); phase = Verifying; timer.start();
    }
    void transferred(quint64 id, const QString& path) {
        if(quitFenced || id != request) return;
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
            verify(false);
        }
    }
    void poll() {
        if(quitFenced) return;
        if(phase != Idle && !routeValid(route)) { fail(FailureCategory::Network); return; }
        if(verification && verification->worker->wait(0)) {
            const bool current = verification->epoch == epoch && phase == Verifying;
            const bool valid = verification->valid && !verification->cancelled.load();
            const bool miss = verification->restoring && verification->authenticated && !verification->cancelled.load();
            verification.reset();
            if(current) {
                if(!valid && miss && selected && routeValid(route)) {
                    phase = Archive;
                    fetch(selected->archiveUrl, ResourceKind::Archive, stage->archive());
                    return;
                }
                if(!valid || !selected) { fail(FailureCategory::Verification); return; }
                phase = Ready;
                emit owner.downloadReady(operation, selected->offer.build, selected->offer.sha256);
                return;
            }
        }
        if(phase == Idle && !busy()) timer.stop();
    }
    bool stopWorkersForQuit() {
        if(quitFenceSucceeded || quitFenceFailed) return quitFenceSucceeded;
        try {
            if(!quitFenced) {
                quitFenced = true;
                quitWasReady = phase == Ready && stage && selected && routeValid(route);
                ++epoch; request = 0; timer.stop();
                discovery->cancel();
                if(verification) verification->cancelled.store(true);
            }
            const bool transportDrained = transport->stopWorkersForQuit();
            if(verification && !verification->worker->wait(0)) return false;
            if(!transportDrained) {
                if(!transport->quitWorkersPending()) quitFenceFailed = true;
                return false;
            }
            verification.reset();
            // Neither stage owner is released until its worker is physically
            // joined. A transport refusal retains all remaining dependencies.
            transferStage.reset();
            // Freeze only a prior Ready result whose route is still valid.
            // Later intentional core retirement must not trigger poll/clear.
            if(!quitWasReady || !routeValid(route)) {
                phase = Idle;
                selected.reset(); stage.reset(); route.reset(); feedBytes.clear();
            }
            quitFenceSucceeded = true;
        } catch(...) {
            quitFenceFailed = true;
        }
        return quitFenceSucceeded;
    }
};
MacUpdateBackend::MacUpdateBackend(dcpp::DCContext& c, QObject* p)
    : UpdateBackend(p), impl(new Impl(*this, c, {}, {},
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/org.delitants.eiskaltdcpp-updates",
        currentMacUpdateRuntime(), true)) {}
std::unique_ptr<UpdateBackend> MacUpdateBackend::create(dcpp::DCContext& c) {
    auto selection = createSelection(c);
    return std::move(selection.backend);
}
UpdateBackendSelection MacUpdateBackend::createSelection(dcpp::DCContext& c) {
    const auto runtime = currentMacUpdateRuntime();
    if(!runtime) return {std::make_unique<UnsupportedUpdateBackend>(), {}};
    return {std::unique_ptr<UpdateBackend>(new MacUpdateBackend(c, {}, {},
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/org.delitants.eiskaltdcpp-updates", *runtime, nullptr)),
        runtime->identity};
}
MacUpdateBackend::MacUpdateBackend(dcpp::DCContext& c, const QString& fixture, transport_detail::Backend wire, const QString& cache, QObject* p)
    : UpdateBackend(p), impl(new Impl(*this, c, fixture, std::move(wire), cache, {}, false)) {}
MacUpdateBackend::MacUpdateBackend(dcpp::DCContext& c, const QString& fixture, transport_detail::Backend wire,
    const QString& cache, UpdateRuntimeConfig config, QObject* p)
    : UpdateBackend(p), impl(new Impl(*this, c, fixture, std::move(wire), cache, std::move(config), true)) {}
MacUpdateBackend::~MacUpdateBackend() = default;
void MacUpdateBackend::check(quint64 op, Channel channel, CheckKind) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(impl->quitFenced || !op) return;
    if(impl->busy()) { if(op != impl->operation) impl->report(op, FailureCategory::Network); return; }
    impl->clear(); impl->operation = op; impl->channel = channel;
    if((impl->requiresRuntime && !impl->runtime) || impl->feedUrl.isEmpty() ||
        (channel != Channel::Stable && channel != Channel::Preview)) {
        impl->fail(FailureCategory::Unsupported); return;
    }
    impl->route = impl->context.getProxyRoute() ? impl->context.getProxyRoute()->snapshot() : nullptr;
    if(!routeValid(impl->route)) { impl->fail(FailureCategory::Network); return; }
    impl->stage = std::make_shared<Impl::Stage>();
    if(!impl->stage->directory.isValid()) { impl->fail(FailureCategory::Storage); return; }
    impl->phase = Impl::Feed;
    impl->fetch(impl->feedUrl, ResourceKind::Feed, impl->stage->feed());
}
void MacUpdateBackend::download(quint64 op, const UpdateOffer& requested) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(impl->quitFenced || !op) return;
    if(impl->busy()) { if(op != impl->operation) impl->report(op, FailureCategory::Network); return; }
    impl->operation = op;
    if(impl->phase != Impl::Available || !impl->selected || !sameOffer(requested, impl->selected->offer)) {
        impl->fail(FailureCategory::Verification); return;
    }
    if(!routeValid(impl->route)) { impl->fail(FailureCategory::Network); return; }
    if(!impl->cacheDirectory.isEmpty()) { impl->verify(true); return; }
    impl->phase = Impl::Archive;
    impl->fetch(impl->selected->archiveUrl, ResourceKind::Archive, impl->stage->archive());
}
void MacUpdateBackend::cancel() { Q_ASSERT(QThread::currentThread() == thread()); impl->clear(); }
bool MacUpdateBackend::stopWorkersForQuit() {
    if(QThread::currentThread() != thread()) return false;
    return impl->stopWorkersForQuit();
}
bool MacUpdateBackend::quitWorkersPending() const {
    return QThread::currentThread() == thread() && impl->quitFenced &&
        !impl->quitFenceSucceeded && !impl->quitFenceFailed;
}
void MacUpdateBackend::beginInstallHandoff(quint64 op, const UpdateOffer& offer, std::function<bool()> consent) {
    if(QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, op] { impl->refuseHandoff(op); }, Qt::QueuedConnection);
        return;
    }
    impl->beginHandoff(op, offer, std::move(consent));
}
void MacUpdateBackend::armInstallOnQuit(const UpdateOffer&, bool) {
    // The legacy entry never grants consent or substitutes for receipt handoff.
    impl->fail(FailureCategory::Unsupported);
}
void MacUpdateBackend::revokeInstallConsent() {
    if(QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this] { impl->revokeHandoff(); }, Qt::QueuedConnection);
        return;
    }
    impl->revokeHandoff();
}
}
