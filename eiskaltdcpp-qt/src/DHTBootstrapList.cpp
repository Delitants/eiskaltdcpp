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

#include "DHTBootstrapList.h"
#include "QtContext.h"
#include "QtContextAware.h"
#include "WulforUtil.h"

#include <QInputDialog>

#include "dcpp/stdinc.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/DCPlusPlus.h"

using namespace dcpp;

namespace {
constexpr auto DEFAULT_DHT_BOOTSTRAP_URL = "https://dht.hublist.eu/dcDHT.php";
}

DHTBootstrapList::DHTBootstrapList(QWidget *parent): QDialog(parent)
{
    setupUi(this);

    setWindowTitle(tr("DHT bootstrap URLs"));

    QString urls = _q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DHT_BOOTSTRAP_URLS));
    if (urls.trimmed().isEmpty())
        urls = QString::fromLatin1(DEFAULT_DHT_BOOTSTRAP_URL);

    listWidget->addItems(urls.split(";", Qt::SkipEmptyParts));

    connect(pushButton_DOWN, &QPushButton::clicked, this, &DHTBootstrapList::slotDown);
    connect(pushButton_UP,   &QPushButton::clicked, this, &DHTBootstrapList::slotUp);
    connect(pushButton_ADD,  &QPushButton::clicked, this, &DHTBootstrapList::slotAdd);
    connect(pushButton_REM,  &QPushButton::clicked, this, &DHTBootstrapList::slotRem);
    connect(pushButton_EDIT, &QPushButton::clicked, this, &DHTBootstrapList::slotChange);
    connect(this, &QDialog::accepted, this, &DHTBootstrapList::slotAccepted);
}

void DHTBootstrapList::slotAccepted(){
    QString urls;
    for (int i = 0; i < listWidget->count(); i++)
        urls += (urls.isEmpty() ? "" : ";") + listWidget->item(i)->text().trimmed();

    if (urls.trimmed().isEmpty())
        urls = QString::fromLatin1(DEFAULT_DHT_BOOTSTRAP_URL);

    qtCtx()->dcCtx().getSettingsManager()->set(SettingsManager::DHT_BOOTSTRAP_URLS, _tq(urls));
}

void DHTBootstrapList::slotDown(){
    int currentRow = listWidget->currentRow();

    if (currentRow < 0 || currentRow >= listWidget->count() - 1)
        return;

    QListWidgetItem *currentItem = listWidget->takeItem(currentRow);

    listWidget->insertItem(currentRow + 1, currentItem);
    listWidget->setCurrentRow(currentRow + 1);
}

void DHTBootstrapList::slotUp(){
    int currentRow = listWidget->currentRow();

    if (currentRow <= 0)
        return;

    QListWidgetItem *currentItem = listWidget->takeItem(currentRow);

    listWidget->insertItem(currentRow - 1, currentItem);
    listWidget->setCurrentRow(currentRow - 1);
}

void DHTBootstrapList::slotAdd(){
    bool ok = false;
    QString link = QInputDialog::getText(this, tr("DHT bootstrap URL"), tr("URL"), QLineEdit::Normal, "", &ok);

    if (ok && !link.trimmed().isEmpty())
        listWidget->addItem(link.trimmed());
}

void DHTBootstrapList::slotRem(){
    int currentRow = listWidget->currentRow();

    if (currentRow < 0)
        return;

    QListWidgetItem *currentItem = listWidget->takeItem(currentRow);

    delete currentItem;
}

void DHTBootstrapList::slotChange(){
    QListWidgetItem *item = listWidget->currentItem();

    if (!item)
        return;

    bool ok = false;
    QString link = QInputDialog::getText(this, tr("DHT bootstrap URL"), tr("URL"), QLineEdit::Normal, item->text(), &ok);

    if (ok && !link.trimmed().isEmpty())
        item->setText(link.trimmed());
}
