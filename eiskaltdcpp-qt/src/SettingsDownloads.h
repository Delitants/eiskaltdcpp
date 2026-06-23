/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QPoint>
#include <QWidget>

#include "ui_UISettingsDownloads.h"

#include "dcpp/stdinc.h"
#include "dcpp/SettingsManager.h"

class SettingsDownloads :
        public QWidget,
        private Ui::UISettingsDownloads
{
    Q_OBJECT
public:
    SettingsDownloads(QWidget* = nullptr);
    virtual ~SettingsDownloads();

public slots:
    void  ok();

private slots:
    void slotBrowse();
    void slotAddDownloadTo();
    void slotRemoveDownloadTo();
    void slotDownloadToMenu(const QPoint&);
    void slotDownloadToSelectionChanged();
    void slotCfgPublic();

private:
    void init();
    void saveDownloadToEntries();

    QMap< dcpp::SettingsManager::IntSetting, int > other_settings;
};
