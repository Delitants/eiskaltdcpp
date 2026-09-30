#include "dcpp/stdinc.h"
#include "dcpp/ProxyRoute.h"
#include "MacUpdateDiscovery.h"
#include "UpdateLoopbackBridge.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QOperatingSystemVersion>
#include <QPointer>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <limits>
#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

namespace {
QString qt(NSString* value) { return [value isKindOfClass:NSString.class] ? QString::fromUtf8(value.UTF8String) : QString(); }
NSString* ns(const QString& value) { return [NSString stringWithUTF8String:value.toUtf8().constData()]; }
NSError* denied() { return [NSError errorWithDomain:@"org.eiskaltdcpp.update-discovery" code:1
    userInfo:@{NSLocalizedDescriptionKey: @"Update discovery was rejected."}]; }
bool matches(SUAppcastItem* item, const std::optional<updates::SignedUpdate>& expected) {
    if(!item || !expected || item.signingValidationStatus != SPUAppcastSigningValidationStatusSucceeded) return false;
    const auto& r = *expected;
    return qt(item.versionString) == QString::number(r.offer.build) &&
        qt(item.displayVersionString) == r.offer.version &&
        qt(item.fileURL.absoluteString) == r.archiveUrl.toString(QUrl::FullyEncoded) &&
        item.contentLength == static_cast<uint64_t>(r.offer.size) &&
        qt(item.minimumSystemVersion) == r.offer.minimumOs.toString() &&
        qt(item.channel) == (r.offer.channel == updates::Channel::Preview ? QStringLiteral("preview") : QString()) &&
        qt(item.propertiesDictionary[@"enclosure"][@"sparkle:edSignature"]).toLatin1() == r.archiveSignatureBase64 &&
        !item.deltaUpdate && item.deltaUpdates.count == 0 && [item.installationType isEqualToString:@"application"];
}
bool setting(NSBundle* bundle, NSString* key, bool expected) {
    id value = [bundle objectForInfoDictionaryKey:key];
    return [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID() &&
        [value boolValue] == expected;
}
}

@interface EDDiscoveryDriver : NSObject <SPUUpdaterDelegate, SPUUserDriver> {
@public
    std::optional<updates::SignedUpdate> expected;
    std::function<void(bool)> completion;
}
@property NSString* feed;
@property BOOL preview;
@property BOOL enabled;
@property BOOL verified;
@property BOOL found;
@property BOOL absent;
@property BOOL violation;
@end

@implementation EDDiscoveryDriver
- (void)reject { self.violation = YES; if(self.enabled && completion) completion(false); }
- (NSString*)feedURLStringForUpdater:(SPUUpdater*)updater { return self.feed; }
- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
    return self.preview ? [NSSet setWithObject:@"preview"] : [NSSet set];
}
- (BOOL)updater:(SPUUpdater*)updater mayPerformUpdateCheck:(SPUUpdateCheck)check error:(NSError* __autoreleasing*)error {
    if(self.enabled && check == SPUUpdateCheckUpdateInformation) return YES;
    if(error) *error = denied();
    return NO;
}
- (BOOL)updater:(SPUUpdater*)updater shouldDownloadReleaseNotesForUpdate:(SUAppcastItem*)item { return NO; }
- (void)updater:(SPUUpdater*)updater didFinishLoadingAppcast:(SUAppcast*)appcast {
    self.verified = appcast.signingValidationStatus == SPUAppcastSigningValidationStatusSucceeded;
    if(!self.verified) [self reject];
}
- (SUAppcastItem*)bestValidUpdateInAppcast:(SUAppcast*)appcast forUpdater:(SPUUpdater*)updater {
    SUAppcastItem* selected = nil;
    if(self.enabled && self.verified && !self.violation) {
        for(SUAppcastItem* item in appcast.items) {
            if(matches(item, expected)) {
                if(selected) { [self reject]; return [SUAppcastItem emptyAppcastItem]; }
                selected = item;
            }
        }
    }
    // nil would ask Sparkle to choose its own (possibly foreign-CPU) candidate.
    return selected ?: [SUAppcastItem emptyAppcastItem];
}
- (BOOL)updater:(SPUUpdater*)updater shouldProceedWithUpdate:(SUAppcastItem*)item updateCheck:(SPUUpdateCheck)check error:(NSError* __autoreleasing*)error {
    if(self.enabled && self.verified && !self.violation && check == SPUUpdateCheckUpdateInformation && matches(item, expected)) return YES;
    self.violation = YES;
    if(error) *error = denied();
    return NO;
}
- (void)updater:(SPUUpdater*)updater didFindValidUpdate:(SUAppcastItem*)item {
    if(self.enabled && self.verified && matches(item, expected)) self.found = YES;
    else [self reject];
}
- (void)updaterDidNotFindUpdate:(SPUUpdater*)updater error:(NSError*)error { self.absent = YES; }
- (void)updater:(SPUUpdater*)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check error:(NSError*)error {
    const bool noUpdate = self.absent && !expected && [error.domain isEqualToString:SUSparkleErrorDomain] && error.code == SUNoUpdateError;
    const bool success = self.enabled && self.verified && !self.violation && check == SPUUpdateCheckUpdateInformation &&
        ((expected && self.found && !error) || noUpdate);
    if(self.enabled && completion) completion(success);
}
- (void)updater:(SPUUpdater*)updater willDownloadUpdate:(SUAppcastItem*)item withRequest:(NSMutableURLRequest*)request {
    request.URL = [NSURL URLWithString:@"http://127.0.0.1:1/"];
    [self reject];
}
// None of these user-driver callbacks belongs to an information-only cycle.
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request reply:(void (^)(SUUpdatePermissionResponse*))reply {
    reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:NO sendSystemProfile:NO]); [self reject];
}
- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancel { cancel(); [self reject]; }
- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)item state:(SPUUserUpdateState*)state reply:(void (^)(SPUUserUpdateChoice))reply { reply(SPUUserUpdateChoiceSkip); [self reject]; }
- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)data { [self reject]; }
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error { [self reject]; }
- (void)showUpdateNotFoundWithError:(NSError*)error acknowledgement:(void (^)(void))ack { ack(); [self reject]; }
- (void)showUpdaterError:(NSError*)error acknowledgement:(void (^)(void))ack { ack(); [self reject]; }
- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancel { cancel(); [self reject]; }
- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length { [self reject]; }
- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length { [self reject]; }
- (void)showDownloadDidStartExtractingUpdate { [self reject]; }
- (void)showExtractionReceivedProgress:(double)progress { [self reject]; }
- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply { reply(SPUUserUpdateChoiceSkip); [self reject]; }
- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated retryTerminatingApplication:(void (^)(void))retry { [self reject]; }
- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched acknowledgement:(void (^)(void))ack { ack(); [self reject]; }
- (void)dismissUpdateInstallation { [self reject]; }
@end

namespace updates {
struct MacUpdateDiscovery::Impl {
    MacUpdateDiscovery& owner;
    QString fixtureBundle;
    UpdateLoopbackBridge bridge;
    QTimer timer;
    QElapsedTimer elapsed;
    std::unique_ptr<QTemporaryDir> staging;
    std::shared_ptr<const dcpp::ProxyRouteSnapshot> route;
    std::optional<SignedUpdate> selected;
    SPUUpdater* updater = nil;
    EDDiscoveryDriver* driver = nil;
    quint64 generation = 0, operation = 0;
    bool busy = false;
    Impl(MacUpdateDiscovery& o, const QString& fixture) : owner(o), fixtureBundle(fixture) {
        timer.setInterval(100);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] {
            if(!routeValid() || elapsed.elapsed() >= 30000) finish(false);
        });
    }
    bool routeValid() const { return route && route->valid && route->revoked && !route->revoked->load(); }
    void clear() {
        ++generation;
        busy = false;
        timer.stop();
        if(driver) { driver.enabled = NO; driver->completion = {}; }
        updater = nil;
        driver = nil;
        bridge.stop();
        staging.reset();
        selected.reset();
        route.reset();
    }
    void finish(bool success) {
        if(!busy) return;
        success = success && routeValid();
        const auto result = selected;
        const auto op = operation;
        clear();
        if(!success) emit owner.failed(op);
        else if(result) emit owner.available(op, *result);
        else emit owner.noUpdate(op);
    }
    void start(const QByteArray& bytes, Channel channel, quint64 epoch) {
        if(!busy || epoch != generation) return;
        NSBundle* bundle = fixtureBundle.isEmpty() ? NSBundle.mainBundle : [NSBundle bundleWithPath:ns(fixtureBundle)];
        id expiration = [bundle objectForInfoDictionaryKey:@"SUSignedFeedFailureExpirationInterval"];
        const auto buildString = qt([bundle objectForInfoDictionaryKey:@"CFBundleVersion"]);
        bool numeric = false;
        const auto build = buildString.toULongLong(&numeric);
        if(!routeValid() || !bundle || !numeric || build == 0 || build > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
            QString::number(build) != buildString || !setting(bundle, @"SURequireSignedFeed", true) ||
            !setting(bundle, @"SUVerifyUpdateBeforeExtraction", true) || !setting(bundle, @"SUEnableDownloaderService", false) ||
            !setting(bundle, @"SUEnableAutomaticChecks", false) || !setting(bundle, @"SUAutomaticallyUpdate", false) ||
            !setting(bundle, @"SUEnableSystemProfiling", false) || ![expiration isKindOfClass:NSNumber.class] ||
            CFGetTypeID((__bridge CFTypeRef)expiration) == CFBooleanGetTypeID() ||
            [expiration doubleValue] != 0.0) { finish(false); return; }
        const auto feed = verifyUpdateFeed(bytes, qt([bundle objectForInfoDictionaryKey:@"SUPublicEDKey"]).toLatin1());
        if(!feed) { finish(false); return; }
        UpdateIdentity installed;
        installed.build = build; installed.bundleId = qt(bundle.bundleIdentifier); installed.cpu = QSysInfo::buildCpuArchitecture();
        const auto os = QOperatingSystemVersion::current();
        installed.os = QVersionNumber(os.majorVersion(), os.minorVersion(), os.microVersion());
        if(installed.bundleId.isEmpty() || (installed.cpu != "arm64" && installed.cpu != "x86_64")) { finish(false); return; }
        selected = selectUpdate(*feed, installed, channel);
        staging = std::make_unique<QTemporaryDir>();
        if(!staging->isValid()) { finish(false); return; }
        QFile file(staging->filePath("appcast.xml"));
        if(!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
            file.write(feed->signedBytes) != feed->signedBytes.size() || !file.flush()) { finish(false); return; }
        file.close();
        const auto url = bridge.beginSession(file.fileName(), route);
        if(!url.isValid()) { finish(false); return; }
        driver = [EDDiscoveryDriver new];
        driver.feed = ns(url.toString(QUrl::FullyEncoded)); driver.enabled = YES; driver.preview = channel == Channel::Preview;
        driver->expected = selected;
        const QPointer<MacUpdateDiscovery> guard(&owner);
        driver->completion = [guard, epoch](bool success) {
            if(!guard) return;
            // Release the updater only after returning from Sparkle's delegate stack.
            QTimer::singleShot(0, guard, [guard, epoch, success] {
                if(guard && guard->impl->busy && guard->impl->generation == epoch) guard->impl->finish(success);
            });
        };
        updater = [[SPUUpdater alloc] initWithHostBundle:bundle applicationBundle:bundle userDriver:driver delegate:driver];
        updater.automaticallyChecksForUpdates = NO; updater.automaticallyDownloadsUpdates = NO; updater.sendsSystemProfile = NO;
        NSError* error = nil;
        if(![updater startUpdater:&error]) { finish(false); return; }
        [updater checkForUpdateInformation];
    }
};
MacUpdateDiscovery::MacUpdateDiscovery(QObject* parent) : MacUpdateDiscovery(QString(), parent) {}
MacUpdateDiscovery::MacUpdateDiscovery(const QString& fixture, QObject* parent) : QObject(parent), impl(new Impl(*this, fixture)) {}
MacUpdateDiscovery::~MacUpdateDiscovery() { impl->clear(); }
bool MacUpdateDiscovery::check(quint64 op, const QByteArray& bytes, Channel channel, std::shared_ptr<const dcpp::ProxyRouteSnapshot> route) {
    if(!op || impl->busy || !QCoreApplication::instance() || QThread::currentThread() != QCoreApplication::instance()->thread() || thread() != QThread::currentThread()) return false;
    impl->busy = true; impl->operation = op; impl->route = std::move(route);
    const auto epoch = ++impl->generation;
    impl->elapsed.start(); impl->timer.start();
    if(bytes.size() > 1024 * 1024) QTimer::singleShot(0, this, [this, epoch] { if(impl->generation == epoch) impl->finish(false); });
    else QTimer::singleShot(0, this, [this, bytes, channel, epoch] { impl->start(bytes, channel, epoch); });
    return true;
}
void MacUpdateDiscovery::cancel() { impl->clear(); }
bool MacUpdateDiscovery::active() const { return impl->busy; }
}
