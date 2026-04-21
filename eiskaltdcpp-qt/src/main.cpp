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
#include "dcpp/Thread.h"
#include "dcpp/Singleton.h"

#include "WulforUtil.h"
#include "WulforSettings.h"
#include "QtContext.h"
#include "QtContextAware.h"
#include "HubManager.h"
#include "Notification.h"
#include "VersionGlobal.h"
#include "FinishedTransfers.h"
#include "QueuedUsers.h"
#include "ArenaWidgetManager.h"
#include "ArenaWidgetFactory.h"
#include "MainWindow.h"
#include "GlobalTimer.h"
#include "EmoticonFactory.h"

#if defined(Q_OS_HAIKU)
#include "EiskaltApp_haiku.h"
#elif defined(Q_OS_MAC)
#include "EiskaltApp_mac.h"
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
#include <QMainWindow>
#include <QRegularExpression>
#include <QObject>
#include <QScopeGuard>
#include <cstdlib>

#ifdef DBUS_NOTIFY
#include <QtDBus>
#endif

void callBack(void *, const std::string &a)
{
    std::cout << QObject::tr("Loading: ").toStdString() << a << std::endl;
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
    char buf[512];
    snprintf(buf, sizeof(buf),
             "EiskaltDC++ crashed during startup.\n\n"
             "Exception code: 0x%08lX\nAddress: %p\n\n"
             "This may indicate a missing DLL or incompatible library.\n"
             "Please report this to the developers.",
             ep->ExceptionRecord->ExceptionCode,
             ep->ExceptionRecord->ExceptionAddress);
    MessageBoxA(nullptr, buf, "EiskaltDC++ — Fatal Error",
                MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#if defined(Q_OS_MAC)
// Dock click handling is done via EiskaltEventFilter in EiskaltApp_mac.h
// using QApplicationStateChangeEvent
static bool isDarkMacPalette(const QPalette &pal)
{
    return (pal.color(QPalette::Window).lightness() + pal.color(QPalette::Base).lightness()) / 2 < 128;
}

static QColor macBorderColor(const QPalette &pal, const bool strong)
{
    const bool dark = isDarkMacPalette(pal);
    const QColor window = pal.color(QPalette::Window);
    QColor border = dark
            ? window.lighter(strong ? 185 : 165)
            : window.darker(strong ? 155 : 135);

    if (qAbs(border.lightness() - window.lightness()) < (strong ? 40 : 26)) {
        const QColor text = pal.color(QPalette::Text);
        border = dark
                ? text.lighter(strong ? 170 : 145)
                : text.darker(strong ? 170 : 145);
    }

    return border;
}

static QColor macFocusColor(const QPalette &pal)
{
    QColor focus = pal.color(QPalette::Highlight);
    const bool dark = isDarkMacPalette(pal);

    if (dark && focus.lightness() < 150)
        focus = focus.lighter(145);
    else if (!dark && focus.lightness() > 205)
        focus = focus.darker(118);

    return focus;
}

static void applyMacInputContrastStyle(QApplication &app)
{
    const QPalette pal = app.palette();
    const bool dark = isDarkMacPalette(pal);
    const QColor inputBorder = macBorderColor(pal, false);
    const QColor panelBorder = macBorderColor(pal, true);
    const QColor focusBorder = macFocusColor(pal);
    const QColor panelBg = dark ? pal.color(QPalette::Base).lighter(108) : pal.color(QPalette::Base);
    const QColor altBg = dark ? pal.color(QPalette::Window).lighter(112) : pal.color(QPalette::AlternateBase);

    app.setStyleSheet(app.styleSheet() + QStringLiteral(
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox {"
        " border: 1px solid %1;"
        " border-radius: 6px;"
        " padding: 2px 6px;"
        " background: palette(base);"
        " color: palette(text);"
        "}"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QAbstractSpinBox:focus {"
        " border: 1px solid %2;"
        "}"
        "QComboBox::drop-down, QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
        " border-left: 1px solid %1;"
        "}"
        "QAbstractItemView, QListView, QTreeView, QTableView {"
        " border: 1px solid %3;"
        " background: %4;"
        " alternate-background-color: %5;"
        "}"
        "QFrame#frame_INPUT {"
        " border: 1px solid %3;"
        " border-radius: 8px;"
        " background: %4;"
        "}"
        "QFrame#settingsPagePanel {"
        " border: 1px solid %3;"
        " background: %4;"
        "}"
    ).arg(inputBorder.name(), focusBorder.name(), panelBorder.name(), panelBg.name(), altBg.name()));
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

    EiskaltApp app(argc, argv, _q(dcpp::Util::getLoginName()+"EDCPP"));
    app.setQuitOnLastWindowClosed(false);
#if defined(Q_OS_MAC)
    applyMacInputContrastStyle(app);
#endif
    int ret = 0;

    parseCmdLine(app.arguments());

    // TEMP: disable single-instance early exit while debugging startup.
    // The old block was causing clean exit(0) before the UI came up.

#if !defined (Q_OS_WIN) && !defined (Q_OS_HAIKU) && defined (__GLIBC__)
    installHandlers();
#endif

#if defined(FORCE_XDG) && !defined(Q_OS_WIN)
    migrateConfig();
#endif

    auto dcContext = dcpp::startup(callBack, nullptr);
    dcContext->getTimerManager()->start();

    dcContext->getHashManager()->setPriority(Thread::IDLE);

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
    ctx.settings()->loadTheme();

    ctx.createWulforUtil();
    ctx.settings()->loadTranslation();
#if defined(Q_OS_MAC)
    // On macOS, enable tray icon (appears in menu bar) for window show/hide functionality
    qtCtx()->settings()->setBool(WB_TRAY_ENABLED, true);
#endif

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

    app.setWindowIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiICON_APPL));

    ctx.createArenaWidgetManager();

    ctx.createMainWindow();
#if defined(Q_OS_MAC)
    qtCtx()->mainWindow()->setUnload(false);
#else // defined(Q_OS_MAC)
    qtCtx()->mainWindow()->setUnload(!qtCtx()->settings()->getBool(WB_TRAY_ENABLED));
#endif // defined(Q_OS_MAC)

    QObject::connect(&app, &QtSingleCoreApplication::messageReceived, qtCtx()->mainWindow(), &MainWindow::parseInstanceLine);

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

    qtCtx()->mainWindow()->autoconnect();
    qtCtx()->mainWindow()->parseCmdLine(app.arguments());

    if (!qtCtx()->settings()->getBool(WB_MAINWINDOW_HIDE) || !qtCtx()->settings()->getBool(WB_TRAY_ENABLED))
        qtCtx()->mainWindow()->show();    ret = app.exec();

#if defined(Q_OS_MAC)
    // macOS emergency exit path:
    // background core threads can still fire ClientListener callbacks during
    // DCContext shutdown/destruction, causing EXC_BAD_ACCESS on exit.
    // Save settings, flush stdio, and terminate the process before core teardown.
    if (qtCtx() && qtCtx()->settings())
        qtCtx()->settings()->save();
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
