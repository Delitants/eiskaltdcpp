/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QScrollArea>

#include "ui_UISettings.h"
class SettingsConnection;

#ifdef USE_TORRENT
class SettingsTorrent;
#endif

class Settings :
        public QDialog,
        private Ui::UISettings
{
    Q_OBJECT

    typedef QMap<QListWidgetItem*, int> WidgetMap;
public:
    enum class Page: int {
        Personal=0,
        Connection,
        Downloads,
        Sharing,
        GUI,
        Notifications,
        Logs,
        UserCommands,
        Shortcuts,
        History,
        Advanced
#ifdef USE_TORRENT
        , Torrent
#endif
    };

    Settings();
    virtual ~Settings();

    void navigate(enum Page, int tab = -1);

public slots:
    void accept() override;

protected:
    void changeEvent(QEvent *event) override;

signals:
    void timeToDie();

private slots:
    void slotItemActivated(QListWidgetItem*);
    void dirty();

private:
    void reloadCategoryIcons();
    void init();

    inline void setMouseScroller(QWidget *w);
    inline QWidget *prepareWidget(QWidget *w);

    WidgetMap widgets;
    bool is_dirty;
    bool appearance_style_in_progress;
    SettingsConnection *connectionPage = nullptr;
#ifdef USE_TORRENT
    SettingsTorrent *torrentPage = nullptr;
#endif
};
