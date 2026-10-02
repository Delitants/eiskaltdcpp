#import <Foundation/Foundation.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <catch2/catch_test_macros.hpp>
#include "eiskaltdcpp-qt/src/updates/UpdateFeedVerifier.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <functional>
#include <future>
#include "eiskaltdcpp-qt/src/updates/MacUpdateInstaller.h"
namespace updates {
struct MacUpdateInstallerTestAccess {
    static bool start(MacUpdateInstaller& installer, const SignedUpdate& offer,
                      const QString& feed, const QString& archive, bool committed,
                      const std::function<bool()>& consent) {
        return installer.start(41, offer, feed, archive, committed, consent);
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
    auto lifetime = std::make_unique<updates::MacUpdateInstaller>();
    auto& installer = *lifetime;
    int ready = 0, failures = 0;
    const auto readyConnection = QObject::connect(&installer, &updates::MacUpdateInstaller::readyToExit, [&](quint64 op) { CHECK(op == 41); ++ready; });
    const auto failedConnection = QObject::connect(&installer, &updates::MacUpdateInstaller::failed, [&](quint64 op) { CHECK(op == 41); ++failures; });
    int consentChecks = 0;
    const auto consent = [&] {
        ++consentChecks;
        return mode != "cancel-before-native" && (consentChecks == 1 || mode != "cancel-prepared");
    };
    CHECK_FALSE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, false, consent));
    CHECK(consentChecks == 0);
    CHECK_FALSE(installer.active());
    CHECK_FALSE(std::async(std::launch::async, [&] {
        return updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, true, consent);
    }).get());
    CHECK_FALSE(installer.active()); CHECK(consentChecks == 0);
    const bool committed = mode == "normal" || mode == "bad-identity" || mode == "cancel-prepared" ||
        mode == "bad-minimum-os" || mode == "cancel-before-native" || mode == "bad-feed" || mode == "bad-archive";
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
        REQUIRE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), stagedFeed, stagedArchive, true, consent));
        QElapsedTimer timer; timer.start();
        while(ready + failures == 0 && timer.elapsed() < 30000) {
            QCoreApplication::processEvents(); QThread::msleep(2);
        }
        REQUIRE(ready + failures == 1);
        INFO(installer.failureReason().toStdString());
        CHECK(ready == (mode == "normal" ? 1 : 0));
        CHECK(failures == (mode == "normal" ? 0 : 1));
        CHECK_FALSE(installer.active());
        CHECK_FALSE(updates::MacUpdateInstallerTestAccess::start(installer, feed->releases.front(), feedPath, archive, true, consent));
        if(mode == "cancel-before-native") {
            CHECK(consentChecks == 1);
            CHECK(installer.failureReason() == "Consent");
        } else if(mode == "bad-feed" || mode == "bad-archive") {
            CHECK(consentChecks == 0);
            CHECK(installer.failureReason() == (mode == "bad-feed" ? "Signed feed" : "Archive signature"));
        }
        if(mode == "normal") {
            // A prepared native owner lives until process exit, not test-scope teardown.
            QObject::disconnect(readyConnection);
            QObject::disconnect(failedConnection);
            lifetime.release();
        }
    }
    if(mode == "kill-before-commit") {
        QFile marker(qEnvironmentVariable("EISKALT_PROBE_KILL_MARKER"));
        REQUIRE(marker.open(QIODevice::WriteOnly)); REQUIRE(marker.write("ready") == 5); marker.close();
        QThread::sleep(60); FAIL("parent did not stop disposable child");
    }
}
