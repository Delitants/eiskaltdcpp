#pragma once

#include "dcpp/DCContext.h"
#include "dcpp/ClientManager.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/ProxyRoute.h"
#include "dcpp/Util.h"
#include "QtContext.h"
#include "ArenaWidgetManager.h"
#include "MainWindow.h"
#include "GlobalTimer.h"
#include "WulforSettings.h"
#include "WulforUtil.h"
#include "updates/UpdateBackend.h"
#include "updates/UpdateTransport.h"
#ifdef USE_TORRENT
#include "TorrentRuntime.h"
#include "torrent/TorrentSettings.h"
#endif
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
struct QuitBackend : updates::UpdateBackend {
    dcpp::DCContext& core;
    int fences = 0, installs = 0, checks = 0, revocations = 0;
    quint64 operation = 0;
    quint64 handoffOperation = 0;
    std::function<bool()> handoffConsent;
    bool supportsInstallation() const override { return true; }
    void beginInstallHandoff(quint64 op, const updates::UpdateOffer&, std::function<bool()> consent) override {
        handoffOperation = op; handoffConsent = std::move(consent); ++installs;
    }
    bool fenceResult = true, routeWasRunning = false, fencePending = false;
    std::unique_ptr<updates::UpdateTransport> transport;
    bool quitWorkersPending() const override { return transport ? transport->quitWorkersPending() : fencePending; }
    explicit QuitBackend(dcpp::DCContext& c) : core(c) {}
    void check(quint64 op, updates::Channel, updates::CheckKind) override { ++checks; operation = op; }
    void download(quint64 op, const updates::UpdateOffer&) override { operation = op; }
    void cancel() override {}
    bool rejectRecoveryDisarm = false;
    void revokeInstallConsent() override {
        ++revocations;
        if(fences && rejectRecoveryDisarm) throw std::runtime_error("Fixture disarm refusal");
    }
    void armInstallOnQuit(const updates::UpdateOffer&, bool) override { ++installs; }
    bool stopWorkersForQuit() override {
        ++fences; routeWasRunning = !core.getProxyRoute()->snapshot()->revoked->load();
        return transport ? transport->stopWorkersForQuit() : fenceResult;
    }
};
bool waitFor(const std::function<bool()> &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000)
        QTest::qWait(10);
    return ready();
}

struct IsolatedMainWindow {
    QTemporaryDir home;
    QByteArray previousHome = qgetenv("HOME");
    QByteArray previousConfig = qgetenv("XDG_CONFIG_HOME");
    std::unique_ptr<dcpp::DCContext> core;
    std::unique_ptr<QtContext> gui;

    IsolatedMainWindow()
    {
        REQUIRE(home.isValid());
        qputenv("HOME", home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
        const auto root = (home.path() + QLatin1Char('/')).toStdString();
        dcpp::Util::PathsMap paths;
        paths[dcpp::Util::PATH_USER_CONFIG] = root;
        paths[dcpp::Util::PATH_USER_LOCAL] = root;
        paths[dcpp::Util::PATH_DOWNLOADS] = root + "downloads/";
        dcpp::Util::initialize(paths);
        REQUIRE(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG) == root);

        // Seed a fresh passive profile before full startup can read any settings.
        core = std::make_unique<dcpp::DCContext>();
        core->startupMinimal();
        dcpp::setContext(core.get());
        auto *settings = core->getSettingsManager();
        settings->set(dcpp::SettingsManager::NICK, "offline-toolbar-test");
        settings->set(dcpp::SettingsManager::INCOMING_CONNECTIONS,
                      dcpp::SettingsManager::INCOMING_FIREWALL_PASSIVE);
        settings->set(dcpp::SettingsManager::AUTO_DETECT_CONNECTION, false);
        settings->set(dcpp::SettingsManager::USE_DHT, false);
        settings->set(dcpp::SettingsManager::BIND_ADDRESS, "127.0.0.1");
        settings->set(dcpp::SettingsManager::BIND_ADDRESS6, "::1");
        settings->set(dcpp::SettingsManager::TCP_PORT, 0);
        settings->set(dcpp::SettingsManager::UDP_PORT, 0);
        settings->set(dcpp::SettingsManager::TLS_PORT, 0);
#ifdef LUA_SCRIPT
        settings->set(dcpp::SettingsManager::USE_LUA, false);
#endif
        settings->save();
        core->shutdown();
        // shutdown() uninitializes Util, including every previously set path.
        dcpp::Util::initialize(paths);
        core = std::make_unique<dcpp::DCContext>();
        dcpp::setContext(core.get());
        core->startup();
        REQUIRE(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG) == root);
        REQUIRE(dcpp::Util::getPath(dcpp::Util::PATH_USER_LOCAL) == root);
        REQUIRE_FALSE(core->getClientManager()->isActive());
        REQUIRE_FALSE(core->getSettingsManager()->getBool(dcpp::SettingsManager::USE_DHT));
        REQUIRE(core->getSettingsManager()->get(dcpp::SettingsManager::TCP_PORT) == 0);

#ifdef USE_TORRENT
        eiskalt::torrent::Settings torrent;
        torrent.downloadPath = home.filePath("downloads");
        torrent.dht = torrent.pex = torrent.localDiscovery = torrent.portMapping = false;
        torrent.shareCompleted = false;
        torrent.bootstrapNodes.clear();
        REQUIRE(eiskalt::torrent::saveSettings(TorrentRuntime::settingsPath(), torrent));
#endif

        gui = std::make_unique<QtContext>(*core);
        gui->createSettings();
        gui->settings()->load();
        gui->settings()->setBool(WB_TRAY_ENABLED, false);
        gui->settings()->setBool(WB_EXIT_CONFIRM, false);
        gui->settings()->setBool(WB_ANTISPAM_ENABLED, false);
        const QString layout = qEnvironmentVariable("EISKALT_TEST_TAB_LAYOUT", "multiline");
        REQUIRE((layout == "multiline" || layout == "singleline" || layout == "sidebar"));
        gui->settings()->setBool(WB_MAINWINDOW_USE_SIDEBAR, layout == "sidebar");
        gui->settings()->setBool(WB_MAINWINDOW_USE_M_TABBAR, layout == "multiline");
        gui->createGlobalTimer();
        gui->globalTimer()->stop();
        gui->createWulforUtil();
        gui->wulforUtil()->loadIcons();
        gui->createArenaWidgetManager();
        gui->createMainWindow();
        gui->createHubManager();
        gui->createNotification();
        gui->mainWindow()->resize(1100, 800);
        gui->mainWindow()->show();
        QCoreApplication::processEvents();
        // No autoconnect call, saved hubs, core timer, peers, or resumed jobs.
    }

    ~IsolatedMainWindow()
    {
        gui.reset();
        QtContextAware::setCurrent(nullptr);
        core->shutdown();
        dcpp::setContext(nullptr);
        core.reset();
        if (previousHome.isNull()) qunsetenv("HOME"); else qputenv("HOME", previousHome);
        if (previousConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
        else qputenv("XDG_CONFIG_HOME", previousConfig);
    }
};
}
