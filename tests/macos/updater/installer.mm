#import <Foundation/Foundation.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QThread>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>
#include "eiskaltdcpp-qt/src/updates/UpdateFeedVerifier.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <functional>
#include <future>
#include <system_error>
#include "eiskaltdcpp-qt/src/updates/MacUpdateInstaller.h"
namespace updates {
struct MacUpdateInstallerTestAccess {
    static std::unique_ptr<MacUpdateInstaller> make(bool failWorkerStart) {
        if(!failWorkerStart) return std::make_unique<MacUpdateInstaller>();
        return std::unique_ptr<MacUpdateInstaller>(new MacUpdateInstaller(nullptr,
            [](std::function<void()>) -> std::thread {
                throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
            }));
    }
    static bool cancel(MacUpdateInstaller& installer, bool deadline = false) { return installer.cancel(deadline); }
    static bool start(MacUpdateInstaller& installer, const SignedUpdate& offer,
                      const QString& feed, const QString& archive, bool committed,
                      const std::function<bool()>& consent, int deadlineMilliseconds = 30000) {
        return installer.start(41, offer, feed, archive, committed, consent, deadlineMilliseconds);
    }
};
}

TEST_CASE("Production native installer starts only from committed validated staging", "[update-install-adapter]") {
    const auto mode = qEnvironmentVariable("EISKALT_INSTALL_MODE");
    REQUIRE(!mode.isEmpty());
    REQUIRE([NSBundle.mainBundle.bundleIdentifier hasPrefix:@"org.eiskaltdcpp.updater-probe."]);
    const int fd = socket(AF_INET, SOCK_STREAM, 0); REQUIRE(fd >= 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(443);
    inet_pton(AF_INET, "203.0.113.1", &address.sin_addr);
    const int connected = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const int error = errno; close(fd);
    REQUIRE(connected == -1); REQUIRE(error == EPERM);
    const auto feedPath = qEnvironmentVariable("EISKALT_PROBE_LOCAL_FEED");
    const auto archive = qEnvironmentVariable("EISKALT_PROBE_LOCAL_ARCHIVE");
    QFile file(feedPath); REQUIRE(file.open(QIODevice::ReadOnly));
    const auto key = QByteArray([[NSBundle.mainBundle objectForInfoDictionaryKey:@"SUPublicEDKey"] UTF8String]);
    const auto feed = updates::verifyUpdateFeed(file.readAll(), key); REQUIRE(feed);
    REQUIRE(feed->releases.size() == 1);
    const bool workerFailure = mode == "worker-start-failure" || mode == "cancel-worker-start" || mode == "deadline-worker-start";
    const bool handoffCancellation = mode == "cancel-after-handoff" || mode == "deadline-after-handoff";
    const bool installs = mode == "normal" || handoffCancellation;
    auto lifetime = updates::MacUpdateInstallerTestAccess::make(workerFailure);
    auto& installer = *lifetime;
    int ready = 0, failures = 0;
    using Stage = updates::MacUpdateInstaller::Stage;
    QList<Stage> stages;
    int cancelAttempts = 0;
    bool acceptedCancel = false, repeatedCancel = false, wrongOwnerRejected = false, pendingAfterCancel = false;
    int handoffAttempts = 0;
    bool handoffActive = false, handoffCancelRejected = false, handoffDeadlineRejected = false;
    bool queuedHandoffChecked = false, queuedHandoffRejected = false;
    const auto readyConnection = QObject::connect(&installer, &updates::MacUpdateInstaller::readyToExit, [&](quint64 op) { CHECK(op == 41); ++ready; });
    const auto failedConnection = QObject::connect(&installer, &updates::MacUpdateInstaller::failed, [&](quint64 op) { CHECK(op == 41); ++failures; });
    const auto stageConnection = QObject::connect(&installer, &updates::MacUpdateInstaller::stageChanged, [&](quint64 op, Stage stage) {
        CHECK(op == 41); stages.append(stage);
        if(handoffCancellation && stage == Stage::HandingOff) {
            ++handoffAttempts; handoffActive = installer.active() && ready == 0 && failures == 0;
            const bool expired = mode == "deadline-after-handoff";
            handoffCancelRejected = !updates::MacUpdateInstallerTestAccess::cancel(installer, expired);
            handoffDeadlineRejected = !updates::MacUpdateInstallerTestAccess::cancel(installer, !expired);
            QTimer::singleShot(0, &installer, [&] {
                queuedHandoffChecked = true;
                queuedHandoffRejected = !updates::MacUpdateInstallerTestAccess::cancel(installer) &&
                    !updates::MacUpdateInstallerTestAccess::cancel(installer, true);
            });
        }
        const bool wanted = (mode == "cancel-check" && stage == Stage::Checking) ||
            (mode == "cancel-download" && stage == Stage::Downloading) ||
            ((mode == "cancel-extraction" || mode == "deadline-extraction") && stage == Stage::Extracting) ||
            (mode == "cancel-ready" && stage == Stage::Ready);
        if(wanted && ++cancelAttempts == 1) {
            const bool expired = mode == "deadline-extraction";
            wrongOwnerRejected = !std::async(std::launch::async, [&] {
                return updates::MacUpdateInstallerTestAccess::cancel(installer, expired);
            }).get();
            acceptedCancel = updates::MacUpdateInstallerTestAccess::cancel(installer, expired);
            repeatedCancel = updates::MacUpdateInstallerTestAccess::cancel(installer, !expired);
            pendingAfterCancel = installer.active() && ready == 0 && failures == 0;
        }
    });
    int consentChecks = 0;
    const auto consent = [&] {
        ++consentChecks;
        return mode != "cancel-before-native" && (consentChecks == 1 || mode != "cancel-prepared");
    };
    CHECK_FALSE(updates::MacUpdateInstallerTestAccess::cancel(installer));
    CHECK_FALSE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, true, consent, 0));
    CHECK_FALSE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, true, consent, 300001));
    CHECK_FALSE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, false, consent));
    CHECK(consentChecks == 0);
    CHECK_FALSE(installer.active());
    CHECK_FALSE(std::async(std::launch::async, [&] {
        return updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, true, consent);
    }).get());
    CHECK_FALSE(installer.active()); CHECK(consentChecks == 0);
    const bool committed = mode != "hide" && mode != "cancel-quit" && mode != "kill-before-commit";
    if(committed) {
        const auto stagedFeed = mode == "bad-feed" ? QFileInfo(feedPath).absolutePath() + "/bad-feed.xml" : feedPath;
        const auto stagedArchive = mode == "bad-archive" ? QFileInfo(archive).absolutePath() + "/bad-archive.zip" : archive;
        for(const auto& path : {stagedFeed, stagedArchive}) {
            struct stat state{};
            REQUIRE(::lstat(QFile::encodeName(path).constData(), &state) == 0);
            REQUIRE(S_ISREG(state.st_mode));
            REQUIRE(state.st_uid == getuid());
            REQUIRE(state.st_nlink == 1);
            REQUIRE((state.st_mode & 0777) == 0600);
            REQUIRE(state.st_size > 0);
            const auto parent = QFileInfo(path).absolutePath();
            REQUIRE(::lstat(QFile::encodeName(parent).constData(), &state) == 0);
            REQUIRE(S_ISDIR(state.st_mode));
            REQUIRE(state.st_uid == getuid());
            REQUIRE((state.st_mode & 0077) == 0);
        }
        REQUIRE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), stagedFeed, stagedArchive, true, consent,
            mode == "deadline-verifier" ? 1 : 30000));
        if(mode == "destroy-verifier") {
            lifetime.reset();
            CHECK(ready == 0); CHECK(failures == 0); CHECK(consentChecks == 0);
            return;
        }
        if(mode == "cancel-verifier" || mode == "cancel-worker-start" || mode == "deadline-worker-start") {
            const bool expired = mode == "deadline-worker-start";
            REQUIRE(updates::MacUpdateInstallerTestAccess::cancel(installer, expired));
            REQUIRE(updates::MacUpdateInstallerTestAccess::cancel(installer, !expired));
            CHECK(installer.active()); CHECK(ready == 0); CHECK(failures == 0);
        }
        QElapsedTimer timer; timer.start();
        while(ready + failures == 0 && timer.elapsed() < 30000) {
            QCoreApplication::processEvents(); QThread::msleep(2);
        }
        REQUIRE(ready + failures == 1);
        QCoreApplication::processEvents();
        INFO(installer.failureReason().toStdString());
        CHECK(ready == (installs ? 1 : 0));
        CHECK(failures == (installs ? 0 : 1));
        CHECK_FALSE(installer.active());
        CHECK_FALSE(updates::MacUpdateInstallerTestAccess::cancel(installer));
        CHECK_FALSE(updates::MacUpdateInstallerTestAccess::cancel(installer, true));
        CHECK_FALSE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, true, consent));
        if(mode == "cancel-before-native") {
            CHECK(consentChecks == 1);
            CHECK(installer.failureReason() == "Consent");
        } else if(mode == "bad-feed" || mode == "bad-archive") {
            CHECK(consentChecks == 0);
            CHECK(installer.failureReason() == (mode == "bad-feed" ? "Signed feed" : "Archive signature"));
        }
        if(mode == "cancel-verifier" || mode == "deadline-verifier") {
            CHECK(consentChecks == 0);
            CHECK(stages == QList<Stage>{Stage::Verifying});
            CHECK(installer.failureReason() == (mode == "deadline-verifier" ? "Deadline" : "Cancelled"));
        }
        if(workerFailure) {
            CHECK(consentChecks == 0); CHECK_FALSE(stages.contains(Stage::Checking));
            CHECK(installer.failureReason() == (mode == "worker-start-failure" ? "Worker start" :
                mode == "deadline-worker-start" ? "Deadline" : "Cancelled"));
        }
        if(handoffCancellation) {
            CHECK(handoffAttempts == 1); CHECK(handoffActive);
            CHECK(handoffCancelRejected); CHECK(handoffDeadlineRejected);
            CHECK(queuedHandoffChecked); CHECK(queuedHandoffRejected);
        }
        if(mode == "cancel-check" || mode == "cancel-download" || mode == "cancel-extraction" ||
            mode == "cancel-ready" || mode == "deadline-extraction") {
            CHECK(cancelAttempts == 1); CHECK(acceptedCancel); CHECK(repeatedCancel);
            CHECK(wrongOwnerRejected); CHECK(pendingAfterCancel);
            CHECK(installer.failureReason().startsWith(mode == "deadline-extraction" ? "Deadline" : "Cancelled"));
            if(mode == "cancel-extraction" || mode == "deadline-extraction") CHECK(stages.contains(Stage::Ready));
        }
        if(installs) {
            // A prepared native owner lives until process exit, not test-scope teardown.
            QObject::disconnect(readyConnection);
            QObject::disconnect(failedConnection);
            QObject::disconnect(stageConnection);
            lifetime.release();
        }
    }
    if(mode == "kill-before-commit") {
        QFile marker(qEnvironmentVariable("EISKALT_PROBE_KILL_MARKER"));
        REQUIRE(marker.open(QIODevice::WriteOnly)); REQUIRE(marker.write("ready") == 5); marker.close();
        QThread::sleep(60); FAIL("parent did not stop disposable child");
    }
}
