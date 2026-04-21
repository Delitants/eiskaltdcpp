/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QWidget>
#include "ui_UISettingsGUI.h"

class QCheckBox;
class QLineEdit;
class QSpinBox;
class QToolButton;

class SettingsGUI :
        public QWidget,
        private Ui::UISettingsGUI
{
Q_OBJECT
public:
    explicit SettingsGUI(QWidget *parent = nullptr);
    virtual ~SettingsGUI();

private:
    void init();

private Q_SLOTS:
    void slotChatColorItemClicked(QListWidgetItem *);
    void slotBrowseLng();
    void slotLngIndexChanged(int);
    void slotGetColor();
    void slotSetTransparency(int);
    void slotResetTransferColors();
    void slotBrowseChatPictureDir();

Q_SIGNALS:
    void saveFonts();

public Q_SLOTS:
    void ok();

private:
    // clean colors (without transparency)
    QColor h_color;
    QColor shared_files_color;
    QColor chat_background_color;
    QColor downloads_clr;
    QColor uploads_clr;

    QLineEdit *lineEdit_CHAT_PICTURE_DIR = nullptr;
    QToolButton *toolButton_CHAT_PICTURE_DIR = nullptr;
    QCheckBox *checkBox_CHAT_PICTURE_AUTOCLEAN = nullptr;
    QSpinBox *spinBox_CHAT_PICTURE_DAYS = nullptr;
};
