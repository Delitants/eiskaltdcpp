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

#include "SettingsGUI.h"
#include "ChatEdit.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "dcpp/DCPlusPlus.h"
#include "WulforSettings.h"
#include "WulforUtil.h"
#include "MainWindow.h"
#include "Notification.h"
#include "CustomFontModel.h"

#include <QListWidgetItem>
#include <QPixmap>
#include <QColor>
#include <QColorDialog>
#include <QStyleFactory>
#include <QFontDialog>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QSystemTrayIcon>
#include <QHeaderView>
#include <QMap>
#include <QComboBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QToolButton>

#ifndef CLIENT_ICONS_DIR
#define CLIENT_ICONS_DIR ""
#endif

SettingsGUI::SettingsGUI(QWidget *parent) :
    QWidget(parent)
{
    setupUi(this);

    init();
}

SettingsGUI::~SettingsGUI(){

}

void SettingsGUI::init(){
    {//Basic tab
        WulforUtil *WU = qtCtx()->wulforUtil();
        auto polishCombo = [](QComboBox *combo, int minChars = 0) {
            if (!combo)
                return;

            combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            if (minChars > 0)
                combo->setMinimumContentsLength(minChars);
            combo->setStyleSheet(combo->styleSheet() + QStringLiteral("QComboBox { combobox-popup: 0; }"));
            combo->setMaxVisibleItems(qMax(2, qMin(combo->count(), 10)));
            if (combo->view())
                combo->view()->setTextElideMode(Qt::ElideNone);
        };

        int i = 0;
        int k = -1;

        QDir translationsDir(WU->getTranslationsPath());

        const QMap<QString, QString> langNames ({
            { "en.qm",       tr("English") },
            { "ru.qm",       tr("Russian") },
            { "be.qm",       tr("Belarusian") },
            { "hu.qm",       tr("Hungarian") },
            { "fr.qm",       tr("French") },
            { "pl.qm",       tr("Polish") },
            { "pt_BR.qm",    tr("Portuguese (Brazil)") },
            { "sr.qm",       tr("Serbian (Cyrillic)") },
            { "sr@latin.qm", tr("Serbian (Latin)") },
            { "uk.qm",       tr("Ukrainian") },
            { "es.qm",       tr("Spanish") },
            { "eu.qm",       tr("Basque") },
            { "bg.qm",       tr("Bulgarian") },
            { "sk.qm",       tr("Slovak") },
            { "cs.qm",       tr("Czech") },
            { "de.qm",       tr("German") },
            { "el.qm",       tr("Greek") },
            { "it.qm",       tr("Italian") },
            { "vi.qm",       tr("Vietnamese") },
            { "zh_CN.qm",    tr("Chinese (China)") },
            { "sv_SE.qm",    tr("Swedish (Sweden)") },
            { "tr.qm",       tr("Turkish") },
            { "da.qm",       tr("Danish") },
            { "ka.qm",       tr("Georgian") },
        });

        QString full_path;
        QString lang;

        for (const auto &f : translationsDir.entryList(QDir::Files | QDir::NoSymLinks)){
            full_path = QDir::toNativeSeparators( translationsDir.filePath(f) );
            lang = langNames[f];

            if (!lang.isEmpty()){
                comboBox_LANGS->addItem(lang, full_path);

                if (qtCtx()->settings()->getStr(WS_TRANSLATION_FILE).endsWith(f))
                    k = i;

                ++i;
            }
        }
        comboBox_LANGS->setCurrentIndex(k);
        polishCombo(comboBox_LANGS, 14);

        lineEdit_LANGFILE->setText(qtCtx()->settings()->getStr(WS_TRANSLATION_FILE));

        toolButton_LANGBROWSE->setIcon(WU->getPixmap(WulforUtil::eiFOLDER_BLUE));

        if (qtCtx()->settings()->getBool(WB_MAINWINDOW_REMEMBER))
            radioButton_REMEMBER->setChecked(true);
        else if (qtCtx()->settings()->getBool(WB_MAINWINDOW_HIDE))
            radioButton_HIDE->setChecked(true);
        else
            radioButton_SHOW->setChecked(true);
        checkBox_MINIMIZE_ON_CLOSE->setChecked(qtCtx()->settings()->getBool(WB_MAINWINDOW_MINIMIZE_ON_CLOSE));

        groupBox_TRAY->setChecked(qtCtx()->settings()->getBool(WB_TRAY_ENABLED));
        groupBox_TRAY->setEnabled(QSystemTrayIcon::isSystemTrayAvailable());
        if (qtCtx()->settings()->getBool(WB_TRAY_ICON_MONOCHROME))
            radioButton_TRAY_MONOCHROME->setChecked(true);
        else
            radioButton_TRAY_COLORED->setChecked(true);

        if (qtCtx()->settings()->getBool(WB_MAINWINDOW_USE_SIDEBAR))
            comboBox_TABBAR->setCurrentIndex(2);
        else if (qtCtx()->settings()->getBool(WB_MAINWINDOW_USE_M_TABBAR))
            comboBox_TABBAR->setCurrentIndex(1);
        else
            comboBox_TABBAR->setCurrentIndex(0);
        polishCombo(comboBox_TABBAR, 28);

        checkBox_HIDE_ICONS_IN_MENU->setChecked(qtCtx()->settings()->getBool("mainwindow/dont-show-icons-in-menus", false));

        if (!comboBox_APP_ICON_THEME) {
            auto *group = new QGroupBox(tr("Toolbar icon theme"), tab_2);
            auto *layout = new QGridLayout(group);
            auto *labelTheme = new QLabel(tr("Theme"), group);

            comboBox_APP_ICON_THEME = new QComboBox(group);
            comboBox_APP_ICON_THEME->addItem(tr("Default"), QStringLiteral("default"));
            comboBox_APP_ICON_THEME->addItem(QStringLiteral("Faenza"), QStringLiteral("faenza"));
            comboBox_APP_ICON_THEME->addItem(QStringLiteral("Haiku"), QStringLiteral("haiku"));
            comboBox_APP_ICON_THEME->addItem(tr("Monochrome"), QStringLiteral("monochrome"));
            comboBox_APP_ICON_THEME->addItem(QStringLiteral("Apex"), QStringLiteral("apex"));

            layout->addWidget(labelTheme, 0, 0);
            layout->addWidget(comboBox_APP_ICON_THEME, 0, 1);
            layout->setColumnStretch(1, 1);

            verticalLayout_4->insertWidget(verticalLayout_4->count() - 1, group);
            polishCombo(comboBox_APP_ICON_THEME, 12);
        }

        const QString appIconTheme = qtCtx()->settings()->getStr(WS_APP_ICONTHEME, QStringLiteral("default"));
        const int appIconThemeIndex = comboBox_APP_ICON_THEME->findData(appIconTheme.trimmed().isEmpty()
                                                                        ? QStringLiteral("default")
                                                                        : appIconTheme);
        comboBox_APP_ICON_THEME->setCurrentIndex(appIconThemeIndex >= 0 ? appIconThemeIndex : 0);

        if (!comboBox_USER_ICON_THEME) {
            auto *group = new QGroupBox(tr("User list icon theme"), tab_2);
            auto *layout = new QGridLayout(group);
            auto *labelTheme = new QLabel(tr("Theme"), group);

            comboBox_USER_ICON_THEME = new QComboBox(group);
            comboBox_USER_ICON_THEME->addItem(tr("Original"), QStringLiteral("default"));
            comboBox_USER_ICON_THEME->addItem(QStringLiteral("Apex"), QStringLiteral("apex"));

            layout->addWidget(labelTheme, 0, 0);
            layout->addWidget(comboBox_USER_ICON_THEME, 0, 1);
            layout->setColumnStretch(1, 1);

            verticalLayout_4->insertWidget(verticalLayout_4->count() - 1, group);
            polishCombo(comboBox_USER_ICON_THEME, 12);
        }

        const QString userIconTheme = qtCtx()->settings()->getStr(WS_APP_USERTHEME, QStringLiteral("default"));
        const int userIconThemeIndex = comboBox_USER_ICON_THEME->findData(userIconTheme == QStringLiteral("apex")
                                                                         ? QStringLiteral("apex")
                                                                         : QStringLiteral("default"));
        comboBox_USER_ICON_THEME->setCurrentIndex(userIconThemeIndex >= 0 ? userIconThemeIndex : 0);

    }
    {//Chat tab
        checkBox_CHATJOINS->setChecked(qtCtx()->settings()->getBool(WB_CHAT_SHOW_JOINS));
        checkBox_JOINSFAV->setChecked(qtCtx()->settings()->getBool(WB_CHAT_SHOW_JOINS_FAV));
        checkBox_CHATHIDDEN->setChecked(qtCtx()->settings()->getBool(WB_SHOW_HIDDEN_USERS));
        checkBox_IGNOREPMHUB->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::IGNORE_HUB_PMS, true));
        checkBox_IGNOREPMBOT->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::IGNORE_BOT_PMS, true));
        checkBox_REDIRECTPMBOT->setChecked(qtCtx()->settings()->getBool(WB_CHAT_REDIRECT_BOT_PMS));
        checkBox_REDIRECT_UNREAD->setChecked(qtCtx()->settings()->getBool("hubframe/redirect-pm-to-main-chat", false));
        checkBox_KEEPFOCUS->setChecked(qtCtx()->settings()->getBool(WB_CHAT_KEEPFOCUS));
        checkBox_UNREADEN_DRAW_LINE->setChecked(qtCtx()->settings()->getBool("hubframe/unreaden-draw-line", true));
        checkBox_USE_CTRL_ENTER->setChecked(qtCtx()->settings()->getBool(WB_USE_CTRL_ENTER));
        checkBox_ROTATING->setChecked(qtCtx()->settings()->getBool(WB_CHAT_ROTATING_MSGS));
        checkBox_EMOT->hide();
        checkBox_EMOTFORCE->hide();
        checkBox_SMILEPANEL->hide();
        checkBox_HIDESMILEPANEL->hide();
    }
    {//Chat (extended) tab
        comboBox_DBL_CLICK->setCurrentIndex(qtCtx()->settings()->getInt(WI_CHAT_DBLCLICK_ACT));
        comboBox_MDL_CLICK->setCurrentIndex(qtCtx()->settings()->getInt(WI_CHAT_MDLCLICK_ACT));
        comboBox_DEF_MAGNET_ACTION->setCurrentIndex(qtCtx()->settings()->getInt(WI_DEF_MAGNET_ACTION));
        comboBox_APP_UNIT_BASE->setCurrentIndex(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::APP_UNIT_BASE, true));
        checkBox_HIGHLIGHTFAVS->setChecked(qtCtx()->settings()->getBool(WB_CHAT_HIGHLIGHT_FAVS));
        checkBox_CHAT_SHOW_IP->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::USE_IP, true));
        checkBox_CHAT_SHOW_CC->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::GET_USER_COUNTRY, true));
        checkBox_BB_CODE->setChecked(qtCtx()->settings()->getBool("hubframe/use-bb-code", true));
        lineEdit_TIMESTAMP->setText(qtCtx()->settings()->getStr(WS_CHAT_TIMESTAMP));

        if (!lineEdit_CHAT_PICTURE_DIR) {
            auto *group = new QGroupBox(tr("Chat pictures"), tab_5);
            auto *layout = new QGridLayout(group);
            auto *labelDir = new QLabel(tr("Folder"), group);
            lineEdit_CHAT_PICTURE_DIR = new QLineEdit(group);
            toolButton_CHAT_PICTURE_DIR = new QToolButton(group);
            toolButton_CHAT_PICTURE_DIR->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiFOLDER_BLUE));
            checkBox_CHAT_PICTURE_AUTOCLEAN = new QCheckBox(tr("Auto-clean files older than"), group);
            spinBox_CHAT_PICTURE_DAYS = new QSpinBox(group);
            spinBox_CHAT_PICTURE_DAYS->setRange(1, 365);
            spinBox_CHAT_PICTURE_DAYS->setSuffix(tr(" days"));
            spinBox_CHAT_PICTURE_DAYS->setMaximumWidth(100);

            layout->addWidget(labelDir, 0, 0);
            layout->addWidget(lineEdit_CHAT_PICTURE_DIR, 0, 1);
            layout->addWidget(toolButton_CHAT_PICTURE_DIR, 0, 2);
            layout->addWidget(checkBox_CHAT_PICTURE_AUTOCLEAN, 1, 0, 1, 2);
            layout->addWidget(spinBox_CHAT_PICTURE_DAYS, 1, 2);
            layout->setColumnStretch(1, 1);

            verticalLayout_5->insertWidget(verticalLayout_5->count() - 1, group);

            connect(toolButton_CHAT_PICTURE_DIR, &QToolButton::clicked, this, &SettingsGUI::slotBrowseChatPictureDir);
            connect(checkBox_CHAT_PICTURE_AUTOCLEAN, &QCheckBox::toggled, spinBox_CHAT_PICTURE_DAYS, &QWidget::setEnabled);
        }

        const QString chatPictureDir = qtCtx()->settings()->getStr(WS_CHAT_PICTURE_DIR, ChatEdit::defaultChatPictureDir());
        lineEdit_CHAT_PICTURE_DIR->setText(chatPictureDir);
        checkBox_CHAT_PICTURE_AUTOCLEAN->setChecked(qtCtx()->settings()->getBool(WB_CHAT_PICTURE_AUTOCLEAN, true));
        spinBox_CHAT_PICTURE_DAYS->setValue(qtCtx()->settings()->getInt(WI_CHAT_PICTURE_CLEAN_DAYS, 7));
        spinBox_CHAT_PICTURE_DAYS->setEnabled(checkBox_CHAT_PICTURE_AUTOCLEAN->isChecked());

        spinBox_OUT_IN_HIST->setValue(qtCtx()->settings()->getInt(WI_OUT_IN_HIST));
        spinBox_PARAGRAPHS->setValue(qtCtx()->settings()->getInt(WI_CHAT_MAXPARAGRAPHS));

        comboBox_CHAT_SEPARATOR->setCurrentIndex(comboBox_CHAT_SEPARATOR->findText(qtCtx()->settings()->getStr(WS_CHAT_SEPARATOR)));
    }
    {//Color tab
        QColor c;
        QPixmap p(10, 10);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_LOCAL_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Local user"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_OP_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Operator"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_BOT_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Bot"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_PRIV_LOCAL_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Private: local user"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_PRIV_USER_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Private: user"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_SAY_NICK));
        p.fill(c);
        new QListWidgetItem(p, tr("Chat: Say nick"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_STAT_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Status"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_USER_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("User"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_FAVUSER_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Favorite User"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_TIME_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Time stamp"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_MSG_COLOR));
        p.fill(c);
        new QListWidgetItem(p, tr("Message"), listWidget_CHATCOLOR);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_FIND_COLOR));
        h_color = c;

        c.setAlpha(qtCtx()->settings()->getInt(WI_CHAT_FIND_COLOR_ALPHA));
        p.fill(c);
        toolButton_H_COLOR->setIcon(p);

        c = QColor::fromString(qtCtx()->settings()->getStr(WS_APP_SHARED_FILES_COLOR));
        shared_files_color = c;
        c.setAlpha(qtCtx()->settings()->getInt(WI_APP_SHARED_FILES_ALPHA));
        p.fill(c);
        toolButton_SHAREDFILES->setIcon(p);

        downloads_clr = qvariant_cast<QColor>(qtCtx()->settings()->getVar("transferview/download-bar-color", QColor()));
        uploads_clr = qvariant_cast<QColor>(qtCtx()->settings()->getVar("transferview/upload-bar-color", QColor()));

        if (downloads_clr.isValid()){
            c = downloads_clr;
            p.fill(c);

            toolButton_DOWNLOADSCLR->setIcon(p);
        }
        if (uploads_clr.isValid()){
            c = uploads_clr;
            p.fill(c);

            toolButton_UPLOADSCLR->setIcon(p);
        }

        checkBox_CHAT_BACKGROUND_COLOR->setChecked(qtCtx()->settings()->getBool("hubframe/change-chat-background-color", false));
        toolButton_CHAT_BACKGROUND_COLOR->setEnabled(qtCtx()->settings()->getBool("hubframe/change-chat-background-color", false));
        if (!qtCtx()->settings()->getStr("hubframe/chat-background-color", "").isEmpty()){
            c = QColor::fromString(qtCtx()->settings()->getStr("hubframe/chat-background-color"));
            chat_background_color = c;
            c.setAlpha(255);
            p.fill(c);
            toolButton_CHAT_BACKGROUND_COLOR->setIcon(p);
        }

        horizontalSlider_H_COLOR->setValue(qtCtx()->settings()->getInt(WI_CHAT_FIND_COLOR_ALPHA));
        horizontalSlider_SHAREDFILES->setValue(qtCtx()->settings()->getInt(WI_APP_SHARED_FILES_ALPHA));
    }
    {// Fonts tab
        CustomFontModel *model = new CustomFontModel(this);
        tableView->setModel(model);

        tableView->horizontalHeader()->restoreState(QByteArray::fromBase64(qtCtx()->settings()->getStr(WS_SETTINGS_GUI_FONTS_STATE).toUtf8()));

        connect(tableView, &QTableView::doubleClicked, model, &CustomFontModel::itemDoubleClicked);
        connect(this, &SettingsGUI::saveFonts, model, &CustomFontModel::ok);
    }

    connect(listWidget_CHATCOLOR, &QListWidget::itemDoubleClicked, this, &SettingsGUI::slotChatColorItemClicked);
    connect(toolButton_LANGBROWSE, &QToolButton::clicked, this, &SettingsGUI::slotBrowseLng);
    connect(comboBox_LANGS, qOverload<int>(&QComboBox::activated), this, &SettingsGUI::slotLngIndexChanged);
    connect(toolButton_H_COLOR, &QToolButton::clicked, this, &SettingsGUI::slotGetColor);
    connect(toolButton_SHAREDFILES, &QToolButton::clicked, this, &SettingsGUI::slotGetColor);
    connect(toolButton_CHAT_BACKGROUND_COLOR, &QToolButton::clicked, this, &SettingsGUI::slotGetColor);
    connect(toolButton_DOWNLOADSCLR, &QToolButton::clicked, this, &SettingsGUI::slotGetColor);
    connect(toolButton_UPLOADSCLR, &QToolButton::clicked, this, &SettingsGUI::slotGetColor);
    connect(pushButton_RESET, &QPushButton::clicked, this, &SettingsGUI::slotResetTransferColors);
    connect(horizontalSlider_H_COLOR, &QSlider::valueChanged, this, &SettingsGUI::slotSetTransparency);
    connect(horizontalSlider_SHAREDFILES, &QSlider::valueChanged, this, &SettingsGUI::slotSetTransparency);
}

void SettingsGUI::ok(){
    SettingsManager *SM = qtCtx()->dcCtx().getSettingsManager();
    {//Basic tab
        qtCtx()->settings()->setStr(WS_TRANSLATION_FILE, lineEdit_LANGFILE->text());

        qtCtx()->settings()->setBool(WB_MAINWINDOW_REMEMBER, radioButton_REMEMBER->isChecked());
        qtCtx()->settings()->setBool(WB_MAINWINDOW_HIDE, radioButton_HIDE->isChecked());
        qtCtx()->settings()->setBool(WB_MAINWINDOW_MINIMIZE_ON_CLOSE, checkBox_MINIMIZE_ON_CLOSE->isChecked());

        const bool trayEnabledChanged = qtCtx()->settings()->getBool(WB_TRAY_ENABLED) != groupBox_TRAY->isChecked();
        const bool trayMonochrome = radioButton_TRAY_MONOCHROME->isChecked();
        const bool trayIconChanged = qtCtx()->settings()->getBool(WB_TRAY_ICON_MONOCHROME) != trayMonochrome;

        qtCtx()->settings()->setBool(WB_TRAY_ENABLED, groupBox_TRAY->isChecked());
        qtCtx()->settings()->setBool(WB_TRAY_ICON_MONOCHROME, trayMonochrome);

        if (trayEnabledChanged)
            qtCtx()->notification()->enableTray(qtCtx()->settings()->getBool(WB_TRAY_ENABLED));
        else if (trayIconChanged)
            qtCtx()->notification()->resetTrayIcon();

        if (comboBox_TABBAR->currentIndex() == 2){
            qtCtx()->settings()->setBool(WB_MAINWINDOW_USE_SIDEBAR, true);
            qtCtx()->settings()->setBool(WB_MAINWINDOW_USE_M_TABBAR, false);
        }
        else if (comboBox_TABBAR->currentIndex() == 1){
            qtCtx()->settings()->setBool(WB_MAINWINDOW_USE_SIDEBAR, false);
            qtCtx()->settings()->setBool(WB_MAINWINDOW_USE_M_TABBAR, true);
        }
        else{
            qtCtx()->settings()->setBool(WB_MAINWINDOW_USE_SIDEBAR, false);
            qtCtx()->settings()->setBool(WB_MAINWINDOW_USE_M_TABBAR, false);
        }

        qtCtx()->settings()->setBool("mainwindow/dont-show-icons-in-menus", checkBox_HIDE_ICONS_IN_MENU->isChecked());

        const QString oldAppIconTheme = qtCtx()->settings()->getStr(WS_APP_ICONTHEME, QStringLiteral("default"));
        const QString newAppIconTheme = comboBox_APP_ICON_THEME
                ? comboBox_APP_ICON_THEME->currentData().toString()
                : QStringLiteral("default");
        const bool appIconThemeChanged = oldAppIconTheme != newAppIconTheme;

        qtCtx()->settings()->setStr(WS_APP_ICONTHEME, newAppIconTheme);

        if (appIconThemeChanged && qtCtx()->mainWindow())
            qtCtx()->mainWindow()->reloadIconTheme();

        const QString oldUserIconTheme = qtCtx()->settings()->getStr(WS_APP_USERTHEME, QStringLiteral("default"));
        const QString newUserIconTheme = comboBox_USER_ICON_THEME
                ? comboBox_USER_ICON_THEME->currentData().toString()
                : QStringLiteral("default");
        const bool userIconThemeChanged = oldUserIconTheme != newUserIconTheme;

        qtCtx()->settings()->setStr(WS_APP_USERTHEME, newUserIconTheme);

        if (userIconThemeChanged && qtCtx()->wulforUtil()->loadUserIcons()) {
            for (QWidget *widget : QApplication::allWidgets()) {
                if (widget->objectName() != QStringLiteral("treeView_USERS"))
                    continue;

                widget->update();
                if (auto *view = qobject_cast<QAbstractItemView *>(widget))
                    view->viewport()->update();
            }
        }
    }
    {//Chat tab
        qtCtx()->settings()->setBool(WB_SHOW_HIDDEN_USERS, checkBox_CHATHIDDEN->isChecked());
        qtCtx()->settings()->setBool(WB_CHAT_SHOW_JOINS, checkBox_CHATJOINS->isChecked());
        qtCtx()->settings()->setBool(WB_CHAT_SHOW_JOINS_FAV, checkBox_JOINSFAV->isChecked());
        qtCtx()->settings()->setBool(WB_CHAT_REDIRECT_BOT_PMS, checkBox_REDIRECTPMBOT->isChecked());
        qtCtx()->settings()->setBool("hubframe/redirect-pm-to-main-chat", checkBox_REDIRECT_UNREAD->isChecked());
        qtCtx()->settings()->setBool(WB_CHAT_KEEPFOCUS, checkBox_KEEPFOCUS->isChecked());
        qtCtx()->settings()->setBool("hubframe/unreaden-draw-line", checkBox_UNREADEN_DRAW_LINE->isChecked());
        qtCtx()->settings()->setBool(WB_CHAT_ROTATING_MSGS, checkBox_ROTATING->isChecked());
        qtCtx()->settings()->setBool(WB_USE_CTRL_ENTER, checkBox_USE_CTRL_ENTER->isChecked());
        qtCtx()->settings()->setBool(WB_APP_ENABLE_EMOTICON, false);
        qtCtx()->settings()->setBool(WB_APP_FORCE_EMOTICONS, false);
        qtCtx()->settings()->setBool(WB_CHAT_USE_SMILE_PANEL, false);
        qtCtx()->settings()->setBool(WB_CHAT_HIDE_SMILE_PANEL, false);
    }
    {//Chat (extended) tab
        qtCtx()->settings()->setInt(WI_CHAT_DBLCLICK_ACT, comboBox_DBL_CLICK->currentIndex());
        qtCtx()->settings()->setInt(WI_CHAT_MDLCLICK_ACT, comboBox_MDL_CLICK->currentIndex());
        qtCtx()->settings()->setInt(WI_DEF_MAGNET_ACTION, comboBox_DEF_MAGNET_ACTION->currentIndex());
        SM->set(SettingsManager::APP_UNIT_BASE, comboBox_APP_UNIT_BASE->currentIndex());
        qtCtx()->settings()->setBool(WB_CHAT_HIGHLIGHT_FAVS, checkBox_HIGHLIGHTFAVS->isChecked());
        SM->set(SettingsManager::USE_IP, checkBox_CHAT_SHOW_IP->isChecked());
        qtCtx()->settings()->setBool("hubframe/use-bb-code", checkBox_BB_CODE->isChecked());

        qtCtx()->settings()->setStr(WS_CHAT_TIMESTAMP, lineEdit_TIMESTAMP->text());

        qtCtx()->settings()->setInt(WI_OUT_IN_HIST, spinBox_OUT_IN_HIST->value());
        qtCtx()->settings()->setInt(WI_CHAT_MAXPARAGRAPHS, spinBox_PARAGRAPHS->value());

        SM->set(SettingsManager::IGNORE_BOT_PMS, checkBox_IGNOREPMBOT->isChecked());
        SM->set(SettingsManager::IGNORE_HUB_PMS, checkBox_IGNOREPMHUB->isChecked());
        SM->set(SettingsManager::GET_USER_COUNTRY, checkBox_CHAT_SHOW_CC->isChecked());
        qtCtx()->settings()->setStr(WS_CHAT_SEPARATOR, comboBox_CHAT_SEPARATOR->currentText());
        if (lineEdit_CHAT_PICTURE_DIR)
            qtCtx()->settings()->setStr(WS_CHAT_PICTURE_DIR, lineEdit_CHAT_PICTURE_DIR->text().trimmed());
        if (checkBox_CHAT_PICTURE_AUTOCLEAN)
            qtCtx()->settings()->setBool(WB_CHAT_PICTURE_AUTOCLEAN, checkBox_CHAT_PICTURE_AUTOCLEAN->isChecked());
        if (spinBox_CHAT_PICTURE_DAYS)
            qtCtx()->settings()->setInt(WI_CHAT_PICTURE_CLEAN_DAYS, spinBox_CHAT_PICTURE_DAYS->value());
    }
    {//Color tab
        int i = 0;

        qtCtx()->settings()->setStr(WS_CHAT_LOCAL_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_OP_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_BOT_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_PRIV_LOCAL_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_PRIV_USER_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_SAY_NICK, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_STAT_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_USER_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_FAVUSER_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_TIME_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());
        qtCtx()->settings()->setStr(WS_CHAT_MSG_COLOR, QColor(listWidget_CHATCOLOR->item(i++)->icon().pixmap(10, 10).toImage().pixel(0, 0)).name());

        qtCtx()->settings()->setStr(WS_CHAT_FIND_COLOR, h_color.name());
        qtCtx()->settings()->setInt(WI_CHAT_FIND_COLOR_ALPHA, horizontalSlider_H_COLOR->value());

        qtCtx()->settings()->setStr(WS_APP_SHARED_FILES_COLOR, shared_files_color.name());
        qtCtx()->settings()->setInt(WI_APP_SHARED_FILES_ALPHA, horizontalSlider_SHAREDFILES->value());

        qtCtx()->settings()->setBool("hubframe/change-chat-background-color", checkBox_CHAT_BACKGROUND_COLOR->isChecked());
        if (chat_background_color.isValid())
            qtCtx()->settings()->setStr("hubframe/chat-background-color", chat_background_color.name());
        if (!checkBox_CHAT_BACKGROUND_COLOR->isChecked())
            qtCtx()->settings()->setStr("hubframe/chat-background-color", QTextEdit().palette().color(QPalette::Active, QPalette::Base).name());
        if (downloads_clr.isValid())
            qtCtx()->settings()->setVar("transferview/download-bar-color", downloads_clr);
        if (uploads_clr.isValid())
            qtCtx()->settings()->setVar("transferview/upload-bar-color", uploads_clr);
    }

    qtCtx()->settings()->setStr(WS_SETTINGS_GUI_FONTS_STATE, tableView->horizontalHeader()->saveState().toBase64());

    emit saveFonts();

    qtCtx()->settings()->setStr(WS_APP_EMOTICON_THEME, QString());
}

void SettingsGUI::slotChatColorItemClicked(QListWidgetItem *item){
    QPixmap p(10, 10);
    QColor color(item->icon().pixmap(10, 10).toImage().pixel(0, 0));
    color = QColorDialog::getColor(color);

    if (color.isValid()) {
        p.fill(color);
        item->setIcon(p);
    }
}

void SettingsGUI::slotGetColor(){
    QPixmap p(10, 10);

    if (sender() == toolButton_H_COLOR){
        QColor color = QColorDialog::getColor(h_color);

        if (color.isValid()){
            h_color = color;

            color.setAlpha(horizontalSlider_H_COLOR->value());
            p.fill(color);
            toolButton_H_COLOR->setIcon(p);
        }
    }
    else if (sender() == toolButton_SHAREDFILES){
        QColor color = QColorDialog::getColor(shared_files_color);

        if (color.isValid()){
            shared_files_color = color;

            color.setAlpha(horizontalSlider_SHAREDFILES->value());
            p.fill(color);
            toolButton_SHAREDFILES->setIcon(p);
        }
    }
    else if (sender() == toolButton_CHAT_BACKGROUND_COLOR){
        QColor color = QColorDialog::getColor(chat_background_color);

        if (color.isValid()){
            chat_background_color = color;

            color.setAlpha(255);
            p.fill(color);
            toolButton_CHAT_BACKGROUND_COLOR->setIcon(p);
        }
    }
    else if (sender() == toolButton_DOWNLOADSCLR){
        QColor color = QColorDialog::getColor(chat_background_color);

        if (color.isValid()){
            downloads_clr = color;

            color.setAlpha(255);
            p.fill(color);
            toolButton_DOWNLOADSCLR->setIcon(p);
        }
    }
    else if (sender() == toolButton_UPLOADSCLR){
        QColor color = QColorDialog::getColor(chat_background_color);

        if (color.isValid()){
            uploads_clr = color;

            color.setAlpha(255);
            p.fill(color);
            toolButton_UPLOADSCLR->setIcon(p);
        }
    }
}

void SettingsGUI::slotSetTransparency(int value){
    QPixmap p(10, 10);
    QColor color;

    if (sender() == horizontalSlider_H_COLOR)
        color = h_color;
    else
        color = shared_files_color;

    color.setAlpha(value);

    if (color.isValid())
        p.fill(color);

    if (sender() == horizontalSlider_H_COLOR)
        toolButton_H_COLOR->setIcon(p);
    else
        toolButton_SHAREDFILES->setIcon(p);
}

void SettingsGUI::slotBrowseLng(){
    QString file = QFileDialog::getOpenFileName(this,
                                                tr("Select translation"),
                                                qtCtx()->wulforUtil()->getTranslationsPath(),
                                                tr("Translation (*.qm)"));

    if (!file.isEmpty()){
        file = QDir::toNativeSeparators(file);

        qtCtx()->settings()->blockSignals(true);//do not emit signal that translation file has been changed
        qtCtx()->settings()->setStr(WS_TRANSLATION_FILE, file);
        qtCtx()->settings()->blockSignals(false);

        qtCtx()->settings()->loadTranslation();//set language for application
        qtCtx()->mainWindow()->retranslateUi();

        qtCtx()->settings()->setStr(WS_TRANSLATION_FILE, file);//emit signals for other widgets

        lineEdit_LANGFILE->setText(qtCtx()->settings()->getStr(WS_TRANSLATION_FILE));
    }
}

void SettingsGUI::slotBrowseChatPictureDir()
{
    if (!lineEdit_CHAT_PICTURE_DIR)
        return;

    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select chat pictures folder"),
                                                          lineEdit_CHAT_PICTURE_DIR->text().trimmed().isEmpty()
                                                              ? ChatEdit::defaultChatPictureDir()
                                                              : lineEdit_CHAT_PICTURE_DIR->text().trimmed(),
                                                          QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
        lineEdit_CHAT_PICTURE_DIR->setText(QDir::toNativeSeparators(dir));
}

void SettingsGUI::slotLngIndexChanged(int index){
    QString file = comboBox_LANGS->itemData(index).toString();

    qtCtx()->settings()->setStr(WS_TRANSLATION_FILE, file);

    qtCtx()->settings()->blockSignals(true);//do not emit signal that translation file has been changed
    qtCtx()->settings()->setStr(WS_TRANSLATION_FILE, file);
    qtCtx()->settings()->blockSignals(false);

    qtCtx()->settings()->loadTranslation();
    qtCtx()->mainWindow()->retranslateUi();

    qtCtx()->settings()->setStr(WS_TRANSLATION_FILE, file);

    lineEdit_LANGFILE->setText(qtCtx()->settings()->getStr(WS_TRANSLATION_FILE));
}

void SettingsGUI::slotResetTransferColors(){
    qtCtx()->settings()->setVar("transferview/download-bar-color", QColor());
    qtCtx()->settings()->setVar("transferview/upload-bar-color", QColor());

    downloads_clr = QColor();
    uploads_clr = QColor();

    toolButton_DOWNLOADSCLR->setIcon(QIcon());
    toolButton_UPLOADSCLR->setIcon(QIcon());
}
