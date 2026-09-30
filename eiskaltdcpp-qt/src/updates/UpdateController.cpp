#include "UpdateController.h"
#include "UpdateBackend.h"
#include "UpdatePolicy.h"
#include "WulforSettings.h"
#include <QThread>
#include <QTimer>
#include <atomic>
namespace updates {
namespace {
quint64 nextOperation() {
    // A backend can outlive a controller; never reuse its previous operation IDs.
    static std::atomic<quint64> sequence{0};
    return ++sequence;
}
}
struct UpdateController::Impl {
    UpdateController& owner;
    WulforSettings& settings;
    UpdateBackend& backend;
    UpdateIdentity identity;
    Clock clock;
    QTimer timer;
    UpdatePreferences preferences;
    UpdateOffer offer;
    State state = State::Idle;
    CheckKind kind = CheckKind::Automatic;
    QDateTime attempt, success;
    QString ready, consent;
    quint64 operation = 0;
    bool started = false, stopped = false, busy = false;

    Impl(UpdateController& o, WulforSettings& s, UpdateBackend& b, UpdateIdentity i, Clock c)
        : owner(o), settings(s), backend(b), identity(std::move(i)), clock(std::move(c)) {
        preferences = readPreferences();
        attempt = date("updates/lastAttempt"); success = date("updates/lastSuccess");
        timer.setInterval(60000);
        QObject::connect(&timer, &QTimer::timeout, &owner, [&o] { o.poll(); });
    }
    void assertThread() const { Q_ASSERT(QThread::currentThread() == owner.thread()); }
    QDateTime date(const QString& key) const { return QDateTime::fromString(settings.getStr(key), Qt::ISODateWithMs); }
    void saveDate(const QString& key, QDateTime value) { settings.setStr(key, value.toUTC().toString(Qt::ISODateWithMs)); settings.save(); }
    UpdatePreferences readPreferences() const {
        UpdatePreferences result;
        result.checks = settings.getBool("updates/checks", true);
        result.notifications = settings.getBool("updates/notifications", true);
        result.autoInstall = settings.getBool("updates/autoInstall", false);
        const auto channel = settings.getStr("updates/channel", identity.defaultChannel == Channel::Preview ? "preview" : "stable");
        result.channel = channel == "preview" ? Channel::Preview : Channel::Stable;
        return result;
    }
    void setState(State value) {
        state = value;
        // UI callbacks are deferred so they may safely destroy the controller.
        QMetaObject::invokeMethod(&owner, [this, value] {
            if(state == value) emit owner.stateChanged(value);
        }, Qt::QueuedConnection);
    }
    bool matches(quint64 id, State phase) const { return !stopped && busy && id == operation && state == phase; }
    void revoke() { ready.clear(); consent.clear(); backend.revokeInstallConsent(); }
    void cancelOperation() {
        operation = nextOperation(); busy = false;
        revoke(); backend.cancel();
    }
    void beginCheck(CheckKind nextKind) {
        assertThread();
        const auto now = clock();
        if(stopped || !UpdatePolicy::mayCheck(preferences, nextKind, now, attempt, busy) || identity.build == 0) return;
        revoke(); offer = {}; kind = nextKind; busy = true; operation = nextOperation();
        attempt = now; saveDate("updates/lastAttempt", now);
        setState(State::Checking);
        backend.check(operation, preferences.channel, kind);
    }
    void finishCheck(quint64 id, const UpdateOffer* found) {
        if(!matches(id, State::Checking)) return;
        busy = false;
        success = clock(); saveDate("updates/lastSuccess", success);
        const bool accepted = found && UpdatePolicy::accepts(identity, preferences, *found) &&
            settings.getStr("updates/skipped") != found->key();
        const auto checkedKind = kind;
        QMetaObject::invokeMethod(&owner, [this, id, accepted, checkedKind] {
            if(!stopped && operation == id) emit owner.checkFinished(accepted, checkedKind);
        }, Qt::QueuedConnection);
        if(accepted) {
            offer = *found;
            const auto key = offer.key();
            setState(State::Available);
            if(kind == CheckKind::Automatic) {
                QMetaObject::invokeMethod(&owner, [this, id, key] {
                    if(stopped || operation != id || offer.key() != key || !preferences.notifications ||
                        settings.getStr("updates/notified") == key || settings.getStr("updates/deferred") == key) return;
                    settings.setStr("updates/notified", key); settings.save();
                    emit owner.notificationRequested(offer);
                }, Qt::QueuedConnection);
            }
            QMetaObject::invokeMethod(&owner, [this, id, key] {
                if(!stopped && operation == id && state == State::Available && offer.key() == key && preferences.checks && preferences.autoInstall &&
                    settings.getStr("updates/deferred") != key) beginDownload(CheckKind::Automatic);
            }, Qt::QueuedConnection);
        } else {
            offer = {}; setState(State::Idle);
        }
    }
    void beginDownload(CheckKind nextKind) {
        assertThread();
        if(stopped || busy || state != State::Available || !UpdatePolicy::accepts(identity, preferences, offer) ||
            settings.getStr("updates/skipped") == offer.key()) return;
        revoke();
        if(nextKind == CheckKind::Manual) { settings.setStr("updates/deferred", {}); settings.save(); }
        busy = true; kind = nextKind; operation = nextOperation();
        setState(State::Downloading);
        backend.download(operation, offer);
    }
    void fail(quint64 id, FailureCategory category) {
        if(stopped || !busy || operation != id) return;
        const bool manual = kind == CheckKind::Manual;
        cancelOperation();
        offer = {}; setState(State::Idle);
        const auto now = clock();
        const int code = static_cast<int>(category);
        if(code < 0 || code > 3) category = FailureCategory::Verification;
        const auto key = "updates/errorAt/" + QString::number(static_cast<int>(category));
        const auto previous = date(key);
        if(!manual && previous.isValid() && previous.secsTo(now) < 3600) return;
        if(!manual) saveDate(key, now);
        QString message;
        switch(category) {
        case FailureCategory::Network: message = tr("The update connection failed. No direct fallback was used."); break;
        case FailureCategory::Verification: message = tr("The update could not be verified and will not be installed."); break;
        case FailureCategory::Storage: message = tr("The update could not be saved safely."); break;
        case FailureCategory::Unsupported: message = tr("Update installation is not supported on this system."); break;
        }
        QMetaObject::invokeMethod(&owner, [this, message, manual] {
            if(!stopped) emit owner.errorReported(message, manual);
        }, Qt::QueuedConnection);
    }
};

UpdateController::UpdateController(WulforSettings& settings, UpdateBackend& backend, UpdateIdentity identity, Clock clock, QObject* parent)
    : QObject(parent), impl(std::make_unique<Impl>(*this, settings, backend, std::move(identity), std::move(clock))) {
    qRegisterMetaType<UpdateOffer>(); qRegisterMetaType<FailureCategory>(); qRegisterMetaType<CheckKind>();
    connect(&backend, &UpdateBackend::available, this, [this](quint64 id, const UpdateOffer& offer) { impl->finishCheck(id, &offer); }, Qt::QueuedConnection);
    connect(&backend, &UpdateBackend::noUpdate, this, [this](quint64 id) { impl->finishCheck(id, nullptr); }, Qt::QueuedConnection);
    connect(&backend, &UpdateBackend::failed, this, [this](quint64 id, const QString&, FailureCategory category) { impl->fail(id, category); }, Qt::QueuedConnection);
    connect(&backend, &UpdateBackend::downloadReady, this, [this](quint64 id, quint64 build, const QByteArray& digest) {
        if(!impl->matches(id, State::Downloading)) return;
        if(build != impl->offer.build || digest != impl->offer.sha256) { impl->fail(id, FailureCategory::Verification); return; }
        impl->busy = false; impl->ready = impl->offer.key(); impl->setState(State::Ready);
    }, Qt::QueuedConnection);
    connect(&backend, &UpdateBackend::progress, this, [this](quint64 id, qint64 received, qint64 total) {
        if(impl->matches(id, State::Downloading) && received >= 0 && total >= 0 && total <= impl->offer.size && received <= impl->offer.size &&
            (total == 0 || received <= total)) emit progress(received, total);
    }, Qt::QueuedConnection);
}
UpdateController::~UpdateController() { shutdown(); }
UpdatePreferences UpdateController::preferences() const { return impl->preferences; }
UpdateController::State UpdateController::state() const { return impl->state; }
UpdateOffer UpdateController::offer() const { return impl->offer; }
QDateTime UpdateController::lastAttempt() const { return impl->attempt; }
QDateTime UpdateController::lastSuccess() const { return impl->success; }
void UpdateController::start() {
    impl->assertThread();
    if(impl->started || impl->stopped) return;
    impl->started = true; impl->timer.start();
    QTimer::singleShot(3000, this, [this] { poll(); });
}
void UpdateController::poll() { if(impl->started && !impl->stopped) impl->beginCheck(CheckKind::Automatic); }
void UpdateController::applyPreferences(UpdatePreferences value) {
    impl->assertThread(); if(impl->stopped) return;
    if(value.channel != Channel::Stable && value.channel != Channel::Preview) value.channel = Channel::Stable;
    const bool channelChanged = value.channel != impl->preferences.channel;
    impl->preferences = value;
    impl->settings.setBool("updates/checks", value.checks);
    impl->settings.setBool("updates/notifications", value.notifications);
    impl->settings.setBool("updates/autoInstall", value.autoInstall);
    impl->settings.setStr("updates/channel", value.channel == Channel::Preview ? "preview" : "stable");
    impl->settings.save();
    if(channelChanged) {
        impl->cancelOperation(); impl->offer = {}; impl->setState(State::Idle);
    } else if(impl->busy && impl->kind == CheckKind::Automatic &&
        (!value.checks || (!value.autoInstall && impl->state == State::Downloading))) {
        cancel();
    }
}
void UpdateController::checkNow() { impl->beginCheck(CheckKind::Manual); }
void UpdateController::download() { impl->beginDownload(CheckKind::Manual); }
void UpdateController::installOnNextQuit() {
    impl->assertThread();
    if(impl->stopped || impl->state != State::Ready || impl->ready != impl->offer.key()) return;
    impl->consent = impl->ready;
    impl->settings.setStr("updates/deferred", {}); impl->settings.save();
}
void UpdateController::later() {
    impl->assertThread(); if(impl->stopped || !impl->offer.build) return;
    impl->settings.setStr("updates/deferred", impl->offer.key()); impl->settings.save();
    impl->consent.clear(); impl->backend.revokeInstallConsent();
    if(impl->busy) cancel();
}
void UpdateController::skipVersion() {
    impl->assertThread(); if(impl->stopped || !impl->offer.build) return;
    impl->settings.setStr("updates/skipped", impl->offer.key()); impl->settings.save();
    impl->cancelOperation(); impl->offer = {}; impl->setState(State::Idle);
}
void UpdateController::cancel() {
    impl->assertThread(); if(impl->stopped) return;
    impl->cancelOperation(); impl->offer = {}; impl->setState(State::Idle);
}
void UpdateController::normalQuitCommitted() {
    impl->assertThread(); if(impl->stopped) return;
    auto preferences = impl->readPreferences();
    if(impl->settings.getStr("updates/deferred") == impl->ready) preferences.autoInstall = false;
    if(impl->state == State::Ready && impl->ready == impl->offer.key() &&
        UpdatePolicy::accepts(impl->identity, preferences, impl->offer) &&
        impl->settings.getStr("updates/skipped") != impl->ready &&
        UpdatePolicy::mayInstall(preferences, true, impl->ready, impl->consent)) {
        impl->stopped = true; impl->timer.stop(); impl->setState(State::Handoff);
        impl->backend.armInstallOnQuit(impl->offer, false);
    } else shutdown();
}
void UpdateController::shutdown() {
    impl->assertThread(); if(impl->stopped) return;
    impl->stopped = true; impl->timer.stop(); impl->cancelOperation(); impl->setState(State::Stopped);
}
}
