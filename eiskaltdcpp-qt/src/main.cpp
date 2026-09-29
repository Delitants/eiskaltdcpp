/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#ifdef BUILD_STATIC
#include <QtPlugin>
#if defined(_WIN32)
Q_IMPORT_PLUGIN (QWindowsAudioPlugin);
Q_IMPORT_PLUGIN (QWindowsIntegrationPlugin);
Q_IMPORT_PLUGIN (QSQLiteDriverPlugin);
Q_IMPORT_PLUGIN (QWindowsVistaStylePlugin);
#elif defined(__linux) // defined(_WIN32)
Q_IMPORT_PLUGIN (QXcbIntegrationPlugin);
Q_IMPORT_PLUGIN (QSQLiteDriverPlugin);
#endif // defined(_WIN32)
#endif // BUILD_STATIC

#include <stdlib.h>
#include <iostream>
#include <string>

using namespace std;

#include "dcpp/stdinc.h"
#include "dcpp/DCPlusPlus.h"

#include "dcpp/forward.h"
#include "dcpp/QueueManager.h"
#include "dcpp/HashManager.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/Thread.h"
#include "dcpp/Singleton.h"

#include "WulforUtil.h"
#include "WulforSettings.h"
#include "QtContext.h"
#include "QtContextAware.h"
#include "HubManager.h"
#include "Notification.h"
#include "VersionGlobal.h"
#include "LocalizedDefaults.h"
#include "FinishedTransfers.h"
#include "QueuedUsers.h"
#include "ArenaWidgetManager.h"
#include "ArenaWidgetFactory.h"
#include "DiagnosticLog.h"
#include "MainWindow.h"
#include "GlobalTimer.h"
#include "EmoticonFactory.h"

#if defined(Q_OS_HAIKU)
#include "EiskaltApp_haiku.h"
#elif defined(Q_OS_MAC)
#include "EiskaltApp_mac.h"
#include "MacInputStyle.h"
#else
#include "EiskaltApp.h"
#endif

#ifdef USE_ASPELL
#include "SpellCheck.h"
#endif

#ifdef USE_JS
#include "ScriptEngine.h"
#endif

#include <QApplication>
#if defined(Q_OS_MAC)
#include <QAccessible>
#include <QPointer>
#include "MacGuiConfigCompatibility.h"
#endif
#include <QIcon>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QRegularExpression>
#include <QObject>
#include <QScopeGuard>
#include <QSettings>
#include <QTimer>
#include <cstdlib>

#ifdef DBUS_NOTIFY
#include <QtDBus>
#endif

void callBack(void *, const std::string &a)
{
    std::cout << QObject::tr("Loading: ").toStdString() << a << std::endl;
}

QString readBootstrapSetting(const QString& tag)
{
    QFile file(QDir::home().filePath(QStringLiteral(".config/eiskaltdc++/DCPlusPlus.xml")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    const QString xml = QString::fromUtf8(file.readAll());
    const QRegularExpression expression(QStringLiteral("<%1\\b[^>]*>([^<]*)</%1>").arg(tag));
    const QRegularExpressionMatch match = expression.match(xml);

    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

bool bootstrapDiagnosticLogEnabled()
{
    const QString value = readBootstrapSetting(QStringLiteral("LogDiagnostic"));
    return value.isEmpty() || value != QStringLiteral("0");
}

QString bootstrapDiagnosticLogDirectory()
{
    const QString value = readBootstrapSetting(QStringLiteral("LogDirectory"));
    return value.isEmpty()
            ? QDir::home().filePath(QStringLiteral(".local/share/eiskaltdc++/Logs/"))
            : value;
}

QString bootstrapDiagnosticLogFileName()
{
    const QString value = readBootstrapSetting(QStringLiteral("LogFileDiagnostic"));
    return value.isEmpty() ? QStringLiteral("Diagnostic.log") : value;
}

void parseCmdLine(const QStringList &);

#if !defined(Q_OS_WIN)
#include <unistd.h>
#include <signal.h>
#if !defined (Q_OS_HAIKU) && defined (__GLIBC__)
#include <execinfo.h>

#ifdef ENABLE_STACKTRACE
#include "extra/stacktrace.h"
#endif // ENABLE_STACKTRACE

void installHandlers();
#endif

#ifdef FORCE_XDG
#include <QTextStream>
void migrateConfig();
#endif

#else //WIN32
#include <locale.h>
#include <windows.h>
#include <string>
#include <sstream>

/**
 * Show a diagnostic MessageBox when the Qt GUI app fails to start.
 * WIN32 subsystem apps have no console, so a missing DLL or early
 * crash is completely silent without explicit error reporting.
 */
static LONG WINAPI earlyExceptionHandler(EXCEPTION_POINTERS *ep)
{
    wchar_t buf[512];
    swprintf(buf, sizeof(buf) / sizeof(buf[0]),
             L"EiskaltDC++ crashed during startup.\n\n"
             L"Exception code: 0x%08lX\nAddress: %p\n\n"
             L"This may indicate a missing DLL or incompatible library.\n"
             L"Please report this to the developers.",
             static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
             ep->ExceptionRecord->ExceptionAddress);
    MessageBoxW(nullptr, buf, L"EiskaltDC++ - Fatal Error",
                MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_HAIKU) && defined(__GLIBC__)
void catchSIG(int sigNum)
{
    psignal(sigNum, "Catching signal ");

#ifdef ENABLE_STACKTRACE
    printBacktrace(sigNum);
#endif

    if (EiskaltApp *eapp = dynamic_cast<EiskaltApp*>(qApp)) {
        eapp->getSharedMemory().unlock();
        eapp->getSharedMemory().detach();
    }

    raise(SIGINT);
    std::abort();
}

template <int sigNum = 0, int ... Params>
void catchSignals()
{
    if (!sigNum)
        return;

    psignal(sigNum, "Installing handler for");
    signal(sigNum, catchSIG);

    catchSignals<Params ... >();
}

void installHandlers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;

    if (sigaction(SIGPIPE, &sa, nullptr) == -1) {
        printf("Cannot handle SIGPIPE\n");
    } else {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
    }

    catchSignals<SIGSEGV, SIGABRT, SIGBUS, SIGTERM>();

    printf("Signal handlers installed.\n");
}
#endif

#if defined(Q_OS_MAC)
// Dock click handling is done via EiskaltEventFilter in EiskaltApp_mac.h
// using QApplicationStateChangeEvent
class MacAppearanceStyleUpdater : public QObject
{
public:
    explicit MacAppearanceStyleUpdater(QApplication &app) :
        QObject(&app),
        app_(app)
    {
        apply();
        app_.installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (!updating_ &&
            (event->type() == QEvent::PaletteChange ||
             event->type() == QEvent::ApplicationPaletteChange ||
             event->type() == QEvent::StyleChange)) {
            QTimer::singleShot(0, this, [this]() { apply(); });
        }

        return QObject::eventFilter(object, event);
    }

private:
    void apply()
    {
        if (updating_)
            return;

        updating_ = true;
        const QString style = mac_input_style::macInputContrastStyle(app_.palette());
        if (style != app_.styleSheet())
            app_.setStyleSheet(style);
        updating_ = false;
    }

    QApplication &app_;
    bool updating_ = false;
};

struct MacGuiConfigMigration
{
    bool needed = false;
    QString configPath;
    QString backupPath;
};

static QString macGuiConfigCompatibilityVersion()
{
    return mac_gui_config::compatibilityVersion();
}

static QString macGuiConfigCompatibilityKey()
{
    return QStringLiteral("app/config-compat-version");
}

static MacGuiConfigMigration inspectMacGuiConfigForMigration()
{
    MacGuiConfigMigration migration;
    migration.configPath = _q(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG)) +
            QStringLiteral("EiskaltDC++_Qt.conf");

    if (!QFileInfo::exists(migration.configPath))
        return migration;

    QSettings settings(migration.configPath, QSettings::IniFormat);
    const QString storedVersion = settings.value(macGuiConfigCompatibilityKey()).toString();
    migration.needed = mac_gui_config::needsMigration(storedVersion);

    if (migration.needed) {
        QString backupPath = migration.configPath + QStringLiteral(".backup-before-") +
                macGuiConfigCompatibilityVersion();
        int suffix = 1;
        while (QFileInfo::exists(backupPath)) {
            backupPath = migration.configPath + QStringLiteral(".backup-before-") +
                    macGuiConfigCompatibilityVersion() + QStringLiteral(".%1").arg(suffix++);
        }

        if (QFile::copy(migration.configPath, backupPath))
            migration.backupPath = backupPath;
    }

    return migration;
}

static void applyMacGuiConfigMigration(const MacGuiConfigMigration &migration)
{
    if (!qtCtx() || !qtCtx()->settings())
        return;

    WulforSettings *settings = qtCtx()->settings();

    settings->setBool(QStringLiteral("hubframe/change-chat-background-color"), false);
    settings->setStr(QStringLiteral("hubframe/chat-background-color"), QString());
    settings->setStr(WS_CHAT_TIME_COLOR, QString());
    settings->setStr(WS_CHAT_MSG_COLOR, QString());

    settings->setStr(WS_CHAT_USERLIST_STATE, QString());
    settings->setStr(WS_TRANSFERS_STATE, QString());
    settings->setStr(WS_DQUEUE_STATE, QString());
    settings->setStr(WS_SEARCH_STATE, QString());
    settings->setStr(WS_MAINWINDOW_STATE, QString());
    settings->setStr(WS_FTRANSFERS_FILES_STATE, QString());
    settings->setStr(WS_FTRANSFERS_USERS_STATE, QString());
    settings->setStr(WS_FAV_HUBS_STATE, QString());
    settings->setStr(WS_PUBLICHUBS_STATE, QString());
    settings->setStr(WS_SETTINGS_GUI_FONTS_STATE, QString());

    settings->setStr(macGuiConfigCompatibilityKey(), macGuiConfigCompatibilityVersion());
    settings->save();

    QString message = QObject::tr(
        "Old or incompatible EiskaltDC++ GUI settings were detected.\n\n"
        "Safe settings such as hubs, account details, sharing, downloads and history were kept. "
        "Theme, chat color, window layout and table-column state from the older config were reset "
        "because they can break live light/dark switching on current macOS.\n\n");

    if (!migration.backupPath.isEmpty()) {
        message += QObject::tr("A backup of the previous GUI config was saved here:\n%1")
                .arg(migration.backupPath);
    } else {
        message += QObject::tr("The previous GUI config could not be backed up, but incompatible "
                               "visual settings were still discarded.");
    }

    QMessageBox::warning(nullptr,
                         QObject::tr("EiskaltDC++ settings updated"),
                         message);
}
#endif



#ifdef FORCE_XDG
void migrateConfig(){
    // Temporary no-op restore to satisfy linker on macOS builds.
    // Real migration logic can be re-added later if needed.
}
#endif


int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN)
    // Install an early crash handler so WIN32 subsystem apps don't
    // die silently when a DLL is missing or initialization fails.
    SetUnhandledExceptionFilter(earlyExceptionHandler);
#endif

    setlocale(LC_ALL, "");

    DiagnosticLog::instance().install();

#if defined(Q_OS_MAC)
    // Qt 6.11's Cocoa accessibility bridge can abort when fast-changing item
    // views are inspected while their accessible wrappers are being deleted.
    qputenv("QT_ACCESSIBILITY", QByteArray("0"));
    QAccessible::setActive(false);
    // Native macOS menus convert QAction icons to CGImages during menu-bar
    // focus sync. Keep menu icons disabled there while preserving toolbar and
    // tab icons that use the same actions/pixmaps.
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    EiskaltApp app(argc, argv, _q(dcpp::Util::getLoginName()+"EDCPP"));
    app.setQuitOnLastWindowClosed(false);
#if defined(Q_OS_MAC)
    // Install IPC buffering before core initialization can process native events.
    QObject::connect(&app, &QtSingleCoreApplication::messageReceived, &app,
                     [&app](const QString &message) {
        if (message.isEmpty())
            app.pendingOpenEvents().enqueueActivation();
        else {
            for (const QString &source : message.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                app.pendingOpenEvents().enqueue(source);
        }
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&app] {
        app.pendingOpenEvents().setReady(false);
    });
#endif
    DiagnosticLog::instance().configure(bootstrapDiagnosticLogEnabled(),
                                        bootstrapDiagnosticLogDirectory(),
                                        bootstrapDiagnosticLogFileName());
    DiagnosticLog::instance().logLine(QStringLiteral("bootstrap started"));
#if defined(Q_OS_MAC)
    MacAppearanceStyleUpdater macAppearanceStyleUpdater(app);
#endif
    int ret = 0;

    parseCmdLine(app.arguments());

    if (app.isRunning()){
        DiagnosticLog::instance().logLine(
            QStringLiteral("secondary instance detected; owner_pid=%1 owner_path=%2; forwarding arguments and exiting")
                .arg(app.instanceOwnerPid())
                .arg(app.instanceOwnerPath().isEmpty()
                        ? QStringLiteral("<unknown>")
                        : app.instanceOwnerPath()));
        QStringList args = app.arguments();
        args.removeFirst(); // remove path to executable
#if !defined(Q_OS_HAIKU)
        app.sendMessage(args.join("\n"));
#endif
        return 0;
    }

#if !defined (Q_OS_WIN) && !defined (Q_OS_HAIKU) && defined (__GLIBC__)
    installHandlers();
#endif

#if defined(FORCE_XDG) && !defined(Q_OS_WIN)
    migrateConfig();
#endif

    auto dcContext = dcpp::startup(callBack, nullptr);
    DiagnosticLog::instance().configure(dcContext->getSettingsManager()->getBool(dcpp::SettingsManager::LOG_DIAGNOSTIC, true),
                                        _q(dcContext->getSettingsManager()->get(dcpp::SettingsManager::LOG_DIRECTORY, true)),
                                        _q(dcContext->getSettingsManager()->get(dcpp::SettingsManager::LOG_FILE_DIAGNOSTIC, true)));
    DiagnosticLog::instance().startHeartbeat(&app);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        DiagnosticLog::instance().logLine(QStringLiteral("aboutToQuit"));
    });
    dcContext->getTimerManager()->start();

    dcContext->getHashManager()->setPriority(Thread::IDLE);

#if defined(Q_OS_MAC)
    const MacGuiConfigMigration macGuiConfigMigration = inspectMacGuiConfigForMigration();
#endif

    app.setOrganizationName("EiskaltDC++ Team");
    app.setApplicationName("EiskaltDC++ Qt");
    app.setApplicationVersion(QString::fromStdString(eiskaltdcppVersionString));

        QtContext ctx(*dcContext);
    // Guard: ensure settings are saved on scope exit.
    // The guard runs before ~QtContext.
    auto cleanupGuard = qScopeGuard([&]() {
        qtCtx()->settings()->save();
    });

    ctx.createGlobalTimer();
    ctx.createSettings();

    ctx.settings()->load();
#if defined(Q_OS_MAC)
    if (macGuiConfigMigration.needed)
        applyMacGuiConfigMigration(macGuiConfigMigration);
    else {
        ctx.settings()->setStr(macGuiConfigCompatibilityKey(), macGuiConfigCompatibilityVersion());
        ctx.settings()->save();
    }
#endif
    ctx.settings()->loadTheme();

    ctx.createWulforUtil();
    ctx.settings()->loadTranslation();
    LocalizedDefaults::refreshAwayMessageSetting(dcContext->getSettingsManager(),
                                                 qtCtx()->wulforUtil()->getTranslationsPath());
    // Create and load emoticon factory
    ctx.createEmoticonFactory();
    if (qtCtx()->emoticonFactory())
        qtCtx()->emoticonFactory()->load();

    Text::hubDefaultCharset = qtCtx()->wulforUtil()->qtEnc2DcEnc(qtCtx()->settings()->getStr(WS_DEFAULT_LOCALE)).toStdString();
    // Safety: if the conversion returned an empty string (should not happen
    // after the qtEnc2DcEnc fix, but guard against it), fall back to the
    // system charset so NMDC hubs get a real encoding for iconv.
    if (Text::hubDefaultCharset.empty())
        Text::hubDefaultCharset = Text::systemCharset;

    if (qtCtx()->wulforUtil()->loadUserIcons())
        std::cout << QObject::tr("UserList icons has been loaded").toStdString() << std::endl;

    if (qtCtx()->wulforUtil()->loadIcons())
        std::cout << QObject::tr("Application icons has been loaded").toStdString() << std::endl;

#if !defined(Q_OS_MAC)
    app.setWindowIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiICON_APPL));
#endif

    ctx.createArenaWidgetManager();

    ctx.createMainWindow();
#if defined(Q_OS_MAC)
    qtCtx()->mainWindow()->setUnload(false);
    const QPointer<MainWindow> openTarget(qtCtx()->mainWindow());
    app.pendingOpenEvents().setHandler([openTarget](const QString &source) {
        if (openTarget)
            openTarget->parseInstanceLine(source);
    }, [openTarget] {
        return openTarget && !QApplication::activeModalWidget() && !QApplication::activePopupWidget();
    });
    QObject::connect(openTarget.data(), &QObject::destroyed, &app, [&app] {
        app.pendingOpenEvents().setReady(false);
    });
#else // defined(Q_OS_MAC)
    qtCtx()->mainWindow()->setUnload(!qtCtx()->settings()->getBool(WB_TRAY_ENABLED));
    QObject::connect(&app, &QtSingleCoreApplication::messageReceived, qtCtx()->mainWindow(), &MainWindow::parseInstanceLine);
#endif // defined(Q_OS_MAC)

    ctx.createHubManager();

    qtCtx()->settings()->loadTheme();

#ifdef USE_ASPELL
#if defined(Q_OS_MAC)
    // TEMP: disable Aspell on macOS while debugging startup crashes.
    // Current build logs show Aspell cannot find dictionaries and then the app segfaults.
    qtCtx()->settings()->setBool(WB_APP_ENABLE_ASPELL, false);
#else
    if (qtCtx()->settings()->getBool(WB_APP_ENABLE_ASPELL))
        ctx.createSpellCheck();
#endif
#endif

    ctx.createNotification();

#ifdef USE_JS
#if defined(Q_OS_MAC)
    // TEMP: disable JS/Lua script engine on macOS while debugging shutdown crashes.
#else
    ctx.createScriptEngine();
    QObject::connect(qtCtx()->scriptEngine(), SIGNAL(scriptChanged(QString)), qtCtx()->mainWindow(), SLOT(slotJSFileChanged(QString)));
#endif
#endif

    ctx.createFinishedUploads();
    qtCtx()->arenaWidgetManager()->add(ctx.finishedUploads());
    ctx.createFinishedDownloads();
    qtCtx()->arenaWidgetManager()->add(ctx.finishedDownloads());
    ctx.createQueuedUsers();
    qtCtx()->arenaWidgetManager()->add(ctx.queuedUsers());

    const QStringList startupArguments = app.arguments();
    const bool showMainWindow = !qtCtx()->settings()->getBool(WB_MAINWINDOW_HIDE) ||
                                !qtCtx()->settings()->getBool(WB_TRAY_ENABLED);

    if (showMainWindow)
        qtCtx()->mainWindow()->show();

    QTimer::singleShot(0, qtCtx()->mainWindow(), [startupArguments
#if defined(Q_OS_MAC)
                                               , &app
#endif
    ]() {
        if (!qtCtx() || !qtCtx()->mainWindow())
            return;

        if (!qtCtx()->mainWindow()->ensureNickConfigured())
            return;

        qtCtx()->mainWindow()->autoconnect();
        qtCtx()->mainWindow()->parseCmdLine(startupArguments);
#if defined(Q_OS_MAC)
        // Startup arguments can also open modal dialogs; finish those first.
        app.pendingOpenEvents().setReady(true);
#endif
    });

    ret = app.exec();

#if defined(Q_OS_MAC)
    // macOS emergency exit path:
    // background core threads can still fire ClientListener callbacks during
    // DCContext shutdown/destruction, causing EXC_BAD_ACCESS on exit.
    // Save settings, release the single-instance marker, flush stdio, and
    // terminate the process before core teardown.
    if (qtCtx() && qtCtx()->settings())
        qtCtx()->settings()->save();
    app.releaseSingleInstance();
    fflush(nullptr);
    std::_Exit(ret);
#endif

#if !defined(Q_OS_MAC)
    dcContext->shutdown();
#endif
#if !defined(Q_OS_MAC)
    // Non-macOS: keep explicit shutdown path.
    if (ctx.mainWindow()) {
        ctx.mainWindow()->hide();
        ctx.mainWindow()->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        ctx.destroyMainWindow();
    }

    dcContext->shutdown();
    dcContext.reset();
#endif

    return ret;
}


void parseCmdLine(const QStringList &args){
    for (const auto &arg : args){
        if (arg == "-h" || arg == "--help"){
            About().printHelp();

            exit(0);
        }
        else if (arg == "-V" || arg == "--version"){
            About().printVersion();

            exit(0);
        }
    }
}
