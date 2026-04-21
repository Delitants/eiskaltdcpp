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

#pragma once

#include <QApplication>
#include <QEvent>
#include <QFileOpenEvent>
#include <QObject>
#include <QSessionManager>
#include <QTimer>

#include "QtContext.h"
#include "QtContextAware.h"
#include "MainWindow.h"
#include "qtsingleapp/qtsinglecoreapplication.h"
#include "WulforSettings.h"
#include "dcpp/Util.h"

class EiskaltEventFilter : public QObject
{
    Q_OBJECT

signals:
    void fileOpenRequested(const QString &path);
    void clickedOnDock();

public:
    explicit EiskaltEventFilter(QObject *parent = nullptr)
        : QObject(parent), counter(0), has_activity(true),
          prevAppState(Qt::ApplicationHidden)
    {
        timer.setInterval(60000);
        connect(&timer, &QTimer::timeout, this, &EiskaltEventFilter::tick);
        timer.start();
    }

    virtual ~EiskaltEventFilter() {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        Q_UNUSED(obj);

        if (!event)
            return false;

        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::Wheel:
        {
            has_activity = true;
            counter = 0;
            break;
        }
        case QEvent::ApplicationStateChange:
        {
            // Detect Dock click via application state change
            // https://stackoverflow.com/questions/15143369/qt-on-os-x-how-to-detect-clicking-the-app-dock-icon
            auto *ev = static_cast<QApplicationStateChangeEvent *>(event);
            if (prevAppState == Qt::ApplicationActive && ev->applicationState() == Qt::ApplicationActive) {
                emit clickedOnDock();
            }
            prevAppState = ev->applicationState();
            break;
        }
        case QEvent::FileOpen: {
            auto *foe = static_cast<QFileOpenEvent *>(event);
            if (foe)
                emit fileOpenRequested(foe->file());
            break;
        }
        default:
        {
            has_activity = false;
            break;
        }
        }

        return QObject::eventFilter(obj, event);
    }

private slots:
    void tick()
    {
        if (!has_activity)
            ++counter;

        if (qtCtx()->settings()->getBool(WB_APP_AUTOAWAY_BY_TIMER)) {
            int mins = qtCtx()->settings()->getInt(WI_APP_AUTOAWAY_INTERVAL);
            if (!mins)
                return;
            int mins_done = (counter * timer.interval() / 1000) / 60;
            if (mins <= mins_done)
                dcpp::Util::setAway(qtCtx()->dcCtx(), true);
        }
        else if (has_activity && !dcpp::Util::getManualAway())
            dcpp::Util::setAway(qtCtx()->dcCtx(), false);
    }

private:
    QTimer timer;
    int counter;
    bool has_activity;
    Qt::ApplicationState prevAppState;
};

class EiskaltApp : public QtSingleCoreApplication
{
    Q_OBJECT

public:
    EiskaltApp(int &argc, char **argv, const QString &id)
        : QtSingleCoreApplication(argc, argv, id),
          dockFilter_(new EiskaltEventFilter(this))
    {
        installEventFilter(dockFilter_);

        QObject::connect(dockFilter_, &EiskaltEventFilter::fileOpenRequested,
                         this, &EiskaltApp::fileOpenRequested);
        QObject::connect(dockFilter_, &EiskaltEventFilter::clickedOnDock,
                         this, &EiskaltApp::handleDockClick);
    }

signals:
    void fileOpenRequested(const QString &path);

public slots:
    void handleDockClick()
    {
        // Show/hide main window when Dock icon is clicked
        if (qtCtx() && qtCtx()->mainWindow()) {
            MainWindow *mw = qtCtx()->mainWindow();
            if (mw->isVisible() && mw->isActiveWindow()) {
                mw->hide();
            } else {
                mw->show();
                mw->showNormal();
                mw->raise();
                mw->activateWindow();
                mw->setWindowState(mw->windowState() & ~Qt::WindowMinimized);
            }
        }
    }

protected:
    bool event(QEvent *e) override
    {
        if (e && e->type() == QEvent::Quit && qtCtx() && qtCtx()->mainWindow()) {
            auto *mw = qtCtx()->mainWindow();
            mw->setUnload(true);

            if (!mw->confirmExit()) {
                e->ignore();
                return true;
            }

            mw->beginExit();
            mw->close();
            return true;
        }

        return QtSingleCoreApplication::event(e);
    }

    void commitData(QSessionManager &manager)
    {
        if (qtCtx() && qtCtx()->mainWindow()) {
            auto *mw = qtCtx()->mainWindow();
            mw->setUnload(true);

            if (!mw->confirmExit()) {
                manager.cancel();
                return;
            }

            mw->beginExit();
            mw->close();
        }

        manager.release();
    }

    void saveState(QSessionManager &) {}

private:
    EiskaltEventFilter *dockFilter_ = nullptr;
};
