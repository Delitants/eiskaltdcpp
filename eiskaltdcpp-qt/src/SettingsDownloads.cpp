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

#include "SettingsDownloads.h"
#include "DownloadToSettings.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "WulforUtil.h"
#include "PublicHubsList.h"

#include "dcpp/stdinc.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/DCPlusPlus.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QDir>

using namespace dcpp;

SettingsDownloads::SettingsDownloads(QWidget *parent):
        QWidget(parent)
{
    setupUi(this);

    other_settings.insert(SettingsManager::PRIO_LOWEST, 0);
    other_settings.insert(SettingsManager::AUTODROP_ALL, 1);
    other_settings.insert(SettingsManager::AUTODROP_FILELISTS, 2);
    other_settings.insert(SettingsManager::AUTODROP_DISCONNECT, 3);
    other_settings.insert(SettingsManager::AUTO_SEARCH, 4);
    other_settings.insert(SettingsManager::AUTO_SEARCH_AUTO_MATCH, 5);
    other_settings.insert(SettingsManager::SKIP_ZERO_BYTE, 6);
    other_settings.insert(SettingsManager::DONT_DL_ALREADY_SHARED, 7);
    other_settings.insert(SettingsManager::DONT_DL_ALREADY_QUEUED, 8);
    other_settings.insert(SettingsManager::SFV_CHECK, 9);
    other_settings.insert(SettingsManager::KEEP_LISTS, 10);
    other_settings.insert(SettingsManager::KEEP_FINISHED_FILES, 11);
    other_settings.insert(SettingsManager::COMPRESS_TRANSFERS, 12);
    other_settings.insert(SettingsManager::SEGMENTED_DL, 13);

    init();
}

SettingsDownloads::~SettingsDownloads(){
}

void SettingsDownloads::ok(){
    SettingsManager *SM = qtCtx()->dcCtx().getSettingsManager();

    QString dl_dir = lineEdit_DLDIR->text(), udl_dir = lineEdit_UNF_DL_DIR->text();

    if (!dl_dir.endsWith(PATH_SEPARATOR))
        dl_dir += PATH_SEPARATOR_STR;

    if (!udl_dir.endsWith(PATH_SEPARATOR))
        udl_dir += PATH_SEPARATOR_STR;

    SM->set(SettingsManager::NO_USE_TEMP_DIR, !checkBox_NO_USE_TEMP_DIR->isChecked());
    SM->set(SettingsManager::AUTO_SEARCH_TIME, spinBox_AUTO_SEARCH_TIME->value());
    SM->set(SettingsManager::SEGMENT_SIZE, spinBox_SEGMENT_SIZE->value());
    SM->set(SettingsManager::DOWNLOAD_DIRECTORY, _tq(dl_dir));
    SM->set(SettingsManager::TEMP_DOWNLOAD_DIRECTORY, _tq(udl_dir));
    SM->set(SettingsManager::DOWNLOAD_SLOTS, spinBox_MAXDL->value());
    SM->set(SettingsManager::MAX_DOWNLOAD_SPEED, spinBox_NONEWDL->value());

    //Auto-priority
    SM->set(SettingsManager::PRIO_HIGHEST_SIZE, _tq(QString().setNum(spinBox_HTPMAX->value())));
    SM->set(SettingsManager::PRIO_HIGH_SIZE, _tq(QString().setNum(spinBox_HPMAX->value())));
    SM->set(SettingsManager::PRIO_NORMAL_SIZE, _tq(QString().setNum(spinBox_NPMAX->value())));
    SM->set(SettingsManager::PRIO_LOW_SIZE, _tq(QString().setNum(spinBox_LPMAX->value())));

    // Auto-drop
    SM->set(SettingsManager::AUTODROP_SPEED, _tq(QString().setNum(spinBox_DROPSB->value())));
    SM->set(SettingsManager::AUTODROP_ELAPSED, _tq(QString().setNum(spinBox_MINELAPSED->value())));
    SM->set(SettingsManager::AUTODROP_MINSOURCES, _tq(QString().setNum(spinBox_MINSRCONLINE->value())));
    SM->set(SettingsManager::AUTODROP_INTERVAL, _tq(QString().setNum(spinBox_CHECKEVERY->value())));
    SM->set(SettingsManager::AUTODROP_INACTIVITY, _tq(QString().setNum(spinBox_MAXINACT->value())));
    SM->set(SettingsManager::AUTODROP_FILESIZE, _tq(QString().setNum(spinBox_MINFSZ->value())));

    for (auto it = other_settings.constBegin(); it != other_settings.constEnd(); ++it) {
        SM->set(it.key(), listWidget->item(it.value())->checkState() == Qt::Checked);
    }
    
    SM->set(SettingsManager::ALLOW_SIM_UPLOADS, checkBox_ALLOW_SIM_UPLOADS->isChecked());
    SM->set(SettingsManager::ALLOW_UPLOAD_MULTI_HUB, checkBox_ALLOW_UPLOAD_MULTI_HUB->isChecked());
}

void SettingsDownloads::init(){
    {//Downloads
        lineEdit_DLDIR->setText(_q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DOWNLOAD_DIRECTORY, true)));
        lineEdit_UNF_DL_DIR->setText(_q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TEMP_DOWNLOAD_DIRECTORY, true)));
        checkBox_NO_USE_TEMP_DIR->setChecked(!qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::NO_USE_TEMP_DIR, true));
        spinBox_AUTO_SEARCH_TIME->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::AUTO_SEARCH_TIME, true));
        spinBox_SEGMENT_SIZE->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::SEGMENT_SIZE, true));
        spinBox_MAXDL->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DOWNLOAD_SLOTS, true));
        spinBox_NONEWDL->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::MAX_DOWNLOAD_SPEED, true));

        toolButton_BROWSE->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiFOLDER_BLUE));
        toolButton_BROWSE1->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiFOLDER_BLUE));
        pushButton_DOWNLOADTO_ADD->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiEDITADD));
        pushButton_DOWNLOADTO_REMOVE->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiEDITDELETE));
        groupBox_3->hide();
        pushButton_CFGLISTS->hide();

        connect(toolButton_BROWSE, &QToolButton::clicked, this, &SettingsDownloads::slotBrowse);
        connect(toolButton_BROWSE1, &QToolButton::clicked, this, &SettingsDownloads::slotBrowse);
    }
    {//Download to
        const QList<DownloadToEntry> entries = decodeDownloadTo(
            qtCtx()->settings()->getStr(WS_DOWNLOADTO_PATHS),
            qtCtx()->settings()->getStr(WS_DOWNLOADTO_ALIASES)
        );

        for (const DownloadToEntry& entry : entries) {
            QTreeWidgetItem *item = new QTreeWidgetItem(treeWidget);

            item->setText(0, entry.path);
            item->setText(1, entry.alias);
        }

        treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(treeWidget, &QWidget::customContextMenuRequested, this, &SettingsDownloads::slotDownloadToMenu);
        connect(treeWidget, &QTreeWidget::itemSelectionChanged, this, &SettingsDownloads::slotDownloadToSelectionChanged);
        connect(pushButton_DOWNLOADTO_ADD, &QPushButton::clicked, this, &SettingsDownloads::slotAddDownloadTo);
        connect(pushButton_DOWNLOADTO_REMOVE, &QPushButton::clicked, this, &SettingsDownloads::slotRemoveDownloadTo);
        slotDownloadToSelectionChanged();
    }
    {//Queue
        //Auto-priority
        spinBox_HTPMAX->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::PRIO_HIGHEST_SIZE, true));
        spinBox_HPMAX->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::PRIO_HIGH_SIZE, true));
        spinBox_NPMAX->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::PRIO_NORMAL_SIZE, true));
        spinBox_LPMAX->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::PRIO_LOW_SIZE, true));

        //Auto-drop
        spinBox_DROPSB->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::AUTODROP_SPEED, true));
        spinBox_MINELAPSED->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::AUTODROP_ELAPSED, true));
        spinBox_MINSRCONLINE->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::AUTODROP_MINSOURCES, true));
        spinBox_CHECKEVERY->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::AUTODROP_INTERVAL, true));
        spinBox_MAXINACT->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::AUTODROP_INACTIVITY, true));
        spinBox_MINFSZ->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::AUTODROP_FILESIZE, true));

        for (auto it = other_settings.constBegin(); it != other_settings.constEnd(); ++it) {
            listWidget->item(it.value())->setCheckState(((bool)qtCtx()->dcCtx().getSettingsManager()->get(it.key()))? Qt::Checked : Qt::Unchecked);
        }
    }
    {
        checkBox_ALLOW_SIM_UPLOADS->setCheckState(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::ALLOW_SIM_UPLOADS, true)? Qt::Checked : Qt::Unchecked);
        checkBox_ALLOW_UPLOAD_MULTI_HUB->setCheckState(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::ALLOW_UPLOAD_MULTI_HUB, true)? Qt::Checked : Qt::Unchecked);
    }
}

void SettingsDownloads::slotBrowse(){
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select directory"), QDir::homePath());

    if (dir.isEmpty())
        return;

    dir = QDir::toNativeSeparators(dir);

    if (sender() == toolButton_BROWSE)
        lineEdit_DLDIR->setText(dir);
    else if (sender() == toolButton_BROWSE1)
        lineEdit_UNF_DL_DIR->setText(dir);
}

void SettingsDownloads::slotAddDownloadTo()
{
    bool accepted = false;
    const QString alias = QInputDialog::getText(
        this,
        tr("Enter alias for directory"),
        tr("Alias"),
        QLineEdit::Normal,
        QString(),
        &accepted
    );

    if (!accepted || alias.isEmpty()) {
        return;
    }

    QString dir = QFileDialog::getExistingDirectory(this, tr("Select directory"), QDir::homePath());

    if (dir.isEmpty()) {
        return;
    }

    dir = QDir::toNativeSeparators(dir);

    QTreeWidgetItem *item = new QTreeWidgetItem(treeWidget);
    item->setText(0, dir);
    item->setText(1, alias);

    saveDownloadToEntries();
    slotDownloadToSelectionChanged();
}

void SettingsDownloads::slotRemoveDownloadTo()
{
    const QList<QTreeWidgetItem*> selected = treeWidget->selectedItems();

    if (selected.isEmpty()) {
        return;
    }

    const QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        tr("Action confirm"),
        tr("Remove selected entries?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (ret != QMessageBox::Yes) {
        return;
    }

    qDeleteAll(selected);
    saveDownloadToEntries();
    slotDownloadToSelectionChanged();
}

void SettingsDownloads::slotDownloadToMenu(const QPoint&)
{
    QMenu menu(this);
    QAction *addAction = new QAction(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiEDITADD), tr("Add"), &menu);
    QAction *removeAction = new QAction(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiEDITDELETE), tr("Remove"), &menu);

    removeAction->setEnabled(!treeWidget->selectedItems().isEmpty());

    menu.addAction(addAction);
    menu.addAction(removeAction);

    QAction *result = menu.exec(QCursor::pos());

    if (result == addAction) {
        slotAddDownloadTo();
    } else if (result == removeAction) {
        slotRemoveDownloadTo();
    }
}

void SettingsDownloads::slotDownloadToSelectionChanged()
{
    pushButton_DOWNLOADTO_REMOVE->setEnabled(!treeWidget->selectedItems().isEmpty());
}

void SettingsDownloads::saveDownloadToEntries()
{
    QList<DownloadToEntry> entries;
    entries.reserve(treeWidget->topLevelItemCount());

    for (int index = 0; index < treeWidget->topLevelItemCount(); ++index) {
        QTreeWidgetItem *item = treeWidget->topLevelItem(index);
        entries.append({ item->text(0), item->text(1) });
    }

    const auto encoded = encodeDownloadTo(entries);
    qtCtx()->settings()->setStr(WS_DOWNLOADTO_PATHS, encoded.first);
    qtCtx()->settings()->setStr(WS_DOWNLOADTO_ALIASES, encoded.second);
}

void SettingsDownloads::slotCfgPublic(){
    PublicHubsList h;

    h.exec();
}
