#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/ProxyRoute.h"
#include "MacUpdateBackend.h"
#include "MacUpdateDiscovery.h"
#include "UpdateArchiveVerifier.h"
#include "UpdateStagingCache.h"
#include "UpdateUrlPolicy.h"
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <thread>
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
    return [key isKindOfClass:NSString.class] ? QByteArray([key UTF8String]) : QByteArray{};
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
        std::atomic_bool cancelled{false}, done{false};
        bool valid = false;
        bool restoring = false, authenticated = false;
        quint64 epoch = 0;
        std::thread worker;
        ~Verification() { if(worker.joinable()) worker.join(); }
    };
    MacUpdateBackend& owner;
    dcpp::DCContext& context;
    QString fixture, cacheDirectory;
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
    bool quitFenced = false, quitFenceSucceeded = false;

    Impl(MacUpdateBackend& o, dcpp::DCContext& c, QString bundle, transport_detail::Backend wire, QString cache)
        : owner(o), context(c), fixture(std::move(bundle)), cacheDirectory(std::move(cache)),
          transport(new UpdateTransport(c, std::move(wire), nullptr)),
          discovery(new MacUpdateDiscovery(fixture, nullptr)) {
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
        const auto key = pinnedKey(fixture);
        const auto cache = cacheDirectory;
        try {
            job->worker = std::thread([value, key, selection, bytes, selectedRoute, cache] {
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
                value->done.store(true);
            });
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
        if(verification && verification->done.load()) {
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
        if(quitFenced) return quitFenceSucceeded;
        quitFenced = true;
        ++epoch; request = 0; timer.stop();
        const bool wasReady = phase == Ready && stage && selected && routeValid(route);
        try {
            discovery->cancel();
            if(verification) verification->cancelled.store(true);
            const bool transportDrained = transport->stopWorkersForQuit();
            if(verification && verification->worker.joinable()) verification->worker.join();
            verification.reset();
            // Neither stage owner is released until its worker is physically
            // joined. A transport refusal retains all remaining dependencies.
            if(!transportDrained) return false;
            transferStage.reset();
            // Freeze only a prior Ready result whose route is still valid.
            // Later intentional core retirement must not trigger poll/clear.
            if(!wasReady || !routeValid(route)) {
                phase = Idle;
                selected.reset(); stage.reset(); route.reset(); feedBytes.clear();
            }
            quitFenceSucceeded = true;
        } catch(...) {
            // Sticky refusal: retain the backend/context, never grant consent.
        }
        return quitFenceSucceeded;
    }
};
MacUpdateBackend::MacUpdateBackend(dcpp::DCContext& c, QObject* p) : MacUpdateBackend(c, {}, {},
    QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/org.delitants.eiskaltdcpp-updates", p) {}
MacUpdateBackend::MacUpdateBackend(dcpp::DCContext& c, const QString& fixture, transport_detail::Backend wire, const QString& cache, QObject* p)
    : UpdateBackend(p), impl(new Impl(*this, c, fixture, std::move(wire), cache)) {}
MacUpdateBackend::~MacUpdateBackend() = default;
void MacUpdateBackend::check(quint64 op, Channel channel, CheckKind) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(impl->quitFenced || !op) return;
    if(impl->busy()) { if(op != impl->operation) impl->report(op, FailureCategory::Network); return; }
    impl->clear(); impl->operation = op; impl->channel = channel;
    const auto feedUrl = feedUrlForCpu(QSysInfo::buildCpuArchitecture());
    if(feedUrl.isEmpty() || (channel != Channel::Stable && channel != Channel::Preview)) {
        impl->fail(FailureCategory::Unsupported); return;
    }
    impl->route = impl->context.getProxyRoute() ? impl->context.getProxyRoute()->snapshot() : nullptr;
    if(!routeValid(impl->route)) { impl->fail(FailureCategory::Network); return; }
    impl->stage = std::make_shared<Impl::Stage>();
    if(!impl->stage->directory.isValid()) { impl->fail(FailureCategory::Storage); return; }
    impl->phase = Impl::Feed;
    impl->fetch(feedUrl, ResourceKind::Feed, impl->stage->feed());
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
void MacUpdateBackend::armInstallOnQuit(const UpdateOffer&, bool) {
    // Fail closed until the committed-quit installer handoff is integrated.
    impl->fail(FailureCategory::Unsupported);
}
void MacUpdateBackend::revokeInstallConsent() { /* No installer can be armed by this checkpoint. */ }
}
