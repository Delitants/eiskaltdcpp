#include "UpdateController.h"
#include "UpdateBackend.h"
#include "UpdatePolicy.h"
#include "WulforSettings.h"
#include <QThread>
#include <QTimer>
#include <QPointer>
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
    QuitDisposition quitDisposition = QuitDisposition::NotCommitted;
    CheckKind kind = CheckKind::Automatic;
    QDateTime attempt, success;
    QString ready, consent;
    quint64 operation = 0;
    bool started = false, stopped = false, busy = false;
    bool quitFenced = false, quitFenceSucceeded = false, quitFenceTerminal = false;
    bool recoveryDisarmAttempted = false, recoveryDisarmSucceeded = false;
    bool handoffConsentRevoked = false;

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
            if(!quitFenced && state == value) emit owner.stateChanged(value);
        }, Qt::QueuedConnection);
    }
    bool matches(quint64 id, State phase) const { return !stopped && !quitFenced && busy && id == operation && state == phase; }
    void revoke() { ready.clear(); consent.clear(); backend.revokeInstallConsent(); }
    void cancelOperation() {
        operation = nextOperation(); busy = false;
        revoke(); backend.cancel();
    }
    void beginCheck(CheckKind nextKind) {
        assertThread();
        if(stopped || quitFenced) return;
        if(identity.build == 0) {
            if(nextKind == CheckKind::Manual) {
                QMetaObject::invokeMethod(&owner, [this] {
                    if(!stopped && !quitFenced)
                        emit owner.errorReported(tr("Update installation is not supported on this system."), true);
                }, Qt::QueuedConnection);
            }
            return;
        }
        const auto now = clock();
        if(!UpdatePolicy::mayCheck(preferences, nextKind, now, attempt, busy)) return;
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
            if(!stopped && !quitFenced && operation == id) emit owner.checkFinished(accepted, checkedKind);
        }, Qt::QueuedConnection);
        if(accepted) {
            offer = *found;
            const auto key = offer.key();
            setState(State::Available);
            if(kind == CheckKind::Automatic) {
                QMetaObject::invokeMethod(&owner, [this, id, key] {
                    if(stopped || quitFenced || operation != id || offer.key() != key || !preferences.notifications ||
                        settings.getStr("updates/notified") == key || settings.getStr("updates/deferred") == key) return;
                    settings.setStr("updates/notified", key); settings.save();
                    emit owner.notificationRequested(offer);
                }, Qt::QueuedConnection);
            }
            QMetaObject::invokeMethod(&owner, [this, id, key] {
                if(!stopped && !quitFenced && operation == id && state == State::Available && offer.key() == key && preferences.checks && preferences.autoInstall &&
                    settings.getStr("updates/deferred") != key) beginDownload(CheckKind::Automatic);
            }, Qt::QueuedConnection);
        } else {
            offer = {}; setState(State::Idle);
        }
    }
    void beginDownload(CheckKind nextKind) {
        assertThread();
        if(stopped || quitFenced || busy || state != State::Available || !UpdatePolicy::accepts(identity, preferences, offer) ||
            settings.getStr("updates/skipped") == offer.key()) return;
        revoke();
        if(nextKind == CheckKind::Manual) { settings.setStr("updates/deferred", {}); settings.save(); }
        busy = true; kind = nextKind; operation = nextOperation();
        setState(State::Downloading);
        backend.download(operation, offer);
    }
    void fail(quint64 id, FailureCategory category) {
        if(stopped || quitFenced || !busy || operation != id) return;
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
            if(!stopped && !quitFenced) emit owner.errorReported(message, manual);
        }, Qt::QueuedConnection);
    }
    bool consentCurrent(quint64 id, const QString& key) {
        if(quitDisposition != QuitDisposition::AwaitingHandoff || handoffConsentRevoked || operation != id) return false;
        bool approved = false;
        try {
            const auto current = readPreferences();
            approved = quitFenced && quitFenceSucceeded && state == State::Handoff && backend.supportsInstallation() &&
                ready == key && offer.key() == key && UpdatePolicy::accepts(identity, current, offer) &&
                settings.getStr("updates/deferred") != key && settings.getStr("updates/skipped") != key &&
                UpdatePolicy::mayInstall(current, true, key, consent);
        } catch(...) {}
        if(!approved) handoffConsentRevoked = true;
        return approved;
    }
    void finishHandoff(quint64 id, bool prepared) {
        if(quitDisposition != QuitDisposition::AwaitingHandoff || operation != id) return;
        prepared = prepared && !handoffConsentRevoked;
        handoffConsentRevoked = true;
        quitDisposition = prepared ? QuitDisposition::Prepared : QuitDisposition::Refused;
        if(!prepared) {
            ready.clear(); consent.clear();
            try { backend.revokeInstallConsent(); } catch(...) {}
        }
        setState(State::Stopped);
        QMetaObject::invokeMethod(&owner, [this, prepared] { emit owner.installHandoffFinished(prepared); }, Qt::QueuedConnection);
    }
};

UpdateController::UpdateController(WulforSettings& settings, UpdateBackend& backend, UpdateIdentity identity, Clock clock, QObject* parent)
    : QObject(parent), impl(std::make_unique<Impl>(*this, settings, backend, std::move(identity), std::move(clock))) {
    qRegisterMetaType<UpdateOffer>(); qRegisterMetaType<FailureCategory>(); qRegisterMetaType<CheckKind>();
    connect(&backend, &UpdateBackend::installHandoffFinished, this,
        [this](quint64 id, bool prepared) { impl->finishHandoff(id, prepared); }, Qt::QueuedConnection);
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
UpdateController::QuitDisposition UpdateController::quitDisposition() const { return impl->quitDisposition; }
UpdateOffer UpdateController::offer() const { return impl->offer; }
QDateTime UpdateController::lastAttempt() const { return impl->attempt; }
QDateTime UpdateController::lastSuccess() const { return impl->success; }
bool UpdateController::supportsInstallation() const { return impl->backend.supportsInstallation(); }
bool UpdateController::notificationsAllowed(const QString& offerKey) const {
    return QThread::currentThread() == thread() && !impl->stopped &&
        !impl->quitFenced && impl->preferences.notifications && impl->offer.build &&
        impl->offer.key() == offerKey && impl->settings.getStr("updates/deferred") != offerKey &&
        impl->settings.getStr("updates/skipped") != offerKey;
}
void UpdateController::start() {
    impl->assertThread();
    if(impl->started || impl->stopped || impl->quitFenced) return;
    impl->started = true; impl->timer.start();
    QTimer::singleShot(3000, this, [this] { poll(); });
}
void UpdateController::poll() { if(impl->started && !impl->stopped && !impl->quitFenced) impl->beginCheck(CheckKind::Automatic); }
void UpdateController::applyPreferences(UpdatePreferences value) {
    impl->assertThread(); if(impl->stopped || impl->quitFenced) return;
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
    if(impl->stopped || impl->quitFenced || impl->state != State::Ready || impl->ready != impl->offer.key()) return;
    impl->consent = impl->ready;
    impl->settings.setStr("updates/deferred", {}); impl->settings.save();
}
void UpdateController::later() {
    impl->assertThread(); if(impl->stopped || impl->quitFenced || !impl->offer.build) return;
    impl->settings.setStr("updates/deferred", impl->offer.key()); impl->settings.save();
    impl->consent.clear(); impl->backend.revokeInstallConsent();
    if(impl->busy) cancel();
}
void UpdateController::skipVersion() {
    impl->assertThread(); if(impl->stopped || impl->quitFenced || !impl->offer.build) return;
    impl->settings.setStr("updates/skipped", impl->offer.key()); impl->settings.save();
    impl->cancelOperation(); impl->offer = {}; impl->setState(State::Idle);
}
void UpdateController::cancel() {
    impl->assertThread(); if(impl->stopped || impl->quitFenced) return;
    impl->cancelOperation(); impl->offer = {}; impl->setState(State::Idle);
}
bool UpdateController::stopWorkersForQuit() {
    if(QThread::currentThread() != thread()) return false;
    if(impl->quitFenceTerminal) return impl->quitFenceSucceeded;
    if(!impl->quitFenced) {
        impl->quitFenced = true;
        impl->timer.stop(); impl->operation = nextOperation(); impl->busy = false;
    }
    try {
        impl->quitFenceSucceeded = impl->backend.stopWorkersForQuit();
        impl->quitFenceTerminal = impl->quitFenceSucceeded || !impl->backend.quitWorkersPending();
    } catch(...) { impl->quitFenceTerminal = true; }
    return impl->quitFenceSucceeded;
}
bool UpdateController::quitWorkersPending() const {
    return QThread::currentThread() == thread() && impl->quitFenced && !impl->quitFenceTerminal;
}
bool UpdateController::disarmForCheckpointRecovery() {
    if(QThread::currentThread() != thread()) return false;
    if(!impl->quitFenced || !impl->quitFenceSucceeded || impl->stopped ||
       impl->quitDisposition != QuitDisposition::NotCommitted) return false;
    if(impl->recoveryDisarmAttempted) return impl->recoveryDisarmSucceeded;
    impl->recoveryDisarmAttempted = true;
    impl->ready.clear(); impl->consent.clear();
    try {
        impl->backend.revokeInstallConsent();
        impl->recoveryDisarmSucceeded = true;
    } catch(...) { /* Unknown native disarm outcome cannot authorize exit. */ }
    return impl->recoveryDisarmSucceeded;
}
void UpdateController::normalQuitCommitted() {
    impl->assertThread(); if(impl->stopped) return;
    if(impl->recoveryDisarmAttempted) {
        impl->quitDisposition = impl->recoveryDisarmSucceeded ? QuitDisposition::NoUpdate : QuitDisposition::Refused;
        impl->stopped = true; impl->setState(State::Stopped);
        return;
    }
    if(!stopWorkersForQuit()) {
        impl->quitDisposition = QuitDisposition::Refused;
        impl->stopped = true; impl->ready.clear(); impl->consent.clear();
        try { impl->backend.revokeInstallConsent(); } catch(...) {}
        impl->setState(State::Stopped);
        return;
    }
    try {
        const auto preferences = impl->readPreferences();
        if(impl->backend.supportsInstallation() && impl->state == State::Ready && impl->ready == impl->offer.key() &&
            UpdatePolicy::accepts(impl->identity, preferences, impl->offer) &&
            impl->settings.getStr("updates/deferred") != impl->ready &&
            impl->settings.getStr("updates/skipped") != impl->ready &&
            UpdatePolicy::mayInstall(preferences, true, impl->ready, impl->consent)) {
            impl->stopped = true; impl->timer.stop(); impl->setState(State::Handoff);
            impl->quitDisposition = QuitDisposition::AwaitingHandoff;
            impl->operation = nextOperation();
            const auto id = impl->operation;
            const auto key = impl->ready;
            QPointer<UpdateController> guard(this);
            auto* ownerThread = thread();
            impl->backend.beginInstallHandoff(id, impl->offer, [guard, ownerThread, id, key] {
                // Do not dereference a guarded QObject from a foreign thread.
                if(QThread::currentThread() != ownerThread || !guard) return false;
                return guard->impl->consentCurrent(id, key);
            });
        } else {
            impl->quitDisposition = QuitDisposition::NoUpdate;
            shutdown();
        }
    } catch(...) {
        if(impl->quitDisposition == QuitDisposition::AwaitingHandoff) impl->finishHandoff(impl->operation, false);
        else {
            impl->quitDisposition = QuitDisposition::Refused;
            shutdown();
        }
    }
}
void UpdateController::shutdown() {
    impl->assertThread();
    if(impl->quitDisposition == QuitDisposition::AwaitingHandoff) impl->finishHandoff(impl->operation, false);
    if(impl->stopped) return;
    impl->stopped = true; impl->timer.stop(); impl->cancelOperation(); impl->setState(State::Stopped);
}
}
