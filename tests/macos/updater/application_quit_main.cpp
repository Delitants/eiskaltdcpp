// Exercise the production entrypoint, changing only the fixture IPC identity.
#include "dcpp/stdinc.h"
#include "EiskaltApp_mac.h"
#include <QUuid>
class IsolatedQuitApp final : public EiskaltApp {
public:
    IsolatedQuitApp(int& argc, char** argv, const QString&)
        : EiskaltApp(argc, argv, "eiskalt-quit-qa-" + QUuid::createUuid().toString()) {}
};
#define EiskaltApp IsolatedQuitApp
#define main eiskaltApplicationMain
#include "../../../eiskaltdcpp-qt/src/main.cpp"
#undef main
#undef EiskaltApp
#include "TorrentRuntime.h"
#include "torrent/TorrentSettings.h"
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>

static bool fixtureQuitObserved = false;
static void startQuitDriver() {
    auto* timer = new QTimer(qApp);
    timer->setInterval(25);
    QObject::connect(timer, &QTimer::timeout, timer, [timer] {
        auto* context = qtCtx();
        if(!context || !context->mainWindow() || !context->queuedUsers()) return;
        timer->stop();
        auto* window = context->mainWindow();
        const auto hashPath = qEnvironmentVariable("HOME") + "/shares/untouched.bin";
        QFile hashFile(hashPath);
        if(!hashFile.open(QIODevice::ReadOnly)) qFatal("Hash fixture missing");
        const auto bytes = hashFile.readAll();
        dcpp::TigerTree tree;
        tree.update(bytes.constData(), bytes.size());
        tree.finalize();
        context->dcCtx().getHashManager()->addTree(hashPath.toStdString(),
            uint32_t(QFileInfo(hashPath).lastModified().toSecsSinceEpoch()), tree);
        context->settings()->setBool(WB_EXIT_CONFIRM, false);
        context->settings()->setBool(WB_TRAY_ENABLED, false);
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [window] {
            fixtureQuitObserved = window->normalQuitPrepared();
            fprintf(stdout, "CHECKED_WINDOW_QUIT=%d\n", int(fixtureQuitObserved)); fflush(stdout);
            auto* application = qobject_cast<EiskaltApp*>(qApp);
            fprintf(stdout, "IPC_HELD_AT_QUIT=%d\n", int(application && application->getSharedMemory().isAttached()));
            fflush(stdout);
        });
        QObject::connect(window, &QObject::destroyed, qApp, [] {
            auto* application = qobject_cast<EiskaltApp*>(qApp);
            fprintf(stdout, "IPC_HELD_DURING_GUI_TEARDOWN=%d\n",
                int(application && application->getSharedMemory().isAttached()));
            fflush(stdout);
        }, Qt::DirectConnection);
        if(QCoreApplication::arguments().contains(QStringLiteral("--quit-via-event"))) {
            QEvent quit(QEvent::Quit);
            QCoreApplication::sendEvent(qApp, &quit);
            fprintf(stdout, "QUIT_EVENT_DEFERRED=%d\n", int(!quit.isAccepted() && !window->normalQuitPrepared()));
            fflush(stdout);
        } else {
            window->beginExit();
            window->close();
        }
    });
    timer->start();
}
Q_COREAPP_STARTUP_FUNCTION(startQuitDriver)

int main(int argc, char** argv) {
    const auto home = qEnvironmentVariable("HOME");
    if(!home.startsWith("/private/tmp/eiskalt-app-quit-") || home != qEnvironmentVariable("CFFIXED_USER_HOME") ||
        qEnvironmentVariable("XDG_CONFIG_HOME") != home + "/.config") return 72;
    const auto config = home + "/.config/eiskaltdc++/";
    if(!QDir().mkpath(config)) return 73;
    dcpp::Util::PathsMap paths;
    paths[dcpp::Util::PATH_USER_CONFIG] = config.toStdString();
    paths[dcpp::Util::PATH_USER_LOCAL] = (home + "/.local/share/eiskaltdc++/").toStdString();
    paths[dcpp::Util::PATH_DOWNLOADS] = (home + "/downloads/").toStdString();
    dcpp::Util::initialize(paths);
    dcpp::DCContext seed;
    seed.startupMinimal();
    dcpp::setContext(&seed);
    auto* settings = seed.getSettingsManager();
    settings->set(dcpp::SettingsManager::NICK, "offline-quit-entry-fixture");
    settings->set(dcpp::SettingsManager::INCOMING_CONNECTIONS, dcpp::SettingsManager::INCOMING_FIREWALL_PASSIVE);
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
    eiskalt::torrent::Settings torrent;
    torrent.downloadPath = home + "/downloads";
    torrent.dht = torrent.pex = torrent.localDiscovery = torrent.portMapping = false;
    torrent.shareCompleted = false; torrent.bootstrapNodes.clear();
    if(!eiskalt::torrent::saveSettings(TorrentRuntime::settingsPath(), torrent)) return 74;
    seed.shutdown(); dcpp::setContext(nullptr);
    const auto result = eiskaltApplicationMain(argc, argv);
    // _Exit cannot produce this receipt. Both GUI/core and QApplication have
    // already completed the actual production entrypoint's normal teardown.
    fprintf(stdout, "PRODUCTION_MAIN_RETURNED=%d CHECKED=%d\n", result, int(fixtureQuitObserved));
    fflush(stdout);
    return result == 0 && fixtureQuitObserved ? 0 : 75;
}
