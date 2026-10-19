// Full-GUI integration target only: do not link with QT_CONTEXT_MINIMAL.
#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/ClientManager.h"
#include "dcpp/Client.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/Util.h"
#include "ArenaWidgetManager.h"
#include "GlobalTimer.h"
#include "HubFrame.h"
#include "PMWindow.h"
#include "ChatEdit.h"
#include "HubManager.h"
#include "ViewLayout.h"
#include "MainWindow.h"
#include "QtContext.h"
#include "SettingsConnection.h"
#include "SettingsGUI.h"
#include "SettingsTorrent.h"
#include "CertificateSettings.h"
#include "FileHasher.h"
#include "ProxyTestRunner.h"
#include "ShellCommandRunner.h"
#include "updates/ApplicationQuitSession.h"
#include "updates/UpdateTransport.h"
#ifdef __APPLE__
#include <dlfcn.h>
#endif

#include "Notification.h"
#include "Settings.h"
#include "dcpp/ProxyRoute.h"
#include <QRadioButton>
#include <QMessageBox>
#include "TorrentRuntime.h"
#include "TorrentWindow.h"
#if __has_include("TorrentLocationProbe.h")
#include "TorrentLocationProbe.h"
#endif
#include "TransferView.h"
#include "AutoFitColumns.h"
#include "TabButton.h"
#include "TabNavigation.h"
#include "ToolBar.h"
#include "PublicHubs.h"
#include "FavoriteHubs.h"
#include "FavoriteUsers.h"
#include "DownloadQueue.h"
#include "ADLS.h"
#include "SpyFrame.h"
#include "QueuedUsers.h"
#include "SearchFrame.h"
#include "FinishedTransfers.h"
#include "FileBrowserModel.h"
#include "ShareBrowser.h"
#include "UserListModel.h"
#include "WulforSettings.h"
#include "WulforUtil.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentSettings.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QSystemTrayIcon>
#include <QStyleOptionComboBox>
#include <QStyleOptionGroupBox>
#include <QStyleOptionTabWidgetFrame>
#include <QStyleFactory>
#include <QPainter>
#include <QScrollArea>
#include <QAccessible>
#include <QAbstractItemView>
#include <QNetworkProxy>
#include <QMouseEvent>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardItemModel>
#include <QSplitter>
#include <QStyleHints>
#include <QTemporaryDir>

namespace updates {
struct UpdateTransportTestAccess {
    static std::unique_ptr<UpdateTransport> create(dcpp::DCContext& context, transport_detail::Backend backend) {
        return std::unique_ptr<UpdateTransport>(new UpdateTransport(context, std::move(backend), nullptr));
    }
};
}
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QThreadPool>
#include <QSemaphore>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QTabWidget>
#include <QToolButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTreeWidget>
#include <QTranslator>
#include <functional>
#include <iostream>
#include <memory>
#include <atomic>
#include <future>
#include "dcpp/ScopedFunctor.h"
#include "dcpp/BufferedSocket.h"
#include "dcpp/Semaphore.h"
#include "dcpp/HashManager.h"
#include "dcpp/DownloadManager.h"
#include "dcpp/QueueManager.h"
#include "dcpp/File.h"

struct TransferViewTestAccess {
    template<class View> static void openLocation(View &view, const eiskalt::torrent::Job &job,
                                                  std::function<bool(const QString &)> probe) {
        if constexpr (requires { view.openTorrentLocation(job, probe); })
            view.openTorrentLocation(job, std::move(probe));
        else FAIL("No guarded asynchronous Transfer location entry point");
    }
};
#include "SearchBlacklist.h"
#include "Antispam.h"
#include "updates/UpdateBackend.h"
#include "updates/UpdateController.h"
#include "updates/SettingsUpdates.h"
#include <QCheckBox>
#include <QStackedWidget>
#include <QProcess>
#include <QProcessEnvironment>
#include <QXmlStreamReader>
#if __has_include("updates/ApplicationQuitCoordinator.h")
#include "updates/ApplicationQuitCoordinator.h"
#define HAS_APPLICATION_QUIT_COORDINATOR 1
#endif
void installQuitFixtureTransport(dcpp::BufferedSocket*, std::unique_ptr<dcpp::Socket>);
bool startQuitFixtureDownload(dcpp::DCContext&, dcpp::Socket&, const std::string&, const std::string&);

struct NotificationTestAccess {
    static void replaceModule(Notification& notification, std::unique_ptr<NotifyModule> module) {
        delete notification.notify;
        notification.notify = module.release();
    }
};

#include "ApplicationQuitTestFixture.h"

namespace {
template<class T> bool guiFence(T& owner) {
    if constexpr(requires { owner.stopWorkersForQuit(); }) return owner.stopWorkersForQuit();
    else return false;
}
template<class T> bool hubFence(T& owner) {
    if constexpr(requires { owner.prepareForQuit(); }) return owner.prepareForQuit();
    else return false;
}
template<class T> bool auxiliarySave(T& owner) {
    if constexpr(requires { owner.saveForQuitChecked(); }) return owner.saveForQuitChecked();
    else return false;
}
template<class T> auto retryCheckpoint(T& owner) {
    if constexpr(requires { owner.retryCheckpoint(); }) return owner.retryCheckpoint();
    else return owner.prepare(3000);
}
#ifndef HAS_APPLICATION_QUIT_COORDINATOR
struct ApplicationQuitCoordinator {
    enum class Status { NotAdmitted, Ready, Refused };
    struct Result { Status status; const char* component; };
    ApplicationQuitCoordinator(QtContext&, updates::UpdateController*) {}
    Result prepare(uint32_t) { return {Status::NotAdmitted, "Missing coordinator"}; }
};
#else
using ApplicationQuitCoordinator = updates::ApplicationQuitCoordinator;
#endif

// Observation succeeds on either terminal outcome; saving may still fail.
bool waitForGuiQuitTerminal(MainWindow& window, bool& saved) {
    bool observed = false;
    return waitFor([&] {
        if(!observed) {
            saved = window.stopWorkersForQuit();
            observed = saved || !window.quitWorkersPending();
        }
        return observed;
    });
}

bool waitForCoordinatorQuitTerminal(ApplicationQuitCoordinator& coordinator, uint32_t timeoutMs,
                                   ApplicationQuitCoordinator::Result& terminal) {
    bool observed = false;
    return waitFor([&] {
        if(!observed) {
            terminal = coordinator.prepare(timeoutMs);
#ifdef HAS_APPLICATION_QUIT_COORDINATOR
            observed = terminal.status != ApplicationQuitCoordinator::Status::Pending;
#else
            observed = true;
#endif
        }
        return observed;
    });
}

template<class T> bool enableCheckedQuit(T& window, updates::UpdateController* updater = nullptr) {
    if constexpr(requires { window.enableCheckedQuit(updater); }) return window.enableCheckedQuit(updater);
    else return false;
}
template<class T> bool normalQuitPrepared(T& window) {
    if constexpr(requires { window.normalQuitPrepared(); }) return window.normalQuitPrepared();
    else return false;
}
QMessageBox* checkpointRecoveryPrompt() {
    for(auto* top : QApplication::topLevelWidgets())
        if(top->objectName() == "quitCheckpointRecovery") return qobject_cast<QMessageBox*>(top);
    return nullptr;
}

#ifdef __APPLE__
template<class C> constexpr bool hasAsyncAuxiliaryPersistence() {
    return requires(C& c) { c.retryCheckpoint(true); };
}
#endif

TEST_CASE("Real Preferences exposes shared Updates settings without applying rejected edits", "[qt][update-ui-owner]") {
    IsolatedMainWindow f;
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    auto dialog = std::make_unique<::Settings>(&updater);
    auto* page = dialog->findChild<updates::SettingsUpdates*>();
    REQUIRE(page);
    auto* list = dialog->findChild<QListWidget*>("listWidget"); REQUIRE(list);
    auto* stack = dialog->findChild<QStackedWidget*>("stackedWidget"); REQUIRE(stack);
    REQUIRE(list->count() == 13); // Existing eleven pages plus Torrents and Updates.
    list->setCurrentRow(12);
    REQUIRE(QMetaObject::invokeMethod(dialog.get(), "slotItemActivated", Q_ARG(QListWidgetItem*, list->item(12))));
    REQUIRE(stack->currentWidget()->isAncestorOf(page));
    REQUIRE_FALSE(list->item(12)->icon().isNull());
    const auto image = qEnvironmentVariable("EISKALT_UPDATES_SETTINGS_IMAGE");
    if(!image.isEmpty()) {
        dialog->show(); QTest::qWait(100);
        REQUIRE(dialog->grab().save(image));
        dialog->hide();
    }
    auto* checks = page->findChild<QCheckBox*>("updateChecks"); REQUIRE(checks);
    auto* notices = page->findChild<QCheckBox*>("updateNotifications"); REQUIRE(notices);
    checks->setChecked(false); notices->setChecked(false);
    dialog->reject(); dialog.reset();
    CHECK(updater.preferences().checks); CHECK(updater.preferences().notifications);
    dialog = std::make_unique<::Settings>(&updater);
    dialog->findChild<QCheckBox*>("updateChecks")->setChecked(false);
    dialog->findChild<QCheckBox*>("updateNotifications")->setChecked(false);
    dialog->accept();
    REQUIRE(dialog->result() == QDialog::Accepted);
    CHECK_FALSE(updater.preferences().checks); CHECK_FALSE(updater.preferences().notifications);
    dialog.reset();
    updater.checkNow(); CHECK(backend.checks == 1); // Dialog teardown does not stop the owner.
    ::Settings standalone;
    REQUIRE(standalone.findChild<updates::SettingsUpdates*>());
    CHECK_FALSE(standalone.findChild<QCheckBox*>("updateAutoInstall")->isEnabled());
}

TEST_CASE("Help updater checks show redacted manual results even with notifications disabled", "[qt][update-ui-owner]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    auto* action = window->findChild<QAction*>("checkForUpdates"); REQUIRE(action);
    CHECK_FALSE(action->isEnabled());
    QuitBackend backend(*f.core);
    auto updater = std::make_unique<updates::UpdateController>(*f.gui->settings(), backend,
        updates::UpdateIdentity{1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    CHECK_FALSE(std::async(std::launch::async, [&] { return window->attachUpdateController(*updater); }).get());
    REQUIRE(window->attachUpdateController(*updater)); CHECK_FALSE(window->attachUpdateController(*updater));
    REQUIRE(action->isEnabled());
    auto prefs = updater->preferences(); prefs.notifications = false; updater->applyPreferences(prefs);
    QSignalSpy notifications(window, &MainWindow::notifyUpdateMessage);
    auto* logs = f.core->getLogManager(); logs->clearLiveEntries();
    action->trigger(); REQUIRE(backend.checks == 1);
    emit backend.failed(backend.operation, "secret-password@private-proxy.invalid", updates::FailureCategory::Network);
    REQUIRE(waitFor([&] {
        for(const auto& entry : logs->getLiveEntries())
            if(entry.area == dcpp::LogManager::STATUS && entry.message.find("No direct fallback") != std::string::npos) return true;
        return false;
    }));
    bool system = false, status = false;
    for(const auto& entry : logs->getLiveEntries()) {
        CHECK(entry.message.find("secret-password") == std::string::npos);
        CHECK(entry.message.find("private-proxy") == std::string::npos);
        system |= entry.area == dcpp::LogManager::SYSTEM;
        status |= entry.area == dcpp::LogManager::STATUS;
    }
    CHECK(system); CHECK(status); CHECK(notifications.isEmpty());
    bool red = false;
    for(auto* label : window->findChildren<QLabel*>()) {
        const auto color = label->palette().color(QPalette::WindowText);
        if(label->toolTip().contains("No direct fallback"))
            red |= label->textFormat() == Qt::PlainText && color.red() > color.green() * 1.5;
    }
    CHECK(red);
    logs->clearLiveEntries(); action->trigger(); emit backend.noUpdate(backend.operation);
    REQUIRE(waitFor([&] {
        for(const auto& entry : logs->getLiveEntries())
            if(entry.message.find("No newer compatible version") != std::string::npos) return true;
        return false;
    }));
    CHECK(logs->getLiveEntries().back().message.find("No newer compatible version") != std::string::npos);
    updater.reset(); CHECK_FALSE(action->isEnabled());
    action->trigger(); CHECK(backend.checks == 2);
}

TEST_CASE("Open Preferences does not revive or dereference a destroyed updater", "[qt][update-ui-owner]") {
    IsolatedMainWindow f;
    QuitBackend backend(*f.core);
    auto updater = std::make_unique<updates::UpdateController>(*f.gui->settings(), backend,
        updates::UpdateIdentity{1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    ::Settings dialog(updater.get());
    dialog.navigate(::Settings::Page::Updates);
    auto* check = dialog.findChild<QPushButton*>("updateCheckNow"); REQUIRE(check);
    auto* page = dialog.findChild<updates::SettingsUpdates*>(); REQUIRE(page);
    updater.reset(); check->click();
    CHECK_FALSE(page->isEnabled()); CHECK(backend.checks == 0);
    dialog.accept(); CHECK(backend.checks == 0);
    CHECK(f.gui->settings()->getStr("updates/channel").isEmpty());
}

TEST_CASE("Application Preferences borrows the attached updater rather than creating a separate session", "[qt][update-ui-owner]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    REQUIRE(window->attachUpdateController(updater));
    bool inspected = false, preview = false;
    QTimer::singleShot(0, window, [&] {
        auto* dialog = qobject_cast<::Settings*>(QApplication::activeModalWidget());
        if(!dialog) return;
        if(auto* channel = dialog->findChild<QComboBox*>("updateChannel")) preview = channel->currentIndex() == 1;
        if(auto* check = dialog->findChild<QPushButton*>("updateCheckNow")) {
            check->click(); inspected = backend.checks == 1;
        }
        dialog->reject();
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, window, [] {
        if(auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    watchdog.start(5000);
    SECTION("application menu") { REQUIRE(QMetaObject::invokeMethod(window, "slotToolsSettings")); }
    SECTION("tray speed limits") { REQUIRE(QMetaObject::invokeMethod(f.gui->notification(), "slotShowSpeedLimits")); }
    CHECK(inspected); CHECK(preview); CHECK(backend.checks == 1);
    updater.cancel(); updater.checkNow(); CHECK(backend.checks == 2);
}

TEST_CASE("Automatic update notifications obey preferences and retained GUI fencing", "[qt][update-ui-owner]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    REQUIRE(window->attachUpdateController(updater));
    QSignalSpy notifications(window, &MainWindow::notifyUpdateMessage);
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    auto* logs = f.core->getLogManager(); logs->clearLiveEntries();
    SECTION("notifications disabled") {
        auto prefs = updater.preferences(); prefs.notifications = false; updater.applyPreferences(prefs);
        updater.start(); updater.poll(); REQUIRE(backend.checks == 1); emit backend.available(backend.operation, offer);
        REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
        QTest::qWait(50); CHECK(notifications.isEmpty());
        for(const auto& entry : logs->getLiveEntries()) CHECK(entry.message.find("3.0.0-pre.2") == std::string::npos);
    }
    SECTION("notification dispatch") {
        updater.start(); updater.poll(); REQUIRE(backend.checks == 1); emit backend.available(backend.operation, offer);
        REQUIRE(waitFor([&] { return notifications.count() == 1; }));
        CHECK(notifications.front().at(0).toString() == offer.key());
        CHECK(notifications.front().at(2).toString().contains("3.0.0-pre.2"));
        REQUIRE_FALSE(logs->getLiveEntries().empty());
        CHECK(logs->getLiveEntries().back().area == dcpp::LogManager::STATUS);
    }
    SECTION("retained GUI suppresses pending callbacks") {
        updater.checkNow(); REQUIRE(backend.checks == 1);
        bool saved = false;
        REQUIRE(waitForGuiQuitTerminal(*window, saved));
        REQUIRE(saved);
        emit backend.failed(backend.operation, "secret-password", updates::FailureCategory::Network);
        QTest::qWait(50); CHECK(notifications.isEmpty());
        for(const auto& entry : logs->getLiveEntries()) CHECK(entry.message.find("No direct fallback") == std::string::npos);
        CHECK_FALSE(window->findChild<QAction*>("checkForUpdates")->isEnabled());
        CHECK_FALSE(window->attachUpdateController(updater));
    }
}

TEST_CASE("Queued update notices revalidate policy at the notification module boundary", "[qt][update-notification-delivery]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    auto* notification = f.gui->notification();
    auto* settings = f.gui->settings();
    settings->setBool(WB_NOTIFY_ENABLED, true);
    settings->setBool(WB_NOTIFY_SHOW_ON_ACTIVE, true);
    settings->setBool(WB_NOTIFY_SHOW_ON_VISIBLE, true);
    settings->setBool(WB_NOTIFY_SND_ENABLED, false);
    settings->setInt(WI_NOTIFY_EVENTMAP, Notification::ANY);
    struct Recorder final : NotifyModule {
        QStringList titles, messages;
        void showMessage(const QString& title, const QString& message, QObject*) override {
            titles.append(title); messages.append(message);
        }
    };
    auto recorder = std::make_unique<Recorder>();
    auto* recorded = recorder.get();
    NotificationTestAccess::replaceModule(*notification, std::move(recorder));
    QuitBackend backend(*f.core);
    auto updater = std::make_unique<updates::UpdateController>(*settings, backend,
        updates::UpdateIdentity{1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    REQUIRE(window->attachUpdateController(*updater));
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    std::function<void()> afterDispatch;
    bool expectGuiFence = false, guiFenceRequested = false, guiFenceSaved = false, guiFencePending = false;
    bool expected = false;
    SECTION("enabled delivery") { expected = true; }
    SECTION("global notifications disabled") { settings->setBool(WB_NOTIFY_ENABLED, false); }
    SECTION("global Other events disabled") { settings->setInt(WI_NOTIFY_EVENTMAP, 0x0B); }
    SECTION("global text suppression") { notification->setSuppressTxt(true); }
    SECTION("global notifications disabled after dispatch") {
        afterDispatch = [&] { settings->setBool(WB_NOTIFY_ENABLED, false); };
    }
    SECTION("global Other events disabled after dispatch") {
        afterDispatch = [&] { settings->setInt(WI_NOTIFY_EVENTMAP, 0x0B); };
    }
    SECTION("global text suppressed after dispatch") {
        afterDispatch = [&] { notification->setSuppressTxt(true); };
    }
    SECTION("visible window suppression") {
        settings->setBool(WB_NOTIFY_SHOW_ON_ACTIVE, false);
        settings->setBool(WB_NOTIFY_SHOW_ON_VISIBLE, false);
    }
    SECTION("updater preference revoked after dispatch") {
        afterDispatch = [&] { auto prefs = updater->preferences(); prefs.notifications = false; updater->applyPreferences(prefs); };
    }
    SECTION("GUI quit fence after dispatch") {
        expectGuiFence = true;
        afterDispatch = [&] {
            guiFenceRequested = true;
            guiFenceSaved = window->stopWorkersForQuit();
            guiFencePending = window->quitWorkersPending();
        };
    }
    SECTION("controller destroyed after dispatch") { afterDispatch = [&] { updater.reset(); }; }
    SECTION("controller quit fence after dispatch") { afterDispatch = [&] { REQUIRE(updater->stopWorkersForQuit()); }; }
    SECTION("controller shutdown after dispatch") { afterDispatch = [&] { updater->shutdown(); }; }
    SECTION("Later after dispatch") { afterDispatch = [&] { updater->later(); }; }
    SECTION("Skip version after dispatch") { afterDispatch = [&] { updater->skipVersion(); }; }
    SECTION("Cancel offer after dispatch") { afterDispatch = [&] { updater->cancel(); }; }
    int dispatched = 0;
    QObject::connect(window, &MainWindow::notifyUpdateMessage, window, [&](const QString&, const QString&, const QString&) {
        ++dispatched;
        if(afterDispatch) afterDispatch();
    }, Qt::DirectConnection);
    updater->start(); updater->poll(); REQUIRE(backend.checks == 1);
    emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return dispatched == 1; }));
    if(expectGuiFence) {
        REQUIRE(guiFenceRequested);
        CHECK((guiFenceSaved || guiFencePending));
        CHECK_FALSE(window->isEnabled());
        CHECK(recorded->messages.isEmpty());
        REQUIRE(waitForGuiQuitTerminal(*window, guiFenceSaved));
        REQUIRE(guiFenceSaved);
    }
    QTest::qWait(50);
    REQUIRE(recorded->messages.size() == int(expected));
    if(expected) {
        CHECK(recorded->titles.front() == Settings::tr("Updates"));
        CHECK(recorded->messages.front().contains(offer.version));
        emit window->notifyUpdateMessage("stale-offer-key", Settings::tr("Updates"), "Stale notice");
        QTest::qWait(50); CHECK(recorded->messages.size() == 1);
    }
}

TEST_CASE("Generic Other notifications do not require an updater owner", "[qt][update-notification-delivery]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    auto* notification = f.gui->notification();
    auto* settings = f.gui->settings();
    settings->setBool(WB_NOTIFY_ENABLED, true);
    settings->setBool(WB_NOTIFY_SHOW_ON_ACTIVE, true);
    settings->setBool(WB_NOTIFY_SHOW_ON_VISIBLE, true);
    settings->setBool(WB_NOTIFY_SND_ENABLED, false);
    settings->setInt(WI_NOTIFY_EVENTMAP, Notification::ANY);
    struct Recorder final : NotifyModule {
        int delivered = 0;
        void showMessage(const QString&, const QString&, QObject*) override { ++delivered; }
    };
    auto recorder = std::make_unique<Recorder>();
    auto* recorded = recorder.get();
    NotificationTestAccess::replaceModule(*notification, std::move(recorder));
    emit window->notifyMessage(Notification::ANY, "Other event", "Generic notice");
    REQUIRE(waitFor([&] { return recorded->delivered == 1; }));
    emit window->notifyUpdateMessage("unowned-offer", "Updates", "No attached updater");
    QTest::qWait(50);
    CHECK(recorded->delivered == 1);
}

TEST_CASE("Actual window close waits for the committed matching update receipt", "[qt][window-quit]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    REQUIRE(enableCheckedQuit(*window, &updater));
    QSignalSpy ready(window, SIGNAL(normalQuitReady())); REQUIRE(ready.isValid());
    const auto route = f.core->getProxyRoute()->snapshot();
    REQUIRE_FALSE(window->close());
    CHECK(window->isVisible()); CHECK_FALSE(window->isEnabled());
    CHECK(f.core->isRunning()); CHECK(backend.routeWasRunning);
    REQUIRE(waitFor([&] { return window->quitProducersRetired(); }));
    CHECK(route->revoked->load());
    REQUIRE(waitFor([&] { return updater.quitDisposition() == updates::UpdateController::QuitDisposition::AwaitingHandoff; }));
    CHECK(backend.installs == 1); REQUIRE(backend.handoffConsent); CHECK(backend.handoffConsent());
    CHECK_FALSE(normalQuitPrepared(*window)); CHECK(ready.isEmpty());
    bool delivered = false; QObject outside;
    QMetaObject::invokeMethod(&outside, [&] { delivered = true; }, Qt::QueuedConnection);
    emit backend.installHandoffFinished(backend.handoffOperation + 1, true);
    QCoreApplication::processEvents(); CHECK(delivered); CHECK(ready.isEmpty());
    CHECK_FALSE(window->close()); CHECK(backend.installs == 1);
    bool success = true, shutdown = false;
    SECTION("native preparation") {}
    SECTION("native refusal retains owners") { success = false; }
    SECTION("controller cancellation is not native completion") { shutdown = true; }
    if(shutdown) updater.shutdown();
    else emit backend.installHandoffFinished(backend.handoffOperation, success);
    if(success && !shutdown) {
        REQUIRE(waitFor([&] { return ready.count() == 1; }));
        CHECK(normalQuitPrepared(*window)); CHECK(window->close());
        emit backend.installHandoffFinished(backend.handoffOperation, true);
        QCoreApplication::processEvents(); CHECK(ready.count() == 1);
    } else {
        REQUIRE(waitFor([&] { return updater.quitDisposition() == updates::UpdateController::QuitDisposition::Refused; }));
        QCoreApplication::processEvents(); CHECK(ready.isEmpty());
        CHECK_FALSE(normalQuitPrepared(*window)); CHECK_FALSE(window->close());
    }
    CHECK(f.core->isRunning());
    CHECK(QFile::exists(f.home.filePath("Queue.xml")));
    CHECK(QFile::exists(f.home.filePath("torrent/state/jobs.json")));
}

TEST_CASE("Actual no-update window quit prepares core before deferred exit readiness", "[qt][window-quit]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    REQUIRE(enableCheckedQuit(*window));
    QSignalSpy ready(window, SIGNAL(normalQuitReady())); REQUIRE(ready.isValid());
    const auto route = f.core->getProxyRoute()->snapshot();
    CHECK_FALSE(window->close()); CHECK(ready.isEmpty());
    CHECK_FALSE(window->isEnabled());
    REQUIRE(waitFor([&] { return window->quitProducersRetired(); }));
    CHECK(route->revoked->load());
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(normalQuitPrepared(*window)); CHECK(window->close());
    CHECK(QFile::exists(f.home.filePath("DCPlusPlus.xml")));
    CHECK(QFile::exists(f.home.filePath("Queue.xml")));
    CHECK(f.core->prepareShutdownChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready);
}

TEST_CASE("Window quit drains held workers without blocking heartbeat or admitting retained input", "[qt][window-quit][gui-worker-drain]") {
    IsolatedMainWindow f; auto* window = f.gui->mainWindow();
    REQUIRE(enableCheckedQuit(*window));
    QSignalSpy ready(window, SIGNAL(normalQuitReady())); REQUIRE(ready.isValid());
    std::atomic_bool entered{false}, released{false};
    AsyncRunner* worker = nullptr; QFutureWatcher<void>* future = nullptr;
    auto cleanup = dcpp::makeScopedFunctor([&] {
        released = true; if(worker) worker->wait(); if(future) future->waitForFinished();
    });
    auto work = [&] { entered = true; while(!released) QThread::msleep(1); };
    SECTION("window thread") { worker = new AsyncRunner(window); worker->setRunFunction(work); worker->start(); }
    SECTION("notification thread") { worker = new AsyncRunner(f.gui->notification()); worker->setRunFunction(work); worker->start(); }
    SECTION("future") { future = new QFutureWatcher<void>(window); future->setFuture(QtConcurrent::run(work)); }
    SECTION("thread-local teardown after finished signal") {
        struct HeldTeardown {
            std::atomic_bool& released;
            ~HeldTeardown() { while(!released) QThread::msleep(1); }
        };
        worker = new AsyncRunner(window);
        worker->setRunFunction([&] { thread_local HeldTeardown held{released}; entered = true; });
        worker->start();
        QElapsedTimer finish; finish.start();
        while(!worker->isFinished() && finish.elapsed() < 2000) QThread::msleep(1);
        REQUIRE(worker->isFinished()); CHECK_FALSE(worker->wait(0));
    }
    QElapsedTimer elapsed; elapsed.start();
    while(!entered && elapsed.elapsed() < 2000) QThread::msleep(1);
    REQUIRE(entered.load());
    bool late = false;
    QPointer<QObject> retained = new QObject(window); retained->deleteLater();
    QMetaObject::invokeMethod(window, [&] { late = true; }, Qt::QueuedConnection);
    QTimer heartbeat; int beats = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++beats; }); heartbeat.start(1);
    const auto route = f.core->getProxyRoute()->snapshot();
    std::promise<void> releaseGate;
    auto releaseSignal = releaseGate.get_future();
    // Explicit release keeps the held-state checks valid under delayed scheduling.
    // The watchdog still retires a worker if a regressed close blocks the caller.
    auto release = std::async(std::launch::async, [&, signal = std::move(releaseSignal)] {
        signal.wait_for(std::chrono::seconds(2)); released = true;
    });
    elapsed.restart(); CHECK_FALSE(window->close()); CHECK(elapsed.elapsed() < 50);
    CHECK_FALSE(released.load());
    CHECK_FALSE(route->revoked->load()); CHECK(ready.isEmpty());
    QTest::qWait(35); CHECK(beats > 0); CHECK_FALSE(late); REQUIRE(retained);
    CHECK_FALSE(released.load());
    CHECK_FALSE(route->revoked->load()); CHECK(ready.isEmpty());
    releaseGate.set_value();
    release.get(); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(normalQuitPrepared(*window)); CHECK(route->revoked->load()); REQUIRE(retained);
}

#ifdef __APPLE__
#include "../macos/updater/CoreWriteProbe.h"
template<class C> constexpr bool hasAsyncCorePersistence() {
    return requires(C& c) { c.prepareShutdownAsyncChecked(3000); };
}
TEST_CASE("Core persistence hold refuses quit without blocking or exiting late", "[.core-write-retirement][mainwindow]") {
    IsolatedMainWindow fixture;
    fixture.gui->createSearchBlacklist(); fixture.gui->createAntiSpam();
    auto* search = fixture.gui->searchBlacklist(); auto* spam = fixture.gui->antiSpam();
    search->setList(SearchBlacklist::NAME, {"original-rule"});
    spam->setPhrase("initialize"); spam->setPhrase("original-question");
    REQUIRE(waitFor([&] { return fixture.gui->mainWindow()->stopWorkersForQuit(); }));
    updates::ApplicationQuitSession session(*fixture.gui);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe;
    int mode = 1;
    QString file = "DCPlusPlus.xml.tmp";
    SECTION("actual Settings write") { mode = 1; }
    SECTION("actual Queue write before Settings") { file = "Queue.xml.tmp"; }
    SECTION("native Settings flush") { mode = 3; }
    SECTION("thread-local exit after write") {
        // The baseline writes on the GUI thread; holding its process-exit TLS
        // would hang the fixture. An absent async path fails this case explicitly.
        if constexpr(!hasAsyncCorePersistence<dcpp::DCContext>()) {
            FAIL("No asynchronous core persistence receipt");
        }
        mode = 6;
    }
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; });
    heartbeat.start(1);
    probe.arm(QFile::encodeName(fixture.home.filePath(file)).constData(), mode);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start();
    session.request(100);
    const auto duration = elapsed.elapsed();
    CHECK(duration < 100);
    CHECK(waitFor([&] { return probe.entered(); }));
    CHECK(probe.writer() != probe.gui);
    search->setList(SearchBlacklist::NAME, {"late-rule"}); spam->setPhrase("late-question");
    CHECK(search->getList(SearchBlacklist::NAME) == QList<QString>{"original-rule"});
    CHECK(spam->getPhrase() == "original-question");
    CHECK(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(ready.isEmpty());
    CHECK(waitFor([&] { return watchdog.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }));
    CHECK(watchdog.get());
    CHECK(advanced.load());
    CHECK(ready.isEmpty());
    if(session.state() == updates::ApplicationQuitSession::State::Refused) {
        session.request(3000);
        CHECK(waitFor([&] { return ready.count() == 1; }));
    }
    std::fprintf(stderr, "CORE_RETIREMENT mode=%d file=%s request_ms=%lld on_gui=%d heartbeat=%d exits=%d\n",
        mode, file.toUtf8().constData(), static_cast<long long>(duration),
        probe.writer() == probe.gui, advanced.load(), int(ready.count()));
}
#endif

TEST_CASE("Worker-drain timeout retains running owners and permits explicit post-drain retry", "[qt][window-quit][gui-worker-drain]") {
    IsolatedMainWindow f; auto* window = f.gui->mainWindow();
    updates::ApplicationQuitSession session(*f.gui);
    QSignalSpy refused(&session, &updates::ApplicationQuitSession::refused);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    std::atomic_bool entered{false}, released{false};
    auto* worker = new AsyncRunner(window);
    worker->setRunFunction([&] { entered = true; while(!released) QThread::msleep(1); });
    auto cleanup = dcpp::makeScopedFunctor([&] { released = true; worker->wait(); });
    worker->start(); QElapsedTimer elapsed; elapsed.start();
    while(!entered && elapsed.elapsed() < 2000) QThread::msleep(1);
    REQUIRE(entered.load());
    const auto route = f.core->getProxyRoute()->snapshot();
    auto release = std::async(std::launch::async, [&] { QThread::msleep(200); released = true; });
    bool lateTick = false;
    SECTION("deadline observed while worker is held") {}
    SECTION("worker ends after deadline before timer delivery") { lateTick = true; }
    elapsed.restart(); session.request(50); CHECK(elapsed.elapsed() < 40);
    if(lateTick) { QThread::msleep(80); released = true; REQUIRE(worker->wait(2000)); }
    REQUIRE(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(refused.count() == 1); CHECK(ready.isEmpty()); CHECK(worker->isRunning() == !lateTick);
    CHECK_FALSE(route->revoked->load()); CHECK(f.core->isRunning()); CHECK_FALSE(window->isEnabled());
    release.get(); REQUIRE(worker->wait(2000)); QTest::qWait(20); CHECK(ready.isEmpty());
    session.request(1000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::ExitReady);
    CHECK(route->revoked->load()); CHECK(refused.count() == 1);
}

TEST_CASE("Native updater pending retirement times out before core retirement and requires explicit retry", "[qt][window-quit][updater-retirement-session]") {
    IsolatedMainWindow f;
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    backend.fenceResult = false; backend.fencePending = true;
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy refused(&session, &updates::ApplicationQuitSession::refused);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    const auto route = f.core->getProxyRoute()->snapshot();
    QElapsedTimer elapsed; elapsed.start(); session.request(50);
    CHECK(elapsed.elapsed() < 40);
    CHECK(session.state() == updates::ApplicationQuitSession::State::Preparing);
    CHECK_FALSE(route->revoked->load());
    int beats = 0; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++beats; }); heartbeat.start(1);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(beats > 0); CHECK_FALSE(route->revoked->load()); CHECK(f.core->isRunning());
    CHECK(ready.isEmpty()); CHECK(refused.count() == 1); CHECK(backend.installs == 0);
    backend.fencePending = false; backend.fenceResult = true;
    QTest::qWait(30); CHECK(ready.isEmpty()); CHECK_FALSE(route->revoked->load());
    session.request(1000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(route->revoked->load()); CHECK(backend.installs == 0); CHECK(refused.count() == 1);
}

#ifdef __APPLE__
TEST_CASE("Real updater filesystem hold reaches finite quit refusal with retained context", "[.][updater-retirement-session-io]") {
    const auto arm = reinterpret_cast<void(*)(const char*, int)>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_arm"));
    const auto entered = reinterpret_cast<int(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_entered"));
    const auto release = reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_release"));
    REQUIRE(arm); REQUIRE(entered); REQUIRE(release);
    IsolatedMainWindow f; QTemporaryDir staging;
    QuitBackend backend(*f.core);
    backend.transport = updates::UpdateTransportTestAccess::create(*f.core, [](const auto&, const auto&, const auto& sink) {
        const QByteArray bytes(65536, 'x'); sink(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size());
    });
    arm(QFile::encodeName(staging.path()).constData(), 1);
    auto cleanup = dcpp::makeScopedFunctor([&] { release(); });
    const auto route = f.core->getProxyRoute()->snapshot();
    backend.transport->fetch(QUrl("https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/arm64.xml"),
        updates::ResourceKind::Feed, staging.filePath("resource"), route);
    QElapsedTimer elapsed; elapsed.start();
    while(!entered() && elapsed.elapsed() < 3000) QThread::msleep(1);
    REQUIRE(entered());
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    auto watchdog = std::async(std::launch::async, [&] { QThread::msleep(350); release(); });
    int beats = 0; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++beats; }); heartbeat.start(1);
    elapsed.restart(); session.request(50); CHECK(elapsed.elapsed() < 40);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(beats > 0); CHECK_FALSE(route->revoked->load()); CHECK(f.core->isRunning());
    CHECK(backend.transport->quitWorkersPending()); CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    CHECK_FALSE(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    watchdog.get(); QTest::qWait(30);
    CHECK(ready.isEmpty()); CHECK_FALSE(route->revoked->load()); CHECK(backend.installs == 0);
    session.request(1000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(route->revoked->load()); CHECK(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}
#endif

TEST_CASE("Worker timeout cannot commit an update even after the worker finishes", "[qt][window-quit][gui-worker-drain]") {
    IsolatedMainWindow f; auto* window = f.gui->mainWindow();
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    std::atomic_bool entered{false}, released{false};
    auto* worker = new AsyncRunner(window);
    worker->setRunFunction([&] { entered = true; while(!released) QThread::msleep(1); });
    auto cleanup = dcpp::makeScopedFunctor([&] { released = true; worker->wait(); });
    worker->start(); QElapsedTimer elapsed; elapsed.start();
    while(!entered && elapsed.elapsed() < 2000) QThread::msleep(1);
    REQUIRE(entered.load());
    const auto route = f.core->getProxyRoute()->snapshot();
    session.request(50);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(backend.fences == 1); CHECK(backend.installs == 0); CHECK_FALSE(backend.handoffConsent);
    CHECK_FALSE(route->revoked->load()); CHECK(ready.isEmpty());
    released = true; REQUIRE(worker->wait(2000)); QTest::qWait(25);
    CHECK(backend.installs == 0); CHECK(ready.isEmpty()); CHECK_FALSE(route->revoked->load());
    session.request(1000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::AwaitingHandoff; }));
    CHECK(backend.fences == 1); CHECK(backend.installs == 1); REQUIRE(backend.handoffConsent);
    CHECK(backend.handoffConsent()); CHECK(route->revoked->load()); CHECK(ready.isEmpty());
    emit backend.installHandoffFinished(backend.handoffOperation, true);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
}

TEST_CASE("Actual window hide modal and canceled confirmation do not commit quit", "[qt][window-quit]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    REQUIRE(enableCheckedQuit(*window, &updater));
    f.gui->settings()->setBool(WB_TRAY_ENABLED, true);
    f.gui->settings()->setBool(WB_MAINWINDOW_MINIMIZE_ON_CLOSE, true);
    CHECK_FALSE(window->close()); CHECK_FALSE(window->isVisible()); CHECK(backend.fences == 0);
    window->show(); f.gui->settings()->setBool(WB_TRAY_ENABLED, false);
    QDialog modal(window); modal.setModal(true); modal.show(); QCoreApplication::processEvents();
    CHECK_FALSE(window->close()); CHECK(backend.fences == 0); modal.hide();
    f.gui->settings()->setBool(WB_EXIT_CONFIRM, true);
    QTimer::singleShot(0, qApp, [] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        REQUIRE(box); box->done(QMessageBox::No);
    });
    CHECK_FALSE(window->close()); CHECK(backend.fences == 0); CHECK(backend.installs == 0);
    CHECK(window->isEnabled()); CHECK_FALSE(normalQuitPrepared(*window));
    CHECK_FALSE(f.core->getProxyRoute()->snapshot()->revoked->load());
}

TEST_CASE("Actual window retries a repaired auxiliary checkpoint without restarting producers", "[qt][window-quit][checkpoint-recovery]") {
    IsolatedMainWindow f;
    f.gui->createSearchBlacklist();
    const auto blocked = f.home.filePath("searchblacklist"); REQUIRE(QDir().mkpath(blocked));
    auto* window = f.gui->mainWindow(); REQUIRE(enableCheckedQuit(*window));
    QSignalSpy ready(window, SIGNAL(normalQuitReady())); REQUIRE(ready.isValid());
    CHECK_FALSE(window->close()); QCoreApplication::processEvents();
    CHECK_FALSE(window->isEnabled()); CHECK_FALSE(normalQuitPrepared(*window)); CHECK(ready.isEmpty());
    REQUIRE(waitFor([&] { auto* prompt = checkpointRecoveryPrompt(); return prompt && prompt->isVisible(); }));
    REQUIRE(QDir(blocked).removeRecursively());
    CHECK(ready.isEmpty()); CHECK_FALSE(normalQuitPrepared(*window));
    CHECK_FALSE(window->close()); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(normalQuitPrepared(*window)); CHECK(QFile::exists(blocked));
    CHECK(f.core->getProxyRoute()->snapshot()->revoked->load());
    CHECK(f.core->isRunning());
}

TEST_CASE("Checkpoint recovery button retains frozen rules and permanently disarms a ready update", "[qt][window-quit][checkpoint-recovery]") {
    IsolatedMainWindow f; f.gui->createSearchBlacklist(); f.gui->createAntiSpam();
    f.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"original-rule"});
    f.gui->antiSpam()->setPhrase("initialize"); f.gui->antiSpam()->setPhrase("original-question");
    QString blocked = f.home.filePath("searchblacklist");
    SECTION("search blacklist") {}
    SECTION("anti-spam") { blocked = f.home.filePath("antispam"); }
    REQUIRE(QDir().mkpath(blocked));
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    auto* window = f.gui->mainWindow(); REQUIRE(enableCheckedQuit(*window, &updater));
    const auto route = f.core->getProxyRoute()->snapshot(); auto* hash = f.core->getHashManager();
    const int priorRevocations = backend.revocations;
    QSignalSpy ready(window, SIGNAL(normalQuitReady())); REQUIRE(ready.isValid());
    CHECK_FALSE(window->close()); CHECK(ready.isEmpty()); CHECK_FALSE(window->isEnabled());
    CHECK(backend.installs == 0);
    f.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"late-rule"});
    f.gui->antiSpam()->setPhrase("late-question");
    REQUIRE(waitFor([&] { return backend.revocations > priorRevocations; }));
    QTest::qWait(30); CHECK(ready.isEmpty());
    CHECK_FALSE(window->close()); CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    // Collect the blocked retry before repairing its path or clicking again.
    REQUIRE(waitFor([] {
        auto* prompt = checkpointRecoveryPrompt();
        return prompt && prompt->isVisible();
    }));
    REQUIRE(QDir(blocked).removeRecursively());
    QTest::qWait(30); CHECK(ready.isEmpty());
    QMessageBox* prompt = checkpointRecoveryPrompt();
    CHECK(prompt != nullptr);
    int retryClicks = -1;
    if(prompt) {
        CHECK(prompt->isVisible()); CHECK(prompt->isEnabled()); CHECK_FALSE(prompt->isModal());
        auto* button = prompt->findChild<QPushButton*>("retryQuitCheckpoint"); REQUIRE(button);
        CHECK(button->isEnabled());
        QSignalSpy clicked(button, &QPushButton::clicked);
        QTest::mouseClick(button, Qt::LeftButton);
        retryClicks = clicked.count(); CHECK(retryClicks == 1);
    } else CHECK_FALSE(window->close());
    const bool recovered = waitFor([&] { return ready.count() == 1; });
    INFO("checkpoint-recovery retry clicks=" << retryClicks << " ready=" << ready.count()
         << " prompt=" << (prompt ? prompt->text().toStdString() : "none"));
    REQUIRE(recovered);
    CHECK(normalQuitPrepared(*window)); CHECK(backend.installs == 0); CHECK(backend.fences == 1);
    CHECK(updater.quitDisposition() == updates::UpdateController::QuitDisposition::NoUpdate);
    CHECK(route->revoked->load()); CHECK(f.core->getHashManager() == hash);
    CHECK(f.core->getClientManager()->getClients().empty());
    QFile search(f.home.filePath("searchblacklist")); REQUIRE(search.open(QIODevice::ReadOnly));
    CHECK(search.readAll() == "NAME:original-rule\n");
    QFile spam(f.home.filePath("antispam")); REQUIRE(spam.open(QIODevice::ReadOnly));
    CHECK(spam.readAll() == "|ANTISPAM_PHRASE|original-question\n|ANTISPAM_KEY|\n|ATTEMPTS|0\n");
}

TEST_CASE("A failed updater recovery disarm cannot authorize recovered exit", "[qt][window-quit][checkpoint-recovery]") {
    IsolatedMainWindow f; f.gui->createSearchBlacklist();
    const auto blocked = f.home.filePath("searchblacklist"); REQUIRE(QDir().mkpath(blocked));
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    auto cleanup = dcpp::makeScopedFunctor([&] { backend.rejectRecoveryDisarm = false; });
    auto* window = f.gui->mainWindow(); REQUIRE(enableCheckedQuit(*window, &updater));
    QSignalSpy ready(window, SIGNAL(normalQuitReady())); REQUIRE(ready.isValid());
    backend.rejectRecoveryDisarm = true;
    CHECK_FALSE(window->close()); REQUIRE(waitFor([&] { return backend.revocations == 1; })); CHECK(ready.isEmpty());
    REQUIRE(QDir(blocked).removeRecursively()); backend.rejectRecoveryDisarm = false;
    CHECK_FALSE(window->close()); QTest::qWait(30);
    CHECK(ready.isEmpty()); CHECK_FALSE(normalQuitPrepared(*window)); CHECK(backend.installs == 0);
    CHECK(backend.fences == 1); CHECK(backend.revocations == 1);
}

TEST_CASE("Direct coordinator checkpoint recovery cannot preserve installer consent", "[qt][application-quit][checkpoint-recovery]") {
    IsolatedMainWindow f; f.gui->createSearchBlacklist();
    const auto blocked = f.home.filePath("searchblacklist"); REQUIRE(QDir().mkpath(blocked));
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    ApplicationQuitCoordinator coordinator(*f.gui, &updater);
    ApplicationQuitCoordinator::Result terminal{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, terminal));
    REQUIRE(terminal.status == ApplicationQuitCoordinator::Status::Refused);
    REQUIRE(QDir(blocked).removeRecursively());
    REQUIRE(retryCheckpoint(coordinator).status == ApplicationQuitCoordinator::Status::Ready);
    updater.normalQuitCommitted();
    CHECK(backend.installs == 0);
    CHECK(updater.quitDisposition() == updates::UpdateController::QuitDisposition::NoUpdate);
}

TEST_CASE("Application quit aggregates real GUI hub Torrent and core saves without install", "[qt][application-quit]") {
    IsolatedMainWindow f;
    f.gui->createSearchBlacklist(); f.gui->createAntiSpam();
    f.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"retained-rule"});
    f.gui->antiSpam()->setPhrase("initialize"); f.gui->antiSpam()->setPhrase("retained-question");
    f.gui->antiSpam()->addToBlack({"blocked-fixture"});
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    updater.checkNow();
    updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, 2, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    auto* runtime = f.gui->mainWindow()->findChild<TorrentRuntime*>(); REQUIRE(runtime);
    auto* engine = runtime->engine(); REQUIRE(engine);
    const auto id = engine->add("magnet:?xt=urn:btih:0123456789012345678901234567890123456789&dn=isolated-active-job");
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(waitFor([&] { return !engine->jobs().isEmpty() && !engine->jobs().front().paused; }));
    f.gui->mainWindow()->newHubFrame("adc://127.0.0.1:1", "");
    QPointer<HubFrame> hub = f.gui->mainWindow()->findChild<HubFrame*>(); REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if(hub) hub->close(); });
    const auto route = f.core->getProxyRoute()->snapshot();
    QFile hashed(f.home.filePath("hashed.bin")); REQUIRE(hashed.open(QIODevice::WriteOnly));
    REQUIRE(hashed.write("fixture-bytes") == 13); hashed.close();
    dcpp::TigerTree tree(1024); tree.update("fixture-bytes", 13); tree.finalize();
    dcpp::File input(hashed.fileName().toStdString(), dcpp::File::READ, dcpp::File::OPEN);
    f.core->getHashManager()->addTree(hashed.fileName().toStdString(), input.getLastModified(), tree);
    const auto part = f.home.filePath("partial.part"), target = f.home.filePath("partial.bin");
    dcpp::Socket peer;
    REQUIRE(startQuitFixtureDownload(*f.core, peer, part.toStdString(), target.toStdString()));
    bool late = false, unrelatedDelivered = false;
    QMetaObject::invokeMethod(f.gui->mainWindow(), [&] { late = true; }, Qt::QueuedConnection);
    QObject unrelated;
    QMetaObject::invokeMethod(&unrelated, [&] { unrelatedDelivered = true; }, Qt::QueuedConnection);
    ApplicationQuitCoordinator coordinator(*f.gui, &updater);
    CHECK(std::async(std::launch::async, [&] { return coordinator.prepare(3000).status; }).get() ==
          ApplicationQuitCoordinator::Status::NotAdmitted);
    QDialog modal(f.gui->mainWindow()); modal.setModal(true); modal.show();
    QCoreApplication::processEvents(); late = false;
    CHECK(coordinator.prepare(3000).status == ApplicationQuitCoordinator::Status::NotAdmitted);
    CHECK(backend.fences == 0); CHECK_FALSE(route->revoked->load());
    modal.hide();
    unrelatedDelivered = false;
    QMetaObject::invokeMethod(&unrelated, [&] { unrelatedDelivered = true; }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(f.gui->mainWindow(), [&] { late = true; }, Qt::QueuedConnection);
    ApplicationQuitCoordinator::Result terminal{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, terminal));
    REQUIRE(terminal.status == ApplicationQuitCoordinator::Status::Ready);
    CHECK(backend.fences == 1); CHECK(backend.routeWasRunning); CHECK(backend.installs == 0);
    CHECK(route->revoked->load()); CHECK(f.core->isRunning());
    CHECK(f.core->getClientManager()->getClients().empty());
    CHECK(f.core->getDownloadManager()->getDownloadCount() == 0);
    CHECK(f.core->getQueueManager()->getPos(target.toStdString()) == 128 * 1024);
    CHECK(QFileInfo(part).size() == 192 * 1024);
    QFile queue(f.home.filePath("Queue.xml")); REQUIRE(queue.open(QIODevice::ReadOnly));
    QXmlStreamReader xml(queue.readAll());
    qint64 savedBytes = 0; bool savedTarget = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == u"Download") {
            CHECK(xml.attributes().value("Target") == target);
            CHECK(xml.attributes().value("TempTarget") == part);
            savedTarget = true;
        }
        if (xml.name() == u"Segment") savedBytes += xml.attributes().value("Size").toLongLong();
    }
    CHECK_FALSE(xml.hasError()); CHECK(savedTarget); CHECK(savedBytes == 128 * 1024);
    REQUIRE(hub); CHECK_FALSE(hub->isEnabled());
    CHECK(QFile::exists(f.home.filePath("DCPlusPlus.xml")));
    CHECK(QFile::exists(f.home.filePath("HashIndex.xml")));
    CHECK(QFile::exists(f.home.filePath("torrent/state/jobs.json")));
    QCoreApplication::processEvents(); CHECK_FALSE(late); CHECK(unrelatedDelivered);
    updater.checkNow(); CHECK(backend.checks == 1);
    CHECK(coordinator.prepare(3000).status == ApplicationQuitCoordinator::Status::Ready);
    CHECK(backend.fences == 1); CHECK(backend.installs == 0);
}

TEST_CASE("Prepared application owners remain alive through a matching update handoff receipt", "[qt][application-quit][update-handoff-composition]") {
    IsolatedMainWindow f;
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    bool install = true, prepared = true;
    SECTION("prepared") { updater.installOnNextQuit(); }
    SECTION("refused") { updater.installOnNextQuit(); prepared = false; }
    SECTION("ordinary no update") { install = false; }
    ApplicationQuitCoordinator coordinator(*f.gui, &updater);
    ApplicationQuitCoordinator::Result terminal{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, terminal));
    REQUIRE(terminal.status == ApplicationQuitCoordinator::Status::Ready);
    auto* core = f.core.get(); QPointer<MainWindow> window = f.gui->mainWindow();
    CHECK(core->getProxyRoute()->snapshot()->revoked->load()); CHECK(backend.installs == 0);
    QSignalSpy receipt(&updater, &updates::UpdateController::installHandoffFinished);
    updater.normalQuitCommitted();
    if(install) {
        REQUIRE(updater.quitDisposition() == updates::UpdateController::QuitDisposition::AwaitingHandoff);
        REQUIRE(backend.handoffConsent); CHECK(backend.handoffConsent()); CHECK(backend.installs == 1);
        // Intentional core route retirement cannot authorize remote work, but
        // the retained local-stage consent and receipt remain usable.
        emit backend.installHandoffFinished(backend.handoffOperation, prepared);
        REQUIRE(waitFor([&] { return receipt.count() == 1; }));
        CHECK(receipt.front().front().toBool() == prepared);
        CHECK(updater.quitDisposition() == (prepared ? updates::UpdateController::QuitDisposition::Prepared :
            updates::UpdateController::QuitDisposition::Refused));
        CHECK_FALSE(backend.handoffConsent());
    } else {
        CHECK(updater.quitDisposition() == updates::UpdateController::QuitDisposition::NoUpdate);
        CHECK(backend.installs == 0); CHECK(receipt.isEmpty());
    }
    REQUIRE(window); CHECK(f.core.get() == core); CHECK(core->isRunning()); CHECK_FALSE(window->isEnabled());
}

TEST_CASE("Application quit retains an earlier updater or GUI refusal after core succeeds", "[qt][application-quit]") {
    IsolatedMainWindow f;
    QuitBackend backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const auto config = f.home.filePath("EiskaltDC++_Qt.conf");
    QString component;
    SECTION("updater") { backend.fenceResult = false; component = "Updater"; }
    SECTION("GUI") {
        REQUIRE(f.gui->settings()->saveChecked());
        REQUIRE(QFile::remove(config)); REQUIRE(QDir().mkpath(config)); component = "GUI";
    }
    auto* hash = f.core->getHashManager(); QPointer<MainWindow> window = f.gui->mainWindow();
    ApplicationQuitCoordinator coordinator(*f.gui, &updater);
    ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, result));
    REQUIRE(result.status == ApplicationQuitCoordinator::Status::Refused);
    CHECK(QString(result.component) == component);
    CHECK(f.core->prepareShutdownChecked(3000).status == dcpp::DCContext::ShutdownStatus::Ready);
    CHECK(f.core->getHashManager() == hash); REQUIRE(window); CHECK_FALSE(window->isEnabled());
    CHECK(backend.routeWasRunning); CHECK(backend.installs == 0);
    if(QFileInfo(config).isDir()) REQUIRE(QDir(config).removeRecursively());
    backend.fenceResult = true;
    CHECK(coordinator.prepare(3000).status == ApplicationQuitCoordinator::Status::Refused);
    CHECK(backend.fences == 1);
}

TEST_CASE("Application quit rejects updater parents nested under retained GUI roots", "[qt][application-quit][application-quit-order]") {
    IsolatedMainWindow f;
    QuitBackend backend(*f.core);
    auto* container = new QObject(f.gui->mainWindow());
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)},
        QDateTime::currentDateTimeUtc, container);
    ApplicationQuitCoordinator coordinator(*f.gui, &updater);
    CHECK(coordinator.prepare(3000).status == ApplicationQuitCoordinator::Status::NotAdmitted);
    CHECK(backend.fences == 0); CHECK(f.gui->mainWindow()->isEnabled());
    CHECK_FALSE(f.core->getProxyRoute()->snapshot()->revoked->load());
}

TEST_CASE("GUI refusal delays auxiliary snapshot reads until core callback drain", "[qt][application-quit][application-quit-order]") {
    IsolatedMainWindow f;
    f.gui->createSearchBlacklist();
    struct SaveObserver : dcpp::SettingsManagerListener {
        QString path;
        bool beforeAuxiliary = false;
        void on(Save, dcpp::SimpleXML&) override { beforeAuxiliary = !QFile::exists(path); }
    } observer;
    observer.path = f.home.filePath("searchblacklist");
    f.core->getSettingsManager()->addListener(&observer);
    auto remove = dcpp::makeScopedFunctor([&] { f.core->getSettingsManager()->removeListener(&observer); });
    REQUIRE(f.gui->settings()->saveChecked());
    const auto config = f.home.filePath("EiskaltDC++_Qt.conf");
    REQUIRE(QFile::remove(config)); REQUIRE(QDir().mkpath(config));
    ApplicationQuitCoordinator coordinator(*f.gui);
    ApplicationQuitCoordinator::Result terminal{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, terminal));
    REQUIRE(terminal.status == ApplicationQuitCoordinator::Status::Refused);
    CHECK(observer.beforeAuxiliary);
    CHECK(QFile::exists(observer.path));
    REQUIRE(QDir(config).removeRecursively());
}

TEST_CASE("Auxiliary GUI quit snapshots freeze intent and refuse persistence failures", "[qt][application-quit][auxiliary-quit]") {
    IsolatedMainWindow f;
    f.gui->createSearchBlacklist(); f.gui->createAntiSpam();
    auto* search = f.gui->searchBlacklist(); auto* spam = f.gui->antiSpam();
    search->setList(SearchBlacklist::NAME, {"fixture-rule"});
    spam->setPhrase("initialize"); spam->setPhrase("fixture-question"); spam->addToBlack({"blocked-fixture"});
    SECTION("saved terminal intent") {
        CHECK_FALSE(std::async(std::launch::async, [&] { return auxiliarySave(*search); }).get());
        CHECK_FALSE(std::async(std::launch::async, [&] { return auxiliarySave(*spam); }).get());
        REQUIRE(auxiliarySave(*search)); REQUIRE(auxiliarySave(*spam));
        const auto read = [&](const QString& name) {
            QFile file(f.home.filePath(name)); REQUIRE(file.open(QIODevice::ReadOnly)); return file.readAll();
        };
        CHECK(read("searchblacklist") == "NAME:fixture-rule\n");
        CHECK(read("antispam") == "|ANTISPAM_PHRASE|fixture-question\n|ANTISPAM_KEY|\n|ATTEMPTS|0\n");
        CHECK(read("blacklist") == "blocked-fixture\n");
        CHECK(read("graylist").isEmpty()); CHECK(read("whitelist").isEmpty());
        search->setList(SearchBlacklist::NAME, {"late-rule"});
        spam->setPhrase("late-question"); spam->clearBlack();
        CHECK(search->getList(SearchBlacklist::NAME) == QList<QString>{"fixture-rule"});
        CHECK(spam->getPhrase() == "fixture-question");
        CHECK(spam->getBlack() == QList<QString>{"blocked-fixture"});
        CHECK(auxiliarySave(*search)); CHECK(auxiliarySave(*spam));
    }
    SECTION("failed terminal intent") {
        const auto searchFile = f.home.filePath("searchblacklist");
        const auto spamFile = f.home.filePath("antispam");
        REQUIRE(QDir().mkpath(searchFile)); REQUIRE(QDir().mkpath(spamFile));
        CHECK_FALSE(auxiliarySave(*search)); CHECK_FALSE(auxiliarySave(*spam));
        CHECK(QFile::exists(f.home.filePath("blacklist")));
        REQUIRE(QDir(searchFile).removeRecursively()); REQUIRE(QDir(spamFile).removeRecursively());
        search->setList(SearchBlacklist::NAME, {"late-rule"}); spam->setPhrase("late-question");
        REQUIRE(auxiliarySave(*search)); REQUIRE(auxiliarySave(*spam));
        QFile searchSaved(searchFile); REQUIRE(searchSaved.open(QIODevice::ReadOnly));
        CHECK(searchSaved.readAll() == "NAME:fixture-rule\n");
        QFile spamSaved(spamFile); REQUIRE(spamSaved.open(QIODevice::ReadOnly));
        CHECK(spamSaved.readAll() == "|ANTISPAM_PHRASE|fixture-question\n|ANTISPAM_KEY|\n|ATTEMPTS|0\n");
    }
}

TEST_CASE("Application quit refuses auxiliary save failure even after a ready core", "[qt][application-quit]") {
    IsolatedMainWindow f;
    f.gui->createSearchBlacklist(); f.gui->createAntiSpam();
    const auto blocked = f.home.filePath("searchblacklist");
    REQUIRE(QDir().mkpath(blocked));
    ApplicationQuitCoordinator coordinator(*f.gui);
    ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, result));
    REQUIRE(result.status == ApplicationQuitCoordinator::Status::Refused);
    CHECK(QString(result.component) == "Search blacklist");
    CHECK(result.core.status == dcpp::DCContext::ShutdownStatus::Ready);
    CHECK(f.core->isRunning()); CHECK_FALSE(f.gui->mainWindow()->isEnabled());
    CHECK(QFile::exists(f.home.filePath("antispam")));
    CHECK(QFile::exists(f.home.filePath("DCPlusPlus.xml")));
    REQUIRE(QDir(blocked).removeRecursively());
    CHECK(coordinator.prepare(3000).status == ApplicationQuitCoordinator::Status::Refused);
    CHECK_FALSE(QFile::exists(blocked));
    REQUIRE(retryCheckpoint(coordinator).status == ApplicationQuitCoordinator::Status::Ready);
    CHECK(QFile::exists(blocked)); CHECK_FALSE(f.gui->mainWindow()->isEnabled());
}

TEST_CASE("Application quit keeps failed core owners in an isolated child process", "[qt][application-quit]") {
    for(const auto& mode : {QString("snapshot"), QString("socket"), QString("both")}) {
        QProcess child; auto env = QProcessEnvironment::systemEnvironment();
        env.insert("EISKALT_QUIT_CHILD", mode); child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {"[application-quit-child]"});
        REQUIRE(child.waitForStarted());
        const bool finished = child.waitForFinished(30000);
        if(!finished) { child.kill(); child.waitForFinished(); }
        INFO(child.readAllStandardOutput().constData()); INFO(child.readAllStandardError().constData());
        REQUIRE(finished); CHECK(child.exitStatus() == QProcess::NormalExit); CHECK(child.exitCode() == 0);
    }
}

TEST_CASE("Auxiliary retry rejects foreign and modal callers without replaying core saves", "[qt][application-quit][checkpoint-recovery]") {
    IsolatedMainWindow f; f.gui->createSearchBlacklist();
    const auto blocked = f.home.filePath("searchblacklist"); REQUIRE(QDir().mkpath(blocked));
    struct Counter : dcpp::SettingsManagerListener {
        int saves = 0;
        void on(Save, dcpp::SimpleXML&) override { ++saves; }
    } counter;
    f.core->getSettingsManager()->addListener(&counter);
    auto cleanup = dcpp::makeScopedFunctor([&] { f.core->getSettingsManager()->removeListener(&counter); });
    ApplicationQuitCoordinator coordinator(*f.gui);
    ApplicationQuitCoordinator::Result terminal{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, terminal));
    REQUIRE(terminal.status == ApplicationQuitCoordinator::Status::Refused);
    CHECK(counter.saves == 1);
    const auto foreign = std::async(std::launch::async, [&] { return retryCheckpoint(coordinator); }).get();
    CHECK(foreign.status == ApplicationQuitCoordinator::Status::NotAdmitted);
    CHECK(foreign.core.status == dcpp::DCContext::ShutdownStatus::UnsupportedContext);
    QDialog modal(f.gui->mainWindow()); modal.setModal(true); modal.show();
    QCoreApplication::processEvents();
    CHECK(retryCheckpoint(coordinator).status == ApplicationQuitCoordinator::Status::NotAdmitted);
    CHECK(counter.saves == 1); modal.hide();
    REQUIRE(QDir(blocked).removeRecursively());
    REQUIRE(retryCheckpoint(coordinator).status == ApplicationQuitCoordinator::Status::Ready);
    CHECK(counter.saves == 1); CHECK(QFile::exists(blocked));
    CHECK(f.core->getProxyRoute()->snapshot()->revoked->load());
    CHECK(f.core->getClientManager()->getClients().empty());
}

TEST_CASE("Auxiliary retry cannot replace lost frozen intent with a new owner", "[qt][application-quit][checkpoint-recovery]") {
    IsolatedMainWindow f; f.gui->createAntiSpam();
    f.gui->antiSpam()->setPhrase("initialize"); f.gui->antiSpam()->setPhrase("retained-question");
    const auto blocked = f.home.filePath("antispam"); REQUIRE(QDir().mkpath(blocked));
    ApplicationQuitCoordinator coordinator(*f.gui);
    ApplicationQuitCoordinator::Result terminal{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, 3000, terminal));
    REQUIRE(terminal.status == ApplicationQuitCoordinator::Status::Refused);
    f.gui->destroyAntiSpam(); f.gui->createAntiSpam();
    REQUIRE(QDir(blocked).removeRecursively());
    CHECK(retryCheckpoint(coordinator).status != ApplicationQuitCoordinator::Status::Ready);
    CHECK_FALSE(QFile::exists(blocked));
}

TEST_CASE("Application quit failed-core child fixture", "[.][application-quit-child]") {
    const auto mode = qEnvironmentVariable("EISKALT_QUIT_CHILD");
    REQUIRE((mode == "snapshot" || mode == "socket" || mode == "both"));
    std::set_terminate([] { std::_Exit(81); });
    IsolatedMainWindow f;
    f.gui->createSearchBlacklist(); f.gui->createAntiSpam();
    auto* core = f.core.get(); auto* hash = core->getHashManager();
    QPointer<MainWindow> window = f.gui->mainWindow();
    QuitBackend backend(*core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    dcpp::Semaphore entered, release;
    auto drain = dcpp::makeScopedFunctor([&] {
        release.signal(); dcpp::BufferedSocket::waitShutdown(3000);
    });
    if(mode == "both") {
        REQUIRE(f.gui->settings()->saveChecked());
        const auto config = f.home.filePath("EiskaltDC++_Qt.conf");
        REQUIRE(QFile::remove(config)); REQUIRE(QDir().mkpath(config));
    }
    if(mode == "snapshot") REQUIRE(QDir().mkpath(f.home.filePath("Favorites.xml.tmp")));
    else {
        struct SlowTransport : dcpp::Socket {
            dcpp::Semaphore& entered; dcpp::Semaphore& release;
            SlowTransport(dcpp::Semaphore& a, dcpp::Semaphore& b) : entered(a), release(b) {}
            ~SlowTransport() override { entered.signal(); release.wait(); }
        };
        auto* socket = dcpp::BufferedSocket::getSocket('\n', *core);
        auto transport = std::make_unique<SlowTransport>(entered, release);
        transport->create(dcpp::Socket::TYPE_TCP, AF_INET);
        installQuitFixtureTransport(socket, std::move(transport)); dcpp::BufferedSocket::putSocket(socket);
        REQUIRE(entered.wait(3000));
    }
    ApplicationQuitCoordinator coordinator(*f.gui, &updater);
    ApplicationQuitCoordinator::Result result{};
    REQUIRE(waitForCoordinatorQuitTerminal(coordinator, mode == "snapshot" ? 3000 : 25, result));
    REQUIRE(result.status == ApplicationQuitCoordinator::Status::Refused);
    REQUIRE(f.core.get() == core); REQUIRE(core->isRunning()); REQUIRE(core->getHashManager() == hash);
    REQUIRE(window); REQUIRE_FALSE(window->isEnabled()); REQUIRE(backend.installs == 0);
    if(mode == "snapshot") {
        REQUIRE(QString(result.component) == "Favorites");
        REQUIRE(QDir(f.home.filePath("Favorites.xml.tmp")).removeRecursively());
    } else {
        REQUIRE(result.core.status == dcpp::DCContext::ShutdownStatus::SocketTimedOut);
        REQUIRE(QString(result.component) == (mode == "both" ? "GUI" : "Buffered sockets"));
        if(mode == "both") {
            REQUIRE_FALSE(QFile::exists(f.home.filePath("searchblacklist")));
            REQUIRE_FALSE(QFile::exists(f.home.filePath("antispam")));
        }
        release.signal(); REQUIRE(dcpp::BufferedSocket::waitShutdown(3000));
    }
    REQUIRE(coordinator.prepare(3000).status == ApplicationQuitCoordinator::Status::Refused);
    REQUIRE_FALSE(window->close()); REQUIRE(window);
    // Deliberately retain the entire failed context/GUI until OS test-process exit.
    std::fflush(stdout); std::fflush(stderr); std::_Exit(0);
}

TEST_CASE("Hub registration is delivered on the GUI owner thread", "[qt][hub-quit-fence]") {
    IsolatedMainWindow f;
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    std::atomic_bool registered{false}, ownerThread{false};
    QObject::connect(f.gui->hubManager(), &HubManager::hubRegistered, f.gui->mainWindow(),
        [&](QObject*) {
            ownerThread = QThread::currentThread() == qApp->thread();
            registered = true;
        }, Qt::DirectConnection);
    f.gui->mainWindow()->newHubFrame(QString("adc://127.0.0.1:%1").arg(server.serverPort()), "");
    QPointer<HubFrame> hub = f.gui->mainWindow()->findChild<HubFrame*>();
    REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if(hub) hub->close(); });
    REQUIRE(waitFor([&] { return registered.load(); }));
    CHECK(ownerThread.load());
}

TEST_CASE("Favourite hub editor keeps legacy ADC compatibility scoped and off by default", "[qt][adc-legacy-challenge]") {
    IsolatedMainWindow fixture;
    FavoriteHubs favorites(*fixture.core);
    bool seen = false, initiallyOff = false, addressChangeCleared = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, qApp, [] {
        if(auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    QTimer::singleShot(0, &favorites, [&] {
        auto* editor = qobject_cast<FavoriteHubEditor*>(QApplication::activeModalWidget());
        if(!editor) return;
        seen = true;
        initiallyOff = !editor->checkBox_LEGACY_ADC_PASSWORD->isChecked();
        editor->lineEdit_NAME->setText("Legacy fixture");
        editor->lineEdit_ADDRESS->setText("adc://legacy-gui.invalid");
        editor->checkBox_LEGACY_ADC_PASSWORD->setChecked(true);
        editor->lineEdit_ADDRESS->setText("adc://replacement-gui.invalid");
        addressChangeCleared = !editor->checkBox_LEGACY_ADC_PASSWORD->isChecked();
        editor->lineEdit_ADDRESS->setText("adc://legacy-gui.invalid");
        editor->checkBox_LEGACY_ADC_PASSWORD->setChecked(true);
        editor->accept();
    });
    watchdog.start(5000);
    REQUIRE(QMetaObject::invokeMethod(&favorites, "slotAdd_newButtonClicked", Qt::DirectConnection));
    watchdog.stop();
    REQUIRE(seen);
    CHECK(initiallyOff);
    CHECK(addressChangeCleared);
    auto* entry = fixture.core->getFavoriteManager()->getFavoriteHubEntry("adc://legacy-gui.invalid");
    REQUIRE(entry);
    CHECK(entry->getLegacyAdcPasswordChallenges());
    CHECK_FALSE(entry->getConnect());
    CHECK_FALSE(fixture.core->getFavoriteManager()->getFavoriteHubEntry("adc://other.invalid"));
}

TEST_CASE("GUI quit preparation retires connected and inactive hub clients but retains tabs", "[qt][mainwindow][hub-quit-fence]") {
    IsolatedMainWindow f;
    QTcpServer first, second;
    REQUIRE(first.listen(QHostAddress::LocalHost));
    REQUIRE(second.listen(QHostAddress::LocalHost));
    const QString one = QString("adc://127.0.0.1:%1").arg(first.serverPort());
    const QString two = QString("dchub://127.0.0.1:%1").arg(second.serverPort());
    f.gui->mainWindow()->newHubFrame(one, "");
    f.gui->mainWindow()->newHubFrame(two, "");
    QList<QPointer<HubFrame>> hubs;
    for(auto* hub : f.gui->mainWindow()->findChildren<HubFrame*>()) hubs.append(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] {
        for(auto hub : hubs) if(hub) {
            hub->close();
            if(hub && !hub->parent()) delete hub.data();
        }
    });
    REQUIRE(hubs.size() == 2);
    REQUIRE(waitFor([&] { return f.gui->hubManager()->getHubs().size() == 2; }));
    REQUIRE(f.core->getClientManager()->getClients().size() == 2);
    // Registered tabs can be detached from the window's QObject subtree.
    hubs.front()->setParent(nullptr);
    bool late = false;
    QMetaObject::invokeMethod(hubs.front(), [&] { late = true; }, Qt::QueuedConnection);
    bool saved = false;
    REQUIRE(waitForGuiQuitTerminal(*f.gui->mainWindow(), saved));
    REQUIRE(saved);
    CHECK(f.core->getClientManager()->getClients().empty());
    CHECK(f.gui->hubManager()->getHubs().isEmpty());
    CHECK(f.gui->hubManager()->activeHub() == nullptr);
    for(auto hub : hubs) { REQUIRE(hub); CHECK_FALSE(hub->isEnabled()); }
    CHECK_FALSE(f.gui->settings()->getStr(view_layout::hubStateKey(one)).isEmpty());
    CHECK_FALSE(f.gui->settings()->getStr(view_layout::hubStateKey(two)).isEmpty());
    QCoreApplication::processEvents();
    CHECK_FALSE(late);
    CHECK(guiFence(*f.gui->mainWindow()));
}

TEST_CASE("Private message Enter and history preserve visual composer formatting",
          "[qt][chat-composer-integration]") {
    IsolatedMainWindow fixture;
    fixture.gui->settings()->setBool(WB_USE_CTRL_ENTER, false);
    fixture.gui->settings()->setInt(WI_OUT_IN_HIST, 10);
    PMWindow pm(QString::fromStdString(dcpp::CID().toBase32()), "adc://127.0.0.1:1");
    pm.resize(800, 500);
    pm.show();
    auto *editor = qobject_cast<ChatEdit *>(pm.inputWidget());
    REQUIRE(editor);
    editor->setFocus();
    REQUIRE(waitFor([&] { return editor->hasFocus(); }));
    editor->setPlainText("first");
    QTest::keyClick(editor, Qt::Key_Return);
    REQUIRE(editor->toPlainText().isEmpty());
    editor->setPlainText("second");
    editor->selectAll();
    editor->wrapWithTag("b");
    QTest::keyClick(editor, Qt::Key_Return);
    REQUIRE(editor->toPlainText().isEmpty());
    editor->setPlainText("draft");
    pm.prevMsg();
    CHECK(editor->toPlainText() == "second");
    QTextCursor cursor(editor->document());
    cursor.select(QTextCursor::Document);
    CHECK(cursor.charFormat().fontWeight() >= QFont::Bold);
    pm.nextMsg();
    CHECK(editor->toPlainText() == "draft");
}

TEST_CASE("Hub composer sends BBCode over the wire and recalls it visually",
          "[qt][chat-composer-integration]") {
    IsolatedMainWindow fixture;
    fixture.gui->settings()->setBool(WB_USE_CTRL_ENTER, false);
    fixture.gui->settings()->setInt(WI_OUT_IN_HIST, 10);
    fixture.core->getSettingsManager()->set(dcpp::SettingsManager::NICK, "ComposerQA");
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    fixture.gui->mainWindow()->newHubFrame(
        QString("dchub://127.0.0.1:%1").arg(server.serverPort()), "");
    QPointer<HubFrame> hub = fixture.gui->mainWindow()->findChild<HubFrame *>();
    REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if (hub) hub->close(); });
    REQUIRE(waitFor([&] { return server.hasPendingConnections(); }));
    std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
    REQUIRE(peer);
    peer->write("$Lock EXTENDEDPROTOCOLABCABCABCABCABCABC Pk=Fixture|");
    QByteArray received;
    const auto receivedText = [&](const QByteArray &text) {
        received += peer->readAll();
        return received.contains(text);
    };
    REQUIRE(waitFor([&] { return receivedText("$ValidateNick ComposerQA|"); }));
    peer->write("$Hello ComposerQA|");
    REQUIRE(waitFor([&] { return receivedText("$MyINFO"); }));
    auto *editor = hub->findChild<ChatEdit *>("plainTextEdit_INPUT");
    REQUIRE(editor);
    editor->setFocus();
    REQUIRE(waitFor([&] { return editor->hasFocus(); }));
    editor->setPlainText("first");
    QTest::keyClick(editor, Qt::Key_Return);
    REQUIRE(waitFor([&] { return receivedText("<ComposerQA> first|"); }));
    editor->setPlainText("second");
    editor->selectAll();
    editor->wrapWithTag("b");
    QTest::keyClick(editor, Qt::Key_Return);
    REQUIRE(waitFor([&] { return receivedText("<ComposerQA> [b]second[/b]|"); }));
    editor->setPlainText("draft");
    hub->prevMsg();
    CHECK(editor->toPlainText() == "second");
    QTextCursor cursor(editor->document());
    cursor.select(QTextCursor::Document);
    CHECK(cursor.charFormat().fontWeight() >= QFont::Bold);
    hub->nextMsg();
    CHECK(editor->toPlainText() == "draft");
    editor->setBBCodeText("/me [b][/b]/clear");
    QTest::keyClick(editor, Qt::Key_Return);
    REQUIRE(waitFor([&] { return receivedText("<ComposerQA> /me /clear|"); }));
}

TEST_CASE("Retained hub preparation rejects later reconnect redirect command and deletion", "[qt][hub-quit-fence]") {
    IsolatedMainWindow f;
    f.gui->mainWindow()->newHubFrame("adc://127.0.0.1:1", "");
    QPointer<HubFrame> hub = f.gui->mainWindow()->findChild<HubFrame*>();
    REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if(hub) hub->close(); });
    CHECK_FALSE(std::async(std::launch::async, [&] { return hubFence(*hub); }).get());
    REQUIRE(hubFence(*hub));
    CHECK(f.core->getClientManager()->getClients().empty());
    bool late = false, childLate = false;
    auto* child = new QObject(hub);
    QMetaObject::invokeMethod(child, [&] { childLate = true; }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(hub, [&] { late = true; }, Qt::QueuedConnection);
    hub->deleteLater();
    hub->reconnect();
    hub->createPMWindow(QString("unavailable"));
    hub->addPM("unavailable", "late message");
    CHECK_FALSE(hub->hasCID(dcpp::CID(), "unavailable"));
    QShowEvent show;
    QCoreApplication::sendEvent(hub, &show);
    CHECK(f.gui->hubManager()->activeHub() == nullptr);
    REQUIRE(QMetaObject::invokeMethod(hub, "follow", Qt::DirectConnection,
                                    Q_ARG(QString, "adc://127.0.0.1:2")));
    CHECK(hub->parseForCmd("/rebuild", hub));
    CHECK_FALSE(hub->close());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    REQUIRE(hub);
    CHECK_FALSE(late);
    CHECK_FALSE(childLate);
    CHECK(f.core->getClientManager()->getClients().empty());
    CHECK(hubFence(*hub));
}

TEST_CASE("Hub preparation drains shell work owned by a separate chat widget", "[qt][hub-quit-fence][hub-shell-fence]") {
    IsolatedMainWindow f;
    f.gui->mainWindow()->newHubFrame("adc://127.0.0.1:1", "");
    QPointer<HubFrame> hub = f.gui->mainWindow()->findChild<HubFrame*>();
    REQUIRE(hub);
    QWidget chat(f.gui->mainWindow());
    auto cleanup = dcpp::makeScopedFunctor([&] { if(hub) hub->close(); });
    const QString ready = f.home.filePath("shell-ready");
    QFile script(f.home.filePath("chat-command"));
    REQUIRE(script.open(QIODevice::WriteOnly));
    script.write(("#!/bin/sh\ntouch '" + ready + "'\nwhile :; do sleep 0.1; done\n").toUtf8());
    script.close();
    REQUIRE(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    REQUIRE(hub->parseForCmd("/sh " + script.fileName(), &chat));
    auto* worker = chat.findChild<ShellCommandRunner*>();
    REQUIRE(worker);
    auto drain = dcpp::makeScopedFunctor([&] { worker->cancel(); worker->wait(); });
    REQUIRE(waitFor([&] { return QFile::exists(ready); }));
    REQUIRE(worker->isRunning());
    REQUIRE(waitFor([&] { return hubFence(*hub); }));
    CHECK_FALSE(worker->isRunning());
    CHECK(worker->wait(0));
}

TEST_CASE("Hub modal refusal does not consume later retained preparation", "[qt][hub-quit-fence]") {
    IsolatedMainWindow f;
    f.gui->mainWindow()->newHubFrame("adc://127.0.0.1:1", "");
    QPointer<HubFrame> hub = f.gui->mainWindow()->findChild<HubFrame*>();
    REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if(hub) hub->close(); });
    QDialog modal(hub);
    modal.setModal(true); modal.show();
    REQUIRE(waitFor([&] { return QApplication::activeModalWidget() == &modal; }));
    CHECK_FALSE(hubFence(*hub));
    CHECK(hub->isEnabled());
    CHECK(f.core->getClientManager()->getClients().size() == 1);
    modal.hide();
    REQUIRE(hubFence(*hub));
    CHECK(f.core->getClientManager()->getClients().empty());
}

TEST_CASE("GUI quit preparation joins retained workers without pumping pending input", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    std::atomic_bool entered{false}, released{false}, finished{false};
    auto* worker = new AsyncRunner(window);
    worker->setRunFunction([&] {
        entered = true;
        while(!released) QThread::msleep(1);
        finished = true;
    });
    auto releaseOnExit = dcpp::makeScopedFunctor([&] { released = true; worker->wait(); });
    worker->start();
    QElapsedTimer start; start.start();
    while(!entered && start.elapsed() < 1000) QThread::msleep(1);
    REQUIRE(entered.load());
    bool lateInput = false, otherInput = false;
    QObject unrelated;
    QMetaObject::invokeMethod(window, [&] { lateInput = true; }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(&unrelated, [&] { otherInput = true; }, Qt::QueuedConnection);
    auto release = std::async(std::launch::async, [&] { QThread::msleep(40); released = true; });
    CHECK_FALSE(std::async(std::launch::async, [&] { return guiFence(*window); }).get());
    CHECK_FALSE(guiFence(*window));
    REQUIRE(waitFor([&] { return guiFence(*window); })); release.get();
    CHECK(finished.load()); CHECK(worker->isFinished()); CHECK_FALSE(window->isEnabled());
    QCoreApplication::processEvents(); CHECK_FALSE(lateInput); CHECK(otherInput);
    CHECK(guiFence(*window));
    CHECK(f.core->getHashManager() != nullptr); // GUI fence retains the core.
}

TEST_CASE("GUI quit preparation saves history intent and caches Torrent persistence refusal", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    f.gui->settings()->setBool("app/clear-search-history-on-exit", true);
    f.gui->settings()->setStr(WS_SEARCH_HISTORY, "old search");
    auto* runtime = f.gui->mainWindow()->findChild<TorrentRuntime*>(); REQUIRE(runtime);
    bool expected = true;
    SECTION("successful worker and GUI checkpoint") {}
    SECTION("failed Torrent checkpoint") {
        REQUIRE(QDir().mkpath(f.home.filePath("torrent/state/jobs.json"))); expected = false;
    }
    auto* window = f.gui->mainWindow();
    bool saved = false;
    REQUIRE(waitForGuiQuitTerminal(*window, saved));
    CHECK(saved == expected);
    CHECK_FALSE(window->quitWorkersPending());
    CHECK(runtime->shutdownChecked() == expected);
    CHECK(f.gui->settings()->getStr(WS_SEARCH_HISTORY).isEmpty());
    if(!expected) REQUIRE(QDir(f.home.filePath("torrent/state/jobs.json")).removeRecursively());
    CHECK(guiFence(*f.gui->mainWindow()) == expected);
    CHECK(runtime->shutdownChecked() == expected);
}

TEST_CASE("Checked GUI quit snapshots retained hidden layouts before syncing", "[qt][mainwindow][gui-layout-receipt]") {
    IsolatedMainWindow f;
    f.gui->createDownloadQueue(); f.gui->createFavoriteHubs(); f.gui->createFavoriteUsers();
    f.gui->createPublicHubs(); f.gui->createADLS(); f.gui->createSpyFrame(); f.gui->createQueuedUsers();
    auto* search = new SearchFrame(f.gui->mainWindow());
    struct View { QWidget* widget; QString key; bool base64; };
    const QList<View> views{
        {f.gui->transferView(), WS_TRANSFERS_STATE, true},
        {f.gui->downloadQueue(), WS_DQUEUE_STATE, false},
        {f.gui->favoriteHubs(), WS_FAV_HUBS_STATE, false},
        {f.gui->favoriteUsers(), WS_FAVUSERS_STATE, false},
        {f.gui->publicHubs(), WS_PUBLICHUBS_STATE, false},
        {f.gui->adls(), WS_ADLS_STATE, false},
        {f.gui->spyFrame(), "spyframe-header-state", false},
        {f.gui->queuedUsers(), "queued-users/headerstate", false},
        {search, WS_SEARCH_STATE, true}
    };
    QHash<QString, QByteArray> expected;
    for(const auto& view : views) {
        REQUIRE(view.widget);
        if(view.widget != f.gui->transferView()) view.widget->setParent(f.gui->mainWindow());
        auto* tree = view.widget->findChild<QTreeView*>(); REQUIRE(tree);
        tree->header()->resizeSection(0, 247);
        expected.insert(view.key, tree->header()->saveState());
    }
    REQUIRE(waitFor([&] { return guiFence(*f.gui->mainWindow()); }));
    // A different filename prevents QSettings' per-path shared cache from
    // substituting pending in-memory values for a physical persistence read.
    const auto snapshot = f.home.filePath("checked-layout-copy.ini");
    REQUIRE(QFile::copy(f.home.filePath("EiskaltDC++_Qt.conf"), snapshot));
    QSettings disk(snapshot, QSettings::IniFormat);
    for(const auto& view : views) {
        INFO(view.key.toStdString());
        const auto value = disk.value(view.key);
        CHECK((view.base64 ? QByteArray::fromBase64(value.toString().toUtf8()) : value.toByteArray()) == expected.value(view.key));
    }
}

TEST_CASE("Checked GUI quit freezes late settings mutations and teardown layouts", "[qt][mainwindow][gui-layout-receipt]") {
    IsolatedMainWindow f;
    auto* settings = f.gui->settings();
    settings->setStr("receipt/string", "before"); settings->setInt("receipt/int", 7);
    settings->setBool("receipt/bool", true); settings->setVar("receipt/variant", QByteArray("before"));
    REQUIRE(waitFor([&] { return guiFence(*f.gui->mainWindow()); }));
    QSignalSpy strings(settings, &WulforSettings::strValueChanged);
    QSignalSpy ints(settings, &WulforSettings::intValueChanged);
    QSignalSpy variants(settings, &WulforSettings::varValueChanged);
    settings->setStr("receipt/string", "after"); settings->setInt("receipt/int", 8);
    settings->setBool("receipt/bool", false); settings->setVar("receipt/variant", QByteArray("after"));
    CHECK(settings->getStr("receipt/string") == "before"); CHECK(settings->getInt("receipt/int") == 7);
    CHECK(settings->getBool("receipt/bool")); CHECK(settings->getVar("receipt/variant").toByteArray() == "before");
    CHECK(strings.isEmpty()); CHECK(ints.isEmpty()); CHECK(variants.isEmpty());
    const auto layout = settings->getStr(WS_TRANSFERS_STATE);
    auto* tree = f.gui->transferView()->findChild<QTreeView*>(); REQUIRE(tree);
    tree->header()->resizeSection(0, 359);
    f.gui->transferView()->close();
    CHECK(settings->getStr(WS_TRANSFERS_STATE) == layout);
    CHECK(settings->saveChecked());
}

TEST_CASE("Torrent layout write refusal blocks the checked GUI quit receipt", "[qt][mainwindow][gui-layout-receipt]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow(); window->showTorrents();
    QPointer<TorrentWindow> tab(window->findChild<TorrentWindow*>()); REQUIRE(tab);
    const auto layout = QDir(QFileInfo(TorrentRuntime::settingsPath()).absolutePath()).filePath("window.ini");
    if(QFile::exists(layout)) REQUIRE(QFile::remove(layout));
    REQUIRE(QDir().mkpath(layout));
    REQUIRE(waitFor([&] { return guiFence(*window) || !window->quitWorkersPending(); }));
    CHECK_FALSE(guiFence(*window));
    REQUIRE(QDir(layout).removeRecursively());
    CHECK_FALSE(guiFence(*window));
    tab->close(); QCoreApplication::processEvents();
    CHECK_FALSE(QFile::exists(layout));
    REQUIRE(tab);
}

TEST_CASE("GUI quit waits for initially parentless hidden history workers", "[qt][mainwindow][gui-layout-review]") {
    IsolatedMainWindow f;
    f.gui->createFinishedDownloads();
    auto* history = f.gui->finishedDownloads(); REQUIRE(history);
    REQUIRE(history->parent() == nullptr);
    REQUIRE((history->state() & ArenaWidget::Hidden) != 0);
    QSemaphore entered, released;
    auto* worker = new AsyncRunner(history);
    worker->setRunFunction([&] { entered.release(); released.acquire(); });
    auto cleanup = dcpp::makeScopedFunctor([&] {
        released.release(); worker->wait(); history->setParent(f.gui->mainWindow());
    });
    worker->start(); REQUIRE(entered.tryAcquire(1, 5000));
    auto* window = f.gui->mainWindow();
    CHECK_FALSE(guiFence(*window));
    CHECK(window->quitWorkersPending());
    CHECK(worker->isInterruptionRequested());
    CHECK_FALSE(history->isEnabled());
    released.release(); REQUIRE(worker->wait(5000));
    REQUIRE(waitFor([&] { return guiFence(*window); }));
}

TEST_CASE("Shared layout keys retain the most recently edited tab at quit", "[qt][mainwindow][gui-layout-review]") {
    IsolatedMainWindow f;
    QWidget* first = nullptr; QWidget* second = nullptr; QString key; bool base64 = false;
    const auto browserPair = [&](bool reverse) {
        const auto path = f.home.filePath("shared-files.xml");
        QFile listing(path); REQUIRE(listing.open(QIODevice::WriteOnly));
        REQUIRE(listing.write("<FileListing Version=\"1\" Base=\"/\"><Directory Name=\"fixture\"/></FileListing>") > 0);
        listing.close();
        auto* older = new ShareBrowser(f.core->getClientManager()->getUser(CID(std::string(39, 'A'))), path, "");
        REQUIRE(waitFor([&] { return older->findChild<QTreeView*>("treeView_LPANE") != nullptr; }));
        auto* newer = new ShareBrowser(f.core->getClientManager()->getUser(CID(std::string(39, 'A'))), path, "");
        REQUIRE(waitFor([&] { return newer->findChild<QTreeView*>("treeView_LPANE") != nullptr; }));
        first = reverse ? newer : older; second = reverse ? older : newer;
        key = WS_SHARE_LPANE_STATE; base64 = true;
    };
    SECTION("first search edited last") {
        first = new SearchFrame(f.gui->mainWindow()); second = new SearchFrame(f.gui->mainWindow());
        key = WS_SEARCH_STATE; base64 = true;
    }
    SECTION("second search edited last") {
        second = new SearchFrame(f.gui->mainWindow()); first = new SearchFrame(f.gui->mainWindow());
        key = WS_SEARCH_STATE; base64 = true;
    }
    SECTION("upload history edited last") {
        f.gui->createFinishedUploads(); f.gui->createFinishedDownloads();
        first = f.gui->finishedUploads(); second = f.gui->finishedDownloads(); key = WS_FTRANSFERS_FILES_STATE;
    }
    SECTION("download history edited last") {
        f.gui->createFinishedUploads(); f.gui->createFinishedDownloads();
        first = f.gui->finishedDownloads(); second = f.gui->finishedUploads(); key = WS_FTRANSFERS_FILES_STATE;
    }
    SECTION("first file browser edited last") { browserPair(false); }
    SECTION("second file browser edited last") { browserPair(true); }
    REQUIRE(first); REQUIRE(second);
    first->setParent(f.gui->mainWindow()); second->setParent(f.gui->mainWindow());
    const auto treeName = key == WS_SHARE_LPANE_STATE ? QStringLiteral("treeView_LPANE") : QString();
    auto* firstTree = first->findChild<QTreeView*>(treeName); auto* secondTree = second->findChild<QTreeView*>(treeName);
    REQUIRE(firstTree); REQUIRE(secondTree);
    firstTree->header()->resizeSection(0, 251);
    secondTree->header()->resizeSection(0, 273);
    firstTree->header()->resizeSection(0, 319);
    const auto expected = firstTree->header()->saveState();
    REQUIRE(waitFor([&] { return guiFence(*f.gui->mainWindow()); }));
    const auto snapshot = f.home.filePath("shared-layout-copy.ini");
    REQUIRE(QFile::copy(f.home.filePath("EiskaltDC++_Qt.conf"), snapshot));
    QSettings disk(snapshot, QSettings::IniFormat);
    CHECK((base64 ? QByteArray::fromBase64(disk.value(key).toString().toUtf8()) : disk.value(key).toByteArray()) == expected);
}

TEST_CASE("History mode switches preserve the latest inactive layout at quit", "[qt][mainwindow][gui-layout-mode]") {
    IsolatedMainWindow f;
    f.gui->createFinishedUploads(); f.gui->createFinishedDownloads();
    QWidget* edited = nullptr; QWidget* other = nullptr; int from = 0;
    SECTION("uploads leave Files") { edited = f.gui->finishedUploads(); other = f.gui->finishedDownloads(); }
    SECTION("downloads leave Files") { edited = f.gui->finishedDownloads(); other = f.gui->finishedUploads(); }
    SECTION("uploads leave Users") { edited = f.gui->finishedUploads(); other = f.gui->finishedDownloads(); from = 1; }
    SECTION("downloads leave Users") { edited = f.gui->finishedDownloads(); other = f.gui->finishedUploads(); from = 1; }
    REQUIRE(edited); REQUIRE(other);
    edited->setParent(f.gui->mainWindow()); other->setParent(f.gui->mainWindow());
    const auto changeMode = [&](QWidget* view, int index) {
        auto* combo = view->findChild<QComboBox*>("comboBox"); REQUIRE(combo);
        combo->setCurrentIndex(index);
        REQUIRE(QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, index)));
    };
    if(from != 0) { changeMode(edited, from); changeMode(other, from); }
    auto* latest = edited->findChild<QTreeView*>(); auto* stale = other->findChild<QTreeView*>();
    REQUIRE(latest); REQUIRE(stale);
    stale->header()->resizeSection(0, 251); latest->header()->resizeSection(0, 319);
    const auto expected = latest->header()->saveState();
    const QString key = from == 0 ? WS_FTRANSFERS_FILES_STATE : WS_FTRANSFERS_USERS_STATE;
    changeMode(edited, 1 - from);
    REQUIRE(f.gui->settings()->getVar(key).toByteArray() == expected);
    REQUIRE(waitFor([&] { return guiFence(*f.gui->mainWindow()); }));
    const auto snapshot = f.home.filePath("inactive-history-copy.ini");
    REQUIRE(QFile::copy(f.home.filePath("EiskaltDC++_Qt.conf"), snapshot));
    QSettings disk(snapshot, QSettings::IniFormat);
    CHECK(disk.value(key).toByteArray() == expected);
}

TEST_CASE("History mode restore keeps actual model sorting aligned with its header", "[qt][mainwindow][gui-layout-sort]") {
    IsolatedMainWindow f;
    QStandardItemModel columns(0, 7);
    QHeaderView saved(Qt::Horizontal); saved.setModel(&columns);
    saved.setSortIndicator(4, Qt::AscendingOrder);
    f.gui->settings()->setVar(WS_FTRANSFERS_USERS_STATE, saved.saveState());
    f.gui->createFinishedDownloads();
    auto* history = f.gui->finishedDownloads(); REQUIRE(history);
    history->setParent(f.gui->mainWindow());
    // Finish the database producer before installing synthetic sorting rows.
    for (auto* runner : history->findChildren<AsyncRunner*>())
        REQUIRE(runner->wait(5000));
    auto* tree = history->findChild<QTreeView*>(); REQUIRE(tree);
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(tree->model()); REQUIRE(proxy);
    auto* source = qobject_cast<FinishedTransfersModel*>(proxy->sourceModel()); REQUIRE(source);
    source->clearModel();
    for (int i = 0; i < 3; ++i) {
        const QString name = QStringLiteral("row-%1").arg(i);
        source->addFile({{"TARGET", name}, {"FNAME", name}, {"TIME", QString::number(3 - i)}, {"FULL", true}});
        const quint64 speeds[] = {2, 3, 1};
        source->addUser({{"CID", name}, {"NICK", name}, {"TIME", QString::number(3 - i)},
                         {"SPEED", QVariant::fromValue(speeds[i])}, {"FULL", true}});
    }
    const auto visibleNames = [&] {
        QStringList names;
        for (int row = 0; row < proxy->rowCount(); ++row)
            names.append(proxy->index(row, 0).data().toString());
        return names;
    };
    auto* combo = history->findChild<QComboBox*>("comboBox"); REQUIRE(combo);
    tree->sortByColumn(2, Qt::DescendingOrder);
    REQUIRE(visibleNames() == QStringList{"row-0", "row-1", "row-2"});
    combo->setCurrentIndex(1);
    REQUIRE(QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, 1)));
    REQUIRE(tree->header()->sortIndicatorSection() == 4);
    REQUIRE(tree->header()->sortIndicatorOrder() == Qt::AscendingOrder);
    CHECK(visibleNames() == QStringList{"row-2", "row-0", "row-1"});
    combo->setCurrentIndex(0);
    REQUIRE(QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, 0)));
    REQUIRE(tree->header()->sortIndicatorSection() == 2);
    REQUIRE(tree->header()->sortIndicatorOrder() == Qt::DescendingOrder);
    CHECK(visibleNames() == QStringList{"row-0", "row-1", "row-2"});
    REQUIRE(waitFor([&] { return guiFence(*f.gui->mainWindow()); }));
}

TEST_CASE("GUI quit safely retains a file browser before queued UI initialization", "[qt][mainwindow][gui-layout-review]") {
    QTemporaryDir output; REQUIRE(output.isValid());
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"[gui-layout-uninitialized-child]", "--reporter", "xml",
                                                         "--out", output.filePath("child.xml")});
    REQUIRE(child.waitForStarted());
    const bool finished = child.waitForFinished(30000);
    if(!finished) { child.kill(); child.waitForFinished(); }
    const auto stdoutBytes = child.readAllStandardOutput();
    const auto stderrBytes = child.readAllStandardError();
    INFO(stdoutBytes.constData()); INFO(stderrBytes.constData());
    REQUIRE(finished);
    REQUIRE(stderrBytes.contains("uninitialized-browser-before-fence"));
    CHECK(child.exitStatus() == QProcess::NormalExit);
    CHECK(child.exitCode() == 0);
}

TEST_CASE("Uninitialized file browser quit child", "[.][gui-layout-uninitialized-child]") {
    IsolatedMainWindow f;
    const auto path = f.home.filePath("files.xml");
    QFile listing(path); REQUIRE(listing.open(QIODevice::WriteOnly));
    REQUIRE(listing.write("<FileListing Version=\"1\" Base=\"/\"><Directory Name=\"fixture\"/></FileListing>") > 0);
    listing.close();
    auto* browser = new ShareBrowser(UserPtr(new User(CID::generate())), path, "");
    const auto runners = browser->findChildren<AsyncRunner*>(); REQUIRE(runners.size() == 1);
    REQUIRE(runners.front()->wait(5000));
    REQUIRE(browser->findChildren<QTreeView*>().isEmpty());
    std::fputs("uninitialized-browser-before-fence\n", stderr); std::fflush(stderr);
    bool saved = false;
    REQUIRE(waitForGuiQuitTerminal(*f.gui->mainWindow(), saved));
    REQUIRE(saved);
    CHECK(browser->findChildren<QTreeView*>().isEmpty());
    delete browser;
}

TEST_CASE("File hasher cooperates with interruption before touching its source", "[qt][mainwindow][gui-hash-fence]") {
    QTemporaryDir root; REQUIRE(root.isValid());
    const auto source = root.filePath("payload");
    QFile file(source); REQUIRE(file.open(QIODevice::WriteOnly)); REQUIRE(file.write("known bytes") == 11); file.close();
    HashThread thread; thread.setFile(source);
    QObject::connect(&thread, &QThread::started, &thread, [&] { thread.requestInterruption(); }, Qt::DirectConnection);
    thread.start(); REQUIRE(thread.wait(2000));
    CHECK(thread.getHash().isEmpty());
}

TEST_CASE("File-list runner skips work cancelled before its callback", "[qt][gui-worker-cancel-runner]") {
    AsyncRunner worker;
    bool ran = false;
    worker.setRunFunction([&] { ran = true; });
    QObject::connect(&worker, &QThread::started, &worker, [&] { worker.requestInterruption(); }, Qt::DirectConnection);
    worker.start(); REQUIRE(worker.wait(2000));
    CHECK_FALSE(ran);
}

TEST_CASE("GUI settings sync refusal remains sticky while retaining the core", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    f.gui->mainWindow()->newHubFrame("adc://127.0.0.1:1", "");
    QPointer<HubFrame> hub = f.gui->mainWindow()->findChild<HubFrame*>();
    REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if(hub) hub->close(); });
    REQUIRE(f.gui->settings()->saveChecked());
    const auto config = f.home.filePath("EiskaltDC++_Qt.conf");
    REQUIRE(QFile::remove(config)); REQUIRE(QDir().mkpath(config));
    CHECK_FALSE(guiFence(*f.gui->mainWindow()));
    CHECK_FALSE(f.gui->mainWindow()->isEnabled());
    CHECK(f.core->getHashManager() != nullptr);
    CHECK(f.core->getClientManager()->getClients().empty());
    REQUIRE(hub);
    CHECK_FALSE(hub->isEnabled());
    CHECK_FALSE(f.gui->settings()->getStr(view_layout::hubStateKey("adc://127.0.0.1:1")).isEmpty());
    bool saved = true;
    REQUIRE(waitForGuiQuitTerminal(*f.gui->mainWindow(), saved));
    CHECK_FALSE(saved);
    REQUIRE(QDir(config).removeRecursively());
    CHECK_FALSE(guiFence(*f.gui->mainWindow()));
    auto* runtime = f.gui->mainWindow()->findChild<TorrentRuntime*>(); REQUIRE(runtime);
    CHECK(runtime->shutdownChecked());
}

TEST_CASE("GUI quit preparation rejects nested modal input before consuming its owner attempt", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    QDialog dialog(f.gui->mainWindow()); dialog.setWindowModality(Qt::ApplicationModal); dialog.show();
    QCoreApplication::processEvents(); REQUIRE(QApplication::activeModalWidget() == &dialog);
    CHECK_FALSE(guiFence(*f.gui->mainWindow())); CHECK(f.gui->mainWindow()->isEnabled());
    dialog.close(); QCoreApplication::processEvents();
    bool saved = false;
    REQUIRE(waitForGuiQuitTerminal(*f.gui->mainWindow(), saved));
    CHECK(saved);
}

TEST_CASE("GUI preparation drains a future and fences direct file hashing and IPC", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    FileHasher dialog(window);
    REQUIRE(dialog.findChild<HashThread*>());
    QFile file(f.home.filePath("payload")); REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write("known bytes") == 11); file.close();
    auto* input = dialog.findChild<QLineEdit*>("lineEdit_FILE"); REQUIRE(input);
    input->setText(file.fileName());
    std::atomic_bool entered{false}, released{false};
    auto* watcher = new QFutureWatcher<void>(window);
    auto releaseOnExit = dcpp::makeScopedFunctor([&] { released = true; watcher->waitForFinished(); });
    bool delivered = false;
    QObject::connect(watcher, &QFutureWatcher<void>::finished, window, [&] { delivered = true; });
    watcher->setFuture(QtConcurrent::run([&] { entered = true; while(!released) QThread::msleep(1); }));
    QElapsedTimer start; start.start();
    while(!entered && start.elapsed() < 1000) QThread::msleep(1);
    REQUIRE(entered.load());
    auto release = std::async(std::launch::async, [&] { QThread::msleep(40); released = true; });
    CHECK_FALSE(guiFence(*window));
    REQUIRE(waitFor([&] { return guiFence(*window); })); release.get(); CHECK(watcher->isFinished());
    REQUIRE(QMetaObject::invokeMethod(&dialog, "slotStart", Qt::DirectConnection));
    CHECK_FALSE(dialog.findChild<HashThread*>()->isRunning());
    window->parseInstanceLine("eiskaltdcpp-qt adc://127.0.0.1:1");
    window->newHubFrame("adc://127.0.0.1:1", "");
    CHECK(window->findChildren<HubFrame*>().isEmpty());
    CHECK(window->widgetForRole(ArenaWidget::Torrent) == nullptr);
    QCoreApplication::processEvents(); CHECK_FALSE(delivered);
}

TEST_CASE("Cancelled shell worker does not launch a child after cancellation", "[qt][mainwindow][gui-shell-fence]") {
    QTemporaryDir root; REQUIRE(root.isValid());
    const auto output = root.filePath("unexpected-child");
    ShellCommandRunner worker("/bin/sh", {"-c", "touch \"" + output + "\""});
    QObject::connect(&worker, &QThread::started, &worker, [&] { worker.cancel(); }, Qt::DirectConnection);
    worker.start(); REQUIRE(worker.wait(2000));
    CHECK_FALSE(QFile::exists(output));
}

TEST_CASE("GUI preparation cancels and joins its own running shell command", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    const auto ready = f.home.filePath("child-ready");
    auto* worker = new ShellCommandRunner("/bin/sh",
        {"-c", "printf ready > \"" + ready + "\"; exec sleep 10"}, f.gui->mainWindow());
    auto cancelOnExit = dcpp::makeScopedFunctor([&] { worker->cancel(); worker->wait(); });
    worker->start();
    QElapsedTimer start; start.start();
    while(!QFile::exists(ready) && start.elapsed() < 2000) QThread::msleep(1);
    REQUIRE(QFile::exists(ready)); REQUIRE(worker->isRunning());
    start.restart(); REQUIRE(waitFor([&] { return guiFence(*f.gui->mainWindow()); }));
    CHECK(worker->isFinished()); CHECK(start.elapsed() < 3000);
}

TEST_CASE("GUI preparation includes the separately owned notification service", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    auto* notifications = f.gui->notification(); REQUIRE(notifications);
    std::atomic_bool entered{false}, released{false}, finished{false};
    auto* worker = new AsyncRunner(notifications);
    worker->setRunFunction([&] { entered = true; while(!released) QThread::msleep(1); finished = true; });
    auto releaseOnExit = dcpp::makeScopedFunctor([&] { released = true; worker->wait(); });
    worker->start();
    QElapsedTimer start; start.start();
    while(!entered && start.elapsed() < 1000) QThread::msleep(1);
    REQUIRE(entered.load());
    bool delivered = false;
    QMetaObject::invokeMethod(notifications, [&] { delivered = true; }, Qt::QueuedConnection);
    auto release = std::async(std::launch::async, [&] { QThread::msleep(80); released = true; });
    CHECK_FALSE(guiFence(*f.gui->mainWindow()));
    REQUIRE(waitFor([&] { return guiFence(*f.gui->mainWindow()); }));
    CHECK(finished.load()); CHECK(worker->isFinished()); release.get();
    QCoreApplication::processEvents(); CHECK_FALSE(delivered);
}

TEST_CASE("Prepared GUI retains deferred owners and rejects the legacy window-close path", "[qt][mainwindow][gui-quit-fence]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    QPointer<QObject> retained = new QObject(window);
    retained->deleteLater();
    bool saved = false;
    REQUIRE(waitForGuiQuitTerminal(*window, saved));
    REQUIRE(saved);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(retained);
    window->close();
    CHECK(window->isVisible());
    CHECK(f.gui->notification() != nullptr);
    CHECK(guiFence(*window));
}

template<class T> T *child(QObject *parent, const char *name)
{
    auto *result = parent->findChild<T *>(QString::fromLatin1(name));
    REQUIRE(result);
    return result;
}
}

TEST_CASE("Public hubs keeps its source selector in a compact footer", "[qt][mainwindow][public-hubs-footer]")
{
    IsolatedMainWindow fixture;
    // Deliberately invalid scheme: FavoriteManager rejects it before networking.
    fixture.core->getSettingsManager()->set(dcpp::SettingsManager::HUBLIST_SERVERS,
                                           "qa://offline-only");
    PublicHubs hubs(*fixture.core);
    hubs.resize(900, 550);
    hubs.show();
    QTest::qWait(30);
    auto *table = child<QTreeView>(&hubs, "treeView");
    auto *sources = child<QComboBox>(&hubs, "comboBox_HUBS");
    auto *refresh = child<QPushButton>(&hubs, "pushButton_REFRESH");
    auto *status = child<QLabel>(&hubs, "label_STATUS");
    const auto message = QString("Download failed: test error (%1)").arg(QString(180, 'x'));
    REQUIRE(QMetaObject::invokeMethod(&hubs, "setStatus", Q_ARG(QString, message)));
    QTest::qWait(50);
    CHECK(table->geometry().top() < sources->mapTo(&hubs, QPoint()).y());
    CHECK(table->geometry().bottom() < sources->mapTo(&hubs, QPoint()).y());
    CHECK(status->mapTo(&hubs, QPoint()).y() >= table->geometry().bottom());
    CHECK(status->toolTip() == message);
    CHECK(status->text() != message);
    for (int width : {900, 500, 360}) {
        hubs.resize(width, 550);
        QTest::qWait(30);
        INFO("Requested width " << width << ", actual " << hubs.width());
        CHECK(hubs.width() == width);
        CHECK(sources->isVisible());
        CHECK(refresh->isVisible());
        CHECK(sources->width() >= 100);
        CHECK(refresh->mapTo(&hubs, refresh->rect().bottomRight()).x() < width);
        CHECK(status->mapTo(&hubs, status->rect().bottomRight()).x()
              < sources->mapTo(&hubs, QPoint()).x());
    }
    CHECK_FALSE(fixture.core->getFavoriteManager()->isDownloading());
}

TEST_CASE("Public hubs keeps an available filter and live visible hub count", "[qt][mainwindow][public-hubs]")
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    struct RestoreScheme {
        bool requested = false;
        ~RestoreScheme() { if (requested) qApp->styleHints()->unsetColorScheme(); }
    } appearance;
    const auto scheme = qEnvironmentVariable("EISKALT_QA_PUBLIC_HUBS_SCHEME");
    if (!scheme.isEmpty()) {
        REQUIRE((scheme == "light" || scheme == "dark"));
        const auto desired = scheme == "dark" ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light;
        appearance.requested = true;
        qApp->styleHints()->setColorScheme(desired);
        QTest::qWait(50);
        REQUIRE(qApp->styleHints()->colorScheme() == desired);
    }
#endif
    IsolatedMainWindow fixture;
    fixture.core->getSettingsManager()->set(dcpp::SettingsManager::HUBLIST_SERVERS,
                                           "qa://offline-only");
    PublicHubs hubs(*fixture.core);
    hubs.resize(900, 550);
    hubs.show();
    QTest::qWait(30);
    auto *frame = child<QFrame>(&hubs, "frame");
    CHECK(frame->isVisible());
    auto *count = child<QLabel>(&hubs, "label_HUBCOUNT");
    auto *filter = child<QLineEdit>(&hubs, "lineEdit_FILTER");
    auto *column = child<QComboBox>(&hubs, "comboBox_FILTER");
    auto *table = child<QTreeView>(&hubs, "treeView");
    auto *proxy = qobject_cast<PublicHubProxyModel *>(table->model());
    REQUIRE(proxy);
    auto *source = qobject_cast<PublicHubModel *>(proxy->sourceModel());
    REQUIRE(source);
    CHECK(count->text() == "Hubs: 0 / 0");

    const auto add = [source](const QString &name, const QString &description,
                             const QString &country = QStringLiteral("Ukraine")) {
        source->addResult({name, description, 12, "dchub://offline.invalid", country,
                           qlonglong(1024), 0, 0, 0, 0, 0.0, ""}, nullptr);
    };
    add("Alpha", "Music");
    add("Beta", "Films");
    add("Gamma", "Music and films");
    CHECK(count->text() == "Hubs: 3 / 3");
    filter->setText("mUsIc");
    CHECK(table->model()->rowCount() == 2);
    CHECK(count->text() == "Hubs: 2 / 3");
    column->setCurrentIndex(COLUMN_PHUB_NAME);
    CHECK(table->model()->rowCount() == 0);
    CHECK(count->text() == "Hubs: 0 / 3");
    filter->setText("ALPHA");
    CHECK(count->text() == "Hubs: 1 / 3");
    CHECK(table->model()->index(0, COLUMN_PHUB_NAME).data().toString() == "Alpha");

    table->header()->resizeSection(COLUMN_PHUB_DESC, 233);
    table->sortByColumn(COLUMN_PHUB_NAME, Qt::DescendingOrder);
    hubs.requestFilter();
    hubs.requestFilter();
    CHECK(frame->isVisible());
    CHECK(filter->hasFocus());
    CHECK(filter->selectedText() == "ALPHA");
    CHECK(table->header()->sectionSize(COLUMN_PHUB_DESC) == 233);
    CHECK(table->header()->sortIndicatorOrder() == Qt::DescendingOrder);
    QTest::keyClick(filter, Qt::Key_Escape);
    CHECK(frame->isVisible());
    CHECK(filter->text().isEmpty());
    CHECK(count->text() == "Hubs: 3 / 3");
    filter->setText("Beta");
    QTest::mouseClick(child<QToolButton>(&hubs, "toolButton_CLOSEFILTER"), Qt::LeftButton);
    CHECK(frame->isVisible());
    CHECK(filter->text().isEmpty());
    CHECK(count->text() == "Hubs: 3 / 3");

    filter->setText("ALPHA");
    source->clearModel();
    CHECK(count->text() == "Hubs: 0 / 0");
    add("Alpha refreshed", "Music");
    add("Other", "Music");
    CHECK(filter->text() == "ALPHA");
    CHECK(count->text() == "Hubs: 1 / 2");
    source->clearModel();
    source->clearModel();
    CHECK(count->text() == "Hubs: 0 / 0");
    for (int width : {900, 500, 360}) {
        hubs.resize(width, 550);
        QTest::qWait(30);
        INFO("Requested width " << width << ", actual " << hubs.width());
        CHECK(hubs.width() == width);
        CHECK(filter->isVisible());
        CHECK(count->isVisible());
        CHECK(count->mapTo(&hubs, count->rect().bottomRight()).x() < width);
        CHECK(filter->width() >= 40);
    }
    const auto capture = qEnvironmentVariable("EISKALT_QA_PUBLIC_HUBS_IMAGE");
    if (!capture.isEmpty()) {
        filter->clear();
        add("Antarctic hub", "Music", "Antarctica");
        add("Bosnian hub", "Films", "Bosnia and Herzegovina");
        hubs.resize(900, 550);
        QTest::qWait(30);
        REQUIRE(hubs.grab().save(capture));
    }
    CHECK_FALSE(fixture.core->getFavoriteManager()->isDownloading());
}

TEST_CASE("Empty public hub refresh emits no invalid removal range", "[qt][mainwindow][public-hubs]")
{
    PublicHubModel model;
    QSignalSpy removals(&model, &QAbstractItemModel::rowsAboutToBeRemoved);
    model.clearModel();
    CHECK(removals.isEmpty());
    model.addResult({"Hub", "", 1, "dchub://offline.invalid", "Ukraine",
                     qlonglong(0), 0, 0, 0, 0, 0.0, ""}, nullptr);
    model.clearModel();
    REQUIRE(removals.count() == 1);
    CHECK(removals.at(0).at(1).toInt() == 0);
    CHECK(removals.at(0).at(2).toInt() == 0);
    model.clearModel();
    CHECK(removals.count() == 1);
}

TEST_CASE("Public hub captions load and refresh from compiled catalogs", "[.public-hubs-i18n][qt]")
{
    const QString directory = qEnvironmentVariable("EISKALT_QA_PUBLIC_HUBS_QMS");
    REQUIRE_FALSE(directory.isEmpty());
    IsolatedMainWindow fixture;
    fixture.core->getSettingsManager()->set(dcpp::SettingsManager::HUBLIST_SERVERS,
                                           "qa://offline-only");
    PublicHubs hubs(*fixture.core);
    hubs.resize(600, 550);
    hubs.show();
    auto *count = child<QLabel>(&hubs, "label_HUBCOUNT");
    auto *filter = child<QLineEdit>(&hubs, "lineEdit_FILTER");
    auto *clear = child<QToolButton>(&hubs, "toolButton_CLOSEFILTER");
    const QStringList catalogs = QDir(directory).entryList({"*.qm"}, QDir::Files);
    REQUIRE(catalogs.size() == 25);
    for (const auto &catalog : catalogs) {
        INFO(catalog.toStdString());
        QTranslator translator;
        REQUIRE(translator.load(directory + '/' + catalog));
        REQUIRE(QCoreApplication::installTranslator(&translator));
        REQUIRE(QMetaObject::invokeMethod(&hubs, "slotSettingsChanged",
            Q_ARG(QString, WS_TRANSLATION_FILE), Q_ARG(QString, catalog)));
        const auto translatedCount = translator.translate("PublicHubs", "Hubs: %1 / %2");
        REQUIRE_FALSE(translatedCount.isEmpty());
        CHECK(count->text() == translatedCount.arg(0).arg(0));
        CHECK(count->accessibleName() == count->text());
        const auto translatedFilter = translator.translate("UIPublicHubs", "Filter hubs");
        REQUIRE_FALSE(translatedFilter.isEmpty());
        CHECK(filter->placeholderText() == translatedFilter);
        CHECK(clear->toolTip() == translator.translate("UIPublicHubs", "Clear filter"));
        QCoreApplication::removeTranslator(&translator);
    }
}

TEST_CASE("Country labels display Antarctica and Bosnia flags", "[qt][mainwindow][public-hubs][country-flags]")
{
    CHECK(WulforUtil::flaggedCountryLabel("Antarctica")
          == QStringLiteral("\U0001F1E6\U0001F1F6 Antarctica"));
    CHECK(WulforUtil::flaggedCountryLabel("  ANTARCTICA  ")
          == QStringLiteral("\U0001F1E6\U0001F1F6 ANTARCTICA"));
    CHECK(WulforUtil::flaggedCountryLabel("Bosnia and Herzegovina")
          == QStringLiteral("\U0001F1E7\U0001F1E6 Bosnia and Herzegovina"));
    CHECK(WulforUtil::flaggedCountryLabel("Bosnia & Herzegovina")
          == QStringLiteral("\U0001F1E7\U0001F1E6 Bosnia & Herzegovina"));
    CHECK(WulforUtil::flaggedCountryLabel("Antarctica", "AQ")
          == QStringLiteral("\U0001F1E6\U0001F1F6 Antarctica"));
    CHECK(WulforUtil::flaggedCountryLabel("Unknown country") == "Unknown country");
    PublicHubModel model;
    model.addResult({"Antarctic hub", "", 1, "dchub://offline.invalid", "Antarctica",
                     qlonglong(0), 0, 0, 0, 0, 0.0, ""}, nullptr);
    CHECK(model.index(0, COLUMN_PHUB_COUNTRY).data().toString()
          == QStringLiteral("\U0001F1E6\U0001F1F6 Antarctica"));
}

TEST_CASE("Remote file rows retain scalable extension icons", "[qt][mainwindow][file-extension-icons]")
{
    IsolatedMainWindow fixture;
    FileBrowserModel model;
    auto *root = model.getRootElem();
    root->appendChild(new FileBrowserItem({QStringLiteral("movie.mp4")}, root));
    root->appendChild(new FileBrowserItem({QStringLiteral("document.pdf")}, root));
    for (int row = 0; row < 2; ++row) {
        const auto decoration = model.data(model.index(row, COLUMN_FILEBROWSER_NAME), Qt::DecorationRole);
        REQUIRE(decoration.userType() == QMetaType::QIcon);
        const auto icon = qvariant_cast<QIcon>(decoration);
        REQUIRE_FALSE(icon.isNull());
        CHECK_FALSE(icon.pixmap(QSize(32, 32)).isNull());
    }
}

TEST_CASE("Opened file list preserves supplied metadata and leaves unknown cells blank", "[qt][mainwindow][share-browser-metadata]")
{
    IsolatedMainWindow fixture;
    QStandardItemModel oldColumns(0, COLUMN_FILEBROWSER_MODIFIED);
    QHeaderView oldHeader(Qt::Horizontal);
    oldHeader.setModel(&oldColumns);
    oldHeader.resizeSection(COLUMN_FILEBROWSER_NAME, 310);
    oldHeader.resizeSection(COLUMN_FILEBROWSER_TS, 145);
    oldHeader.hideSection(COLUMN_FILEBROWSER_ESIZE);
    fixture.gui->settings()->setStr(WS_SHARE_RPANE_STATE, oldHeader.saveState().toBase64());
    const QString listPath = fixture.home.filePath("OfflineQA.xml");
    QFile listFile(listPath);
    REQUIRE(listFile.open(QIODevice::WriteOnly));
    const QByteArray xml = "<FileListing Version=\"1\" Base=\"/\"><Directory Name=\"Media\">"
        "<Directory Name=\"Subfolder\" Date=\"1690000000\"/>"
        "<File Name=\"Movie.mp4\" Size=\"2400000000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"/>"
        "<File Name=\"Document.pdf\" Size=\"42000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"/>"
        "<File Name=\"Song.mp3\" Size=\"7000000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" BR=\"320\" MA=\"MP3\" HIT=\"0\"/>"
        "<File Name=\"Known-video.mkv\" Size=\"1500000000\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" BR=\"8500\" WH=\"1920x1080\" MV=\"H.264\" MA=\"AAC\" TS=\"1700000000\" Date=\"1690000000\" HIT=\"4\"/>"
        "</Directory></FileListing>";
    REQUIRE(listFile.write(xml) == xml.size());
    listFile.close();
    const auto user = fixture.core->getClientManager()->getUser(dcpp::CID(std::string(39, 'A')));
    auto *browser = new ShareBrowser(user, listPath, "Media");
    fixture.gui->arenaWidgetManager()->add(browser);
    QTreeView *files = nullptr;
    REQUIRE(waitFor([&] {
        files = browser->findChild<QTreeView *>("treeView_RPANE");
        return files && files->model() && files->model()->rowCount() == 5;
    }));
    CHECK(files->model()->headerData(COLUMN_FILEBROWSER_MODIFIED, Qt::Horizontal).toString() == "Modified");
    CHECK(files->header()->sectionSize(COLUMN_FILEBROWSER_NAME) == 310);
    CHECK(files->header()->sectionSize(COLUMN_FILEBROWSER_TS) == 145);
    CHECK(files->header()->isSectionHidden(COLUMN_FILEBROWSER_ESIZE));
    CHECK_FALSE(files->header()->isSectionHidden(COLUMN_FILEBROWSER_MODIFIED));
    for (int row = 0; row < files->model()->rowCount(); ++row) {
        auto cell = [&](int column) { return files->model()->index(row, column).data(); };
        const auto name = cell(COLUMN_FILEBROWSER_NAME).toString();
        INFO(name.toStdString());
        if (name == "Subfolder") {
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_MODIFIED).toString().isEmpty());
            CHECK(cell(COLUMN_FILEBROWSER_TS).toString().isEmpty());
        } else if (name == "Movie.mp4" || name == "Document.pdf") {
            for (int column = COLUMN_FILEBROWSER_BR; column <= COLUMN_FILEBROWSER_MODIFIED; ++column)
                CHECK(cell(column).toString().isEmpty());
        } else if (name == "Song.mp3") {
            CHECK(cell(COLUMN_FILEBROWSER_BR).toInt() == 320);
            CHECK(cell(COLUMN_FILEBROWSER_MAUDIO).toString() == "MP3");
            CHECK(cell(COLUMN_FILEBROWSER_HIT).toString() == "0");
            CHECK(cell(COLUMN_FILEBROWSER_TS).toString().isEmpty());
        } else {
            CHECK(cell(COLUMN_FILEBROWSER_WH).toString() == "1920x1080");
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_TS).toString().startsWith("1969"));
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_TS).toString().isEmpty());
            CHECK_FALSE(cell(COLUMN_FILEBROWSER_MODIFIED).toString().isEmpty());
            CHECK(cell(COLUMN_FILEBROWSER_MODIFIED) != cell(COLUMN_FILEBROWSER_TS));
        }
        const auto decoration = files->model()->index(row, 0).data(Qt::DecorationRole);
        REQUIRE(decoration.userType() == QMetaType::QIcon);
        REQUIRE_FALSE(qvariant_cast<QIcon>(decoration).isNull());
    }
    const QString preview = qEnvironmentVariable("EISKALT_QA_SHARE_PREVIEW");
    if (!preview.isEmpty()) {
        fixture.gui->mainWindow()->resize(1450, 720);
        files->header()->resizeSections(QHeaderView::ResizeToContents);
        QTest::qWait(100);
        REQUIRE(fixture.gui->mainWindow()->grab().save(preview));
        files->horizontalScrollBar()->setValue(files->horizontalScrollBar()->maximum());
        QTest::qWait(100);
        REQUIRE(fixture.gui->mainWindow()->grab().save(preview + ".dates.png"));
    }
    fixture.gui->arenaWidgetManager()->rem(browser);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("Close current tab resolves separate arena owners", "[qt][mainwindow][tab-close-owner]")
{
    IsolatedMainWindow fixture;
    auto *manager = fixture.gui->arenaWidgetManager();
    QPointer<ScriptWidget> tab = new ScriptWidget;
    tab->setWidget(new QLabel("Offline separate-owner tab"));
    tab->setArenaShortTitle("Offline QA");
    tab->setArenaTitle("Offline QA");
    manager->add(tab);
    manager->activate(tab);
    REQUIRE(QMetaObject::invokeMethod(fixture.gui->mainWindow(), "slotCloseCurrentWidget"));
    CHECK(tab.isNull());
    if (tab)
        manager->rem(tab);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("DHT controls use inline checkboxes and preserve the advertisement preference", "[qt][dht-advertisement][ui]")
{
#ifdef WITH_DHT
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    sm.set(dcpp::SettingsManager::USE_DHT, true);
    SettingsConnection page;
    page.resize(950, 720);
    page.show();
    auto* tabs = page.findChild<QTabWidget*>();
    REQUIRE(tabs);
    tabs->setCurrentIndex(tabs->count() - 1);
    QTest::qWait(30);
    auto* group = child<QGroupBox>(&page, "groupBox_DHT");
    REQUIRE_FALSE(group->isCheckable());
    auto* enable = child<QCheckBox>(&page, "checkBox_DHT");
    auto* hide = child<QCheckBox>(&page, "checkBox_HIDE_DHT");
    auto* port = child<QSpinBox>(&page, "spinBox_DHT");
    REQUIRE(enable); REQUIRE(hide); REQUIRE(port);
    CHECK(enable->isChecked());
    CHECK_FALSE(hide->isChecked());
    CHECK(enable->text() == QStringLiteral("Enable DHT"));
    CHECK(hide->toolTip().contains("reconnect", Qt::CaseInsensitive));
    CHECK(enable->geometry().bottom() < hide->geometry().top());
    CHECK(hide->geometry().bottom() < port->geometry().top());
    CHECK(enable->geometry().left() == hide->geometry().left());
    hide->click();
    enable->click();
    CHECK_FALSE(port->isEnabled());
    CHECK(hide->isChecked());
    enable->click();
    CHECK(port->isEnabled());
    page.ok();
    CHECK(sm.getBool(dcpp::SettingsManager::USE_DHT));
    const auto saved = fixture.home.filePath("dht-setting-roundtrip.xml");
    sm.save(saved.toStdString());
    hide->click();
    page.ok();
    sm.load(saved.toStdString());
    SettingsConnection restored;
    CHECK(child<QCheckBox>(&restored, "checkBox_DHT")->isChecked());
    CHECK(child<QCheckBox>(&restored, "checkBox_HIDE_DHT")->isChecked());
    const auto preview = qEnvironmentVariable("EISKALT_DHT_PREVIEW");
    if (!preview.isEmpty()) REQUIRE(group->grab().save(preview));
#endif
}

TEST_CASE("ADC and NMDC hub handshakes honor DHT advertisement without disabling DHT", "[qt][dht-advertisement][network]")
{
#ifdef WITH_DHT
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    for (const bool adc : {true, false}) {
        for (const bool enabled : {false, true}) {
            for (const bool hidden : {false, true}) {
                INFO("ADC=" << adc << " enabled=" << enabled << " hidden=" << hidden);
                const auto path = fixture.home.filePath("dht-advertisement.xml");
                QFile config(path);
                REQUIRE(config.open(QIODevice::WriteOnly));
                config.write("<DCPlusPlus><Settings><HideDHTFromHubs type=\"int\">");
                config.write(hidden ? "1" : "0");
                config.write("</HideDHTFromHubs></Settings></DCPlusPlus>");
                config.close();
                sm.load(path.toStdString());
                sm.set(dcpp::SettingsManager::USE_DHT, enabled);
                QTcpServer hub;
                REQUIRE(hub.listen(QHostAddress::LocalHost));
                auto* manager = fixture.core->getClientManager();
                auto cleanup = [manager](dcpp::Client* client) { manager->putClient(client); };
                std::unique_ptr<dcpp::Client, decltype(cleanup)> client(
                    manager->getClient(std::string(adc ? "adc://127.0.0.1:" : "dchub://127.0.0.1:")
                                       + std::to_string(hub.serverPort())), cleanup);
                client->connect();
                REQUIRE(waitFor([&] { return hub.hasPendingConnections(); }));
                std::unique_ptr<QTcpSocket> peer(hub.nextPendingConnection());
                REQUIRE(peer);
                if (!adc) peer->write("$Lock EXTENDEDPROTOCOLABCABCABCABCABCABC Pk=Fixture|");
                QByteArray received;
                REQUIRE(waitFor([&] {
                    received += peer->readAll();
                    return adc ? received.contains('\n') : received.contains("$ValidateNick");
                }));
                CHECK(received.contains(adc ? "ADDHT0" : "DHT0") == (enabled && !hidden));
                CHECK(received.contains(adc ? "ADBAS0" : "TTHSearch"));
                CHECK(sm.getBool(dcpp::SettingsManager::USE_DHT) == enabled);
            }
        }
    }
#endif
}

TEST_CASE("Torrent runtime uses its override without changing the DC proxy", "[qt][mainwindow][torrent-routing-runtime]")
{
    IsolatedMainWindow fixture;
    QTcpServer dcProxy, torrentProxy;
    REQUIRE(dcProxy.listen(QHostAddress::LocalHost));
    REQUIRE(torrentProxy.listen(QHostAddress::LocalHost));
    int dcConnections = 0, torrentConnections = 0;
    auto countConnections = [](QTcpServer &server, int &counter) {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &counter] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                ++counter;
                socket->abort();
                socket->deleteLater();
            }
        });
    };
    countConnections(dcProxy, dcConnections);
    countConnections(torrentProxy, torrentConnections);
    auto *dc = fixture.core->getSettingsManager();
    dc->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_SOCKS5);
    dc->set(dcpp::SettingsManager::SOCKS_SERVER, "127.0.0.1");
    dc->set(dcpp::SettingsManager::SOCKS_PORT, int(dcProxy.serverPort()));
    dc->set(dcpp::SettingsManager::SOCKS_TLS, false);
    auto settings = eiskalt::torrent::loadSettings(TorrentRuntime::settingsPath());
    settings.proxyMode = eiskalt::torrent::ProxyMode::Custom;
    settings.socks5Proxy.host = "127.0.0.1";
    settings.socks5Proxy.port = torrentProxy.serverPort();
    settings.socks5Proxy.udp = false;
    REQUIRE(eiskalt::torrent::saveSettings(TorrentRuntime::settingsPath(), settings));
    auto *runtime = TorrentRuntime::instance();
    runtime->reloadSettings();
    const auto id = runtime->engine()->add(
        "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&tr=http%3A%2F%2Ffixture.invalid%3A49001%2Fannounce",
        fixture.home.filePath("downloads"));
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(waitFor([&] { return torrentConnections > 0; }));
    REQUIRE(dcConnections == 0);
    REQUIRE(dc->get(dcpp::SettingsManager::SOCKS_PORT) == dcProxy.serverPort());
    const int before = torrentConnections;
    dc->set(dcpp::SettingsManager::OUTGOING_CONNECTIONS, dcpp::SettingsManager::OUTGOING_DIRECT);
    runtime->reloadSettings();
    REQUIRE(waitFor([&] { return torrentConnections > before; }));
    REQUIRE(dcConnections == 0);
    REQUIRE(dc->get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_DIRECT);
}

TEST_CASE("MainWindow Torrent action toggles the real singleton without losing state", "[qt][torrent-toggle][mainwindow]")
{
    INFO("Tab layout: " << qEnvironmentVariable("EISKALT_TEST_TAB_LAYOUT", "multiline").toStdString());
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    auto *action = child<QAction>(window, "toolsTorrents");
    auto *manager = fixture.gui->arenaWidgetManager();
    auto *engine = TorrentRuntime::instance()->engine();
    auto *arena = qobject_cast<QDockWidget *>(window->centralWidget());
    REQUIRE(arena);
    REQUIRE_FALSE(window->findChild<TorrentWindow *>());
    CHECK(action->isCheckable());
    CHECK_FALSE(action->isChecked());

    action->trigger();
    QPointer<TorrentWindow> tab(window->findChild<TorrentWindow *>());
    REQUIRE(tab);
    REQUIRE(tab->isVisible());
    CHECK_FALSE(tab->state() & ArenaWidget::Hidden);
    CHECK(action->isChecked());
    REQUIRE(arena->widget() == tab);

    QFile metadata(fixture.home.filePath("offline.torrent"));
    REQUIRE(metadata.open(QIODevice::WriteOnly));
    const QByteArray bytes = QByteArray("d4:infod6:lengthi1e4:name11:payload.bin12:piece lengthi16384e6:pieces20:") +
        QCryptographicHash::hash("x", QCryptographicHash::Sha1) + "ee";
    REQUIRE(metadata.write(bytes) == bytes.size());
    metadata.close();
    const QString id = engine->add(metadata.fileName(), fixture.home.filePath("downloads"), {}, true);
    REQUIRE_FALSE(id.isEmpty());
    auto *jobs = child<QTreeWidget>(tab, "torrentJobs");
    auto *files = child<QTreeWidget>(tab, "torrentFiles");
    REQUIRE(waitFor([&] { return jobs->topLevelItemCount() == 1; }));
    tab->selectJob(id);
    REQUIRE(files->topLevelItemCount() == 1);
    jobs->sortItems(1, Qt::DescendingOrder);
    jobs->setColumnWidth(0, 347);
    jobs->header()->moveSection(2, 1);
    files->setColumnWidth(0, 413);
    files->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    auto *selected = jobs->currentItem();
    const QByteArray jobHeader = jobs->header()->saveState();
    const QByteArray fileHeader = files->header()->saveState();

    action->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(tab);
    REQUIRE(tab->state() & ArenaWidget::Hidden);
    CHECK_FALSE(tab->isVisible());
    CHECK_FALSE(action->isChecked());
    REQUIRE(engine == TorrentRuntime::instance()->engine());
    REQUIRE(engine->jobs().size() == 1);
    CHECK(engine->jobs().first().paused);
    // The real worker still accepts edits while its management tab is closed.
    engine->setWantedFiles(id, {});
    REQUIRE(waitFor([&] { return !engine->files(id).first().wanted; }));

    action->trigger();
    REQUIRE(window->findChild<TorrentWindow *>() == tab);
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    CHECK(jobs->currentItem() == selected);
    CHECK(jobs->currentItem()->isSelected());
    CHECK(jobs->header()->saveState() == jobHeader);
    CHECK(files->header()->saveState() == fileHeader);
    CHECK(files->topLevelItem(0)->checkState(0) == Qt::Unchecked);

    // A different singleton makes Torrents a background tab, not a closed tab.
    auto *uploadsAction = child<QAction>(window, "toolsFinishedUploads");
    uploadsAction->trigger();
    auto *uploads = window->widgetForRole(ArenaWidget::FinishedUploads);
    manager->activate(uploads);
    REQUIRE(arena->widget() == uploads->getWidget());
    uploadsAction->trigger();
    REQUIRE(uploads->state() & ArenaWidget::Hidden);
    CHECK_FALSE(uploadsAction->isChecked());
    uploadsAction->trigger();
    REQUIRE(window->widgetForRole(ArenaWidget::FinishedUploads) == uploads);
    REQUIRE(arena->widget() == uploads->getWidget());
    CHECK(uploadsAction->isChecked());
    REQUIRE_FALSE(tab->isVisible());
    REQUIRE_FALSE(tab->state() & ArenaWidget::Hidden);
    CHECK(action->isChecked());
    action->trigger();
    REQUIRE(arena->widget() == tab);
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    action->trigger();
    REQUIRE(tab->state() & ArenaWidget::Hidden);
    CHECK_FALSE(action->isChecked());

    window->showTorrents();
    window->showTorrents();
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    // Exercise the actual transfer-details connection, including repeated shows.
    manager->rem(tab);
    for (int i = 0; i < 2; ++i) {
        REQUIRE(QMetaObject::invokeMethod(fixture.gui->transferView(), "torrentDetailsRequested",
                    Qt::DirectConnection, Q_ARG(QString, id)));
        REQUIRE(tab->isVisible());
        CHECK(jobs->currentItem()->data(0, Qt::UserRole).toString() == id);
        CHECK(action->isChecked());
    }
    // Cancel the production add dialog: opening a source must never close the tab.
    QTimer::singleShot(0, window, [window] {
        if (auto *dialog = window->findChild<QDialog *>("torrentAddDialog"))
            dialog->reject();
    });
    REQUIRE(window->openTorrentSource(metadata.fileName()));
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    manager->rem(tab);
    REQUIRE(tab->state() & ArenaWidget::Hidden);
    CHECK_FALSE(action->isChecked());
    window->showTorrents();
    REQUIRE(tab->isVisible());
    CHECK(action->isChecked());
    CHECK(engine->jobs().size() == 1);
    CHECK(engine->jobs().first().paused);
    CHECK_FALSE(QFile::exists(fixture.home.filePath("downloads/payload.bin")));
}

TEST_CASE("Transfer Torrent menu exposes the actual download directory", "[qt][torrent-location][torrent-location-async][mainwindow]")
{
    IsolatedMainWindow fixture;
    auto *engine = TorrentRuntime::instance()->engine();
    QFile metadata(fixture.home.filePath("offline.torrent"));
    REQUIRE(metadata.open(QIODevice::WriteOnly));
    const QByteArray bytes = QByteArray("d4:infod6:lengthi1e4:name11:payload.bin12:piece lengthi16384e6:pieces20:") +
        QCryptographicHash::hash("x", QCryptographicHash::Sha1) + "ee";
    REQUIRE(metadata.write(bytes) == bytes.size());
    metadata.close();
    const auto path = fixture.home.filePath("downloads");
    REQUIRE_FALSE(QFileInfo::exists(path));
    const auto id = engine->add(metadata.fileName(), path, {}, true);
    REQUIRE_FALSE(id.isEmpty());
    // The optimistic admission row has no resolved storage location yet.
    REQUIRE(waitFor([&] { return engine->files(id).size() == 1; }));
    REQUIRE(QDir().rmdir(path));
    REQUIRE_FALSE(QFileInfo::exists(path));
    TransferView transfers(*fixture.core);
    transfers.setTorrentEngine(engine);
    transfers.show();
    auto *table = child<QTreeView>(&transfers, "treeView_TRANSFERS");
    REQUIRE(waitFor([&] { return table->model()->rowCount() == 1; }));
    table->selectionModel()->select(table->model()->index(0, 0),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    bool found = false, enabled = false;
    QTimer closer;
    QObject::connect(&closer, &QTimer::timeout, &transfers, [&] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            if (auto *action = menu->findChild<QAction *>("torrentOpenLocation")) {
                found = true;
                enabled = action->isEnabled();
            }
            menu->close();
        }
    });
    closer.start(20);
    REQUIRE(QMetaObject::invokeMethod(&transfers, "slotContextMenu", Qt::DirectConnection, Q_ARG(QPoint, QPoint{})));
    closer.stop();
    CHECK(found);
    CHECK(enabled);
    CHECK(engine->jobs().first().paused);
    CHECK_FALSE(QFile::exists(QDir(path).filePath("payload.bin")));
}

TEST_CASE("Transfer location drops a result from a detached engine", "[qt][torrent-location-async][mainwindow]")
{
    IsolatedMainWindow fixture;
    auto *engine = TorrentRuntime::instance()->engine();
    QFile metadata(fixture.home.filePath("location.torrent"));
    REQUIRE(metadata.open(QIODevice::WriteOnly));
    const QByteArray bytes = QByteArray("d4:infod6:lengthi1e4:name11:payload.bin12:piece lengthi16384e6:pieces20:") +
        QCryptographicHash::hash("x", QCryptographicHash::Sha1) + "ee";
    REQUIRE(metadata.write(bytes) == bytes.size());
    metadata.close();
    const auto id = engine->add(metadata.fileName(), fixture.home.filePath("downloads"), {}, true);
    REQUIRE(waitFor([&] { return engine->files(id).size() == 1; }));
    TransferView transfers(*fixture.core);
    transfers.setTorrentEngine(engine);
    QSemaphore release;
    std::atomic_bool entered{false};
    bool staleWarning = false;
    QTimer dismissUnexpectedWarning;
    QObject::connect(&dismissUnexpectedWarning, &QTimer::timeout, &transfers, [&] {
        if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            staleWarning = true;
            warning->reject();
        }
    });
    dismissUnexpectedWarning.start(10);
    auto cleanup = dcpp::makeScopedFunctor([&] { release.release(8); QThreadPool::globalInstance()->waitForDone(5000); });
    TransferViewTestAccess::openLocation(transfers, engine->jobs().first(), [&](const QString &path) {
        entered = true;
        release.acquire();
        return QFileInfo(path).isDir();
    });
    REQUIRE(waitFor([&] { return entered.load(); }));
    transfers.setTorrentEngine(nullptr);
    release.release();
    REQUIRE(waitFor([&] { return !transfers.property("_torrentLocationPending").toBool(); }));
    CHECK_FALSE(staleWarning);
    CHECK_FALSE(QApplication::activeModalWidget());
    CHECK(engine->jobs().first().paused);
}

TEST_CASE("Checked quit retains a held location receipt without blocking GUI", "[qt][torrent-location-async][mainwindow]")
{
#if __has_include("TorrentLocationProbe.h")
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    auto *requester = new QWidget(window);
    eiskalt::torrent::Job job;
    job.savePath = fixture.home.path();
    QSemaphore release;
    std::atomic_bool entered{false};
    auto cleanup = dcpp::makeScopedFunctor([&] { release.release(8); QThreadPool::globalInstance()->waitForDone(5000); });
    int errors = 0;
    torrent_action_menu::probeDownloadDirectory(requester, job, [] { return true; }, [&] { ++errors; },
        [&](const QString &path) { entered = true; release.acquire(); return QFileInfo(path).isDir(); });
    REQUIRE(waitFor([&] { return entered.load(); }));
    delete requester;
    const auto receipt = window->findChildren<QFutureWatcherBase *>();
    REQUIRE(receipt.size() == 1);
    updates::ApplicationQuitSession session(*fixture.gui);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    const auto route = fixture.core->getProxyRoute()->snapshot();
    QTimer heartbeat;
    int ticks = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; });
    heartbeat.start(1);
    session.request(30);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(ticks > 0);
    CHECK(ready.isEmpty());
    CHECK_FALSE(route->revoked->load());
    CHECK_FALSE(receipt.first()->isFinished());
    release.release();
    REQUIRE(waitFor([&] { return receipt.first()->isFinished(); }));
    CHECK(ready.isEmpty());
    CHECK(errors == 0);
    session.request(3000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(route->revoked->load());
#else
    FAIL("No retained asynchronous location receipt for checked quit");
#endif
}

TEST_CASE("Transfer columns persist manual widths through real view reconstruction", "[qt][transfer-columns][mainwindow]")
{
    QTranslator translation;
    const QString catalog = qEnvironmentVariable("EISKALT_QA_CATALOG");
    if (!catalog.isEmpty()) {
        REQUIRE(translation.load(catalog));
        QCoreApplication::installTranslator(&translation);
    }
    IsolatedMainWindow fixture;
    const QString layoutKey = QStringLiteral("transferview/column-layout-v1");
    fixture.gui->settings()->setStr(WS_TRANSFERS_STATE, "");
    fixture.gui->settings()->setVar(layoutKey, QVariantMap{});
    QVariantMap saved;
    {
        TransferView first(*fixture.core);
        first.resize(1200, 260);
        first.show();
        auto *table = child<QTreeView>(&first, "treeView_TRANSFERS");
        auto *model = qobject_cast<TransferViewModel *>(table->model());
        REQUIRE(model);
        model->addConnection({{"CID", "offline-column-test"}, {"DOWN", true},
            {"USER", "A long example transfer username"}, {"STAT", "Download complete"},
            {"FNAME", "A full readable filename in the real transfer panel.iso"}});
        model->updateTransfer({{"CID", "offline-column-test"}, {"DOWN", true},
            {"ENCRYPTION", "TLS_AES_256_GCM_SHA384"}});
        const int text = table->fontMetrics().horizontalAdvance("TLS_AES_256_GCM_SHA384");
        REQUIRE(waitFor([&] { return table->columnWidth(COLUMN_TRANSFER_ENCRYPTION) >= text + 8; }));
        table->header()->resizeSection(COLUMN_TRANSFER_ENCRYPTION, 93);
        table->header()->moveSection(table->header()->visualIndex(COLUMN_TRANSFER_FNAME), 0);
        first.hide();
        fixture.gui->settings()->save();
        saved = fixture.gui->settings()->getVar(layoutKey).toMap();
        REQUIRE_FALSE(saved.isEmpty());
    }
    TransferView reopened(*fixture.core);
    reopened.resize(1200, 260);
    reopened.show();
    auto *table = child<QTreeView>(&reopened, "treeView_TRANSFERS");
    auto *model = qobject_cast<TransferViewModel *>(table->model());
    REQUIRE(model);
    model->addConnection({{"CID", "offline-reopen-test"}, {"DOWN", true},
        {"USER", "Example peer"}, {"FNAME", "Another readable file after reopening.iso"},
        {"STAT", QCoreApplication::translate("TransferView", "Download complete")}});
    model->updateTransfer({{"CID", "offline-reopen-test"}, {"DOWN", true},
        {"ENCRYPTION", "TLS_AES_256_GCM_SHA384"}});
    QTest::qWait(160);
    REQUIRE(table->columnWidth(COLUMN_TRANSFER_ENCRYPTION) == 93);
    REQUIRE(table->header()->visualIndex(COLUMN_TRANSFER_FNAME) == 0);
    child<QAction>(&reopened, "fitTransferColumns")->trigger();
    REQUIRE(table->columnWidth(COLUMN_TRANSFER_ENCRYPTION) >=
        table->fontMetrics().horizontalAdvance("TLS_AES_256_GCM_SHA384") + 8);
    const QString screenshot = qEnvironmentVariable("EISKALT_QA_COLUMNS_IMAGE");
    if (!screenshot.isEmpty()) {
        reopened.resize(1500, 240);
        QTest::qWait(150);
        REQUIRE(reopened.grab().save(screenshot));
    }
}

TEST_CASE("Open tabs follow repeated application appearance changes", "[qt][tab-navigation-mainwindow][tab-appearance-mainwindow]")
{
    struct RestoreAppearance {
        QPalette palette = qApp->palette();
        QString sheet = qApp->styleSheet();
        ~RestoreAppearance() {
            qApp->setPalette(palette);
            qApp->setStyleSheet(sheet);
        }
    } appearance;
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    auto *manager = fixture.gui->arenaWidgetManager();
    QList<ScriptWidget *> tabs;
    for (const auto &title : {"Public Hubs", "Offline community"}) {
        auto *tab = new ScriptWidget;
        tab->setWidget(new QLabel("Offline appearance test"));
        tab->setArenaShortTitle(title);
        tab->setArenaTitle(title);
        manager->add(tab);
        tabs.append(tab);
    }
    manager->activate(tabs.back());
    const auto *arena = qobject_cast<QDockWidget *>(window->centralWidget());
    REQUIRE(arena);
    auto *single = window->findChild<QTabBar *>("arenaTabbar");
    const auto multiline = window->findChildren<TabButton *>();
    REQUIRE((single || multiline.size() == 2));
    const QString previews = qEnvironmentVariable("EISKALT_QA_APPEARANCE");
    int step = 0;
    for (const bool dark : {true, false, true, false}) {
        INFO("Appearance step " << step << ", dark=" << dark);
        QPalette palette = appearance.palette;
        palette.setColor(QPalette::Window, QColor(dark ? "#292e34" : "#eeeeee"));
        palette.setColor(QPalette::Button, QColor(dark ? "#30363d" : "#d6d6d6"));
        palette.setColor(QPalette::Base, QColor(dark ? "#252a30" : "#ffffff"));
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
            palette.setColor(role, QColor(dark ? "#f0f3f5" : "#121212"));
        qApp->setPalette(palette);
        // Like the macOS input stylesheet, this repolishes widgets without a tab rule.
        qApp->setStyleSheet(appearance.sheet + QStringLiteral(
            "\nQLineEdit { background-color: %1; color: %2; }")
            .arg(palette.color(QPalette::Base).name(), palette.color(QPalette::Text).name()));
        QTest::qWait(40);

        const auto checkSurface = [dark](QWidget *widget, const QRect &rect) {
            const QImage image = widget->grab().toImage();
            const qreal scale = image.devicePixelRatio();
            const QPoint sample(rect.center().x(), rect.top() + 6);
            const auto color = image.pixelColor(qRound(sample.x() * scale), qRound(sample.y() * scale));
            INFO("Rendered tab surface " << color.name().toStdString());
            CHECK((color.lightness() < 128) == dark);
        };
        if (single) {
            REQUIRE(single->count() == 2);
            for (int i = 0; i < single->count(); ++i)
                checkSurface(single, single->tabRect(i));
            const auto before = single->grab().toImage();
            auto *toolbar = window->findChild<ToolBar *>();
            REQUIRE(toolbar);
            toolbar->refreshTabStyle();
            QTest::qWait(20);
            CHECK(single->grab().toImage() == before);
        } else {
            for (auto *button : multiline)
                checkSurface(button, button->rect());
        }
        for (auto *close : window->findChildren<QToolButton *>("tabCloseButton"))
            CHECK((close->palette().color(QPalette::ButtonText).lightness() > 128) == dark);
        CHECK(arena->widget() == tabs.back()->getWidget());
        if (!previews.isEmpty()) {
            QDir().mkpath(previews);
            REQUIRE(window->grab().save(QString("%1/%2-%3-%4.png").arg(previews,
                single ? "singleline" : "multiline").arg(step).arg(dark ? "dark" : "light")));
        }
        ++step;
    }
    for (auto *tab : tabs)
        manager->rem(tab);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("MainWindow keeps forty offline tabs searchable and safely selectable", "[qt][tab-navigation-mainwindow]")
{
    IsolatedMainWindow fixture;
    class OfflineHubTab final : public ScriptWidget {
    public:
        Role role() const override { return ArenaWidget::Hub; }
    };
    auto *window = fixture.gui->mainWindow();
    auto *manager = fixture.gui->arenaWidgetManager();
    auto *arena = qobject_cast<QDockWidget *>(window->centralWidget());
    REQUIRE(arena);
    QList<OfflineHubTab *> tabs;
    for (int i = 0; i < 40; ++i) {
        auto *tab = new OfflineHubTab;
        const auto url = QString("adcs://offline-%1.example.invalid:1511/?kp=SHA256/%2")
            .arg(i).arg(QString(80, 'A'));
        const auto title = i % 3 == 0 ? url : QString("Community %1 with a long readable name").arg(i);
        tab->setWidget(new QLabel("Synthetic offline tab: no network connections"));
        tab->setArenaShortTitle(title);
        tab->setArenaTitle(title + QLatin1Char('\n') + url);
        tabs.append(tab);
        manager->add(tab);
    }
    window->resize(780, 640);
    QTest::qWait(150);
    auto *all = child<QToolButton>(window, "allTabsButton");
    REQUIRE(all->isVisible());
    auto *menu = child<tab_navigation::AllTabsMenu>(window, "allTabsMenu");
    auto *search = child<QLineEdit>(menu, "tabSearch");
    auto *list = child<QListWidget>(menu, "tabSearchResults");
    auto *single = window->findChild<QTabBar *>("arenaTabbar");
    if (single) {
        REQUIRE(single->count() == 40);
        for (int i = 0; i < 40; ++i) {
            CHECK(single->tabRect(i).width() <= tab_navigation::MaximumWidth);
            CHECK_FALSE(single->tabText(i).contains("kp=SHA256/"));
            CHECK(single->tabToolTip(i).contains("kp=SHA256/"));
        }
        single->moveTab(0, 20);
    } else {
        const auto buttons = window->findChildren<TabButton *>();
        REQUIRE(buttons.size() == 40);
        for (auto *button : buttons)
            CHECK(button->width() <= tab_navigation::MaximumWidth);
    }
    all->click();
    QTest::qWait(30);
    REQUIRE(list->count() == 40);
    search->setText("offline-33.example");
    REQUIRE(list->count() == 1);
    const auto staleId = list->item(0)->data(Qt::UserRole).toULongLong();
    QTest::keyClick(search, Qt::Key_Return);
    REQUIRE(waitFor([&] { return arena->widget() == tabs[33]->getWidget(); }));
    manager->activate(tabs[20]);
    manager->rem(tabs.takeAt(33));
    REQUIRE(arena->widget() == tabs[20]->getWidget());
    REQUIRE(QMetaObject::invokeMethod(menu, "selected", Qt::DirectConnection, Q_ARG(quint64, staleId)));
    REQUIRE(arena->widget() == tabs[20]->getWidget());
    all->click();
    QTest::qWait(30);
    search->clear();
    REQUIRE(list->count() == 39);
    menu->hide();
    for (auto *tab : tabs) manager->rem(tab);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("Global GOST preferences preserve native controls and unsaved profiles", "[gost-global-native][gost-global][ui]")
{
    IsolatedMainWindow fixture;
    auto &sm = *fixture.core->getSettingsManager();
    sm.set(dcpp::SettingsManager::AUTO_DETECT_CONNECTION, true);
    sm.set(dcpp::SettingsManager::INCOMING_CONNECTIONS, dcpp::SettingsManager::INCOMING_DIRECT);
    sm.set(dcpp::SettingsManager::SOCKS_SERVER, "socks.invalid");
    sm.set(dcpp::SettingsManager::SOCKS_PASSWORD, "socks-secret");
    sm.set(dcpp::SettingsManager::GOST_SERVER, "gost.invalid");
    sm.set(dcpp::SettingsManager::GOST_PORT, 5541);
    sm.set(dcpp::SettingsManager::GOST_USER, "fixture-user");
    sm.set(dcpp::SettingsManager::GOST_PASSWORD, "fixture-secret");
    const auto generation = fixture.core->getProxyRoute()->snapshot()->generation;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    dialog.resize(1050, 850); dialog.show();
    auto *page = dialog.findChild<SettingsConnection *>();
    REQUIRE(page);
    auto *gost = child<QRadioButton>(page, "globalGostMode");
    auto *socks = child<QRadioButton>(page, "radioButton_SOCKS");
    auto *password = child<QLineEdit>(page, "lineEdit_SPSWD");
    auto *server = child<QLineEdit>(page, "lineEdit_SIP");
    page->showProxyPage(password);
    REQUIRE(waitFor([&] { return gost->isVisible(); }));
    gost->click();
    REQUIRE(gost->isChecked());
    CHECK(server->text() == "gost.invalid");
    CHECK(password->echoMode() == QLineEdit::Password);
    auto *reveal = password->findChild<QAction *>(QStringLiteral("passwordRevealAction"));
    REQUIRE(reveal);
    REQUIRE_FALSE(reveal->icon().isNull());
    // The legacy LineEdit paints a clear label over trailing QLineEdit actions.
    CHECK(password->findChildren<QLabel *>().isEmpty());
    reveal->trigger();
    CHECK(password->echoMode() == QLineEdit::Normal);
    CHECK(password->text() == "fixture-secret");
    reveal->trigger();
    CHECK(password->echoMode() == QLineEdit::Password);
    for (const auto *name : {"globalProxyTestTcpHost", "globalProxyTestDnsQuery"}) {
        CHECK(child<QLineEdit>(page, name)->text() == "google.com");
        CHECK(child<QLineEdit>(page, name)->isReadOnly());
    }
    CHECK_FALSE(child<QCheckBox>(page, "checkBox_RESOLVE")->isEnabled());
    CHECK_FALSE(child<QRadioButton>(page, "radioButton_ACTIVE")->isEnabled());
    CHECK(child<QRadioButton>(page, "radioButton_ACTIVE")->isChecked());
    CHECK(child<QLineEdit>(page, "globalProxyTestDnsResolver")->isEnabled());
    CHECK(child<QLineEdit>(page, "globalGostCaFile")->isVisible());
    password->setText("unsaved-gost-secret");
    reveal->trigger();
    socks->click();
    CHECK(password->echoMode() == QLineEdit::Password);
    CHECK_FALSE(reveal->isChecked());
    CHECK(server->text() == "socks.invalid");
    CHECK(password->text() == "socks-secret");
    gost->click();
    CHECK(password->text() == "unsaved-gost-secret");
    REQUIRE(page->validate());
    CHECK(fixture.core->getProxyRoute()->snapshot()->generation == generation);
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "fixture-secret");
    for (auto *area : dialog.findChildren<QScrollArea *>())
        if (area->widget() && area->widget()->isAncestorOf(password))
            area->ensureWidgetVisible(password, 0, 100);
    QTest::qWait(100);
    QToolButton *eyeButton = nullptr;
    for (auto *button : password->findChildren<QToolButton *>())
        if (button->defaultAction() == reveal) eyeButton = button;
    REQUIRE(eyeButton);
    REQUIRE(eyeButton->isVisible());
    REQUIRE(eyeButton->isEnabled());
    for (auto *button : password->findChildren<QToolButton *>())
        if (button != eyeButton && button->isVisible())
            CHECK_FALSE(button->geometry().intersects(eyeButton->geometry()));
    const auto clickEye = [&] {
        // Native Cocoa exercises window-system hit testing; headless platforms
        // do not reliably route mouse events through a scrolled settings page.
        if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
            QTest::mouseClick(eyeButton, Qt::LeftButton);
        else
            eyeButton->click();
    };
    clickEye();
    CHECK(password->echoMode() == QLineEdit::Normal);
    clickEye();
    CHECK(password->echoMode() == QLineEdit::Password);
    const auto screenshot = qEnvironmentVariable("EISKALT_GOST_UI_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        auto *caField = child<QLineEdit>(page, "globalGostCaFile");
        for (auto *area : dialog.findChildren<QScrollArea *>()) {
            if (area->widget() && area->widget()->isAncestorOf(caField)) {
                area->ensureWidgetVisible(caField, 0, 100);
                QTest::qWait(100);
                REQUIRE(area->viewport()->rect().contains(caField->mapTo(area->viewport(), caField->rect().center())));
            }
        }
        QTest::qWait(100);
        REQUIRE(dialog.grab().save(screenshot));
    }
    reveal->trigger();
    CHECK(password->echoMode() == QLineEdit::Normal);
    dialog.reject();
    CHECK(password->echoMode() == QLineEdit::Password);
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "fixture-secret");
    CHECK(sm.getBool(dcpp::SettingsManager::AUTO_DETECT_CONNECTION));
}

TEST_CASE("DC certificate controls preserve identity on cancel and save new paths only on Apply", "[certificate-ui][ui]")
{
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    const auto oldCert = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE));
    const auto oldKey = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_PRIVATE_KEY_FILE));
    auto read = [](const QString& path) { QFile f(path); REQUIRE(f.open(QIODevice::ReadOnly)); return f.readAll(); };
    const auto certificate = read(oldCert), key = read(oldKey);
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    auto* page = dialog.findChild<SettingsConnection*>();
    REQUIRE(page);
    auto* certField = child<QLineEdit>(page, "dcTlsCertificate");
    auto* keyField = child<QLineEdit>(page, "dcTlsPrivateKey");
    auto* details = child<QLabel>(page, "dcTlsDetails");
    auto* generate = child<QPushButton>(page, "dcTlsGenerate");
    REQUIRE(certField); REQUIRE(keyField); REQUIRE(details); REQUIRE(generate);
    CHECK(certField->text() == oldCert);
    CHECK(keyField->text() == oldKey);
    CHECK(child<QComboBox>(page, "comboBox_TLS")->currentIndex() == 1);
    CHECK(details->text().contains("SHA-256"));
    QTimer confirm;
    QObject::connect(&confirm, &QTimer::timeout, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                if (auto* yes = box->button(QMessageBox::Yes)) yes->click();
    });
    confirm.start(10);
    generate->click();
    confirm.stop();
    REQUIRE(waitFor([&] { return generate->isEnabled(); }));
    REQUIRE(certField->text() != oldCert);
    const auto generated = certField->text();
    CHECK(read(oldCert) == certificate);
    CHECK(read(oldKey) == key);
    CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == oldCert);
    REQUIRE(page->validate());
    SECTION("Cancel retains original settings") {
        dialog.reject();
        CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == oldCert);
    }
    SECTION("Apply saves new paths without replacing the old files") {
        page->ok();
        CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == generated);
        CHECK(read(oldCert) == certificate);
        CHECK(read(oldKey) == key);
    }
}

TEST_CASE("Certificate editor validates imports and manages only its own staged files", "[certificate-ui][ui]")
{
    IsolatedMainWindow fixture;
    auto& sm = *fixture.core->getSettingsManager();
    const auto oldCert = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE));
    const auto oldKey = QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_PRIVATE_KEY_FILE));
    auto panel = std::make_unique<CertificateSettings>(sm,
        QString::fromStdString(fixture.core->getClientManager()->getMyCID().toBase32()),
        fixture.home.filePath("new-identities"));
    auto* cert = child<QLineEdit>(panel.get(), "dcTlsCertificate");
    auto* key = child<QLineEdit>(panel.get(), "dcTlsPrivateKey");
    auto* generate = child<QPushButton>(panel.get(), "dcTlsGenerate");
    cert->setText(fixture.home.filePath("missing.crt"));
    CHECK_FALSE(panel->validationError().isEmpty());
    panel->save();
    CHECK(QString::fromStdString(sm.get(dcpp::SettingsManager::TLS_CERTIFICATE_FILE)) == oldCert);
    cert->setText(oldCert);
    key->setText(oldCert); // A certificate is not a private key.
    CHECK_FALSE(panel->validationError().isEmpty());
    key->setText(oldKey);
    REQUIRE(panel->validationError().isEmpty());
    QTimer confirm;
    bool yes = true;
    QObject::connect(&confirm, &QTimer::timeout, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                if (auto* button = box->button(yes ? QMessageBox::Yes : QMessageBox::No)) button->click();
    });
    auto run = [&] { confirm.start(10); generate->click(); confirm.stop(); };
    SECTION("Cancel deletes only the unsaved new pair") {
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        const auto path = cert->text();
        REQUIRE(path != oldCert);
        REQUIRE(QFileInfo::exists(path));
        panel.reset();
        CHECK_FALSE(QFileInfo::exists(path));
        CHECK(QFileInfo::exists(oldCert)); CHECK(QFileInfo::exists(oldKey));
    }
    SECTION("Save keeps the new private files") {
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        const auto path = key->text();
        REQUIRE(path != oldKey);
        CHECK_FALSE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther));
        panel->save(); panel.reset();
        CHECK(QFileInfo::exists(path));
        CHECK(QFileInfo::exists(oldKey));
    }
    SECTION("Save through a directory alias retains the generated pair") {
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        const auto generatedCert = cert->text(), generatedKey = key->text();
        REQUIRE(generatedCert != oldCert);
        const auto alias = fixture.home.filePath("identity-alias");
        REQUIRE(QFile::link(QFileInfo(generatedCert).absolutePath(), alias));
        cert->setText(alias + "/client.crt"); key->setText(alias + "/client.key");
        REQUIRE(panel->validationError().isEmpty());
        panel->save(); panel.reset();
        CHECK(QFileInfo::exists(generatedCert));
        CHECK(QFileInfo::exists(generatedKey));
    }
    SECTION("Declining generation changes nothing") {
        yes = false; run();
        CHECK(cert->text() == oldCert);
        CHECK_FALSE(QFileInfo::exists(fixture.home.filePath("new-identities")));
    }
    SECTION("An unwritable destination preserves old paths") {
        QFile blocker(fixture.home.filePath("new-identities"));
        REQUIRE(blocker.open(QIODevice::WriteOnly)); blocker.close();
        run();
        REQUIRE(waitFor([&] { return generate->isEnabled(); }));
        CHECK(cert->text() == oldCert); CHECK(key->text() == oldKey);
        CHECK(child<QLabel>(panel.get(), "dcTlsMessage")->text().contains("failed"));
    }
    SECTION("Closing during generation does not write files") {
        run(); panel.reset();
        QThreadPool::globalInstance()->waitForDone();
        CHECK_FALSE(QFileInfo::exists(fixture.home.filePath("new-identities")));
        CHECK(QFileInfo::exists(oldCert)); CHECK(QFileInfo::exists(oldKey));
    }
}

TEST_CASE("Certificate controls fit the native Advanced page", "[certificate-ui-native][certificate-ui][ui]")
{
    IsolatedMainWindow fixture;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    auto* page = dialog.findChild<SettingsConnection*>();
    REQUIRE(page);
    auto *tabs = page->findChild<QTabWidget*>();
    tabs->setCurrentIndex(tabs->count() - 1);
    dialog.resize(1050, 900); dialog.show();
    QTest::qWait(100);
    auto* button = child<QPushButton>(page, "dcTlsGenerate");
    for (auto* area : dialog.findChildren<QScrollArea*>())
        if (area->widget() && area->widget()->isAncestorOf(button)) area->ensureWidgetVisible(button);
    QTest::qWait(100);
    CHECK(button->isVisible());
    CHECK(child<QLineEdit>(page, "dcTlsCertificate")->width() > 200);
    const auto screenshot = qEnvironmentVariable("EISKALT_CERT_UI_SCREENSHOT");
    if (!screenshot.isEmpty()) REQUIRE(dialog.grab().save(screenshot));
}

TEST_CASE("Global invalid GOST preferences do not partially persist other pages", "[gost-global-native][gost-global][ui]")
{
    IsolatedMainWindow fixture;
    auto &sm = *fixture.core->getSettingsManager();
    const auto nick = sm.get(dcpp::SettingsManager::NICK);
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    auto *page = dialog.findChild<SettingsConnection *>();
    REQUIRE(page);
    page->showProxyPage(child<QRadioButton>(page, "globalGostMode"));
    child<QRadioButton>(&dialog, "globalGostMode")->click();
    child<QLineEdit>(&dialog, "lineEdit_SIP")->setText("proxy.invalid");
    child<QLineEdit>(&dialog, "lineEdit_SPORT")->setText("5541");
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *message = qobject_cast<QMessageBox *>(widget)) message->accept();
    });
    dismiss.start(10);
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);
    CHECK(sm.get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_DIRECT);
    CHECK(sm.get(dcpp::SettingsManager::GOST_SERVER).empty());
    CHECK(sm.get(dcpp::SettingsManager::NICK) == nick);
}

TEST_CASE("Global GOST Apply preserves incoming preferences and no-op route generation", "[gost-global-native][gost-global][ui]")
{
    IsolatedMainWindow fixture;
    auto &sm = *fixture.core->getSettingsManager();
    sm.set(dcpp::SettingsManager::AUTO_DETECT_CONNECTION, true);
    sm.set(dcpp::SettingsManager::INCOMING_CONNECTIONS, dcpp::SettingsManager::INCOMING_DIRECT);
    SettingsConnection page;
    child<QRadioButton>(&page, "globalGostMode")->click();
    child<QLineEdit>(&page, "lineEdit_SIP")->setText("proxy.invalid");
    child<QLineEdit>(&page, "lineEdit_SPORT")->setText("5541");
    child<QLineEdit>(&page, "lineEdit_SUSR")->setText("fixture-user");
    child<QLineEdit>(&page, "lineEdit_SPSWD")->setText("fixture-secret");
    REQUIRE(page.validate());
    page.ok();
    CHECK(sm.get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_GOST);
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "fixture-secret");
    CHECK(sm.getBool(dcpp::SettingsManager::AUTO_DETECT_CONNECTION));
    CHECK(sm.get(dcpp::SettingsManager::INCOMING_CONNECTIONS) == dcpp::SettingsManager::INCOMING_DIRECT);
    const auto route = fixture.core->getProxyRoute()->snapshot();
    REQUIRE(route->valid);
    page.ok();
    CHECK(fixture.core->getProxyRoute()->snapshot() == route);
    child<QLineEdit>(&page, "lineEdit_SPSWD")->setText("retained-inactive-secret");
    child<QRadioButton>(&page, "radioButton_DC")->click();
    // Keep this isolated fixture passive instead of starting real incoming probes.
    child<QCheckBox>(&page, "checkBox_AUTO_DETECT_CONNECTION")->setChecked(false);
    child<QRadioButton>(&page, "radioButton_PASSIVE")->setChecked(true);
    page.ok();
    CHECK(sm.get(dcpp::SettingsManager::GOST_PASSWORD) == "retained-inactive-secret");
    CHECK(sm.get(dcpp::SettingsManager::OUTGOING_CONNECTIONS) == dcpp::SettingsManager::OUTGOING_DIRECT);
    CHECK(route->revoked->load());
}

#include "DownloadQueue.h"
#include "DownloadQueueModel.h"
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QPersistentModelIndex>

namespace {
QVariantMap queueRow(const QString &path, const QString &name)
{
    return {{"PATH", path}, {"FNAME", name}, {"STATUS", "1 of 2 user(s) online"},
        {"ESIZE", 4096}, {"DOWN", 1024}, {"PRIO", int(dcpp::QueueItem::NORMAL)},
        {"USERS", "Example peer"}, {"ERRORS", ""}, {"ADDED", "2026-09-27 20:40"},
        {"TTH", "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG"}};
}
}

TEST_CASE("Queue destinations do not bury filenames under absolute path ancestors", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    const auto first = queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "Sample document.txt");
    model.addItem(first);
    const auto group = model.index(0, 0);
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.rowCount(group) == 1);
    CHECK(model.index(0, 0, group).data().toString() == "Sample document.txt");
    const auto decoration = model.index(0, 0, group).data(Qt::DecorationRole);
    const QPixmap icon = decoration.metaType() == QMetaType::fromType<QPixmap>()
        ? decoration.value<QPixmap>() : decoration.value<QIcon>().pixmap(QSize(16, 16), 2.0);
    CHECK(qRound(icon.deviceIndependentSize().width()) >= 16);
    CHECK(group.data(Qt::ToolTipRole).toString().contains("/Users/fixture/.local/share/eiskaltdc++/files"));
    model.addItem(queueRow("/Volumes/Other/files/", "Second document.txt"));
    REQUIRE(model.rowCount() == 2);
    CHECK(model.index(0, 0, model.index(1, 0)).data().toString() == "Second document.txt");
    CHECK(model.remItem(first));
    CHECK(model.rowCount() == 1);
    CHECK_FALSE(model.remItem(queueRow("/not/queued/", "absent.txt")));
    CHECK(model.rowCount() == 1);
}

TEST_CASE("Queue file list unknown sizes and absent hashes are not fake metadata", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    auto row = queueRow("/tmp/queue-fixture/", "Peer file list.xml.bz2");
    row["ESIZE"] = -1;
    row["TTH"] = QString(39, 'A');
    auto *item = model.addItem(row);
    auto index = model.createIndexForItem(item);
    CHECK(index.siblingAtColumn(COLUMN_DOWNLOADQUEUE_SIZE).data().toString().isEmpty());
    CHECK(index.siblingAtColumn(COLUMN_DOWNLOADQUEUE_ESIZE).data().toString().isEmpty());
    CHECK(index.siblingAtColumn(COLUMN_DOWNLOADQUEUE_TTH).data().toString().isEmpty());
}

TEST_CASE("Queue status painting stays inside its cell", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    auto row = queueRow("/tmp/queue-fixture/", "Example.iso");
    row["STATUS"] = QString(300, 'W');
    auto *item = model.addItem(row);
    DownloadQueueDelegate delegate;
    QTreeView view;
    view.setModel(&model);
    QImage canvas(400, 50, QImage::Format_ARGB32);
    canvas.fill(QColor("#d327df"));
    QStyleOptionViewItem option;
    option.initFrom(&view);
    option.widget = &view;
    option.rect = QRect(150, 5, 100, 35);
    QPainter painter(&canvas);
    delegate.paint(&painter, option, model.createIndexForItem(item).siblingAtColumn(COLUMN_DOWNLOADQUEUE_STATUS));
    painter.end();
    bool outsideUnchanged = true;
    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
            if (!option.rect.contains(x, y) && canvas.pixelColor(x, y) != QColor("#d327df"))
                outsideUnchanged = false;
    CHECK(outsideUnchanged);
}

TEST_CASE("Queue columns fit by default and keep explicit widths after reconstruction", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    const QString key = "downloadqueue/column-layout-v1";
    fixture.gui->settings()->setVar(WS_DQUEUE_STATE, QByteArray{});
    fixture.gui->settings()->setVar(key, QVariantMap{});
    {
        DownloadQueue queue(*fixture.core);
        queue.resize(1350, 450);
        queue.show();
        auto *view = child<QTreeView>(&queue, "treeView_TARGET");
        auto *model = qobject_cast<DownloadQueueModel *>(view->model());
        REQUIRE(model);
        model->addItem(queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "A readable file name in the download queue.iso"));
        view->expandAll();
        REQUIRE(waitFor([&] { return view->columnWidth(0) > 340; }));
        view->header()->resizeSection(0, 287);
        view->header()->hideSection(COLUMN_DOWNLOADQUEUE_TTH);
    }
    DownloadQueue reopened(*fixture.core);
    reopened.resize(1350, 450);
    reopened.show();
    auto *view = child<QTreeView>(&reopened, "treeView_TARGET");
    auto *model = qobject_cast<DownloadQueueModel *>(view->model());
    model->addItem(queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "A longer name after reopening must not override the chosen width.iso"));
    view->expandAll();
    QTest::qWait(200);
    CHECK(view->columnWidth(0) == 287);
    CHECK(view->isColumnHidden(COLUMN_DOWNLOADQUEUE_TTH));
    const auto image = qEnvironmentVariable("EISKALT_QUEUE_IMAGE");
    if (!image.isEmpty()) REQUIRE(reopened.grab().save(image));
}

TEST_CASE("Main window warnings are red plain text and recorded in status history", "[qt][queue-warning]")
{
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    auto *logs = fixture.core->getLogManager();
    logs->clearLiveEntries();
    REQUIRE(QMetaObject::invokeMethod(window, "displayStatusMessage", Q_ARG(QString, QString("Normal queue status"))));
    QLabel *banner = nullptr;
    for (auto *label : window->findChildren<QLabel *>())
        if (label->text() == "Normal queue status") banner = label;
    REQUIRE(banner);
    const QColor normalColor = banner->palette().color(QPalette::WindowText);
    const QString message = "Cannot download from passive user while you are in passive mode: <Example peer>";
    REQUIRE(QMetaObject::invokeMethod(window, "displayStatusWarning", Q_ARG(QString, message)));
    CHECK(banner->textFormat() == Qt::PlainText);
    auto color = banner->palette().color(QPalette::WindowText);
    CHECK(color.red() > color.green() * 1.5);
    CHECK(color.red() > color.blue() * 1.5);
    REQUIRE_FALSE(logs->getLiveEntries().empty());
    CHECK(logs->getLiveEntries().back().area == dcpp::LogManager::STATUS);
    REQUIRE(QMetaObject::invokeMethod(window, "displayStatusMessage", Q_ARG(QString, QString("Ready"))));
    CHECK(banner->palette().color(QPalette::WindowText) == normalColor);
}

TEST_CASE("Queue model updates preserve selected file identities and valid row notifications", "[qt][download-queue]")
{
    IsolatedMainWindow fixture;
    DownloadQueueModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    auto a = queueRow("/tmp/model/", "z-last.iso");
    auto b = queueRow("/tmp/model/", "a-first.iso");
    QSignalSpy stats(&model, &DownloadQueueModel::updateStats);
    auto *original = model.addItem(a);
    QPersistentModelIndex selected(model.createIndexForItem(original));
    model.addItem(b);
    model.sort(0, Qt::AscendingOrder);
    CHECK(selected.data().toString() == "z-last.iso");
    CHECK(selected.row() == 1);
    a["DOWN"] = 2048;
    model.updItem(a);
    CHECK(QModelIndex(selected).siblingAtColumn(COLUMN_DOWNLOADQUEUE_DOWN).data().toString().contains("2"));
    CHECK(model.remItem(b));
    CHECK(selected.isValid());
    CHECK(selected.row() == 0);
    CHECK(model.remItem(a));
    CHECK_FALSE(selected.isValid());
    CHECK(model.rowCount() == 0);
    model.addItem(a);
    model.clear();
    CHECK(model.rowCount() == 0);
    REQUIRE_FALSE(stats.isEmpty());
    CHECK(stats.last().at(0).toULongLong() == 0);
    CHECK(stats.last().at(1).toULongLong() == 0);
}

TEST_CASE("Core warning events reach the main window without losing red severity", "[qt][queue-warning]")
{
    IsolatedMainWindow fixture;
    auto *logs = fixture.core->getLogManager();
    const QString text = "Cannot download from passive user while you are in passive mode: Test peer";
    logs->clearLiveEntries();
    logs->warning(text.toStdString());
    QLabel *banner = nullptr;
    REQUIRE(waitFor([&] {
        for (auto *label : fixture.gui->mainWindow()->findChildren<QLabel *>()) {
            if (!label->toolTip().contains(text)) continue;
            const auto color = label->palette().color(QPalette::WindowText);
            if (color.red() > color.green() * 1.5) { banner = label; return true; }
        }
        return false;
    }));
    CHECK(banner->toolTip().count(text) == 1);
    const auto entries = logs->getLiveEntries();
    CHECK(std::count_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return entry.area == dcpp::LogManager::SYSTEM && entry.message.find("Test peer") != std::string::npos;
    }) == 1);
    CHECK(std::count_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return entry.area == dcpp::LogManager::STATUS && entry.message.find("Test peer") != std::string::npos;
    }) == 1);
}

TEST_CASE("Download queue has readable compact light and dark layouts", "[qt][download-queue][queue-preview]")
{
    struct RestorePalette {
        QPalette saved = qApp->palette();
        ~RestorePalette() { qApp->setPalette(saved); }
    } appearance;
    IsolatedMainWindow fixture;
    fixture.gui->settings()->setVar(WS_DQUEUE_STATE, QByteArray{});
    fixture.gui->settings()->setVar("downloadqueue/column-layout-v1", QVariantMap{});
    DownloadQueue queue(*fixture.core);
    queue.resize(1400, 470);
    queue.show();
    auto *view = child<QTreeView>(&queue, "treeView_TARGET");
    auto *model = qobject_cast<DownloadQueueModel *>(view->model());
    auto row = queueRow("/Users/fixture/Downloads/Documentaries/", "Wild Australia - Desert of the Red Kangaroo.mp4");
    row["ESIZE"] = qlonglong(2400000000);
    row["DOWN"] = qlonglong(900000000);
    model->addItem(row);
    row["FNAME"] = "Wild Australia - Jurassic Jungle.mp4";
    row["DOWN"] = 0;
    model->addItem(row);
    row = queueRow("/Users/fixture/.local/share/eiskaltdc++/files/", "Example peer.xml.bz2");
    row["ESIZE"] = -1;
    row["TTH"] = QString(39, 'A');
    model->addItem(row);
    view->expandAll();
    child<QAction>(&queue, "fitQueueColumns")->trigger();
    REQUIRE(view->columnWidth(0) > 340);
    CHECK(view->isColumnHidden(COLUMN_DOWNLOADQUEUE_TTH));
    CHECK(view->isColumnHidden(COLUMN_DOWNLOADQUEUE_ESIZE));
    CHECK(view->y() <= 10);
    CHECK(child<QPushButton>(&queue, "pushButton_EXPAND")->y() > view->y() + view->height());
    const auto images = qEnvironmentVariable("EISKALT_QUEUE_PREVIEWS");
    for (const bool dark : {false, true}) {
        QPalette palette = appearance.saved;
        palette.setColor(QPalette::Window, QColor(dark ? "#25282c" : "#f2f3f5"));
        palette.setColor(QPalette::Button, QColor(dark ? "#30343a" : "#eeeeee"));
        palette.setColor(QPalette::Base, QColor(dark ? "#1d2024" : "#ffffff"));
        palette.setColor(QPalette::AlternateBase, QColor(dark ? "#292d32" : "#f3f5f7"));
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
            palette.setColor(role, QColor(dark ? "#f0f3f5" : "#16191d"));
        qApp->setPalette(palette);
        QTest::qWait(180);
        const auto pixels = view->viewport()->grab().toImage();
        const qreal scale = pixels.devicePixelRatio();
        const auto file = model->index(0, 0, model->index(0, 0));
        const int y = qRound(view->visualRect(file).center().y() * scale);
        const int end = view->header()->length();
        REQUIRE(end + 10 < view->viewport()->width());
        INFO("row fill: inside=" << pixels.pixelColor(qRound((end - 8) * scale), y).name().toStdString()
             << ", outside=" << pixels.pixelColor(qRound((end + 8) * scale), y).name().toStdString());
        CHECK(pixels.pixelColor(qRound((end - 8) * scale), y) ==
              pixels.pixelColor(qRound((end + 8) * scale), y));
        if (!images.isEmpty()) {
            QDir().mkpath(images);
            REQUIRE(queue.grab().save(images + (dark ? "/queue-dark.png" : "/queue-light.png")));
        }
    }
}

namespace {
struct HubSelectionFixture {
    IsolatedMainWindow app;
    UserListModel model;
    UserListProxyModel proxy;
    QItemSelectionModel selection{&proxy};
    QList<UserListItem*> items;

    HubSelectionFixture()
    {
        model.sort(COLUMN_NICK, Qt::AscendingOrder);
        proxy.setSourceModel(&model);
        proxy.setFilterKeyColumn(COLUMN_NICK);
        for (int i = 0; i != 4; ++i) {
            UserPtr user(new User(CID::generate()));
            Identity identity(user, i + 1);
            identity.set("NI", "Peer" + std::to_string(i));
            identity.setBytesShared(std::to_string((4 - i) * 100));
            identity.set("DE", std::to_string(4 - i));
            identity.set("I4", "192.0.2." + std::to_string(i + 1));
            identity.set("I6", "2001:db8::" + std::to_string(4 - i));
            identity.set("EM", "peer" + std::to_string(i) + "@invalid.test");
            identity.set("CO", std::to_string((i + 1) * 10));
            identity.set("TA", "Client" + std::to_string(4 - i));
            items << model.addUser(user, identity,
                                   QString::fromStdString(user->getCID().toBase32()), false);
        }
    }

    QModelIndex source(UserListItem* item, int column = 0) {
        return model.index(item->row(), column);
    }
    void select(UserListItem* item) {
        selection.select(proxy.mapFromSource(source(item)),
                         QItemSelectionModel::Select | QItemSelectionModel::Rows);
        selection.setCurrentIndex(proxy.mapFromSource(source(item)), QItemSelectionModel::NoUpdate);
    }
    void checkSelected(const QList<UserListItem*>& expected) {
        const auto rows = selection.selectedRows();
        REQUIRE(rows.size() == expected.size());
        for (const auto& row : rows) {
            const auto mapped = proxy.mapToSource(row);
            CHECK(expected.contains(static_cast<UserListItem*>(mapped.internalPointer())));
            CHECK(mapped == source(static_cast<UserListItem*>(mapped.internalPointer())));
        }
    }
};
}

TEST_CASE("Hub selections retain user identity across every header sort", "[hub-selection]")
{
    HubSelectionFixture f;
    f.proxy.setFilterRegularExpression("Peer[013]");
    f.select(f.items[0]);
    f.select(f.items[3]);
    QItemSelectionModel sourceSelection(&f.model);
    sourceSelection.setCurrentIndex(f.source(f.items[1]),
                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QPersistentModelIndex proxySaved(f.proxy.mapFromSource(f.source(f.items[3], COLUMN_EMAIL)));
    QList<QPersistentModelIndex> saved;
    for (auto* item : f.items)
        for (int column = 0; column != f.model.columnCount(); ++column)
            saved << f.source(item, column);
    for (int column = 0; column != f.model.columnCount(); ++column) {
        for (const auto order : {Qt::DescendingOrder, Qt::AscendingOrder}) {
            f.proxy.sort(column, order);
            int n = 0;
            for (auto* item : f.items)
                for (int c = 0; c != f.model.columnCount(); ++c)
                    CHECK(saved[n++] == f.source(item, c));
            f.checkSelected({f.items[0], f.items[3]});
            CHECK(f.proxy.mapToSource(f.selection.currentIndex()) == f.source(f.items[3]));
            CHECK(f.proxy.mapToSource(proxySaved) == f.source(f.items[3], COLUMN_EMAIL));
            CHECK(sourceSelection.currentIndex() == f.source(f.items[1]));
            REQUIRE(sourceSelection.selectedRows().size() == 1);
            CHECK(sourceSelection.selectedRows().front() == f.source(f.items[1]));
        }
    }
}

TEST_CASE("Hub identity changes preserve selected rows without removal", "[hub-selection]")
{
    HubSelectionFixture f;
    f.select(f.items[1]);
    f.select(f.items[3]);
    QPersistentModelIndex saved(f.source(f.items[1], COLUMN_SHARE));
    QPersistentModelIndex other(f.source(f.items[2], COLUMN_COMMENT));
    QSignalSpy removed(&f.model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&f.model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&f.model, &QAbstractItemModel::dataChanged);
    Identity identity(f.items[1]->getIdentity());
    bool favorite = false;
    SECTION("nickname moves down") { identity.set("NI", "Zulu"); }
    SECTION("nickname moves up") { identity.set("NI", "Alpha"); }
    SECTION("operator promoted") { identity.setOp(true); }
    SECTION("favorite promoted") { favorite = true; }
    SECTION("share changes while share sorted") {
        f.proxy.sort(COLUMN_SHARE, Qt::DescendingOrder);
        identity.setBytesShared("9999");
    }
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), favorite);
    CHECK(removed.count() == 0);
    CHECK(inserted.count() == 0);
    CHECK(changed.count() == 1);
    CHECK(saved == f.source(f.items[1], COLUMN_SHARE));
    CHECK(other == f.source(f.items[2], COLUMN_COMMENT));
    f.checkSelected({f.items[1], f.items[3]});
    CHECK(f.proxy.mapToSource(f.selection.currentIndex()) == f.source(f.items[3]));
    f.model.removeUser(f.items[1]->getUser());
    CHECK_FALSE(saved.isValid());
    f.checkSelected({f.items[3]});
    f.model.clear();
    CHECK_FALSE(other.isValid());
    CHECK(f.selection.selectedRows().isEmpty());
}

TEST_CASE("Unsorted hub identity changes do not move the selected user", "[hub-selection]")
{
    HubSelectionFixture f;
    f.model.sort(-1);
    f.select(f.items[2]);
    QPersistentModelIndex saved(f.source(f.items[2]));
    Identity identity(f.items[2]->getIdentity());
    identity.setOp(true);
    f.model.updateUser(f.items[2], identity, f.items[2]->getCID(), true);
    CHECK(saved == f.source(f.items[2]));
    CHECK(saved.row() == 2);
    f.checkSelected({f.items[2]});
}

TEST_CASE("Filtered hub updates track the current user and invalidate hidden users", "[hub-selection]")
{
    HubSelectionFixture f;
    f.proxy.setFilterRegularExpression("Peer[13]");
    f.select(f.items[3]);
    f.select(f.items[1]);
    QPersistentModelIndex selected(f.proxy.mapFromSource(f.source(f.items[1])));
    QPersistentModelIndex other(f.proxy.mapFromSource(f.source(f.items[3])));
    QPersistentModelIndex source(f.source(f.items[1]));
    Identity identity(f.items[1]->getIdentity());
    identity.setOp(true);
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), false);
    f.checkSelected({f.items[1], f.items[3]});
    CHECK(f.selection.currentIndex() == selected);
    CHECK(f.proxy.mapToSource(selected) == f.source(f.items[1]));

    identity.set("NI", "Hidden");
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), false);
    CHECK_FALSE(selected.isValid());
    CHECK(source == f.source(f.items[1]));
    CHECK(other.isValid());
    f.checkSelected({f.items[3]});
    CHECK_FALSE(f.proxy.mapFromSource(source).isValid());

    identity.set("NI", "Peer1-returned");
    f.model.updateUser(f.items[1], identity, f.items[1]->getCID(), false);
    CHECK(f.proxy.mapFromSource(source).isValid());
    f.checkSelected({f.items[3]});
    f.select(f.items[1]);
    selected = f.proxy.mapFromSource(source);
    f.model.removeUser(f.items[1]->getUser());
    CHECK_FALSE(selected.isValid());
    CHECK_FALSE(source.isValid());
    CHECK(other.isValid());
    CHECK(f.selection.currentIndex() != selected);
    f.checkSelected({f.items[3]});
    f.model.clear();
    CHECK_FALSE(other.isValid());
    CHECK_FALSE(f.selection.currentIndex().isValid());
}

TEST_CASE("Generated large hub updates preserve selected identities", "[.][hub-scale]")
{
    HubSelectionFixture f;
    for (int count : {1000, 10000}) {
        f.model.clear();
        f.items.clear();
        f.model.sort(-1);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < count; ++i) {
            UserPtr user(new User(CID::generate()));
            Identity identity(user, i + 1);
            identity.set("NI", QString("Peer%1").arg(i, 5, 10, QLatin1Char('0')).toStdString());
            identity.setBytesShared(std::to_string(i * 1024));
            f.items << f.model.addUser(user, identity,
                                      QString::fromStdString(user->getCID().toBase32()), false);
        }
        const double loadMs = timer.nsecsElapsed() / 1e6;
        f.model.sort(COLUMN_SHARE, Qt::DescendingOrder);
        QList<UserListItem*> selected;
        QList<QPersistentModelIndex> sourceSaved, proxySaved;
        for (int i = 0; i < count; i += count / 20) {
            selected << f.items[i];
            f.select(f.items[i]);
            sourceSaved << f.source(f.items[i]);
            proxySaved << f.proxy.mapFromSource(f.source(f.items[i]));
        }
        QSignalSpy reset(&f.model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&f.model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&f.model, &QAbstractItemModel::rowsInserted);
        QSignalSpy layout(&f.model, &QAbstractItemModel::layoutChanged);
        QSignalSpy changed(&f.model, &QAbstractItemModel::dataChanged);
        timer.restart();
        f.proxy.sort(COLUMN_NICK, Qt::AscendingOrder);
        const double sortMs = timer.nsecsElapsed() / 1e6;
        double maxUpdateMs = 0;
        timer.restart();
        for (int i = 0; i != 200; ++i) {
            auto* item = f.items[(i * 97) % count];
            Identity identity(item->getIdentity());
            identity.set("NI", "Updated" + std::to_string(i));
            QElapsedTimer update;
            update.start();
            f.model.updateUser(item, identity, item->getCID(), false);
            maxUpdateMs = std::max(maxUpdateMs, update.nsecsElapsed() / 1e6);
        }
        const double updatesMs = timer.nsecsElapsed() / 1e6;
        CHECK(f.model.rowCount() == count);
        CHECK(f.proxy.rowCount() == count);
        for (int i = 0; i != 200; ++i)
            CHECK(f.items[(i * 97) % count]->getNick() == QString("Updated%1").arg(i));
        for (int row = 1; row < count; ++row)
            CHECK(QString::localeAwareCompare(f.model.index(row - 1, COLUMN_NICK).data().toString(),
                                              f.model.index(row, COLUMN_NICK).data().toString()) <= 0);
        f.checkSelected(selected);
        for (int i = 0; i < selected.size(); ++i) {
            CHECK(sourceSaved[i] == f.source(selected[i]));
            CHECK(f.proxy.mapToSource(proxySaved[i]) == f.source(selected[i]));
        }
        CHECK(f.proxy.mapToSource(f.selection.currentIndex()) == f.source(selected.back()));
        CHECK(reset.count() == 0);
        CHECK(removed.count() == 0);
        CHECK(inserted.count() == 0);
        CHECK(layout.count() == 201);
        CHECK(changed.count() == 200);
        std::cout << "hub-scale users=" << count << " selected=" << selected.size()
                  << " updates=200 load_ms=" << loadMs << " sort_ms=" << sortMs
                  << " updates_ms=" << updatesMs << " max_update_ms=" << maxUpdateMs << '\n';
    }
}

#ifdef __APPLE__
#include "tests/macos/updater/RemovalProbe.h"
TEST_CASE("Held undelivered cleanup reaches finite refusal before core retirement", "[.][cleanup-probe-session]") {
    IsolatedMainWindow f; QTemporaryDir staging; QuitBackend backend(*f.core);
    backend.transport = updates::UpdateTransportTestAccess::create(*f.core, [](const auto&, const auto&, const auto& sink) {
        const uint8_t byte = 'x'; sink(&byte, 1);
    });
    const auto route = f.core->getProxyRoute()->snapshot(); const auto path = staging.filePath("undelivered");
    backend.transport->fetch(QUrl("https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/arm64.xml"),
        updates::ResourceKind::Feed, path, route);
    QElapsedTimer elapsed; elapsed.start();
    while(!QFileInfo::exists(path) && elapsed.elapsed() < 3000) QThread::msleep(1);
    REQUIRE(QFileInfo::exists(path)); QThread::msleep(20);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    QSignalSpy refused(&session, &updates::ApplicationQuitSession::refused);
    RemovalProbe probe; probe.start(path, 4); auto watchdog = probe.watchdog();
    int beats = 0; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++beats; }); heartbeat.start(1);
    elapsed.restart(); session.request(50); probe.trace("session-request", elapsed.elapsed());
    CHECK(elapsed.elapsed() < 40);
    CHECK(session.state() == updates::ApplicationQuitSession::State::Preparing);
    CHECK_FALSE(route->revoked->load()); CHECK(f.core->isRunning());
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused || !ready.isEmpty(); }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(refused.count() == 1); CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    CHECK_FALSE(route->revoked->load()); CHECK(backend.transport->quitWorkersPending());
    REQUIRE(watchdog.get()); QTest::qWait(30);
    CHECK(ready.isEmpty()); CHECK_FALSE(route->revoked->load()); CHECK(backend.installs == 0);
    session.request(1000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(route->revoked->load()); CHECK_FALSE(QFile::exists(path));
}
#endif

#ifdef __APPLE__
TEST_CASE("Auxiliary checkpoint syscall hold preserves event-loop progress and refuses late exit", "[.auxiliary-write-retirement][mainwindow]") {
    IsolatedMainWindow fixture;
    int mode = 1;
    QString file = "searchblacklist";
    SECTION("search blacklist write") {}
    SECTION("search blacklist flush") { mode = 3; }
    SECTION("anti-spam write") { file = "antispam"; }
    SECTION("anti-spam flush") { file = "antispam"; mode = 3; }
    SECTION("physical worker thread teardown") {
        if constexpr(!hasAsyncAuxiliaryPersistence<ApplicationQuitCoordinator>())
            FAIL("No asynchronous auxiliary persistence receipt");
        mode = 6;
    }
    if(file == "searchblacklist") {
        fixture.gui->createSearchBlacklist();
        fixture.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"frozen-rule"});
    } else {
        fixture.gui->createAntiSpam();
        fixture.gui->antiSpam()->setPhrase("initialize");
        fixture.gui->antiSpam()->setPhrase("frozen-question");
    }
    REQUIRE(waitFor([&] { return fixture.gui->mainWindow()->stopWorkersForQuit(); }));
    dcpp::DCContext::ShutdownResult coreResult{};
    REQUIRE(waitFor([&] {
        coreResult = fixture.core->prepareShutdownAsyncChecked(3000);
        return coreResult.status != dcpp::DCContext::ShutdownStatus::Pending;
    }));
    REQUIRE(coreResult.status == dcpp::DCContext::ShutdownStatus::Ready);
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    updates::ApplicationQuitSession session(*fixture.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; });
    heartbeat.start(1);
    probe.arm(QFile::encodeName(fixture.home.filePath(file)).constData(), mode);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start();
    session.request(100);
    const auto duration = elapsed.elapsed();
    CHECK(duration < 100);
    REQUIRE(waitFor([&] { return probe.entered(); }));
    CHECK(probe.writer() != probe.gui);
    if(file == "searchblacklist")
        fixture.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"late-rule"});
    else fixture.gui->antiSpam()->setPhrase("late-question");
    CHECK(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    REQUIRE(watchdog.get()); CHECK(advanced.load());
    QTest::qWait(30);
    CHECK(ready.isEmpty());
    if(session.state() == updates::ApplicationQuitSession::State::Refused) {
        session.request(3000);
        REQUIRE(waitFor([&] { return ready.count() == 1; }));
        CHECK(backend.installs == 0); CHECK(backend.fences == 1);
        QFile saved(fixture.home.filePath(file)); REQUIRE(saved.open(QIODevice::ReadOnly));
        CHECK(saved.readAll() == (file == "searchblacklist" ? QByteArray("NAME:frozen-rule\n") :
            QByteArray("|ANTISPAM_PHRASE|frozen-question\n|ANTISPAM_KEY|\n|ATTEMPTS|0\n")));
    }
    std::fprintf(stderr, "AUXILIARY_RETIREMENT mode=%d file=%s request_ms=%lld on_gui=%d heartbeat=%d exits=%d\n",
        mode, file.toUtf8().constData(), static_cast<long long>(duration),
        probe.writer() == probe.gui, advanced.load(), int(ready.count()));
}

TEST_CASE("Auxiliary worker launch failure permits only frozen checkpoint retry", "[.auxiliary-write-retirement][mainwindow]") {
    if constexpr(!hasAsyncAuxiliaryPersistence<ApplicationQuitCoordinator>())
        FAIL("No asynchronous auxiliary persistence receipt");
    IsolatedMainWindow fixture;
    fixture.gui->createSearchBlacklist();
    fixture.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"retained-rule"});
    REQUIRE(waitFor([&] { return fixture.gui->mainWindow()->stopWorkersForQuit(); }));
    REQUIRE(waitFor([&] {
        return fixture.core->prepareShutdownAsyncChecked(3000).status != dcpp::DCContext::ShutdownStatus::Pending;
    }));
    updates::ApplicationQuitSession session(*fixture.gui);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    CoreWriteProbe probe; probe.arm("", 7);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(probe.entered()); CHECK(ready.isEmpty());
    fixture.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"late-rule"});
    probe.release();
    session.request(3000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    QFile saved(fixture.home.filePath("searchblacklist")); REQUIRE(saved.open(QIODevice::ReadOnly));
    CHECK(saved.readAll() == "NAME:retained-rule\n");
}

TEST_CASE("Held checkpoint recovery retains its worker and permanently disarmed update", "[.auxiliary-write-retirement][mainwindow]") {
    IsolatedMainWindow fixture;
    fixture.gui->createSearchBlacklist(); fixture.gui->createAntiSpam();
    fixture.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"original-rule"});
    fixture.gui->antiSpam()->setPhrase("initialize");
    fixture.gui->antiSpam()->setPhrase("original-question");
    const auto blocked = fixture.home.filePath("searchblacklist");
    REQUIRE(QDir().mkpath(blocked));
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const updates::UpdateOffer offer{2, "3.0.0-pre.2", "fixture", "org.test.eiskalt", "arm64",
        QVersionNumber(26, 0), updates::Channel::Preview, 100, QByteArray(32, 'a')};
    updater.checkNow(); emit backend.available(backend.operation, offer);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Available; }));
    updater.download(); emit backend.downloadReady(backend.operation, offer.build, offer.sha256);
    REQUIRE(waitFor([&] { return updater.state() == updates::UpdateController::State::Ready; }));
    updater.installOnNextQuit();
    updates::ApplicationQuitSession session(*fixture.gui, &updater);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    session.request(3000);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    fixture.gui->searchBlacklist()->setList(SearchBlacklist::NAME, {"late-rule"});
    fixture.gui->antiSpam()->setPhrase("late-question");
    // Mark already-receipted saves to detect any physical replay during recovery.
    const auto marker = QByteArray("already-saved-receipt\n");
    const QStringList cached{"antispam", "DCPlusPlus.xml", "EiskaltDC++_Qt.conf"};
    for(const auto& name : cached) {
        QFile file(fixture.home.filePath(name)); REQUIRE(file.exists());
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(marker) == marker.size());
    }
    REQUIRE(QDir(blocked).removeRecursively());
    CoreWriteProbe probe;
    std::atomic_int ticks{0}; std::atomic_bool advanced{false};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { ++ticks; }); heartbeat.start(1);
    probe.arm(QFile::encodeName(blocked).constData(), 1);
    auto watchdog = probe.watchdog(ticks, advanced);
    QElapsedTimer elapsed; elapsed.start(); session.request(100);
    CHECK(elapsed.elapsed() < 100);
    REQUIRE(waitFor([&] { return probe.entered(); })); CHECK(probe.writer() != probe.gui);
    REQUIRE(waitFor([&] { return session.state() != updates::ApplicationQuitSession::State::Preparing; }));
    CHECK(session.state() == updates::ApplicationQuitSession::State::Refused);
    CHECK(ready.isEmpty()); REQUIRE(watchdog.get()); CHECK(advanced.load());
    QTest::qWait(30); CHECK(ready.isEmpty()); CHECK(backend.installs == 0);
    {
        QFile saved(blocked); REQUIRE(saved.open(QIODevice::ReadOnly));
        CHECK(saved.readAll() == "NAME:original-rule\n"); saved.close();
        REQUIRE(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(saved.write(marker) == marker.size());
    }
    if(session.state() == updates::ApplicationQuitSession::State::Refused) {
        session.request(3000); REQUIRE(waitFor([&] { return ready.count() == 1; }));
        CHECK(backend.fences == 1); CHECK(backend.installs == 0);
        CHECK(updater.quitDisposition() == updates::UpdateController::QuitDisposition::NoUpdate);
        QFile saved(blocked); REQUIRE(saved.open(QIODevice::ReadOnly));
        CHECK(saved.readAll() == marker);
        for(const auto& name : cached) {
            QFile file(fixture.home.filePath(name)); REQUIRE(file.open(QIODevice::ReadOnly));
            CHECK(file.readAll() == marker);
        }
    }
}
#endif

TEST_CASE("Compact hub layout keeps a resizable composer and actionable connection failure", "[qt][compact-native][hub-layout]") {
    IsolatedMainWindow f;
    auto *window = f.gui->mainWindow();
    window->resize(1186, 800);
    window->show();
    window->newHubFrame("adc://127.0.0.1:1", "");
    QPointer<HubFrame> hub = window->findChild<HubFrame*>();
    REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if (hub) hub->close(); });
    QCoreApplication::processEvents();
    auto *split = hub->findChild<QSplitter*>("splitter_CHAT_INPUT");
    REQUIRE(split);
    REQUIRE(split->sizes().size() == 2);
    CHECK(split->sizes().last() <= 150);
    CHECK(split->handleWidth() >= 4);
    hub->coreConnecting(QStringLiteral("Fixture connection attempt"));
    QCoreApplication::processEvents();
    auto *banner = hub->findChild<QFrame*>("hubConnectionFailure");
    REQUIRE(banner);
    CHECK(banner->isHidden());
    hub->coreStatusMsg(QStringLiteral("Authentication rejected by test fixture"));
    hub->coreFailed();
    QCoreApplication::processEvents();
    CHECK_FALSE(banner->isHidden());
    auto *message = banner->findChild<QLabel*>("hubConnectionFailureMessage");
    REQUIRE(message);
    CHECK(message->text().contains("Authentication rejected"));
    CHECK(message->textFormat() == Qt::PlainText);
    REQUIRE(banner->findChild<QPushButton*>("hubConnectionRetry"));
    REQUIRE(banner->findChild<QPushButton*>("hubConnectionSettings"));
    hub->coreConnecting(QStringLiteral("Reconnecting"));
    QCoreApplication::processEvents();
    CHECK(banner->isHidden());
}

TEST_CASE("Hub authentication recovery opens the current favourite editor", "[qt][hub-auth-recovery]") {
    IsolatedMainWindow f;
    auto *window = f.gui->mainWindow();
    window->newHubFrame("adc://127.0.0.1:1", "");
    QPointer<HubFrame> hub = window->findChild<HubFrame*>();
    REQUIRE(hub);
    auto cleanup = dcpp::makeScopedFunctor([&] { if (hub) hub->close(); });
    QCoreApplication::processEvents();
    CHECK(QMetaObject::invokeMethod(hub, "coreAuthenticationFailed", Qt::DirectConnection));
    hub->coreFailed();
    QCoreApplication::processEvents();
    auto *button = hub->findChild<QPushButton*>("hubConnectionSettings");
    REQUIRE(button);
    bool correctEditor = false;
    QTimer modalCloser;
    modalCloser.setInterval(5);
    QObject::connect(&modalCloser, &QTimer::timeout, window, [&] {
        if (auto *editor = qobject_cast<FavoriteHubEditor*>(QApplication::activeModalWidget())) {
            correctEditor = editor->lineEdit_ADDRESS->text() == "adc://127.0.0.1:1" &&
                !editor->checkBox_LEGACY_ADC_PASSWORD->isChecked();
            modalCloser.stop();
            editor->reject();
        } else if (auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modalCloser.stop();
            dialog->reject();
        }
    });
    modalCloser.start();
    button->click();
    QCoreApplication::processEvents();
    CHECK(correctEditor);
}

TEST_CASE("Status icon Off is inline and preserves the selected icon while toggling", "[qt][status-icon-off]")
{
    IsolatedMainWindow fixture;
    auto *settings = fixture.gui->settings();
    for (bool enabled : {false, true}) {
        for (bool monochrome : {false, true}) {
            CAPTURE(enabled, monochrome);
            settings->setBool(WB_TRAY_ENABLED, enabled);
            settings->setBool(WB_TRAY_ICON_MONOCHROME, monochrome);
            SettingsGUI page;
            auto *group = child<QGroupBox>(&page, "groupBox_TRAY");
            CHECK_FALSE(group->isCheckable());
            CHECK(group->title() == QStringLiteral("Status icon"));
            CHECK(group->isEnabled() == QSystemTrayIcon::isSystemTrayAvailable());
            auto *off = child<QCheckBox>(group, "checkBox_TRAY_OFF");
            auto *colored = child<QRadioButton>(group, "radioButton_TRAY_COLORED");
            auto *mono = child<QRadioButton>(group, "radioButton_TRAY_MONOCHROME");
            CHECK(off->text() == QStringLiteral("Off"));
            CHECK(off->isChecked() == !enabled);
            CHECK(mono->isChecked() == monochrome);
            CHECK(colored->isChecked() == !monochrome);
            CHECK(mono->isEnabledTo(group) == enabled);
            CHECK(colored->isEnabledTo(group) == enabled);
            // Exercise local Off behavior independently of this host's tray support.
            group->setEnabled(true);
            page.show();
            QApplication::processEvents();
            CHECK(off->geometry().bottom() < colored->geometry().top());
            CHECK(off->geometry().left() == colored->geometry().left());
            off->click();
            CHECK(mono->isEnabled() == !enabled);
            CHECK(colored->isEnabled() == !enabled);
            CHECK(mono->isChecked() == monochrome);
            off->click();
            CHECK(mono->isEnabled() == enabled);
            CHECK(colored->isEnabled() == enabled);
            CHECK(mono->isChecked() == monochrome);
            CHECK(settings->getBool(WB_TRAY_ENABLED) == enabled);
            CHECK(settings->getBool(WB_TRAY_ICON_MONOCHROME) == monochrome);
        }
    }
}

TEST_CASE("Status icon Off saves inverted enablement and reopens with its icon choice", "[qt][status-icon-off]")
{
    IsolatedMainWindow fixture;
    auto *settings = fixture.gui->settings();
    for (bool enabled : {false, true}) {
        for (bool monochrome : {false, true}) {
            CAPTURE(enabled, monochrome);
            settings->setBool(WB_TRAY_ENABLED, !enabled);
            settings->setBool(WB_TRAY_ICON_MONOCHROME, !monochrome);
            {
                SettingsGUI page;
                child<QCheckBox>(&page, "checkBox_TRAY_OFF")->setChecked(!enabled);
                child<QRadioButton>(&page, monochrome ? "radioButton_TRAY_MONOCHROME"
                                                      : "radioButton_TRAY_COLORED")->setChecked(true);
                page.ok();
            }
            CHECK(settings->getBool(WB_TRAY_ENABLED) == enabled);
            CHECK(settings->getBool(WB_TRAY_ICON_MONOCHROME) == monochrome);
            REQUIRE(settings->saveChecked());
            QSettings persisted(fixture.home.filePath("EiskaltDC++_Qt.conf"), QSettings::IniFormat);
            CHECK(persisted.value(WB_TRAY_ENABLED).toBool() == enabled);
            CHECK(persisted.value(WB_TRAY_ICON_MONOCHROME).toBool() == monochrome);
            SettingsGUI reopened;
            CHECK(child<QCheckBox>(&reopened, "checkBox_TRAY_OFF")->isChecked() == !enabled);
            CHECK(child<QRadioButton>(&reopened, "radioButton_TRAY_MONOCHROME")->isChecked() == monochrome);
        }
    }
}

TEST_CASE("Status icon Off changes are discarded by Cancel and dialog rejection", "[qt][status-icon-off]")
{
    IsolatedMainWindow fixture;
    auto *settings = fixture.gui->settings();
    for (bool cancelButton : {false, true}) {
        for (bool enabled : {false, true}) {
            CAPTURE(cancelButton, enabled);
            settings->setBool(WB_TRAY_ENABLED, enabled);
            settings->setBool(WB_TRAY_ICON_MONOCHROME, true);
            REQUIRE(settings->saveChecked());
            const QString path = fixture.home.filePath("EiskaltDC++_Qt.conf");
            QFile before(path);
            REQUIRE(before.open(QIODevice::ReadOnly));
            const QByteArray saved = before.readAll();
            before.close();
            {
                ::Settings dialog;
                dialog.navigate(::Settings::Page::GUI);
                auto *page = dialog.findChild<SettingsGUI *>();
                REQUIRE(page);
                child<QCheckBox>(page, "checkBox_TRAY_OFF")->setChecked(enabled);
                child<QRadioButton>(page, "radioButton_TRAY_COLORED")->setChecked(true);
                if (cancelButton)
                    child<QDialogButtonBox>(&dialog, "buttonBox")->button(QDialogButtonBox::Cancel)->click();
                else
                    dialog.reject();
                CHECK(dialog.result() == QDialog::Rejected);
            }
            CHECK(settings->getBool(WB_TRAY_ENABLED) == enabled);
            CHECK(settings->getBool(WB_TRAY_ICON_MONOCHROME));
            QFile after(path);
            REQUIRE(after.open(QIODevice::ReadOnly));
            CHECK(after.readAll() == saved);
        }
    }
}

TEST_CASE("Connection owns one unified proxy editor and compact grouped pages", "[qt][connection-refinement]")
{
    IsolatedMainWindow fixture;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    auto *connection = child<SettingsConnection>(&dialog, "UISettingsConnection");
    auto *tabs = child<QTabWidget>(connection, "tabWidget");
    REQUIRE(tabs->count() == 4);
    CHECK(tabs->tabText(0) == "Connection");
    CHECK(tabs->tabText(1) == "Proxy");
    CHECK(tabs->tabText(2) == "Limits");
    CHECK(tabs->tabText(3) == "Advanced");
    auto *profiles = child<QTabWidget>(connection, "connectionProxyProfiles");
    CHECK(profiles->tabText(0) == "Global");
#ifdef USE_TORRENT
    REQUIRE(profiles->count() == 3);
    CHECK(profiles->tabText(1) == "Torrents");
    CHECK(profiles->tabText(2) == "Hub lists");
    auto *torrent = dialog.findChild<SettingsTorrent *>();
    REQUIRE(torrent);
    auto *torrentTabs = torrent->findChild<QTabWidget *>();
    REQUIRE(torrentTabs);
    for (int row = 0; row < torrentTabs->count(); ++row)
        CHECK_FALSE(torrentTabs->tabText(row).contains("Proxy", Qt::CaseInsensitive));
    CHECK(torrent->findChild<QComboBox *>("proxyMode") == nullptr);
    CHECK(torrent->findChild<QPushButton *>("configureTorrentProxy") == nullptr);
    REQUIRE(dialog.findChildren<QComboBox *>("proxyMode").size() == 1);
    CHECK(profiles->widget(1)->isAncestorOf(child<QComboBox>(&dialog, "proxyMode")));
    child<QCheckBox>(torrent, "enabled")->setChecked(false);
    CHECK_FALSE(child<QComboBox>(&dialog, "proxyMode")->isEnabled());
    child<QCheckBox>(torrent, "enabled")->setChecked(true);
#else
    REQUIRE(profiles->count() == 2);
#endif
    CHECK(dialog.findChildren<QLineEdit *>("lineEdit_SIP").size() == 1);
    CHECK(profiles->widget(0)->isAncestorOf(child<QLineEdit>(&dialog, "lineEdit_SIP")));
    CHECK(profiles->widget(profiles->count() - 1)->isAncestorOf(child<QLineEdit>(&dialog, "hubListProxyHost")));
    for (int width : {980, 1186, 1600}) {
        dialog.resize(width, 820);
        dialog.show();
        tabs->setCurrentIndex(0);
        QApplication::processEvents();
        for (const char *name : {"spinBox_TCP", "spinBox_UDP", "spinBox_TLS"}) {
            auto *port = child<QSpinBox>(connection, name);
            CHECK(port->width() >= 100);
            CHECK(port->width() <= 130);
        }
        tabs->setCurrentIndex(3);
        QApplication::processEvents();
        for (const char *name : {"connectionBehaviorGroup", "connectionCountryGroup", "connectionHubListsGroup"})
            CHECK(child<QGroupBox>(connection, name)->isVisible());
    }
    dialog.reject();
}

TEST_CASE("Wrapped Connection TLS selector sizes to content and supports popup selection", "[qt][connection-refinement][connection-tls-popup]")
{
    IsolatedMainWindow fixture;
    const QPalette original = qApp->palette();
    auto restorePalette = dcpp::makeScopedFunctor([&] { qApp->setPalette(original); });
    for (bool dark : {false, true}) {
        CAPTURE(dark);
        QPalette palette = original;
        palette.setColor(QPalette::Window, dark ? QColor(35,35,35) : QColor(240,240,240));
        palette.setColor(QPalette::Base, dark ? QColor(28,28,28) : QColor(Qt::white));
        palette.setColor(QPalette::Text, dark ? QColor(235,235,235) : QColor(24,24,24));
        qApp->setPalette(palette);
        ::Settings dialog;
        dialog.navigate(::Settings::Page::Connection);
        auto *page = dialog.findChild<SettingsConnection *>(); REQUIRE(page);
        auto *tabs = child<QTabWidget>(page, "tabWidget");
        tabs->setCurrentIndex(tabs->count() - 1);
        auto *combo = child<QComboBox>(page, "comboBox_TLS");
        REQUIRE(combo->count() == 3);
        CHECK(QFile::exists(":/control-icons/combo-arrow-down-light.svg"));
        CHECK(QFile::exists(":/control-icons/combo-arrow-down-dark.svg"));
        for (int width : {980, 1600}) {
            dialog.resize(width, 900);
            dialog.show();
            QApplication::processEvents();
            CHECK(combo->width() <= combo->sizeHint().width() + 12);
            CHECK(combo->width() >= combo->fontMetrics().horizontalAdvance(combo->itemText(2)) + 24);
        }
        for (bool enabled : {false, true}) {
            combo->setEnabled(enabled);
            QApplication::processEvents();
            QStyleOptionComboBox option; option.initFrom(combo);
            option.currentText = combo->currentText();
            const QRect arrow = combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, combo);
            CHECK(combo->rect().contains(arrow));
            const QImage image = combo->grab().toImage();
            const qreal scale = image.devicePixelRatio();
            const QPoint center = arrow.center() * scale;
            const QRgb background = image.pixel(center.x(), qMax(0, center.y() - 6));
            int ink = 0;
            for (int y = center.y() - 3; y <= center.y() + 3; ++y)
                for (int x = center.x() - 4; x <= center.x() + 4; ++x)
                    if (image.valid(x,y) && image.pixel(x,y) != background) ++ink;
            CHECK(ink > 4);
        }
        combo->setItemText(2, "Require TLS for every encrypted client connection (translated label)");
        QApplication::processEvents();
        CHECK(combo->width() >= combo->fontMetrics().horizontalAdvance(combo->itemText(2)) + 24);
        combo->setCurrentIndex(0);
        QTest::mouseClick(combo, Qt::LeftButton, Qt::NoModifier, QPoint(combo->width() - 12, combo->height()/2));
        REQUIRE(waitFor([&] { return combo->view()->isVisible(); }));
        CHECK(combo->view()->verticalScrollBar()->maximum() == 0);
        for (int row = 0; row < 3; ++row)
            CHECK(combo->view()->viewport()->rect().contains(combo->view()->visualRect(combo->model()->index(row,0))));
        QTest::keyClick(combo->view(), Qt::Key_Escape);
        CHECK(combo->currentIndex() == 0);
        combo->showPopup();
        REQUIRE(waitFor([&] { return combo->view()->isVisible(); }));
        QTest::keyClick(combo->view(), Qt::Key_Down);
        QTest::keyClick(combo->view(), Qt::Key_Return);
        CHECK(combo->currentIndex() == 1);
        REQUIRE(waitFor([&] { return !combo->view()->isVisible(); }));
        const QPoint openPoint(combo->width() - 12, combo->height()/2);
        QSignalSpy activated(combo, &QComboBox::activated);
        QTest::mouseClick(combo, Qt::LeftButton, Qt::NoModifier, openPoint);
        REQUIRE(waitFor([&] { return combo->view()->isVisible(); }));
        auto *viewport = combo->view()->viewport();
        const QModelIndex targetIndex = combo->model()->index(2,0);
        const QPoint target = combo->view()->visualRect(targetIndex).center();
        REQUIRE(viewport->rect().contains(target));
        REQUIRE(combo->view()->indexAt(target) == targetIndex);
        const QPoint targetGlobal = viewport->mapToGlobal(target);
        REQUIRE((targetGlobal - combo->mapToGlobal(openPoint)).manhattanLength() > 9);
        // Qt's popup release guard ends on pointer movement >9px from opening.
        // Send that real event path explicitly: QTest's cursor warp is QPA-dependent.
        QMouseEvent move(QEvent::MouseMove, QPointF(target), QPointF(targetGlobal),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, target);
        REQUIRE(combo->view()->currentIndex() == targetIndex);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, target);
        REQUIRE(waitFor([&] { return !combo->view()->isVisible(); }));
        CHECK(combo->currentIndex() == 2);
        REQUIRE(activated.count() == 1);
        CHECK(activated.at(0).at(0).toInt() == 2);
        dialog.reject();
    }
}

TEST_CASE("Unified proxy page remains alive while Torrent cancels a pending diagnostic", "[qt][connection-refinement]")
{
#ifdef USE_TORRENT
    IsolatedMainWindow fixture;
    QTcpServer silent; silent.setProxy(QNetworkProxy::NoProxy);
    REQUIRE(silent.listen(QHostAddress::LocalHost));
    auto values = eiskalt::torrent::loadSettings(TorrentRuntime::settingsPath());
    values.enabled = true;
    values.proxyMode = eiskalt::torrent::ProxyMode::Custom;
    values.socks5Proxy.host = "127.0.0.1";
    values.socks5Proxy.port = silent.serverPort();
    REQUIRE(eiskalt::torrent::saveSettings(TorrentRuntime::settingsPath(), values));
    auto dialog = std::make_unique<::Settings>();
    auto *connection = dialog->findChild<SettingsConnection *>(); REQUIRE(connection);
    auto *profiles = child<QTabWidget>(connection, "connectionProxyProfiles");
    dialog->navigate(::Settings::Page::Connection, 1);
    profiles->setCurrentIndex(1);
    dialog->show(); QApplication::processEvents();
    auto *torrent = dialog->findChild<SettingsTorrent *>(); REQUIRE(torrent);
    QPointer<QComboBox> proxyMode = child<QComboBox>(dialog.get(), "proxyMode");
    bool aliveAtOwnerDestruction = false;
    QObject::connect(torrent, &QObject::destroyed, [&] { aliveAtOwnerDestruction = !proxyMode.isNull(); });
    child<QPushButton>(dialog.get(), "torrentProxyTest")->click();
    REQUIRE(waitFor([&] { return silent.hasPendingConnections(); }));
    std::unique_ptr<QTcpSocket> peer(silent.nextPendingConnection());
    REQUIRE(peer);
    dialog->reject();
    QElapsedTimer elapsed; elapsed.start();
    dialog.reset();
    CHECK(elapsed.elapsed() < 1000);
    CHECK(aliveAtOwnerDestruction);
    CHECK(proxyMode.isNull());
    QApplication::processEvents();
#endif
}

TEST_CASE("Connection refinement preserves TLS policy mapping and unified proxy cancellation", "[qt][connection-refinement]")
{
    IsolatedMainWindow fixture;
    auto &settings = *fixture.core->getSettingsManager();
    for (int policy : {0, 1, 2}) {
        ::Settings dialog;
        auto *page = dialog.findChild<SettingsConnection *>(); REQUIRE(page);
        REQUIRE(child<QTabWidget>(page, "tabWidget")->count() == 4);
        child<QComboBox>(page, "comboBox_TLS")->setCurrentIndex(policy);
        dialog.accept();
        REQUIRE(dialog.result() == QDialog::Accepted);
        CHECK(settings.getBool(dcpp::SettingsManager::USE_TLS) == (policy != 0));
        CHECK(settings.getBool(dcpp::SettingsManager::REQUIRE_TLS) == (policy == 2));
    }
    const auto savedProxy = settings.get(dcpp::SettingsManager::SOCKS_SERVER);
    const auto savedHubListProxy = settings.get(dcpp::SettingsManager::HTTP_PROXY);
#ifdef USE_TORRENT
    const auto savedTorrent = eiskalt::torrent::loadSettings(TorrentRuntime::settingsPath());
#endif
    {
        ::Settings dialog;
        child<QComboBox>(&dialog, "comboBox_TLS")->setCurrentIndex(0);
        child<QLineEdit>(&dialog, "lineEdit_SIP")->setText("discarded.invalid");
        child<QLineEdit>(&dialog, "hubListProxyHost")->setText("discarded-hublist.invalid");
#ifdef USE_TORRENT
        child<QComboBox>(&dialog, "proxyMode")->setCurrentIndex(3);
        child<QLineEdit>(&dialog, "torrentProxyHost")->setText("discarded-torrent.invalid");
#endif
        dialog.reject();
    }
    CHECK(settings.getBool(dcpp::SettingsManager::USE_TLS));
    CHECK(settings.getBool(dcpp::SettingsManager::REQUIRE_TLS));
    CHECK(settings.get(dcpp::SettingsManager::SOCKS_SERVER) == savedProxy);
    CHECK(settings.get(dcpp::SettingsManager::HTTP_PROXY) == savedHubListProxy);
#ifdef USE_TORRENT
    const auto after = eiskalt::torrent::loadSettings(TorrentRuntime::settingsPath());
    CHECK(after.proxyMode == savedTorrent.proxyMode);
    CHECK(after.socks5Proxy.host == savedTorrent.socks5Proxy.host);
#endif
}

TEST_CASE("Preferences splitter keeps content adjacent to the bounded sidebar", "[qt][connection-refinement][connection-geometry]")
{
    IsolatedMainWindow fixture;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Connection);
    auto *sidebar = child<QListWidget>(&dialog, "listWidget");
    auto *splitter = child<QSplitter>(&dialog, "splitter");
    auto *page = dialog.findChild<SettingsConnection *>(); REQUIRE(page);
    auto *rootTabs = child<QTabWidget>(page, "tabWidget");
    REQUIRE(splitter->count() == 2);
    const QSize iconSize = sidebar->iconSize();
    QList<bool> hadIcons;
    for (int row = 0; row < sidebar->count(); ++row)
        hadIcons.append(!sidebar->item(row)->icon().isNull());
    auto inspectGeometry = [&](const char *phase, int width) {
        CAPTURE(phase, width);
        dialog.resize(width, 900);
        dialog.show();
        dialog.layout()->activate();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::processEvents();
        const int sidebarRight = sidebar->mapTo(&dialog, QPoint(sidebar->width(), 0)).x();
        const int stackLeft = splitter->widget(1)->mapTo(&dialog, QPoint()).x();
        const int tabsLeft = rootTabs->mapTo(&dialog, QPoint()).x();
        const int contentLeft = child<QCheckBox>(page, "checkBox_AUTO_DETECT_CONNECTION")->mapTo(&dialog, QPoint()).x();
        WARN("geometry width=" << width << " actual=" << dialog.width()
             << " sidebarRight=" << sidebarRight << " stackLeft=" << stackLeft
             << " tabsLeft=" << tabsLeft << " contentLeft=" << contentLeft
             << " split0=" << splitter->sizes().at(0) << " split1=" << splitter->sizes().at(1));
        const int handleAllowance = qMax(4, splitter->handleWidth() + 2);
        CHECK(stackLeft - sidebarRight >= 0);
        CHECK(stackLeft - sidebarRight <= handleAllowance);
        CHECK(qAbs(splitter->sizes().at(0) - sidebar->width()) <= handleAllowance);
        CHECK(tabsLeft - stackLeft >= 0);
        CHECK(tabsLeft - stackLeft <= 16);
        CHECK(sidebar->minimumWidth() == sidebar->maximumWidth());
        CHECK(sidebar->width() == sidebar->minimumWidth());
        CHECK(sidebar->iconSize() == iconSize);
        REQUIRE(sidebar->count() == hadIcons.size());
        for (int row = 0; row < sidebar->count(); ++row)
            CHECK(!sidebar->item(row)->icon().isNull() == hadIcons.at(row));
    };
    for (int width : {980, 1600, 980})
        inspectGeometry("initial and resize", width);
    QPalette alternate = dialog.palette();
    const bool wasDark = alternate.color(QPalette::Window).lightness() < 128;
    alternate.setColor(QPalette::Window, wasDark ? QColor(240,240,240) : QColor(35,35,35));
    alternate.setColor(QPalette::Base, wasDark ? QColor(Qt::white) : QColor(28,28,28));
    alternate.setColor(QPalette::Text, wasDark ? QColor(24,24,24) : QColor(235,235,235));
    dialog.setPalette(alternate);
    for (int width : {980, 1600, 980})
        inspectGeometry("appearance changed", width);
    QFont larger = dialog.font();
    if (larger.pixelSize() > 0) {
        larger.setPixelSize(larger.pixelSize() + 2);
    } else {
        larger.setPointSizeF(qMax(1.0, larger.pointSizeF()) + 2.0);
    }
    dialog.setFont(larger);
    for (int width : {980, 1600, 980})
        inspectGeometry("font changed", width);
    dialog.reject();
}

TEST_CASE("Unified proxy forms save once and reopen with independent route values", "[qt][connection-refinement]")
{
    IsolatedMainWindow fixture;
    {
        ::Settings dialog;
        REQUIRE(child<QTabWidget>(&dialog, "connectionProxyProfiles")->count() >= 2);
        child<QRadioButton>(&dialog, "radioButton_SOCKS")->setChecked(true);
        child<QLineEdit>(&dialog, "lineEdit_SIP")->setText("127.0.0.1");
        child<QLineEdit>(&dialog, "lineEdit_SPORT")->setText("1081");
        child<QLineEdit>(&dialog, "lineEdit_SUSR")->setText("fixture-global");
        child<QLineEdit>(&dialog, "lineEdit_SPSWD")->setText("fixture-global-password");
        child<QLineEdit>(&dialog, "hubListProxyHost")->setText("127.0.0.1");
        child<QLineEdit>(&dialog, "hubListProxyPort")->setText("1082");
#ifdef USE_TORRENT
        auto *mode = child<QComboBox>(&dialog, "proxyMode");
        mode->setCurrentIndex(mode->findData(int(eiskalt::torrent::ProxyMode::Custom)));
        child<QLineEdit>(&dialog, "torrentProxyHost")->setText("127.0.0.1");
        child<QSpinBox>(&dialog, "torrentProxyPort")->setValue(1083);
#endif
        dialog.accept();
        REQUIRE(dialog.result() == QDialog::Accepted);
    }
    ::Settings reopened;
    CHECK(child<QRadioButton>(&reopened, "radioButton_SOCKS")->isChecked());
    CHECK(child<QLineEdit>(&reopened, "lineEdit_SPORT")->text() == "1081");
    CHECK(child<QLineEdit>(&reopened, "lineEdit_SUSR")->text() == "fixture-global");
    CHECK(child<QLineEdit>(&reopened, "lineEdit_SPSWD")->text() == "fixture-global-password");
    CHECK(child<QLineEdit>(&reopened, "hubListProxyPort")->text() == "1082");
#ifdef USE_TORRENT
    CHECK(child<QComboBox>(&reopened, "proxyMode")->currentData().toInt() == int(eiskalt::torrent::ProxyMode::Custom));
    CHECK(child<QSpinBox>(&reopened, "torrentProxyPort")->value() == 1083);
#endif
    reopened.reject();
}

TEST_CASE("Proxy validation reveals the owning nested profile before focus", "[qt][connection-refinement][connection-validation-focus]")
{
    IsolatedMainWindow fixture;
    ::Settings dialog;
    dialog.show();
    dialog.activateWindow();
    REQUIRE(waitFor([&] { return dialog.isActiveWindow(); }));
    dialog.navigate(::Settings::Page::Connection, 0);
    auto *host = child<QLineEdit>(&dialog, "hubListProxyHost");
    auto *port = child<QLineEdit>(&dialog, "hubListProxyPort");
    host->setText("127.0.0.1");
    port->clear();
    QTimer closeWarning;
    QObject::connect(&closeWarning, &QTimer::timeout, [] {
        if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            warning->accept();
    });
    closeWarning.start(10);
    auto *page = dialog.findChild<SettingsConnection *>(); REQUIRE(page);
    CHECK_FALSE(page->validate());
    closeWarning.stop();
    CHECK(child<QTabWidget>(page, "tabWidget")->currentIndex() == 1);
    auto *profiles = child<QTabWidget>(page, "connectionProxyProfiles");
    CHECK(profiles->currentIndex() == profiles->count() - 1);
    CHECK(port->isVisible());
    REQUIRE(waitFor([&] { return QApplication::activeModalWidget() == nullptr; }));
    CHECK(dialog.focusWidget() == port);
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        // No window manager restores activation after the offscreen warning closes.
        // Supply activation only; the production-selected focus child must survive.
        dialog.activateWindow();
        QApplication::setActiveWindow(&dialog);
    }
    CAPTURE(QGuiApplication::platformName().toStdString(), dialog.isActiveWindow(),
            QApplication::activeWindow() == &dialog, dialog.focusWidget() == port,
            QApplication::focusWidget() == port);
    REQUIRE(waitFor([&] { return dialog.isActiveWindow(); }));
    CHECK(dialog.focusWidget() == port);
    CHECK(waitFor([&] { return port->hasFocus(); }));
    dialog.reject();
}

TEST_CASE("Tray speed limits entry opens Connection Limits rather than Proxy", "[qt][connection-refinement][tray-speed-limits]")
{
    IsolatedMainWindow fixture;
    auto *window = fixture.gui->mainWindow();
    window->hide();
    bool sawPreferences = false;
    bool limitsSelected = false;
    bool limitsVisible = false;
    bool proxyHidden = false;
    QTimer modalCloser;
    modalCloser.setInterval(10);
    QObject::connect(&modalCloser, &QTimer::timeout, window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        if (auto *preferences = qobject_cast<::Settings *>(dialog)) {
            sawPreferences = true;
            if (auto *page = preferences->findChild<SettingsConnection *>()) {
                auto *tabs = page->findChild<QTabWidget *>("tabWidget");
                auto *limits = page->findChild<QCheckBox *>("checkBox_THROTTLE_ENABLE");
                auto *proxy = page->findChild<QTabWidget *>("connectionProxyProfiles");
                limitsSelected = tabs && limits && tabs->currentWidget()->isAncestorOf(limits);
                limitsVisible = limits && limits->isVisible();
                proxyHidden = proxy && !proxy->isVisible();
            }
        }
        modalCloser.stop();
        dialog->reject();
    });
    modalCloser.start();
    REQUIRE(QMetaObject::invokeMethod(fixture.gui->notification(), "slotShowSpeedLimits", Qt::DirectConnection));
    CHECK(sawPreferences);
    CHECK(window->isVisible());
    CHECK(limitsSelected);
    CHECK(limitsVisible);
    CHECK(proxyHidden);
}

#ifdef Q_OS_MAC
namespace {
QStyleOptionGroupBox framingGroupOption(QGroupBox *group)
{
    QStyleOptionGroupBox option;
    option.initFrom(group);
    option.text = group->title();
    option.textAlignment = group->alignment();
    option.lineWidth = 1;
    option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel;
    if (group->isCheckable()) {
        option.subControls |= QStyle::SC_GroupBoxCheckBox;
        option.state |= group->isChecked() ? QStyle::State_On : QStyle::State_Off;
    }
    if (group->isFlat()) option.features |= QStyleOptionFrame::Flat;
    return option;
}

int framingColorDistance(QRgb a, QRgb b)
{
    return qMax(qAbs(qAlpha(a) - qAlpha(b)), qMax(qAbs(qRed(a) - qRed(b)),
        qMax(qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b)))));
}

// Paint through the live widget's style: frameShape alone cannot detect a
// stylesheet border on an otherwise NoFrame wrapper or tab pane.
QImage framingStyleSurface(QWidget *widget, bool tabPane)
{
    QImage image(240, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    if (tabPane) {
        QStyleOptionTabWidgetFrame option;
        option.initFrom(widget);
        option.rect = image.rect();
        option.shape = QTabBar::RoundedNorth;
        widget->style()->drawPrimitive(QStyle::PE_FrameTabWidget, &option, &painter, widget);
    } else {
        QStyleOptionFrame option;
        option.initFrom(widget);
        option.rect = image.rect();
        const auto *frame = qobject_cast<QFrame*>(widget);
        option.frameShape = frame ? frame->frameShape() : QFrame::NoFrame;
        option.lineWidth = frame ? frame->lineWidth() : 0;
        // QStyleSheetStyle paints CSS backgrounds/borders in PE_Widget;
        // CE_ShapedFrame alone intentionally omits those stylesheet borders.
        widget->style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, widget);
        widget->style()->drawControl(QStyle::CE_ShapedFrame, &option, &painter, widget);
    }
    painter.end();
    return image;
}

int framingEdgeContrast(const QImage &image)
{
    int contrast = 0;
    for (int x : {40, 120, 200}) {
        for (int edge : {0, 1}) {
            contrast = qMax(contrast, framingColorDistance(image.pixel(x, edge), image.pixel(x, 6)));
            contrast = qMax(contrast, framingColorDistance(image.pixel(x, image.height() - 1 - edge),
                                                          image.pixel(x, image.height() - 7)));
        }
    }
    for (int y : {25, 50, 75}) {
        for (int edge : {0, 1}) {
            contrast = qMax(contrast, framingColorDistance(image.pixel(edge, y), image.pixel(6, y)));
            contrast = qMax(contrast, framingColorDistance(image.pixel(image.width() - 1 - edge, y),
                                                          image.pixel(image.width() - 7, y)));
        }
    }
    return contrast;
}

void framingAppearance(::Settings &dialog, bool dark, bool larger)
{
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, dark ? QColor(35, 35, 35) : QColor(240, 240, 240));
    palette.setColor(QPalette::Base, dark ? QColor(28, 28, 28) : QColor(Qt::white));
    palette.setColor(QPalette::AlternateBase, dark ? QColor(42, 42, 42) : QColor(232, 232, 232));
    palette.setColor(QPalette::Text, dark ? QColor(235, 235, 235) : QColor(24, 24, 24));
    palette.setColor(QPalette::WindowText, palette.color(QPalette::Text));
    qApp->setPalette(palette);
    dialog.setPalette(palette);
    QFont font = qApp->font();
    if (larger) {
        if (font.pixelSize() > 0) font.setPixelSize(font.pixelSize() + 3);
        else font.setPointSizeF(qMax(1.0, font.pointSizeF()) + 3.0);
    }
    dialog.setFont(font);
    dialog.resize(1186, 1000);
    dialog.show();
    dialog.layout()->activate();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
}
}

TEST_CASE("Preferences outer wrappers and nested panes do not add repeated frames",
          "[qt][preferences-framing][cocoa-pixel-probe]")
{
    IsolatedMainWindow fixture;
    const auto originalPalette = qApp->palette();
    auto restorePalette = dcpp::makeScopedFunctor([&] { qApp->setPalette(originalPalette); });
    struct Destination { ::Settings::Page page; const char *root; int tab; };
    const Destination destinations[] = {
        {::Settings::Page::Logs, "UISettingsLog", -1},
        {::Settings::Page::Notifications, "UISettingsNotification", 1},
        {::Settings::Page::Downloads, "UISettingsDownloads", 1},
        {::Settings::Page::Connection, "UISettingsConnection", 1},
        {::Settings::Page::Connection, "UISettingsConnection", 3}
    };
    for (bool dark : {false, true}) for (bool larger : {false, true}) {
        CAPTURE(dark, larger);
        ::Settings dialog;
        framingAppearance(dialog, dark, larger);
        const auto panels = dialog.findChildren<QFrame*>("settingsPagePanel");
        REQUIRE_FALSE(panels.isEmpty());
        for (auto *panel : panels) {
            CHECK(panel->frameShape() == QFrame::NoFrame);
            CHECK(framingEdgeContrast(framingStyleSurface(panel, false)) <= 8);
        }
        for (const auto &destination : destinations) {
            CAPTURE(destination.root, destination.tab);
            dialog.navigate(destination.page, destination.tab);
            QApplication::processEvents();
            auto *page = child<QWidget>(&dialog, destination.root);
            if (destination.page == ::Settings::Page::Connection) {
                auto *rootTabs = child<QTabWidget>(page, "tabWidget");
                auto *group = child<QGroupBox>(page, destination.tab == 3
                    ? "connectionBehaviorGroup" : "groupBox_2");
                const int inset = group->mapTo(&dialog, QPoint()).x()
                    - rootTabs->currentWidget()->mapTo(&dialog, QPoint()).x();
                CAPTURE(inset);
                CHECK(inset >= 0);
                CHECK(inset <= 24);
            }
            for (auto *scroll : page->findChildren<QScrollArea*>())
                CHECK(scroll->frameShape() == QFrame::NoFrame);
            for (auto *tabs : page->findChildren<QTabWidget*>()) {
                CAPTURE(tabs->objectName().toStdString());
                if (tabs->objectName() == "connectionProxyProfiles")
                    dialog.navigate(::Settings::Page::Connection, 1);
                // Exercise each Global/Torrents/Hub lists pane, not just the
                // currently selected root Connection page.
                for (int index = 0; index < tabs->count(); ++index) {
                    tabs->setCurrentIndex(index);
                    QApplication::processEvents();
                    CHECK(framingEdgeContrast(framingStyleSurface(tabs, true)) <= 8);
                }
            }
        }
        dialog.navigate(::Settings::Page::Downloads, 1);
        auto *downloads = child<QWidget>(&dialog, "UISettingsDownloads");
        auto *queue = child<QListWidget>(downloads, "listWidget");
        CHECK(queue->frameShape() == QFrame::NoFrame);
        CHECK(framingEdgeContrast(framingStyleSurface(queue, false)) <= 8);
        // A scoped borderless inner-list rule must not erase the popup's
        // separate surface boundary. No new production property/API is needed.
        dialog.navigate(::Settings::Page::Connection, 3);
        auto *tls = child<QComboBox>(&dialog, "comboBox_TLS");
        tls->view()->ensurePolished();
        auto *popupView = tls->view();
        auto *popupWindow = popupView->window();
        QStringList popupDiagnostics;
        popupDiagnostics << QStringLiteral("TLS popup policy=%1 viewWindowIsDialog=%2 outerContainsView=%3")
            .arg(tls->style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, tls))
            .arg(popupWindow == &dialog)
            .arg(popupWindow != popupView && popupWindow->isAncestorOf(popupView));
        int depth = 0;
        for (auto *node = static_cast<QWidget*>(popupView); node && depth < 12;
             node = node->parentWidget(), ++depth) {
            const auto *frame = qobject_cast<QFrame*>(node);
            const QString stylesheet = node->styleSheet();
            popupDiagnostics << QStringLiteral(
                "ancestor[%1] class=%2 name=%3 isPopupWindow=%4 isWindow=%5 windowType=%6 "
                "visible=%7 settingsContent=%8 settingsInnerView=%9")
                .arg(depth).arg(QString::fromLatin1(node->metaObject()->className())).arg(node->objectName())
                .arg(node == popupWindow).arg(node->isWindow()).arg(int(node->windowType()))
                .arg(node->isVisible()).arg(node->property("settingsContent").toBool())
                .arg(node->property("settingsInnerView").toBool());
            popupDiagnostics << QStringLiteral("geometry=%1,%2 %3x%4 style=%5 stylesheet=%6")
                .arg(node->x()).arg(node->y()).arg(node->width()).arg(node->height())
                .arg(QString::fromLatin1(node->style()->metaObject()->className()))
                .arg(stylesheet);
            if (frame) {
                popupDiagnostics << QStringLiteral(
                    "frameShape=%1 shadow=%2 frameWidth=%3 lineWidth=%4 midLineWidth=%5 CE_ShapedFrameContrast=%6")
                    .arg(int(frame->frameShape())).arg(int(frame->frameShadow())).arg(frame->frameWidth())
                    .arg(frame->lineWidth()).arg(frame->midLineWidth())
                    .arg(framingEdgeContrast(framingStyleSurface(node, false)));
            }
            if (node == &dialog) break;
        }
        INFO(popupDiagnostics.join(QLatin1Char('\n')).toStdString());
        CHECK(framingEdgeContrast(framingStyleSurface(tls->view(), false)) > 8);
        dialog.reject();
    }
}

TEST_CASE("Preferences static and checkable group headers sit above frames without background patches",
          "[qt][preferences-framing][cocoa-pixel-probe]")
{
    IsolatedMainWindow fixture;
    const auto originalPalette = qApp->palette();
    auto restorePalette = dcpp::makeScopedFunctor([&] { qApp->setPalette(originalPalette); });
    // QStyleSheetStyle's ParentStyle is QWindowsStyle. Use its public factory
    // rather than importing private Qt headers into the test target.
    std::unique_ptr<QStyle> titleParentStyle(QStyleFactory::create(QStringLiteral("Windows")));
    REQUIRE(titleParentStyle);
    struct Header { ::Settings::Page page; const char *root; int tab; const char *group; };
    const Header headers[] = {
        {::Settings::Page::Logs, "UISettingsLog", -1, "groupBox_PM"},
        {::Settings::Page::Notifications, "UISettingsNotification", 1, "groupBox_SND"},
        {::Settings::Page::Notifications, "UISettingsNotification", 1, "groupBox_PM"},
        {::Settings::Page::Downloads, "UISettingsDownloads", 1, "groupBox_4"},
        {::Settings::Page::Connection, "UISettingsConnection", 1, "groupBox_2"},
        {::Settings::Page::Connection, "UISettingsConnection", 3, "groupBox_7"}
    };
    for (bool dark : {false, true}) for (bool larger : {false, true}) {
        CAPTURE(dark, larger);
        ::Settings dialog;
        framingAppearance(dialog, dark, larger);
        for (const auto &header : headers) {
            CAPTURE(header.root, header.group);
            dialog.navigate(header.page, header.tab);
            auto *page = child<QWidget>(&dialog, header.root);
            auto *group = child<QGroupBox>(page, header.group);
            group->clearFocus();
            QApplication::processEvents();
            const auto option = framingGroupOption(group);
            const auto *style = group->style();
            const QRect frame = style->subControlRect(QStyle::CC_GroupBox, &option,
                                                       QStyle::SC_GroupBoxFrame, group);
            const QRect label = style->subControlRect(QStyle::CC_GroupBox, &option,
                                                       QStyle::SC_GroupBoxLabel, group);
            const QRect checkbox = style->subControlRect(QStyle::CC_GroupBox, &option,
                                                          QStyle::SC_GroupBoxCheckBox, group);
            CAPTURE(frame, label, checkbox);
            INFO("frame top=" << frame.top() << " label=" << label.x() << "," << label.y()
                 << "," << label.width() << "," << label.height()
                 << " checkbox=" << checkbox.x() << "," << checkbox.y()
                 << "," << checkbox.width() << "," << checkbox.height());
            CHECK(group->rect().contains(label));
            CHECK(label.bottom() < frame.top());
            if (group->isCheckable()) {
                CHECK(group->rect().contains(checkbox));
                CHECK(checkbox.bottom() < frame.top());
            }
            QImage image(group->size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            group->render(&image, QPoint(), QRegion(), QWidget::DrawWindowBackground);
            // Mask glyphs rather than sampling text/antialiasing as a supposed
            // shadow. Sample the title padding against the same header row.
            QImage ink(image.size(), QImage::Format_ARGB32_Premultiplied);
            ink.fill(Qt::transparent);
            QRect maskLabel = label;
            QRect parentLabel;
            int maskFlags = QStyle::visualAlignment(option.direction,
                Qt::AlignLeft | Qt::AlignVCenter) | Qt::TextShowMnemonic;
            if (!group->isCheckable()) {
                // Static QStyleSheetStyle group titles expand the label to
                // ParentStyle's size and draw centered; the custom checkable
                // painter instead draws leading-aligned in the styled rect.
                parentLabel = titleParentStyle->subControlRect(QStyle::CC_GroupBox,
                    &option, QStyle::SC_GroupBoxLabel, group);
                maskLabel.setSize(maskLabel.size().expandedTo(parentLabel.size()));
                maskFlags = Qt::AlignCenter | Qt::TextShowMnemonic;
            }
            INFO("mask checkable=" << group->isCheckable()
                 << " parent=" << parentLabel.x() << ',' << parentLabel.y()
                 << ' ' << parentLabel.width() << 'x' << parentLabel.height()
                 << " expanded=" << maskLabel.x() << ',' << maskLabel.y()
                 << ' ' << maskLabel.width() << 'x' << maskLabel.height());
            {
                QPainter painter(&ink);
                painter.setFont(group->font());
                painter.setPen(Qt::white);
                painter.drawText(maskLabel, maskFlags, group->title());
            }
            int samples = 0, patches = 0;
            QPoint firstPatch(-1, -1);
            const QRect band = label.adjusted(-2, -1, 2, 1).intersected(image.rect().adjusted(3, 1, -3, -1));
            for (int y = band.top(); y <= band.bottom(); ++y) {
                for (int x = band.left(); x <= band.right(); ++x) {
                    if (group->isCheckable() && checkbox.adjusted(-3, -3, 3, 3).contains(x, y)) continue;
                    bool glyph = false;
                    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                        if (ink.valid(x + dx, y + dy) && qAlpha(ink.pixel(x + dx, y + dy)) > 0) glyph = true;
                    if (glyph) continue;
                    ++samples;
                    if (framingColorDistance(image.pixel(x, y), image.pixel(image.width() - 8, y)) > 8) {
                        if (patches == 0) firstPatch = QPoint(x, y);
                        ++patches;
                    }
                }
            }
            const QString evidenceDirectory = qEnvironmentVariable("EISKALT_QA_UI_EVIDENCE");
            if (patches > 0 && !evidenceDirectory.isEmpty()) {
                const QPoint reference(image.width() - 8, firstPatch.y());
                const QColor actual = image.pixelColor(firstPatch);
                const QColor expected = image.pixelColor(reference);
                const QString stem = QStringLiteral("%1-%2-%3-%4")
                    .arg(QString::fromLatin1(header.root)).arg(QString::fromLatin1(header.group))
                    .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"))
                    .arg(larger ? QStringLiteral("larger") : QStringLiteral("normal"));
                WARN("header patch evidence=" << stem.toStdString()
                     << " first=" << firstPatch.x() << ',' << firstPatch.y()
                     << " actualRGBA=" << actual.red() << ',' << actual.green() << ','
                     << actual.blue() << ',' << actual.alpha()
                     << " reference=" << reference.x() << ',' << reference.y()
                     << " referenceRGBA=" << expected.red() << ',' << expected.green() << ','
                     << expected.blue() << ',' << expected.alpha());
                const QDir directory(evidenceDirectory);
                CHECK(directory.mkpath(QStringLiteral(".")));
                CHECK(image.save(directory.filePath(stem + QStringLiteral("-group.png"))));
                CHECK(ink.save(directory.filePath(stem + QStringLiteral("-mask.png"))));
            }
            CAPTURE(samples, patches);
            REQUIRE(samples >= 8);
            CHECK(patches == 0);
        }
        dialog.reject();
    }
}

TEST_CASE("Reframed Preferences groups preserve title indicator keyboard disabled and accessibility behavior",
          "[qt][preferences-framing][preferences-group-input]")
{
    IsolatedMainWindow fixture;
    const auto originalPalette = qApp->palette();
    auto restorePalette = dcpp::makeScopedFunctor([&] { qApp->setPalette(originalPalette); });
    ::Settings dialog;
    framingAppearance(dialog, false, true);
    struct Target { ::Settings::Page page; const char *root; int tab; const char *group; const char *field; };
    const Target targets[] = {
        {::Settings::Page::Logs, "UISettingsLog", -1, "groupBox_PM", "lineEdit_FILE_PMFMT"},
        {::Settings::Page::Notifications, "UISettingsNotification", 1, "groupBox_PM", "lineEdit_SNDPM"}
    };
    for (const auto &target : targets) {
        CAPTURE(target.root, target.group);
        dialog.navigate(target.page, target.tab);
        auto *group = child<QGroupBox>(child<QWidget>(&dialog, target.root), target.group);
        for (auto *ancestor = group->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            if (auto *parentGroup = qobject_cast<QGroupBox*>(ancestor); parentGroup && parentGroup->isCheckable())
                parentGroup->setChecked(true);
        group->setChecked(true);
        auto *field = child<QLineEdit>(group, target.field);
        QApplication::processEvents();
        auto *accessible = QAccessible::queryAccessibleInterface(group);
        REQUIRE(accessible);
        const auto role = accessible->role();
        const auto name = accessible->text(QAccessible::Name);
        CHECK_FALSE(name.isEmpty());
        CHECK(bool(accessible->state().checkable));
        QSignalSpy toggled(group, &QGroupBox::toggled);
        QSignalSpy clicked(group, &QGroupBox::clicked);
        QGroupBox *neighbor = nullptr;
        for (auto *candidate : group->parentWidget()->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly))
            if (candidate != group && candidate->isCheckable()) { neighbor = candidate; break; }
        REQUIRE(neighbor);
        const bool neighborChecked = neighbor->isChecked();
        QSignalSpy neighborToggled(neighbor, &QGroupBox::toggled);
        const auto option = framingGroupOption(group);
        const QRect box = group->style()->subControlRect(QStyle::CC_GroupBox, &option,
                                                          QStyle::SC_GroupBoxCheckBox, group);
        const QRect label = group->style()->subControlRect(QStyle::CC_GroupBox, &option,
                                                            QStyle::SC_GroupBoxLabel, group);
        QTest::mouseClick(group, Qt::LeftButton, Qt::NoModifier, box.center());
        CHECK_FALSE(group->isChecked());
        CHECK_FALSE(field->isEnabled());
        CHECK_FALSE(bool(accessible->state().checked));
        CHECK(toggled.count() == 1);
        CHECK(clicked.count() == 1);
        group->setFocus();
        QTest::keyClick(group, Qt::Key_Space);
        CHECK(group->isChecked());
        CHECK(field->isEnabled());
        CHECK(bool(accessible->state().checked));
        CHECK(toggled.count() == 2);
        CHECK(clicked.count() == 2);
        QTest::mouseClick(group, Qt::LeftButton, Qt::NoModifier, label.center());
        CHECK_FALSE(group->isChecked());
        CHECK(toggled.count() == 3);
        CHECK(clicked.count() == 3);
        // Releasing outside the header, and clicks in the empty frame, must
        // not toggle a group or activate controls in its neighbours.
        const QPoint outside(group->width() - 4, group->height() - 4);
        QTest::mousePress(group, Qt::LeftButton, Qt::NoModifier, box.center());
        QTest::mouseRelease(group, Qt::LeftButton, Qt::NoModifier, outside);
        QTest::mouseClick(group, Qt::LeftButton, Qt::NoModifier, outside);
        CHECK_FALSE(group->isChecked());
        CHECK(toggled.count() == 3);
        CHECK(clicked.count() == 3);
        group->setEnabled(false);
        QTest::mouseClick(group, Qt::LeftButton, Qt::NoModifier, box.center());
        QTest::keyClick(group, Qt::Key_Space);
        CHECK_FALSE(group->isChecked());
        CHECK_FALSE(field->isEnabled());
        CHECK(bool(accessible->state().disabled));
        CHECK(accessible->role() == role);
        CHECK(accessible->text(QAccessible::Name) == name);
        CHECK(toggled.count() == 3);
        CHECK(clicked.count() == 3);
        group->setEnabled(true);
        CHECK(neighbor->isChecked() == neighborChecked);
        CHECK(neighborToggled.count() == 0);
    }
    dialog.reject();
}
#endif

#ifdef Q_OS_MAC
#include "SettingsShortcuts.h"
#include "ShortcutGetter.h"
#include "ShortcutManager.h"

TEST_CASE("Mac Shortcuts displays native keys without changing portable bindings",
          "[gui][shortcuts][macos]")
{
    IsolatedMainWindow fixture;
    auto* window = fixture.gui->mainWindow();
    SettingsShortcuts page;
    auto* model = page.findChild<ShortcutsModel*>();
    REQUIRE(model);

    const auto indexFor = [&](const QString& portable) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto index = model->index(row, 1);
            const auto* item = static_cast<ShortcutItem*>(index.internalPointer());
            if (item && item->shortcut == portable)
                return index;
        }
        return QModelIndex();
    };
    const auto checkRoles = [&](const QModelIndex& index, const QString& portable) {
        REQUIRE(index.isValid());
        const auto sequence = QKeySequence::fromString(portable, QKeySequence::PortableText);
        const QString native = sequence.toString(QKeySequence::NativeText);
        CHECK(native.contains(QChar(0x2318)));
        CHECK(native != portable);
        CHECK(model->data(index, Qt::DisplayRole).toString() == native);
        CHECK(model->data(index, Qt::EditRole).toString() == portable);
        CHECK(model->flags(index).testFlag(Qt::ItemIsEditable));
    };

    auto* quit = window->findChild<QAction*>("fileQuit");
    auto* close = window->findChild<QAction*>("closeWidgetShortCut");
    REQUIRE(quit);
    REQUIRE(close);
    CHECK(quit->shortcut().toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Q"));
    CHECK(close->shortcut().toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+W"));
    checkRoles(indexFor(QStringLiteral("Ctrl+Q")), QStringLiteral("Ctrl+Q"));
    const auto closeIndex = indexFor(QStringLiteral("Ctrl+W"));
    checkRoles(closeIndex, QStringLiteral("Ctrl+W"));

    const QString path = QString::fromStdString(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG))
        + QStringLiteral("shortcuts.txt");
    const auto savedBindings = [&] {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    };
    fixture.gui->shortcutManager()->save();
    const QByteArray before = savedBindings();
    page.ok();
    CHECK(savedBindings() == before);

    // Exercise the existing click editor rather than inventing a setData path.
    const QString custom = QStringLiteral("Ctrl+Alt+Shift+Y");
    QString editorInitial;
    bool edited = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &page, [&] {
        if (auto* getter = window->findChild<ShortcutGetter*>())
            getter->reject();
    });
    watchdog.start(3000);
    QTimer::singleShot(0, &page, [&] {
        auto* getter = window->findChild<ShortcutGetter*>();
        if (!getter)
            return;
        if (auto* input = getter->findChild<QLineEdit*>()) {
            editorInitial = input->text();
            input->setText(custom);
            edited = true;
            getter->accept();
        } else {
            getter->reject();
        }
    });
    REQUIRE(QMetaObject::invokeMethod(&page, "slotIndexClicked", Qt::DirectConnection,
                                     Q_ARG(QModelIndex, closeIndex)));
    watchdog.stop();
    REQUIRE(edited);
    CHECK(editorInitial == QStringLiteral("Ctrl+W"));
    checkRoles(closeIndex, custom);
    page.ok();
    const auto sequence = QKeySequence::fromString(custom, QKeySequence::PortableText);
    CHECK(close->shortcut() == sequence);
    CHECK(fixture.gui->shortcutManager()->getShortcuts().value("closeWidgetShortCut") == sequence);
    const QByteArray saved = savedBindings();
    CHECK(saved.contains("closeWidgetShortCut " + custom.toUtf8() + '\n'));
    CHECK(saved.contains("fileQuit Ctrl+Q\n"));
    CHECK_FALSE(saved.contains(sequence.toString(QKeySequence::NativeText).toUtf8()));

    SettingsShortcuts reopened;
    auto* reopenedModel = reopened.findChild<ShortcutsModel*>();
    REQUIRE(reopenedModel);
    bool foundCustom = false;
    for (int row = 0; row < reopenedModel->rowCount(); ++row) {
        const auto index = reopenedModel->index(row, 1);
        const auto* item = static_cast<ShortcutItem*>(index.internalPointer());
        if (!item || item->shortcut != custom)
            continue;
        foundCustom = true;
        CHECK(reopenedModel->data(index, Qt::DisplayRole).toString()
              == sequence.toString(QKeySequence::NativeText));
        CHECK(reopenedModel->data(index, Qt::EditRole).toString() == custom);
    }
    CHECK(foundCustom);
}
#endif

namespace {
template<class Window> QString cachedQuitPhase(const Window& window) {
    if constexpr(requires { window.quitWorkerComponent(); })
        return QString::fromLatin1(window.quitWorkerComponent());
    return {};
}
QString quitWarning(const QSignalSpy& refused) {
    return refused.isEmpty() ? QString{} : refused.last().first().toString();
}
void checkQuitWarning(const QString& warning, const QString& phase,
                      const QString& status, const QString& branch, uint32_t budget) {
    CHECK(warning.startsWith("Worker timeout; retry quit after the worker finishes"));
    CHECK(warning.contains("last observed: " + phase));
    CHECK(warning.contains("status=" + status));
    CHECK(warning.contains("timeout=" + branch));
    CHECK(warning.contains("budget_ms=" + QString::number(budget)));
    CHECK(warning.contains("elapsed_ms="));
    CHECK(warning.contains("prepare_ms="));
}
}

TEST_CASE("Quit timeout reports cached producer phase and refreshes it only on explicit retry",
          "[qt][window-quit][quit-timeout-diagnostic]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    updates::ApplicationQuitSession session(*f.gui);
    QSignalSpy refused(&session, &updates::ApplicationQuitSession::refused);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    std::atomic_bool entered{false}, released{false};
    auto* worker = new AsyncRunner(window);
    worker->setRunFunction([&] { entered = true; while(!released) QThread::msleep(1); });
    auto cleanup = dcpp::makeScopedFunctor([&] { released = true; worker->wait(); });
    worker->start();
    REQUIRE(waitFor([&] { return entered.load(); }));
    session.request(300);
    REQUIRE(session.state() == updates::ApplicationQuitSession::State::Preparing);
    CHECK(cachedQuitPhase(*window) == "GUI producer workers");
    // Delay delivery beyond the deadline; the warning must not re-poll owners.
    QThread::msleep(350);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    REQUIRE(refused.count() == 1);
    checkQuitWarning(quitWarning(refused), "GUI producer workers", "Pending", "before-poll", 300);
    CHECK(ready.isEmpty());
    released = true;
    REQUIRE(worker->wait(2000));
    QTest::qWait(25);
    CHECK(ready.isEmpty());
    CHECK(cachedQuitPhase(*window) == "GUI producer workers");
    // Physically retire the original owners without asking the session to retry.
    REQUIRE(waitFor([&] { return window->stopWorkersForQuit(true); }));
    REQUIRE(waitFor([&] { return f.core->prepareShutdownAsyncChecked(1000).status == dcpp::DCContext::ShutdownStatus::Ready; }));
    CHECK(cachedQuitPhase(*window) == "Ready");
    // A zero-budget explicit retry exercises Ready-after-deadline on cached receipts.
    session.request(0);
    REQUIRE(session.state() == updates::ApplicationQuitSession::State::Refused);
    REQUIRE(refused.count() == 2);
    checkQuitWarning(quitWarning(refused), "Ready", "Ready", "ready-after-deadline", 0);
    CHECK_FALSE(quitWarning(refused).contains("GUI producer workers"));
    CHECK(ready.isEmpty());
    session.request(1000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(refused.count() == 2);
}

TEST_CASE("Quit timeout reports a Pending updater return without retrying or installing late",
          "[qt][window-quit][quit-timeout-diagnostic]") {
    IsolatedMainWindow f;
    struct SlowPendingBackend : QuitBackend {
        using QuitBackend::QuitBackend;
        bool delay = true;
        bool stopWorkersForQuit() override {
            if(delay) { delay = false; QThread::msleep(80); }
            return QuitBackend::stopWorkersForQuit();
        }
    } backend(*f.core);
    updates::UpdateController updater(*f.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    backend.fenceResult = false; backend.fencePending = true;
    updates::ApplicationQuitSession session(*f.gui, &updater);
    QSignalSpy refused(&session, &updates::ApplicationQuitSession::refused);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    const auto route = f.core->getProxyRoute()->snapshot();
    session.request(20);
    REQUIRE(session.state() == updates::ApplicationQuitSession::State::Refused);
    REQUIRE(refused.count() == 1);
    checkQuitWarning(quitWarning(refused), "Updater workers", "Pending", "pending-return", 20);
    CHECK(backend.fences == 1); CHECK(backend.installs == 0);
    CHECK_FALSE(route->revoked->load()); CHECK(ready.isEmpty());
    backend.fenceResult = true; backend.fencePending = false;
    QTest::qWait(30);
    CHECK(backend.fences == 1); CHECK(backend.installs == 0); CHECK(ready.isEmpty());
    session.request(1000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
    CHECK(backend.fences == 2); CHECK(backend.installs == 0);
}

#ifdef USE_TORRENT
#include "TorrentSharePublisher.h"
TEST_CASE("Quit timeout identifies retained Torrent publisher rather than generic GUI workers",
          "[qt][window-quit][quit-timeout-diagnostic]") {
    IsolatedMainWindow f;
    auto* window = f.gui->mainWindow();
    auto* runtime = window->findChild<TorrentRuntime*>(); REQUIRE(runtime);
    auto* publisher = runtime->findChild<TorrentSharePublisher*>(); REQUIRE(publisher);
    struct Gate { QSemaphore entered, release; };
    const auto gate = std::make_shared<Gate>();
    auto cleanup = dcpp::makeScopedFunctor([gate] { gate->release.release(); });
    publisher->enqueue("diagnostic-only", "diagnostic-only", {f.home.filePath("unused")}, [gate] {
        gate->entered.release(); gate->release.acquire(); return true;
    });
    REQUIRE(gate->entered.tryAcquire(1, 2000));
    updates::ApplicationQuitSession session(*f.gui);
    QSignalSpy refused(&session, &updates::ApplicationQuitSession::refused);
    QSignalSpy ready(&session, &updates::ApplicationQuitSession::exitReady);
    session.request(300);
    REQUIRE(session.state() == updates::ApplicationQuitSession::State::Preparing);
    CHECK(cachedQuitPhase(*window) == "Torrent publisher");
    QThread::msleep(350);
    REQUIRE(waitFor([&] { return session.state() == updates::ApplicationQuitSession::State::Refused; }));
    REQUIRE(refused.count() == 1);
    checkQuitWarning(quitWarning(refused), "Torrent publisher", "Pending", "before-poll", 300);
    CHECK(ready.isEmpty());
    gate->release.release(); QTest::qWait(40);
    CHECK(ready.isEmpty());
    session.request(1000);
    REQUIRE(waitFor([&] { return ready.count() == 1; }));
}
#endif
