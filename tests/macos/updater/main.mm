#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

// Disposable driver only. Cancels prepared fixtures and never targets the real app.
@interface Probe : NSObject <SPUUpdaterDelegate, SPUUserDriver>
@property NSString *feed;
@property NSString *archive;
@property NSString *mode;
@property BOOL found;
@property BOOL downloaded;
@property BOOL extracting;
@property BOOL ready;
@property BOOL done;
@property NSInteger rewrites;
@property BOOL verifiedFeed;
@property BOOL finishedCycle;
@property NSError *error;
@property NSString *foundVersion;
@property NSString *foundArchive;
@end

@implementation Probe
- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater { return self.feed; }
- (NSSet<NSString *> *)allowedChannelsForUpdater:(SPUUpdater *)updater {
    return [NSProcessInfo.processInfo.environment[@"EISKALT_PROBE_PREVIEW"] isEqualToString:@"1"]
        ? [NSSet setWithObject:@"preview"] : [NSSet set];
}
- (BOOL)updater:(SPUUpdater *)updater shouldDownloadReleaseNotesForUpdate:(SUAppcastItem *)item { return NO; }
- (void)updater:(SPUUpdater *)updater willDownloadUpdate:(SUAppcastItem *)item withRequest:(NSMutableURLRequest *)request {
    request.timeoutInterval = 3;
    if (![self.mode isEqualToString:@"unmapped"] && self.verifiedFeed &&
        [item.fileURL.absoluteString isEqualToString:@"https://203.0.113.1/update.zip"] &&
        [request.URL isEqual:item.fileURL]) {
        request.URL = [NSURL URLWithString:self.archive];
        self.rewrites++;
    }
}
- (void)updater:(SPUUpdater *)updater didDownloadUpdate:(SUAppcastItem *)item { self.downloaded = YES; }
- (void)updater:(SPUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)item {
    self.found = YES;
    self.verifiedFeed = item.signingValidationStatus == SPUAppcastSigningValidationStatusSucceeded;
    self.foundVersion = item.versionString;
    self.foundArchive = item.fileURL.absoluteString;
}
- (void)updater:(SPUUpdater *)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check error:(NSError *)error {
    self.finishedCycle = YES;
    if (!self.error) self.error = error;
    self.done = YES;
}
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest *)request reply:(void (^)(SUUpdatePermissionResponse *))reply {
    reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:NO sendSystemProfile:NO]);
}
- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancel {}
- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem *)item state:(SPUUserUpdateState *)state reply:(void (^)(SPUUserUpdateChoice))reply {
    self.found = YES;
    self.verifiedFeed = item.signingValidationStatus == SPUAppcastSigningValidationStatusSucceeded;
    reply(SPUUserUpdateChoiceInstall);
}
- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData *)data { self.done = YES; }
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError *)error { self.error = error; self.done = YES; }
- (void)showUpdateNotFoundWithError:(NSError *)error acknowledgement:(void (^)(void))ack {
    self.error = error; self.done = YES; ack();
}
- (void)showUpdaterError:(NSError *)error acknowledgement:(void (^)(void))ack {
    self.error = error; self.done = YES; ack();
}
- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancel {}
- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length {}
- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length {}
- (void)showDownloadDidStartExtractingUpdate { self.extracting = YES; }
- (void)showExtractionReceivedProgress:(double)progress {}
- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply {
    self.ready = YES;
    // Dismiss still installs on process exit. Cancel and await cycle completion.
    reply(SPUUserUpdateChoiceSkip);
}
- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated retryTerminatingApplication:(void (^)(void))retry {
    self.done = YES;
}
- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched acknowledgement:(void (^)(void))ack { self.done = YES; ack(); }
- (void)dismissUpdateInstallation {}
@end

TEST_CASE("Numeric internal builds preserve prerelease to final release ordering", "[updater-native][bridge-probe]") {
    id<SUVersionComparison> comparator = [SUStandardVersionComparator defaultComparator];
    // Literal fixture allocation: pre.1=1, pre.2=2, pre.10=10, final=11.
    // Display strings are not passed to Sparkle's version comparator.
    REQUIRE([comparator compareVersion:@"1" toVersion:@"2"] == NSOrderedAscending);
    REQUIRE([comparator compareVersion:@"2" toVersion:@"10"] == NSOrderedAscending);
    REQUIRE([comparator compareVersion:@"10" toVersion:@"11"] == NSOrderedAscending);
}

TEST_CASE("Real Sparkle checks signed bytes through the public bridge hooks", "[updater-native][bridge-probe]") {
    // Prove that this process really is forbidden from reaching an origin.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(443);
    inet_pton(AF_INET, "203.0.113.1", &destination.sin_addr);
    int connected = connect(fd, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
    int connectError = errno;
    if (connected == -1 && connectError == EINPROGRESS) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        timeval timeout{3, 0};
        int selected = select(fd + 1, nullptr, &writable, nullptr, &timeout);
        if (selected > 0) {
            socklen_t length = sizeof(connectError);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &connectError, &length);
        }
    }
    close(fd);
    REQUIRE(connected == -1);
    REQUIRE(connectError == EPERM);
    NSDictionary *env = NSProcessInfo.processInfo.environment;
    REQUIRE(env[@"EISKALT_PROBE_FEED"] != nil);
    Probe *probe = [Probe new];
    probe.feed = env[@"EISKALT_PROBE_FEED"];
    probe.archive = env[@"EISKALT_PROBE_ARCHIVE"];
    probe.mode = env[@"EISKALT_PROBE_MODE"];
    SPUUpdater *updater = [[SPUUpdater alloc] initWithHostBundle:NSBundle.mainBundle
        applicationBundle:NSBundle.mainBundle userDriver:probe delegate:probe];
    NSError *error = nil;
    REQUIRE([updater startUpdater:&error]);
    updater.automaticallyChecksForUpdates = NO;
    updater.automaticallyDownloadsUpdates = NO;
    updater.sendsSystemProfile = NO;
    if ([probe.mode isEqualToString:@"information-only"])
        [updater checkForUpdateInformation];
    else
        [updater checkForUpdates];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:25];
    while (!probe.done && deadline.timeIntervalSinceNow > 0) {
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
    INFO((probe.error ? probe.error.description.UTF8String : "no updater error"));
    printf("PROBE found=%d downloaded=%d extracting=%d ready=%d rewrites=%ld error=%ld\n",
        probe.found, probe.downloaded, probe.extracting, probe.ready, (long)probe.rewrites, (long)probe.error.code);
    REQUIRE(probe.done);
    if ([probe.mode isEqualToString:@"information-only"]) {
        REQUIRE(probe.found);
        REQUIRE(probe.verifiedFeed);
        REQUIRE_FALSE(probe.downloaded);
        REQUIRE_FALSE(probe.extracting);
        REQUIRE_FALSE(probe.ready);
        REQUIRE(probe.rewrites == 0);
        REQUIRE(probe.error == nil);
        if (env[@"EISKALT_PROBE_EXPECTED_BUILD"] != nil) {
            REQUIRE([probe.foundVersion isEqualToString:env[@"EISKALT_PROBE_EXPECTED_BUILD"]]);
            REQUIRE([probe.foundArchive isEqualToString:env[@"EISKALT_PROBE_EXPECTED_ARCHIVE"]]);
        }
    } else if ([probe.mode isEqualToString:@"bad-feed"]) {
        REQUIRE_FALSE(probe.found);
        REQUIRE(probe.error != nil);
    } else if ([probe.mode isEqualToString:@"bad-archive"]) {
        REQUIRE(probe.found);
        REQUIRE(probe.verifiedFeed);
        REQUIRE(probe.downloaded);
        REQUIRE(probe.error != nil);
        REQUIRE_FALSE(probe.ready);
        // Reject a launcher failure masquerading as a signature-test success.
        REQUIRE([probe.error.description containsString:@"signature validation before unarchiving failed"]);
    } else if ([probe.mode isEqualToString:@"unmapped"]) {
        REQUIRE(probe.found);
        REQUIRE_FALSE(probe.downloaded);
        REQUIRE(probe.error != nil);
        REQUIRE(probe.rewrites == 0);
    } else {
        REQUIRE(probe.found);
        REQUIRE(probe.verifiedFeed);
        REQUIRE(probe.downloaded);
        REQUIRE(probe.rewrites == 1);
        REQUIRE(probe.ready);
        REQUIRE(probe.finishedCycle);
        REQUIRE(probe.error == nil);
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        return Catch::Session().run(argc, argv);
    }
}
