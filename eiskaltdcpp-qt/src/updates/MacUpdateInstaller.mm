#include "MacUpdateInstaller.h"
#include "UpdateArchiveVerifier.h"
#include "UpdateLoopbackBridge.h"
#include "UpdatePolicy.h"
#include "macos/updater/BundleArchiveVerifier.h"
#include <QCoreApplication>
#include <QFile>
#include <QOperatingSystemVersion>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <thread>
#import <Sparkle/Sparkle.h>

namespace {
QString qt(NSString* s) { return [s isKindOfClass:NSString.class] ? QString::fromUtf8(s.UTF8String) : QString(); }
NSString* ns(const QString& s) { return [NSString stringWithUTF8String:s.toUtf8().constData()]; }
bool same(const updates::SignedUpdate& a, const updates::SignedUpdate& b) {
    const auto& x = a.offer; const auto& y = b.offer;
    return x.build == y.build && x.version == y.version && x.bundleId == y.bundleId && x.cpu == y.cpu &&
        x.minimumOs == y.minimumOs && x.channel == y.channel && x.size == y.size && x.sha256 == y.sha256 &&
        x.notes == y.notes && a.archiveUrl == b.archiveUrl && a.archiveSignatureBase64 == b.archiveSignatureBase64;
}
bool matches(SUAppcastItem* item, const updates::SignedUpdate& r) {
    return item && item.signingValidationStatus == SPUAppcastSigningValidationStatusSucceeded &&
        qt(item.versionString) == QString::number(r.offer.build) && qt(item.displayVersionString) == r.offer.version &&
        qt(item.fileURL.absoluteString) == r.archiveUrl.toString(QUrl::FullyEncoded) &&
        item.contentLength == uint64_t(r.offer.size) && qt(item.minimumSystemVersion) == r.offer.minimumOs.toString() &&
        qt(item.channel) == (r.offer.channel == updates::Channel::Preview ? QStringLiteral("preview") : QString()) &&
        qt(item.propertiesDictionary[@"enclosure"][@"sparkle:edSignature"]).toLatin1() == r.archiveSignatureBase64 &&
        !item.deltaUpdate && item.deltaUpdates.count == 0 && [item.installationType isEqualToString:@"application"];
}
bool boolean(NSBundle* bundle, NSString* key, bool value) {
    id item = [bundle objectForInfoDictionaryKey:key];
    return [item isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)item) == CFBooleanGetTypeID() && [item boolValue] == value;
}
NSError* denied() { return [NSError errorWithDomain:@"org.eiskaltdcpp.update-install" code:1 userInfo:nil]; }
}

@interface EDInstallDriver : NSObject <SPUUpdaterDelegate, SPUUserDriver> {
@public
    updates::SignedUpdate expected;
    std::function<bool()> consent;
    std::function<void(bool)> complete;
}
@property NSString* feed;
@property NSString* archive;
@property BOOL enabled;
@property BOOL verified;
@property BOOL violation;
@property BOOL prepared;
@property BOOL downloaded;
@property NSInteger errorCode;
@end
@implementation EDInstallDriver
- (BOOL)approved { try { return self.enabled && !self.violation && consent && consent(); } catch(...) { return NO; } }
- (NSString*)feedURLStringForUpdater:(SPUUpdater*)updater { return self.feed; }
- (BOOL)updaterShouldRelaunchApplication:(SPUUpdater*)updater { return NO; }
- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
    return expected.offer.channel == updates::Channel::Preview ? [NSSet setWithObject:@"preview"] : [NSSet set];
}
- (BOOL)updater:(SPUUpdater*)updater mayPerformUpdateCheck:(SPUUpdateCheck)check error:(NSError* __autoreleasing*)error {
    if(self.enabled && check == SPUUpdateCheckUpdates) return YES;
    if(error) *error = denied(); return NO;
}
- (BOOL)updater:(SPUUpdater*)updater shouldDownloadReleaseNotesForUpdate:(SUAppcastItem*)item { return NO; }
- (void)updater:(SPUUpdater*)updater didFinishLoadingAppcast:(SUAppcast*)appcast {
    self.verified = appcast.signingValidationStatus == SPUAppcastSigningValidationStatusSucceeded;
    if(!self.verified) self.violation = YES;
}
- (SUAppcastItem*)bestValidUpdateInAppcast:(SUAppcast*)appcast forUpdater:(SPUUpdater*)updater {
    SUAppcastItem* chosen = nil;
    if(self.enabled && self.verified && !self.violation) for(SUAppcastItem* item in appcast.items) {
        if(matches(item, expected)) { if(chosen) { self.violation = YES; break; } chosen = item; }
    }
    return self.violation ? [SUAppcastItem emptyAppcastItem] : (chosen ?: [SUAppcastItem emptyAppcastItem]);
}
- (BOOL)updater:(SPUUpdater*)updater shouldProceedWithUpdate:(SUAppcastItem*)item updateCheck:(SPUUpdateCheck)check error:(NSError* __autoreleasing*)error {
    if(self.enabled && self.verified && !self.violation && check == SPUUpdateCheckUpdates && matches(item, expected)) return YES;
    self.violation = YES; if(error) *error = denied(); return NO;
}
- (void)updater:(SPUUpdater*)updater willDownloadUpdate:(SUAppcastItem*)item withRequest:(NSMutableURLRequest*)request {
    if(self.enabled && self.verified && !self.violation && matches(item, expected) && [request.URL isEqual:item.fileURL]) {
        request.URL = [NSURL URLWithString:self.archive]; request.timeoutInterval = 10;
    } else {
        // Even rejected SDK requests stay on our own server, never a foreign
        // local service that could redirect the request to a remote origin.
        request.URL = [NSURL URLWithString:[self.archive stringByAppendingString:@"/denied"]]; self.violation = YES;
    }
}
- (void)updater:(SPUUpdater*)updater didDownloadUpdate:(SUAppcastItem*)item { self.downloaded = matches(item, expected); }
- (void)updater:(SPUUpdater*)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check error:(NSError*)error {
    self.errorCode = error.code;
    if(self.enabled && complete) complete(!error && !self.violation && self.prepared && self.downloaded && check == SPUUpdateCheckUpdates);
}
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request reply:(void (^)(SUUpdatePermissionResponse*))reply {
    self.violation = YES; reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:NO sendSystemProfile:NO]);
}
- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancel {}
- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)item state:(SPUUserUpdateState*)state reply:(void (^)(SPUUserUpdateChoice))reply {
    const bool valid = self.enabled && self.verified && !self.violation && matches(item, expected);
    if(!valid) self.violation = YES; reply(valid ? SPUUserUpdateChoiceInstall : SPUUserUpdateChoiceSkip);
}
- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)data { self.violation = YES; }
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error { self.violation = YES; }
- (void)showUpdateNotFoundWithError:(NSError*)error acknowledgement:(void (^)(void))ack { self.violation = YES; ack(); }
- (void)showUpdaterError:(NSError*)error acknowledgement:(void (^)(void))ack { self.violation = YES; ack(); }
- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancel {}
- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length { if(length != uint64_t(expected.offer.size)) self.violation = YES; }
- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length {}
- (void)showDownloadDidStartExtractingUpdate {}
- (void)showExtractionReceivedProgress:(double)progress {}
- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply {
    self.prepared = self.downloaded && [self approved];
    // Dismiss can install on any exit. It is allowed only after committed quit
    // and a final live consent check; uncommitted work never creates this driver.
    reply(self.prepared ? SPUUserUpdateChoiceDismiss : SPUUserUpdateChoiceSkip);
}
- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated retryTerminatingApplication:(void (^)(void))retry { self.violation = YES; }
- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched acknowledgement:(void (^)(void))ack { self.violation = YES; ack(); }
- (void)dismissUpdateInstallation {}
@end

namespace updates {
struct MacUpdateInstaller::Impl {
    enum Phase { Idle, Verifying, Native, Prepared, Refused } phase = Idle;
    MacUpdateInstaller& owner;
    UpdateLoopbackBridge bridge;
    QTimer timer;
    std::thread worker;
    std::atomic_bool cancelled{false}, done{false};
    bool valid = false;
    const char* verificationFailure = "Bundle policy";
    QString failure;
    quint64 operation = 0;
    SignedUpdate selected;
    QString feedPath, archivePath;
    std::function<bool()> consent;
    SPUUpdater* updater = nil;
    EDInstallDriver* driver = nil;
    explicit Impl(MacUpdateInstaller& o) : owner(o) {
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] { poll(); });
    }
    ~Impl() {
        timer.stop(); cancelled.store(true);
        if(worker.joinable()) worker.join();
        if(driver) { driver.enabled = NO; driver->complete = {}; driver->consent = {}; }
        updater = nil; driver = nil;
    }
    bool approved() { try { return consent && consent(); } catch(...) { return false; } }
    void finish(bool success, const char* reason = "Native installation") {
        if(phase != Native && phase != Verifying) return;
        timer.stop(); bridge.stop(); phase = success ? Prepared : Refused;
        if(!success) {
            failure = QString::fromLatin1(reason);
            if(driver) failure += QStringLiteral(" (code %1, verified %2, downloaded %3, prepared %4, rejected %5)")
                .arg(driver.errorCode).arg(driver.verified).arg(driver.downloaded).arg(driver.prepared).arg(driver.violation);
        }
        if(driver) { driver.enabled = NO; driver->complete = {}; driver->consent = {}; }
        if(success) emit owner.readyToExit(operation); else emit owner.failed(operation);
    }
    bool verify(const QByteArray& key) {
        verificationFailure = "Signed feed";
        const auto stopped = [this] { return cancelled.load(); };
        QFile file(feedPath);
        if(!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024) return false;
        const auto feed = verifyUpdateFeed(file.readAll(), key, stopped);
        bool bound = false;
        if(feed) for(const auto& release : feed->releases) if(same(release, selected)) bound = true;
        if(!bound || stopped()) return false;
        const auto& o = selected.offer;
        verificationFailure = "Archive signature";
        if(!verifyUpdateArchive(archivePath, {o.size, o.sha256, selected.archiveSignatureBase64}, key, stopped)) return false;
        verificationFailure = "Archive identity";
        return bundleArchiveMatches(archivePath.toStdString(), {o.bundleId.toStdString(), QString::number(o.build).toStdString(),
                o.version.toStdString(), o.cpu.toStdString(), o.minimumOs.toString().toStdString()}, stopped);
    }
    void poll() {
        if(phase == Verifying && done.load()) {
            worker.join();
            if(!valid || cancelled.load()) { finish(false, verificationFailure); return; }
            if(!approved()) { finish(false, "Consent"); return; }
            const auto feed = bridge.beginInstallerSession(feedPath);
            const auto archive = bridge.mapArchive(archivePath, selected.offer.size);
            if(!feed.isValid() || !archive.isValid()) { finish(false, "Local content"); return; }
            driver = [EDInstallDriver new]; driver->expected = selected; driver->consent = consent;
            driver.feed = ns(feed.toString(QUrl::FullyEncoded)); driver.archive = ns(archive.toString(QUrl::FullyEncoded));
            driver.enabled = YES; driver->complete = [this](bool success) {
                QMetaObject::invokeMethod(&owner, [this, success] { finish(success); }, Qt::QueuedConnection);
            };
            updater = [[SPUUpdater alloc] initWithHostBundle:NSBundle.mainBundle applicationBundle:NSBundle.mainBundle userDriver:driver delegate:driver];
            updater.automaticallyChecksForUpdates = NO; updater.automaticallyDownloadsUpdates = NO; updater.sendsSystemProfile = NO;
            phase = Native; timer.stop();
            NSError* error = nil;
            if(![updater startUpdater:&error]) { finish(false); return; }
            [updater checkForUpdates];
        }
    }
};
MacUpdateInstaller::MacUpdateInstaller(QObject* parent) : QObject(parent), impl(new Impl(*this)) {}
MacUpdateInstaller::~MacUpdateInstaller() = default;
bool MacUpdateInstaller::active() const { return impl->phase == Impl::Verifying || impl->phase == Impl::Native; }
QString MacUpdateInstaller::failureReason() const {
    return impl->failure;
}
bool MacUpdateInstaller::start(quint64 operation, const SignedUpdate& selected, const QString& feed,
    const QString& archive, bool committed, std::function<bool()> consent) {
    if(!committed || !operation || !consent || impl->phase != Impl::Idle || !QCoreApplication::instance() ||
        QThread::currentThread() != QCoreApplication::instance()->thread() || thread() != QThread::currentThread()) return false;
    NSBundle* bundle = NSBundle.mainBundle;
    const auto build = qt([bundle objectForInfoDictionaryKey:@"CFBundleVersion"]);
    bool numeric = false; const auto value = build.toULongLong(&numeric);
    const auto os = QOperatingSystemVersion::current();
    const UpdateIdentity identity{value, Channel::Stable, qt(bundle.bundleIdentifier), QSysInfo::buildCpuArchitecture(),
        QVersionNumber(os.majorVersion(), os.minorVersion(), os.microVersion())};
    id expiration = [bundle objectForInfoDictionaryKey:@"SUSignedFeedFailureExpirationInterval"];
    impl->operation = operation; impl->selected = selected; impl->feedPath = feed; impl->archivePath = archive; impl->consent = std::move(consent);
    impl->phase = Impl::Verifying;
    const bool policy = numeric && value > 0 && QString::number(value) == build &&
        UpdatePolicy::accepts(identity, {true, true, false, selected.offer.channel}, selected.offer) &&
        boolean(bundle, @"SURequireSignedFeed", true) && boolean(bundle, @"SUVerifyUpdateBeforeExtraction", true) &&
        boolean(bundle, @"SUEnableDownloaderService", false) && boolean(bundle, @"SUEnableAutomaticChecks", false) &&
        boolean(bundle, @"SUAutomaticallyUpdate", false) && boolean(bundle, @"SUEnableSystemProfiling", false) &&
        [expiration isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)expiration) != CFBooleanGetTypeID() && [expiration doubleValue] == 0;
    const auto key = qt([bundle objectForInfoDictionaryKey:@"SUPublicEDKey"]).toLatin1();
    try {
        impl->worker = std::thread([this, key, policy] {
            @autoreleasepool { try { impl->valid = policy && impl->verify(key); } catch(...) { impl->valid = false; } }
            impl->done.store(true);
        });
        impl->timer.start();
    } catch(...) { QTimer::singleShot(0, this, [this] { impl->finish(false); }); }
    return true;
}
}
